#!/usr/bin/env python3
"""Expand a BK7258 logical app image into 32-byte + CRC16 flash packets."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

PACKET_DATA = 32
PACKET_TOTAL = 34
APP_MAGIC = b"BK7236\0\0"


class ExpansionError(ValueError):
    """Raised when a 32+2 encoded image is malformed."""


def crc16(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = (crc << 1) ^ 0x8005 if crc & 0x8000 else crc << 1
    return crc & 0xFFFF


def expand(data: bytes) -> bytes:
    output = bytearray()
    for offset in range(0, len(data), PACKET_DATA):
        block = data[offset : offset + PACKET_DATA]
        block = block.ljust(PACKET_DATA, b"\xff")
        output += block
        output += struct.pack(">H", crc16(block))
    return bytes(output)


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
    parser.add_argument("--max-size", type=parse_int, required=True)
    parser.add_argument("--require-magic", action="store_true")
    args = parser.parse_args()

    raw = args.input.read_bytes()
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
    if not (args.xip_base <= reset_addr < args.xip_base + len(raw)):
        raise SystemExit(
            f"Reset vector 0x{reset:08x} is outside image XIP range "
            f"0x{args.xip_base:08x}..0x{args.xip_base + len(raw):08x}"
        )
    if args.require_magic and raw[0x100:0x108] != APP_MAGIC:
        raise SystemExit("CP image is missing BK7236 magic at raw offset 0x100")

    encoded = expand(raw)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(encoded)

    manifest = {
        "input": str(args.input),
        "output": str(args.out),
        "logical_size": len(raw),
        "physical_size": len(encoded),
        "xip_base": args.xip_base,
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
