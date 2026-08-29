#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Stream one verified AIDK OTA .bkpack over the native USB CDC port."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import struct
import sys
import tempfile
import time
import zipfile
import zlib
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import BinaryIO

try:
    import serial
    from serial.tools import list_ports
except ImportError as error:
    raise SystemExit(
        "pyserial is required: install it in the Python environment that owns "
        "the Windows/Linux serial ports"
    ) from error


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
CH340_VID = 0x1A86
CH340_PID = 0x7523


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
    root: Path
    catalog: bytes
    signature: bytes
    cp: BinaryIO
    ap: BinaryIO
    version: str
    counter: int

    def close(self) -> None:
        self.cp.close()
        self.ap.close()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Stream a signed AIDK CP/AP OTA package over USB0 CDC, then use "
            "the CH340 CP console for a whole-device reboot and confirmation."
        )
    )
    parser.add_argument("--package", type=Path)
    parser.add_argument(
        "--ota-port",
        help="native USB CDC port; auto-select VID 1209:0001 when omitted",
    )
    parser.add_argument(
        "--control-port",
        help=(
            "CH340 CP console; auto-select VID 1a86:7523 when omitted, or "
            "use 'none' to stage without reboot"
        ),
    )
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--connect-timeout", type=float, default=45.0)
    parser.add_argument(
        "--frame-timeout",
        type=float,
        default=120.0,
        help=(
            "maximum silence while the target erases/programs before the "
            "next OTA protocol frame (default: 120 seconds)"
        ),
    )
    parser.add_argument("--confirm-timeout", type=float, default=120.0)
    parser.add_argument(
        "--status-only",
        action="store_true",
        help="only poll bkota status on the CH340 console",
    )
    parser.add_argument(
        "--reboot-only",
        action="store_true",
        help=(
            "reboot through the CH340 console and strictly confirm the "
            "currently accepted generation without opening native USB"
        ),
    )
    parser.add_argument("--expected-version")
    parser.add_argument("--expected-counter", type=int)
    parser.add_argument(
        "--inspect-only",
        action="store_true",
        help="validate package structure/hashes without opening serial ports",
    )
    return parser.parse_args()


def safe_member(name: str) -> None:
    path = PurePosixPath(name)
    if (
        not name
        or name.startswith("/")
        or "\\" in name
        or path.is_absolute()
        or any(part in {"", ".", ".."} for part in path.parts)
    ):
        raise ValueError(f"unsafe package member: {name!r}")


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


def digest_file(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def regular_image(document: dict[str, object], role: str) -> dict[str, object]:
    image = document.get(role)
    if not isinstance(image, dict) or set(image) != {"sha256", "size", "uri"}:
        raise ValueError(f"catalog {role} object is malformed")
    uri = image.get("uri")
    size = image.get("size")
    digest = image.get("sha256")
    if (
        not isinstance(uri, str)
        or not isinstance(size, int)
        or size <= 0
        or not isinstance(digest, str)
        or re.fullmatch(r"[0-9a-f]{64}", digest) is None
    ):
        raise ValueError(f"catalog {role} metadata is malformed")
    safe_member(uri)
    return image


def extract_object(archive: zipfile.ZipFile, member: str, target: Path) -> None:
    with archive.open(member, "r") as source, target.open("wb") as output:
        shutil.copyfileobj(source, output, 1024 * 1024)


def open_package(package: Path, root: Path) -> PackageObjects:
    if not package.is_file() or package.is_symlink():
        raise ValueError(f"package must be a regular non-symlink file: {package}")
    with zipfile.ZipFile(package, "r") as archive:
        names = archive.namelist()
        if len(names) != len(set(names)):
            raise ValueError("package contains duplicate members")
        for name in names:
            safe_member(name)
        required = {"catalog.json", "catalog.sig"}
        if not required.issubset(names):
            raise ValueError("package has no signed OTA catalog")
        catalog = archive.read("catalog.json")
        signature = archive.read("catalog.sig")
        if not catalog or len(catalog) > 2048 or not signature or len(signature) > 80:
            raise ValueError("catalog or signature size is invalid")
        document = json.loads(catalog)
        target = document.get("target")
        if target != {"board_family": "bk7258", "physical_board": "aidk_ai_toy"}:
            raise ValueError(f"package target is not aidk_ai_toy: {target!r}")
        if document.get("format") != "bk7258.ota/2":
            raise ValueError(f"unsupported OTA format: {document.get('format')!r}")
        cp_meta = regular_image(document, "cp")
        ap_meta = regular_image(document, "ap")
        cp_path = root / "cp.bin"
        ap_path = root / "ap.bin"
        extract_object(archive, str(cp_meta["uri"]), cp_path)
        extract_object(archive, str(ap_meta["uri"]), ap_path)

    for role, path, metadata in (
        ("cp", cp_path, cp_meta),
        ("ap", ap_path, ap_meta),
    ):
        if path.stat().st_size != metadata["size"]:
            raise ValueError(f"{role} size does not match catalog")
        if digest_file(path) != metadata["sha256"]:
            raise ValueError(f"{role} digest does not match catalog")

    release_version = document.get("version")
    counter = document.get("security_counter")
    if not isinstance(release_version, str) or not isinstance(counter, int):
        raise ValueError("catalog version/counter is malformed")
    return PackageObjects(
        root=root,
        catalog=catalog,
        signature=signature,
        cp=cp_path.open("rb"),
        ap=ap_path.open("rb"),
        version=release_version,
        counter=counter,
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
    print(f"AIDK USB OTA: opening port={port_name}", flush=True)
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
            print(f"AIDK USB OTA: opened port={port_name}", flush=True)
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
        stream = objects.cp if obj == CP else objects.ap
        stream.seek(offset)
        result = stream.read(size)
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
            f"AIDK USB OTA: connected port={port_name} "
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
                    f"AIDK USB OTA: phase={frame.obj} image={frame.offset} "
                    f"progress={frame.value}/{total}",
                    flush=True,
                )
            elif frame.kind in {DONE, ERROR}:
                if frame.status != 0:
                    raise RuntimeError(f"target staging failed: {frame.status}")
                print("AIDK USB OTA: signed pair staged", flush=True)
                return
            else:
                raise RuntimeError(f"unexpected target frame: {frame.kind}")


def reboot_and_confirm(
    port_name: str,
    baud: int,
    version: str,
    counter: int,
    timeout: float,
    reboot: bool = True,
) -> None:
    expected = f"pair=confirmed version={version} counter={counter}"
    healthy = (
        re.compile(r"ap magic=[0-9a-fA-F]{8} version=\d+ state=2 error=0\b"),
        re.compile(
            r"cpu2 magic=[0-9a-fA-F]{8} state=8 error=0 .*"
            r"ready=1 online=00000003\b"
        ),
        re.compile(r"rptun magic=[0-9a-fA-F]{8} version=\d+ state=4 error=0\b"),
        re.compile(r"manager state=0\b.*error=0\b"),
        re.compile(r"supervisor state=2\b"),
    )
    deadline = time.monotonic() + timeout
    next_status = time.monotonic() + (12.0 if reboot else 0.0)
    transcript = ""
    with serial.Serial(port_name, baud, timeout=0.2, write_timeout=3.0) as port:
        if reboot:
            port.reset_input_buffer()
            port.write(b"\r\nbkota reboot\r\n")
            port.flush()
        while time.monotonic() < deadline:
            data = port.read(4096)
            if data:
                text = data.decode("utf-8", errors="replace")
                transcript = (transcript + text)[-16384:]
                sys.stdout.write(text)
                sys.stdout.flush()
                if expected in transcript and all(
                    pattern.search(transcript) is not None for pattern in healthy
                ):
                    result = "reboot and automatic confirmation" if reboot else \
                             "running generation confirmation"
                    print(f"AIDK OTA: {result} PASS")
                    return
            now = time.monotonic()
            if now >= next_status:
                port.write(b"\r\nbkota status\r\n")
                port.flush()
                next_status = now + 5.0
    raise TimeoutError(
        f"did not observe healthy {expected!r} on {port_name}; "
        "required AP READY, CPU2 online, RPTUN connected, manager idle, "
        "and supervisor healthy"
    )


def main() -> int:
    args = parse_args()
    if args.status_only or args.reboot_only:
        if args.status_only and args.reboot_only:
            raise ValueError("--status-only and --reboot-only are mutually exclusive")
        if args.expected_version is None or args.expected_counter is None:
            raise ValueError(
                "status/reboot mode requires --expected-version and "
                "--expected-counter"
            )
        if args.control_port == "none":
            raise ValueError("status/reboot mode requires a CH340 control port")
        control_port = unique_port(
            args.control_port, CH340_VID, CH340_PID, "AIDK CH340 console"
        )
        reboot_and_confirm(
            control_port,
            args.baud,
            args.expected_version,
            args.expected_counter,
            args.confirm_timeout,
            reboot=args.reboot_only,
        )
        return 0

    if args.package is None:
        raise ValueError("--package is required for USB OTA")
    if args.inspect_only:
        with tempfile.TemporaryDirectory(prefix="aidk-usb-ota-") as name:
            objects = open_package(args.package.resolve(), Path(name))
            if (
                args.expected_version is not None
                and objects.version != args.expected_version
            ):
                raise ValueError(
                    f"package version {objects.version} does not match "
                    f"{args.expected_version}"
                )
            if (
                args.expected_counter is not None
                and objects.counter != args.expected_counter
            ):
                raise ValueError(
                    f"package counter {objects.counter} does not match "
                    f"{args.expected_counter}"
                )
            objects.close()
            print(
                f"AIDK USB OTA: package PASS version={objects.version} "
                f"counter={objects.counter}"
            )
        return 0
    ota_port = unique_port(args.ota_port, NATIVE_VID, NATIVE_PID, "AIDK native USB CDC")
    control_port = None
    if args.control_port != "none":
        control_port = unique_port(
            args.control_port, CH340_VID, CH340_PID, "AIDK CH340 console"
        )
    with tempfile.TemporaryDirectory(prefix="aidk-usb-ota-") as name:
        objects = open_package(args.package.resolve(), Path(name))
        try:
            stream_package(
                ota_port,
                objects,
                args.connect_timeout,
                args.frame_timeout,
            )
        finally:
            objects.close()
        if control_port is None:
            print("AIDK USB OTA: staged only; run 'bkota reboot' on the CP console")
        else:
            reboot_and_confirm(
                control_port,
                args.baud,
                objects.version,
                objects.counter,
                args.confirm_timeout,
            )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, TimeoutError, ValueError, zipfile.BadZipFile) as error:
        print(f"AIDK USB OTA: FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
