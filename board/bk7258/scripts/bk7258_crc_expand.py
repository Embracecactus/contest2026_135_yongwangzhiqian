#!/usr/bin/env python3
"""Expand a BK7258 logical app image into 32-byte + CRC16 flash packets."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import struct
from pathlib import Path

PACKET_DATA = 32
PACKET_TOTAL = 34
APP_MAGIC = b"BK7236\0\0"

_VENDOR_CRC_PATH = (
    Path(__file__).resolve().parents[3]
    / "tools/vendor/bk7258-sdk-v3.1.1.9/bk_crc16.py"
)
_VENDOR_CRC_SPEC = importlib.util.spec_from_file_location(
    "bk7258_sdk_v3119_crc16", _VENDOR_CRC_PATH
)
if _VENDOR_CRC_SPEC is None or _VENDOR_CRC_SPEC.loader is None:
    raise ImportError(f"cannot load vendored SDK CRC helper: {_VENDOR_CRC_PATH}")
_VENDOR_CRC_MODULE = importlib.util.module_from_spec(_VENDOR_CRC_SPEC)
_VENDOR_CRC_SPEC.loader.exec_module(_VENDOR_CRC_MODULE)
_VENDOR_CRC = _VENDOR_CRC_MODULE.bk_crc16()


class ExpansionError(ValueError):
    """Raised when a 32+2 encoded image is malformed."""


def crc16(data: bytes) -> int:
    return _VENDOR_CRC_MODULE.crc16(data, 0, len(data))


def expand(data: bytes) -> bytes:
    return _VENDOR_CRC.crc16_data(data)


def decode(data: bytes) -> bytes:
    """Verify and remove every BK7258 32-byte + CRC16 packet."""

    if len(data) % PACKET_TOTAL:
        raise ExpansionError(
            f"encoded image size 0x{len(data):x} is not a multiple of "
            f"{PACKET_TOTAL}"
        )

    output = bytearray()
    for offset in range(0, len(data), PACKET_TOTAL):
        block = data[offset : offset + PACKET_DATA]
        stored_crc = struct.unpack_from(">H", data, offset + PACKET_DATA)[0]
        observed_crc = crc16(block)
        if stored_crc != observed_crc:
            raise ExpansionError(
                f"CRC16 mismatch at encoded offset 0x{offset:x}: "
                f"expected 0x{stored_crc:04x}, got 0x{observed_crc:04x}"
            )
        output.extend(block)
    return bytes(output)


def parse_int(value: str) -> int:
    return int(value, 0)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--in", dest="input", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--xip-base", type=parse_int, required=True)
    parser.add_argument(
        "--execution-base",
        type=parse_int,
        help="expected vector-table execution address; defaults to --xip-base",
    )
    parser.add_argument("--max-size", type=parse_int, required=True)
    parser.add_argument(
        "--pad-size",
        type=parse_int,
        help="pad the logical input with erased bytes before 32+2 encoding",
    )
    parser.add_argument("--require-magic", action="store_true")
    args = parser.parse_args()

    raw = args.input.read_bytes()
    input_size = len(raw)
    if args.pad_size is not None:
        if args.pad_size < input_size:
            raise SystemExit(
                f"pad size 0x{args.pad_size:x} is smaller than input 0x{input_size:x}"
            )
        raw = raw.ljust(args.pad_size, b"\xff")
    if len(raw) < 8:
        raise SystemExit("image is too small to contain MSP and Reset vectors")
    if len(raw) > args.max_size:
        raise SystemExit(
            f"image size 0x{len(raw):x} exceeds slot 0x{args.max_size:x}"
        )

    msp, reset = struct.unpack_from("<II", raw)
    reset_addr = reset & ~1
    if not (0x28000000 <= msp < 0x280A0000):
        raise SystemExit(f"MSP 0x{msp:08x} is outside BK7258 SRAM")
    if (reset & 1) == 0:
        raise SystemExit(f"Reset vector 0x{reset:08x} is not Thumb")
    execution_base = args.execution_base if args.execution_base is not None else args.xip_base
    if not (execution_base <= reset_addr < execution_base + len(raw)):
        raise SystemExit(
            f"Reset vector 0x{reset:08x} is outside image execution range "
            f"0x{execution_base:08x}..0x{execution_base + len(raw):08x}"
        )
    if args.require_magic and raw[0x100:0x108] != APP_MAGIC:
        raise SystemExit("CP image is missing BK7236 magic at raw offset 0x100")

    encoded = expand(raw)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(encoded)

    manifest = {
        "input": str(args.input),
        "output": str(args.out),
        "input_size": input_size,
        "logical_size": len(raw),
        "physical_size": len(encoded),
        "xip_base": args.xip_base,
        "execution_base": execution_base,
        "msp": msp,
        "reset": reset,
        "sha256": hashlib.sha256(encoded).hexdigest(),
    }
    args.out.with_suffix(args.out.suffix + ".json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(manifest, sort_keys=True))


if __name__ == "__main__":
    main()
