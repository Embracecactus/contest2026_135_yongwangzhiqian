#!/usr/bin/env python3
"""Validate BK7258 CP/AP packed images and emit split/factory artifacts."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
from pathlib import Path

BOOT_PHYSICAL_OFFSET = 0x000000
BOOT_PHYSICAL_SIZE = 0x011000
CP_PHYSICAL_OFFSET = 0x011000
DATA_PHYSICAL_OFFSET = 0x110000
AP_PHYSICAL_OFFSET = 0x220000
AP_PHYSICAL_END = 0x440000
FLASH_ERASE_SIZE = 0x1000


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def segment(name: str, path: Path, offset: int) -> dict[str, object]:
    size = path.stat().st_size
    return {
        "name": name,
        "file": path.name,
        "physical_offset": offset,
        "length": size,
        "physical_end": offset + size,
        "sha256": sha256(path),
        "bkfil": f"{path.name}@0x{offset:x}-0x{size:x}",
    }


def copy(path: Path, output: Path) -> Path:
    destination = output / path.name
    shutil.copy2(path, destination)
    return destination


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def copy_flash_segment(path: Path, output: Path, name: str) -> Path:
    """Copy a physical image and pad it to a complete erase sector."""

    destination = output / name
    payload = path.read_bytes()
    padded_size = align_up(len(payload), FLASH_ERASE_SIZE)
    destination.write_bytes(payload + b"\xff" * (padded_size - len(payload)))
    return destination


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--boot", type=Path, required=True)
    parser.add_argument("--cp-raw", type=Path, required=True)
    parser.add_argument("--cp-crc", type=Path, required=True)
    parser.add_argument("--ap-raw", type=Path, required=True)
    parser.add_argument("--ap-crc", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    for path in (args.boot, args.cp_raw, args.cp_crc, args.ap_raw, args.ap_crc):
        if not path.is_file():
            raise SystemExit(f"missing input: {path}")

    if args.boot.stat().st_size != BOOT_PHYSICAL_SIZE:
        raise SystemExit("bl_crc.bin must be exactly 0x11000 bytes")
    cp_flash_size = align_up(args.cp_crc.stat().st_size, FLASH_ERASE_SIZE)
    ap_flash_size = align_up(args.ap_crc.stat().st_size, FLASH_ERASE_SIZE)
    if CP_PHYSICAL_OFFSET + cp_flash_size > DATA_PHYSICAL_OFFSET:
        raise SystemExit("CP image overlaps the LittleFS physical boundary")
    if AP_PHYSICAL_OFFSET + ap_flash_size > AP_PHYSICAL_END:
        raise SystemExit("AP image exceeds its 2 MiB logical slot")

    args.output.mkdir(parents=True, exist_ok=True)
    boot = copy(args.boot, args.output)
    cp_raw = copy(args.cp_raw, args.output)
    cp_crc = copy(args.cp_crc, args.output)
    ap_raw = copy(args.ap_raw, args.output)
    ap_crc = copy(args.ap_crc, args.output)
    cp_flash = copy_flash_segment(
        args.cp_crc, args.output, "app_crc_flash.bin"
    )
    ap_flash = copy_flash_segment(
        args.ap_crc, args.output, "app1_crc_flash.bin"
    )

    segments = [
        segment("bootloader", boot, BOOT_PHYSICAL_OFFSET),
        segment("cp_app", cp_flash, CP_PHYSICAL_OFFSET),
        segment("ap_app", ap_flash, AP_PHYSICAL_OFFSET),
    ]

    factory_size = AP_PHYSICAL_OFFSET + ap_flash.stat().st_size
    factory = bytearray(b"\xff" * factory_size)
    for item, path in zip(
        segments, (boot, cp_flash, ap_flash), strict=True
    ):
        start = int(item["physical_offset"])
        payload = path.read_bytes()
        factory[start : start + len(payload)] = payload

    factory_path = args.output / "all-app-factory.bin"
    factory_path.write_bytes(factory)

    manifest = {
        "format": 1,
        "logical_layout": {
            "bootloader": [0x000000, 0x010000],
            "cp_app": [0x010000, 0x100000],
            "littlefs": [0x100000, 0x200000],
            "ap_app": [0x200000, 0x400000],
        },
        "physical_boundaries": {
            "cp": CP_PHYSICAL_OFFSET,
            "littlefs": DATA_PHYSICAL_OFFSET,
            "ap": AP_PHYSICAL_OFFSET,
        },
        "segments": segments,
        "raw_images": {
            "cp": {"file": cp_raw.name, "sha256": sha256(cp_raw)},
            "ap": {"file": ap_raw.name, "sha256": sha256(ap_raw)},
        },
        "crc_images": {
            "cp": {"file": cp_crc.name, "sha256": sha256(cp_crc)},
            "ap": {"file": ap_crc.name, "sha256": sha256(ap_crc)},
        },
        "normal_update": {
            "preserves_littlefs": True,
            "mode": "BKFIL/bk_loader multi-segment offset-length writes",
            "flash_erase_alignment": FLASH_ERASE_SIZE,
            "arguments": [item["bkfil"] for item in segments],
        },
        "factory_image": {
            "file": factory_path.name,
            "length": factory_path.stat().st_size,
            "sha256": sha256(factory_path),
            "preserves_littlefs": False,
            "warning": "Factory image contains 0xff padding across LittleFS",
        },
    }
    manifest_path = args.output / "bk7258-dual-image.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(manifest, sort_keys=True))


if __name__ == "__main__":
    main()
