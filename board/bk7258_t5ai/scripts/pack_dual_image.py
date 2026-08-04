#!/usr/bin/env python3
"""Validate and package BK7258 CP/AP images for the accepted A/B layout."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
from pathlib import Path

from bk7258_ab_layout import (
    AP_A_SIZE,
    AP_A_START,
    AP_XIP_SIZE,
    AP_XIP_START,
    BOOT_SIZE,
    BOOT_START,
    CALIBRATION_TAIL_START,
    CP_A_SIZE,
    CP_A_START,
    CP_XIP_SIZE,
    CP_XIP_START,
    ERASE_SIZE,
    FACTORY_PREFIX_END,
    LAYOUT_ID,
    LITTLEFS_SIZE,
    LITTLEFS_START,
    MIGRATION_WRITE_END,
    OTA_METADATA_SIZE,
    OTA_METADATA_START,
    PAIR_B_SIZE,
    PAIR_B_START,
    USR_CONFIG_SIZE,
    USR_CONFIG_START,
    report as layout_report,
)


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
    """Copy one encoded image and pad it to a complete erase sector."""

    destination = output / name
    payload = path.read_bytes()
    padded_size = align_up(len(payload), ERASE_SIZE)
    destination.write_bytes(payload + b"\xff" * (padded_size - len(payload)))
    return destination


def make_secondary_seed(cp: Path, ap: Path, output: Path) -> Path:
    """Place the same A-generation bytes in the official contiguous B slot."""

    image = bytearray(b"\xff" * PAIR_B_SIZE)
    cp_data = cp.read_bytes()
    ap_data = ap.read_bytes()
    image[: len(cp_data)] = cp_data
    image[CP_A_SIZE : CP_A_SIZE + len(ap_data)] = ap_data
    destination = output / "s_app_seed.bin"
    destination.write_bytes(image)
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

    layout_report()
    if args.boot.stat().st_size != BOOT_SIZE:
        raise SystemExit(f"bl_crc.bin must be exactly 0x{BOOT_SIZE:x} bytes")
    cp_flash_size = align_up(args.cp_crc.stat().st_size, ERASE_SIZE)
    ap_flash_size = align_up(args.ap_crc.stat().st_size, ERASE_SIZE)
    if cp_flash_size > CP_A_SIZE:
        raise SystemExit("CP image exceeds official primary_cp_app")
    if ap_flash_size > AP_A_SIZE:
        raise SystemExit("AP image exceeds official primary_ap_app")

    args.output.mkdir(parents=True, exist_ok=True)
    boot = copy(args.boot, args.output)
    cp_raw = copy(args.cp_raw, args.output)
    cp_crc = copy(args.cp_crc, args.output)
    ap_raw = copy(args.ap_raw, args.output)
    ap_crc = copy(args.ap_crc, args.output)
    cp_flash = copy_flash_segment(args.cp_crc, args.output, "app_crc_flash.bin")
    ap_flash = copy_flash_segment(args.ap_crc, args.output, "app1_crc_flash.bin")
    secondary_seed = make_secondary_seed(cp_flash, ap_flash, args.output)

    primary_segments = [
        segment("primary_bootloader", boot, BOOT_START),
        segment("primary_cp_app", cp_flash, CP_A_START),
        segment("primary_ap_app", ap_flash, AP_A_START),
    ]
    secondary_segment = segment("s_app_seed", secondary_seed, PAIR_B_START)

    # Keep the vendor-owned usr_config envelope and all reserved ranges out of
    # the migration write set.  The prefix initializes boot/A/B/metadata; a
    # second explicit all-FF segment clears only the authorized LittleFS.

    migration = bytearray(b"\xff" * FACTORY_PREFIX_END)
    for item, path in zip(
        (*primary_segments, secondary_segment),
        (boot, cp_flash, ap_flash, secondary_seed),
        strict=True,
    ):
        start = int(item["physical_offset"])
        payload = path.read_bytes()
        migration[start : start + len(payload)] = payload

    factory_path = args.output / "all-app-factory.bin"
    factory_path.write_bytes(migration)
    littlefs_clear_path = args.output / "littlefs_factory_clear.bin"
    littlefs_clear_path.write_bytes(b"\xff" * LITTLEFS_SIZE)
    migration_segments = [
        segment("factory_prefix", factory_path, BOOT_START),
        segment("littlefs_clear", littlefs_clear_path, LITTLEFS_START),
    ]

    manifest = {
        "format": 2,
        "layout_id": LAYOUT_ID,
        "layout": layout_report(),
        "logical_layout": {
            "cp_app": [CP_XIP_START, CP_XIP_START + CP_XIP_SIZE],
            "ap_app": [AP_XIP_START, AP_XIP_START + AP_XIP_SIZE],
        },
        "physical_layout": {
            "primary_cp_app": [CP_A_START, CP_A_START + CP_A_SIZE],
            "primary_ap_app": [AP_A_START, AP_A_START + AP_A_SIZE],
            "s_app": [PAIR_B_START, PAIR_B_START + PAIR_B_SIZE],
            "ota_metadata": [
                OTA_METADATA_START,
                OTA_METADATA_START + OTA_METADATA_SIZE,
            ],
            "usr_config": [USR_CONFIG_START, USR_CONFIG_START + USR_CONFIG_SIZE],
            "littlefs": [LITTLEFS_START, LITTLEFS_START + LITTLEFS_SIZE],
            "calibration_tail_start": CALIBRATION_TAIL_START,
        },
        "segments": primary_segments,
        "secondary_seed": {
            **secondary_segment,
            "same_pair_as_primary": True,
            "rbl_header_present": False,
            "boot_selectable": False,
            "reason": (
                "layout-migration seed only; N15-A must add exact RBL and "
                "trial metadata before B selection is enabled"
            ),
        },
        "migration_segments": migration_segments,
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
            "preserves_secondary": True,
            "preserves_calibration_tail": True,
            "mode": "BKFIL/bk_loader primary sparse segments",
            "flash_erase_alignment": ERASE_SIZE,
            "arguments": [item["bkfil"] for item in primary_segments],
        },
        "factory_image": {
            "file": factory_path.name,
            "length": factory_path.stat().st_size,
            "sha256": sha256(factory_path),
            "layout_migration": True,
            "clears_existing_and_target_littlefs": True,
            "write_ranges": [
                [BOOT_START, FACTORY_PREFIX_END],
                [LITTLEFS_START, MIGRATION_WRITE_END],
            ],
            "loader_arguments": [item["bkfil"] for item in migration_segments],
            "project_write_end": MIGRATION_WRITE_END,
            "calibration_tail_start": CALIBRATION_TAIL_START,
            "preserves_calibration_tail": True,
            "preserves_usr_config": True,
            "preserves_reserved_ranges": True,
            "requires_explicit_owner_gate": True,
        },
        "writes_enabled": False,
    }
    manifest_path = args.output / "bk7258-dual-image.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(manifest, sort_keys=True))


if __name__ == "__main__":
    main()
