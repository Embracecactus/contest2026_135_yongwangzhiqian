#!/usr/bin/env python3
"""Build a signed CP/AP MCUboot pair for the BK7258 32+2 flash layout.

The input files are the board's raw, header-less NuttX payloads. The pinned
upstream MCUboot ``imgtool`` adds the selected header and ECDSA-P256 TLVs;
only then are the logical bytes expanded into Beken's 32-byte-data + 2-byte
CRC packets. CP and AP are padded independently to their logical slot sizes
before being concatenated into the official contiguous ``s_app`` B slot.

The signer is the pinned NuttX MCUboot ``imgtool.py``.  The CRC encoder is
the project-vendored, unmodified BK7258 SDK v3.1.1.9 helper; this script only
adapts signed images to the board's CP/AP layout.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from bk7258_ab_layout import (
    AP_XIP_START,
    AP_XIP_SIZE,
    CP_XIP_START,
    CP_XIP_SIZE,
    LAYOUT_ID,
    LAYOUT_INPUT,
    LAYOUT_SHA256,
    PAIR_B_SIZE,
    PAIR_B_START,
    crc_physical_size,
    report as layout_report,
    verify_contract as verify_partition_contract,
)
from bk7258_crc_expand import expand

HEADER_SIZE = 0x200
ALIGN = 4
IMAGE_MAGIC = 0x96F3B83D


def parse_int(value: str) -> int:
    return int(value, 0)


def validate_payload(path: Path, xip_base: int, slot_size: int,
                     msp_alignment: int, header_size: int) -> None:
    data = path.read_bytes()
    if len(data) < 8:
        raise SystemExit(f"{path}: payload is too small")
    msp, reset = struct.unpack_from("<II", data)
    if (msp % msp_alignment) or not (0x28000000 <= msp < 0x280A0000):
        raise SystemExit(f"{path}: invalid SRAM MSP 0x{msp:08x}")
    if not (reset & 1):
        raise SystemExit(f"{path}: reset vector is not Thumb: 0x{reset:08x}")
    reset_addr = reset & ~1
    lo = xip_base + header_size
    hi = lo + len(data)
    if not (lo <= reset_addr < hi):
        raise SystemExit(
            f"{path}: reset 0x{reset:08x} is outside payload execution range "
            f"0x{lo:08x}..0x{hi:08x}"
        )
    if len(data) > slot_size - header_size:
        raise SystemExit(f"{path}: payload exceeds MCUboot slot")


def sign(imgtool: Path, key: Path, payload: Path, output: Path,
         slot_size: int, version: str, security_counter: str,
         header_size: int, align: int, max_align: int | None,
         public_key_format: str, boot_record: str | None, pad: bool) -> None:
    command = [
        sys.executable,
        str(imgtool),
        "create",
        "--key", str(key),
        "--header-size", hex(header_size),
        "--align", str(align),
        "--slot-size", hex(slot_size),
        "--version", version,
        "--security-counter", security_counter,
        "--pad-header",
        str(payload),
        str(output),
    ]
    if max_align is not None:
        command[command.index("--pad-header"):command.index("--pad-header")] = [
            "--max-align", str(max_align),
        ]
    if public_key_format != "hash":
        command[command.index("--pad-header"):command.index("--pad-header")] = [
            "--public-key-format", public_key_format,
        ]
    if boot_record is not None:
        command[command.index("--pad-header"):command.index("--pad-header")] = [
            "--boot-record", boot_record,
        ]
    if pad:
        command[command.index("--pad-header"):command.index("--pad-header")] = [
            "--pad",
        ]
    subprocess.run(command, check=True)
    image = output.read_bytes()
    if len(image) < header_size + 8:
        raise SystemExit(f"imgtool produced a truncated image: {output}")
    magic = struct.unpack_from("<I", image)[0]
    if magic != IMAGE_MAGIC:
        raise SystemExit(f"{output}: unexpected MCUboot magic 0x{magic:08x}")
    if len(image) > slot_size:
        raise SystemExit(f"{output}: signed image exceeds slot size")


def write_padded(path: Path, data: bytes, size: int) -> None:
    if len(data) > size:
        raise SystemExit(f"{path}: 0x{len(data):x} bytes exceeds 0x{size:x}")
    path.write_bytes(data + b"\xff" * (size - len(data)))


def digest(path: Path) -> dict[str, object]:
    data = path.read_bytes()
    return {"file": path.name, "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest()}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cp-raw", type=Path, required=True)
    parser.add_argument("--ap-raw", type=Path, required=True)
    parser.add_argument("--key", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--security-counter", default="auto")
    parser.add_argument("--partition", type=Path, default=LAYOUT_INPUT)
    parser.add_argument("--expect-layout-id")
    parser.add_argument("--expect-layout-sha256")
    parser.add_argument(
        "--header-size", type=parse_int, default=HEADER_SIZE,
        help="MCUboot header size; v3.1.1.9 secure signing uses 0x1000",
    )
    parser.add_argument("--align", type=int, choices=(1, 2, 4, 8, 16, 32),
                        default=ALIGN)
    parser.add_argument("--max-align", type=int, choices=(8, 16, 32))
    parser.add_argument("--public-key-format", choices=("hash", "full"),
                        default="hash")
    parser.add_argument("--boot-record", help="NuttX imgtool boot record type")
    parser.add_argument("--pad", action="store_true",
                        help="add the MCUboot trailer used by Beken signing")
    parser.add_argument(
        "--imgtool", type=Path,
        default=Path(__file__).resolve().parents[4] /
        "apps/boot/mcuboot/mcuboot/scripts/imgtool.py",
    )
    args = parser.parse_args()

    verify_partition_contract(
        args.partition, args.expect_layout_id, args.expect_layout_sha256
    )

    for path in (args.cp_raw, args.ap_raw, args.key, args.imgtool):
        if not path.is_file():
            raise SystemExit(f"missing input: {path}")

    validate_payload(args.cp_raw, CP_XIP_START, CP_XIP_SIZE, 8,
                     args.header_size)
    # The official AP vector table uses a word-aligned initial stack value;
    # unlike CP, its generated top-of-RAM value is 4-byte aligned.
    validate_payload(args.ap_raw, AP_XIP_START, AP_XIP_SIZE, 4,
                     args.header_size)
    args.output.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="bk7258-mcuboot-") as tmp:
        tmpdir = Path(tmp)
        cp_signed = tmpdir / "cp_signed.bin"
        ap_signed = tmpdir / "ap_signed.bin"
        sign(args.imgtool, args.key, args.cp_raw, cp_signed, CP_XIP_SIZE,
             args.version, args.security_counter, args.header_size, args.align,
             args.max_align, args.public_key_format, args.boot_record, args.pad)
        sign(args.imgtool, args.key, args.ap_raw, ap_signed, AP_XIP_SIZE,
             args.version, args.security_counter, args.header_size, args.align,
             args.max_align, args.public_key_format, args.boot_record, args.pad)

        cp_logical = cp_signed.read_bytes()
        ap_logical = ap_signed.read_bytes()
        cp_crc = expand(cp_logical)
        ap_crc = expand(ap_logical)
        cp_span = crc_physical_size(CP_XIP_SIZE)
        ap_span = crc_physical_size(AP_XIP_SIZE)
        if len(cp_crc) > cp_span or len(ap_crc) > ap_span:
            raise SystemExit("CRC-expanded image exceeds its physical slot")

        cp_signed_path = args.output / "cp_signed.bin"
        ap_signed_path = args.output / "ap_signed.bin"
        cp_crc_path = args.output / "cp_signed_crc.bin"
        ap_crc_path = args.output / "ap_signed_crc.bin"
        cp_signed_path.write_bytes(cp_logical)
        ap_signed_path.write_bytes(ap_logical)
        cp_crc_path.write_bytes(cp_crc)
        ap_crc_path.write_bytes(ap_crc)

        cp_slot = bytearray(b"\xff" * cp_span)
        ap_slot = bytearray(b"\xff" * ap_span)
        cp_slot[:len(cp_crc)] = cp_crc
        ap_slot[:len(ap_crc)] = ap_crc
        pair = bytes(cp_slot + ap_slot)
        if len(pair) != PAIR_B_SIZE:
            raise SystemExit(
                f"computed B pair 0x{len(pair):x} != layout 0x{PAIR_B_SIZE:x}"
            )
        pair_path = args.output / "s_app_mcuboot.bin"
        pair_path.write_bytes(pair)

    manifest = {
        "format": 1,
        "layout_id": LAYOUT_ID,
        "layout_sha256": LAYOUT_SHA256,
        "layout": layout_report(),
        "version": args.version,
        "security_counter": args.security_counter,
        "pair_binding": {
            "rule": "CP/AP ih_ver equality plus protected IMAGE_TLV_SEC_CNT equality",
            "enforced_by": "board-owned BL2 before CP/AP handoff",
            "legacy_without_counter": "both members must omit the counter",
        },
        "header_size": args.header_size,
        "align": args.align,
        "max_align": args.max_align,
        "public_key_format": args.public_key_format,
        "boot_record": args.boot_record,
        "pad": args.pad,
        "cp_xip": [CP_XIP_START, CP_XIP_START + CP_XIP_SIZE],
        "ap_xip": [AP_XIP_START, AP_XIP_START + AP_XIP_SIZE],
        "b_pair_physical": [PAIR_B_START, PAIR_B_START + PAIR_B_SIZE],
        "segments": [
            {"name": "cp_signed", **digest(args.output / "cp_signed.bin")},
            {"name": "ap_signed", **digest(args.output / "ap_signed.bin")},
            {"name": "cp_signed_crc", **digest(args.output / "cp_signed_crc.bin")},
            {"name": "ap_signed_crc", **digest(args.output / "ap_signed_crc.bin")},
            {"name": "s_app_mcuboot", **digest(args.output / "s_app_mcuboot.bin")},
        ],
    }
    (args.output / "mcuboot_pair.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(manifest, sort_keys=True))


if __name__ == "__main__":
    main()
