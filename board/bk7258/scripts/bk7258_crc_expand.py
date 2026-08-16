#!/usr/bin/env python3
"""Expand a BK7258 logical app image into 32-byte + CRC16 flash packets."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from bk7258_crc16 import expand

APP_MAGIC = b"BK7236\0\0"


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
