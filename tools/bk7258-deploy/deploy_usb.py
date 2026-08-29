#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""BK7258 native-USB CDC OTA transport.

This is the host peer of the chip-level ``BK7258_OTA_SOURCE_USB`` source.  The
wire constants below mirror ``chips/bk7258/ap/bk7258_ota_source_usb.c``; a
change on either side is a protocol change and must be made on both.
"""

from __future__ import annotations

import json
import struct
import sys
import time
import zlib
from dataclasses import dataclass
from pathlib import Path

try:
    import serial
    from serial.tools import list_ports
except ImportError as error:
    raise SystemExit(
        "pyserial is required: install it in the Python environment that owns "
        "the Windows/Linux serial ports"
    ) from error

# The archive codec belongs to the maintainer build entry point; this transport
# only consumes already-verified material and never re-parses the package.

_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(_ROOT / "tools" / "bk7258"))

from _lib import package as package_domain  # noqa: E402

PackageError = package_domain.PackageError


MAGIC = 0x314F5441
VERSION = 1
HEADER = struct.Struct("<IHHIIIiIII")
MAX_PAYLOAD = 128
USB_PACKET_SIZE = 64

HELLO = 1
ACK = 2
START = 3
READ = 4
DATA = 5
PROGRESS = 6
DONE = 7
CANCEL = 8
ERROR = 9

CATALOG = 0
SIGNATURE = 1
CP = 2
AP = 3

NATIVE_VID = 0x1209
NATIVE_PID = 0x0001


@dataclass(frozen=True)
class Frame:
    kind: int
    sequence: int
    obj: int
    offset: int
    status: int
    value: int
    payload: bytes


@dataclass
class PackageObjects:
    catalog: bytes
    signature: bytes
    cp: bytes
    ap: bytes
    version: str
    counter: int
    target: str


def unique_port(explicit: str | None, vid: int, pid: int, label: str) -> str:
    if explicit:
        return explicit
    matches = [
        item.device
        for item in list_ports.comports()
        if item.vid == vid and item.pid == pid
    ]
    if len(matches) != 1:
        detail = ", ".join(matches) if matches else "none"
        raise RuntimeError(
            f"expected exactly one {label} {vid:04x}:{pid:04x}; found {detail}"
        )
    return matches[0]


def open_package(
    package: Path, expected_board: str | None = None
) -> PackageObjects:
    """Read one verified package through the maintainer package codec.

    ``package_domain.trust_material`` runs the complete structural and
    signature verification and enforces the archive format, so this transport
    never re-implements those rules and cannot accept a package the release
    tool would have rejected.
    """

    if not package.is_file() or package.is_symlink():
        raise ValueError(f"package must be a regular non-symlink file: {package}")
    _security, _layout, images, catalog, signature = package_domain.trust_material(
        package
    )
    if catalog is None or signature is None:
        raise ValueError("package has no signed OTA catalog")
    if not 0 < len(catalog) <= 2048 or not 0 < len(signature) <= 80:
        raise ValueError("catalog or signature size is invalid")

    document = json.loads(catalog)
    target = document.get("target")
    if not isinstance(target, dict) or target.get("board_family") != "bk7258":
        raise ValueError(f"package target is not a BK7258 board: {target!r}")
    physical_board = target.get("physical_board")
    if not isinstance(physical_board, str):
        raise ValueError("package target has no physical board")
    if expected_board is not None and physical_board != expected_board:
        raise ValueError(
            f"package target board is {physical_board}, not {expected_board}"
        )

    missing = {"cp", "ap"} - set(images)
    if missing:
        raise ValueError(f"package is missing image artifacts: {sorted(missing)}")
    version = document.get("version")
    counter = document.get("security_counter")
    if not isinstance(version, str) or not isinstance(counter, int):
        raise ValueError("catalog version/counter is malformed")
    return PackageObjects(
        catalog=catalog,
        signature=signature,
        cp=images["cp"],
        ap=images["ap"],
        version=version,
        counter=counter,
        target=physical_board,
    )


def read_exact(port: serial.Serial, size: int, deadline: float) -> bytes:
    output = bytearray()
    while len(output) < size:
        if time.monotonic() >= deadline:
            raise TimeoutError(f"serial receive timed out after {len(output)}/{size} bytes")
        chunk = port.read(size - len(output))
        if chunk:
            output.extend(chunk)
    return bytes(output)


def read_frame(port: serial.Serial, timeout: float) -> Frame:
    deadline = time.monotonic() + timeout
    magic = struct.pack("<I", MAGIC)
    window = bytearray()
    while bytes(window) != magic:
        byte = read_exact(port, 1, deadline)
        window.extend(byte)
        if len(window) > len(magic):
            del window[0]
    tail = read_exact(port, HEADER.size - 4, deadline)
    fields = HEADER.unpack(magic + tail)
    if fields[1] != VERSION:
        raise ValueError(f"unsupported target protocol version: {fields[1]}")
    payload_size = fields[8]
    if payload_size > MAX_PAYLOAD:
        raise ValueError(f"target payload exceeds protocol limit: {payload_size}")
    payload = read_exact(port, payload_size, deadline) if payload_size else b""
    if zlib.crc32(payload) & 0xFFFFFFFF != fields[9]:
        raise ValueError("target frame payload CRC mismatch")
    return Frame(
        kind=fields[2],
        sequence=fields[3],
        obj=fields[4],
        offset=fields[5],
        status=fields[6],
        value=fields[7],
        payload=payload,
    )


def write_frame(
    port: serial.Serial,
    kind: int,
    sequence: int,
    obj: int = 0,
    offset: int = 0,
    status: int = 0,
    value: int = 0,
    payload: bytes = b"",
) -> None:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("host payload exceeds protocol limit")
    header = HEADER.pack(
        MAGIC,
        VERSION,
        kind,
        sequence,
        obj,
        offset,
        status,
        value,
        len(payload),
        zlib.crc32(payload) & 0xFFFFFFFF,
    )
    wire = header + payload
    padding = (-len(wire)) % USB_PACKET_SIZE
    if padding:
        wire += bytes(padding)
    written = port.write(wire)
    if written != len(wire):
        raise OSError(f"short serial write: {written}/{len(wire)} bytes")


def open_native_port(port_name: str, timeout: float) -> serial.Serial:
    deadline = time.monotonic() + timeout
    last_error: Exception | None = None
    print(f"BK7258 USB OTA: opening port={port_name}", flush=True)
    while time.monotonic() < deadline:
        try:
            if sys.platform == "win32":
                # The Windows usbser SetCommState/PurgeComm path can wait
                # indefinitely even though CDC bulk data needs neither a UART
                # DCB nor a purge.  Create the same overlapped handle pyserial
                # uses, but configure only bounded read/write timeouts.

                import ctypes
                from serial import win32

                port = serial.Serial(
                    port=None, baudrate=115200, timeout=0.1, write_timeout=5.0
                )
                port.port = port_name
                native_name = port_name
                if port_name.upper().startswith("COM") and int(port_name[3:]) > 8:
                    native_name = "\\\\.\\" + port_name
                handle = win32.CreateFile(
                    native_name,
                    win32.GENERIC_READ | win32.GENERIC_WRITE,
                    0,
                    None,
                    win32.OPEN_EXISTING,
                    win32.FILE_ATTRIBUTE_NORMAL | win32.FILE_FLAG_OVERLAPPED,
                    0,
                )
                if handle == win32.INVALID_HANDLE_VALUE:
                    raise serial.SerialException(
                        f"could not open {port_name}: {ctypes.WinError()}"
                    )
                port._port_handle = handle
                try:
                    port._overlapped_read = win32.OVERLAPPED()
                    port._overlapped_read.hEvent = win32.CreateEvent(
                        None, 1, 0, None
                    )
                    port._overlapped_write = win32.OVERLAPPED()
                    port._overlapped_write.hEvent = win32.CreateEvent(
                        None, 0, 0, None
                    )
                    win32.SetupComm(handle, 4096, 4096)
                    port._orgTimeouts = win32.COMMTIMEOUTS()
                    win32.GetCommTimeouts(handle, ctypes.byref(port._orgTimeouts))
                    timeouts = win32.COMMTIMEOUTS()
                    timeouts.ReadTotalTimeoutConstant = 100
                    timeouts.WriteTotalTimeoutConstant = 5000
                    win32.SetCommTimeouts(handle, ctypes.byref(timeouts))
                except Exception:
                    port._close()
                    raise
                port.is_open = True
            else:
                port = serial.Serial(
                    port_name, 115200, timeout=0.1, write_timeout=5.0
                )
            print(f"BK7258 USB OTA: opened port={port_name}", flush=True)
            return port
        except (OSError, serial.SerialException) as error:
            last_error = error
            time.sleep(0.5)
    raise TimeoutError(f"could not open {port_name}: {last_error}")


def object_read(objects: PackageObjects, obj: int, offset: int, size: int) -> bytes:
    if size <= 0 or size > MAX_PAYLOAD or offset < 0:
        raise ValueError("target requested an invalid object range")
    if obj == CATALOG:
        result = objects.catalog[offset : offset + size]
    elif obj == SIGNATURE:
        result = objects.signature[offset : offset + size]
    elif obj in {CP, AP}:
        blob = objects.cp if obj == CP else objects.ap
        result = blob[offset : offset + size]
    else:
        raise ValueError(f"target requested unknown object {obj}")
    if len(result) != size:
        raise ValueError(f"target requested data outside object {obj}")
    return result


def stream_package(
    port_name: str,
    objects: PackageObjects,
    connect_timeout: float,
    frame_timeout: float,
) -> None:
    with open_native_port(port_name, connect_timeout) as port:
        # Do not call PurgeComm or flush() on the Windows CDC data path.  Both
        # can wait outside pyserial's write timeout when a device-side packet
        # is pending.  Frames are magic-synchronized, and every host OUT frame
        # is padded to the 64-byte Bulk endpoint boundary.

        sequence = 1
        deadline = time.monotonic() + connect_timeout
        while True:
            write_frame(port, HELLO, sequence)
            try:
                reply = read_frame(port, 1.0)
            except TimeoutError:
                if time.monotonic() >= deadline:
                    raise
                continue
            if reply.kind == ACK and reply.sequence == sequence and reply.status == 0:
                break
        sequence += 1
        metadata = struct.pack("<II", len(objects.catalog), len(objects.signature))
        write_frame(port, START, sequence, payload=metadata)
        reply = read_frame(port, 5.0)
        if reply.kind != ACK or reply.sequence != sequence or reply.status != 0:
            raise RuntimeError(f"target rejected OTA start: type={reply.kind} status={reply.status}")

        print(
            f"BK7258 USB OTA: connected port={port_name} "
            f"version={objects.version} counter={objects.counter}",
            flush=True,
        )
        while True:
            frame = read_frame(port, frame_timeout)
            if frame.kind == READ:
                try:
                    payload = object_read(objects, frame.obj, frame.offset, frame.value)
                except (OSError, ValueError) as error:
                    write_frame(
                        port,
                        DATA,
                        frame.sequence,
                        frame.obj,
                        frame.offset,
                        status=-5,
                    )
                    raise RuntimeError(str(error)) from error
                write_frame(
                    port,
                    DATA,
                    frame.sequence,
                    frame.obj,
                    frame.offset,
                    payload=payload,
                )
            elif frame.kind == PROGRESS:
                total = struct.unpack("<I", frame.payload)[0] if len(frame.payload) == 4 else 0
                print(
                    f"BK7258 USB OTA: phase={frame.obj} image={frame.offset} "
                    f"progress={frame.value}/{total}",
                    flush=True,
                )
            elif frame.kind in {DONE, ERROR}:
                if frame.status != 0:
                    raise RuntimeError(f"target staging failed: {frame.status}")
                print("BK7258 USB OTA: signed pair staged", flush=True)
                return
            else:
                raise RuntimeError(f"unexpected target frame: {frame.kind}")
