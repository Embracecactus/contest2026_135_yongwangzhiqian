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
    BL2_SECONDARY_END,
    BL2_SECONDARY_SIZE,
    BL2_SECONDARY_START,
    BL2_SIZE,
    BL2_START,
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
    PAIR_B_SIZE,
    PAIR_B_START,
    USR_CONFIG_SIZE,
    USR_CONFIG_START,
    report as layout_report,
)
from bk7258_trust_chain import (
    TrustChainError,
    load_contract,
    verify_contract_artifacts,
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


def stage_trust_bundle(
    document: dict[str, object], contract: Path, output: Path,
) -> Path:
    """Validate and stage the public boot-chain identity bundle.

    The contract is emitted beside the exact BL1/BL2 raw images and ELFs that
    it describes.  Validate that source bundle first, then copy it into the
    package and validate the destination again.  This keeps a clean output
    directory working and prevents stale files in a previous package from
    satisfying the contract accidentally.
    """

    source = contract.parent
    verify_contract_artifacts(document, source)
    bundle_files = (
        "bootloader.bin",
        "bootloader.elf",
        "bl2.bin",
        "bl2.elf",
    )
    for name in bundle_files:
        source_path = source / name
        destination = output / name
        try:
            if source_path.resolve() != destination.resolve():
                shutil.copy2(source_path, destination)
        except OSError as error:
            raise TrustChainError(
                f"cannot stage trust artifact {source_path}: {error}"
            ) from error

    trust_path = output / "bk7258-trust-chain.json"
    try:
        if contract.resolve() != trust_path.resolve():
            shutil.copy2(contract, trust_path)
    except OSError as error:
        raise TrustChainError(
            f"cannot stage trust contract {contract}: {error}"
        ) from error
    verify_contract_artifacts(document, output)
    return trust_path


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def copy_flash_segment(path: Path, output: Path, name: str) -> Path:
    """Copy one encoded image and pad it to a complete erase sector."""

    destination = output / name
    payload = path.read_bytes()
    padded_size = align_up(len(payload), ERASE_SIZE)
    destination.write_bytes(payload + b"\xff" * (padded_size - len(payload)))
    return destination


def make_secondary_pair(
    cp: Path, ap: Path, output: Path, file_name: str
) -> Path:
    """Place the same CP/AP generation in the official contiguous B slot."""

    image = bytearray(b"\xff" * PAIR_B_SIZE)
    cp_data = cp.read_bytes()
    ap_data = ap.read_bytes()
    image[: len(cp_data)] = cp_data
    image[CP_A_SIZE : CP_A_SIZE + len(ap_data)] = ap_data
    destination = output / file_name
    destination.write_bytes(image)
    return destination


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--boot", type=Path, required=True)
    parser.add_argument("--cp-raw", type=Path, required=True)
    parser.add_argument("--cp-crc", type=Path, required=True)
    parser.add_argument("--ap-raw", type=Path, required=True)
    parser.add_argument("--ap-crc", type=Path, required=True)
    parser.add_argument("--bl2-primary-crc", type=Path)
    parser.add_argument("--bl2-secondary-crc", type=Path)
    parser.add_argument("--trust-chain", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    for path in (args.boot, args.cp_raw, args.cp_crc, args.ap_raw, args.ap_crc,
                 args.bl2_primary_crc, args.bl2_secondary_crc):
        if path is None:
            continue
        if not path.is_file():
            raise SystemExit(f"missing input: {path}")
    if (args.bl2_primary_crc is None) != (args.bl2_secondary_crc is None):
        raise SystemExit("primary and secondary BL2 images must be supplied together")
    mcuboot_profile = args.bl2_primary_crc is not None
    if mcuboot_profile != (args.trust_chain is not None):
        raise SystemExit(
            "MCUboot packaging requires a trust-chain contract; raw packaging forbids it"
        )
    if args.trust_chain is not None and not args.trust_chain.is_file():
        raise SystemExit(f"missing input: {args.trust_chain}")

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
    secondary_file = "s_app_mcuboot.bin" if mcuboot_profile else "s_app_seed.bin"
    secondary_name = "s_app_mcuboot" if mcuboot_profile else "s_app_seed"
    secondary_pair = make_secondary_pair(
        cp_flash, ap_flash, args.output, secondary_file
    )

    bl2_segments = []
    if args.bl2_primary_crc is not None:
        for image, name, offset in (
            (args.bl2_primary_crc, "bl2_crc.bin", BL2_START),
            (args.bl2_secondary_crc, "bl2_secondary_crc.bin", BL2_SECONDARY_START),
        ):
            if image.stat().st_size == 0 or image.stat().st_size > BL2_SIZE:
                raise SystemExit(
                    f"{name} exceeds its 136 KiB physical BL2 envelope"
                )
            packed = copy_flash_segment(image, args.output, name)
            bl2_segments.append(segment(
                "primary_bl2" if offset == BL2_START else "secondary_bl2",
                packed, offset,
            ))

    if args.bl2_primary_crc is not None and len(bl2_segments) != 2:
        raise SystemExit(
            "MCUboot profile requires exactly primary and secondary BL2 segments"
        )

    trust_chain_entry = None
    if args.trust_chain is not None:
        try:
            trust_document = load_contract(args.trust_chain)
            trust_path = stage_trust_bundle(
                trust_document, args.trust_chain, args.output
            )
        except TrustChainError as error:
            raise SystemExit(f"invalid trust-chain package: {error}") from error
        trust_chain_entry = {
            "file": trust_path.name,
            "length": trust_path.stat().st_size,
            "sha256": sha256(trust_path),
            "preflash_target_match_required": True,
        }

    primary_segments = [
        segment("primary_bootloader", boot, BOOT_START),
        segment("primary_cp_app", cp_flash, CP_A_START),
        segment("primary_ap_app", ap_flash, AP_A_START),
    ]
    secondary_segment = segment(secondary_name, secondary_pair, PAIR_B_START)

    # Keep the vendor-owned usr_config envelope and all reserved ranges out of
    # the migration write set.  The prefix initializes boot/A/B/metadata; a
    # second explicit all-FF segment clears only the authorized LittleFS.

    migration = bytearray(b"\xff" * FACTORY_PREFIX_END)
    for item, path in zip(
        (*primary_segments, secondary_segment),
        (boot, cp_flash, ap_flash, secondary_pair),
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
            "usr_config": [USR_CONFIG_START, USR_CONFIG_START + USR_CONFIG_SIZE],
            "primary_bl2": [BL2_START, BL2_START + BL2_SIZE],
            "secondary_bl2": [BL2_SECONDARY_START, BL2_SECONDARY_END],
            "littlefs": [LITTLEFS_START, LITTLEFS_START + LITTLEFS_SIZE],
            "calibration_tail_start": CALIBRATION_TAIL_START,
        },
        "segments": primary_segments,
        ("secondary_pair" if mcuboot_profile else "secondary_seed"): {
            **secondary_segment,
            "same_pair_as_primary": True,
            "rbl_header_present": False,
            "boot_selectable": mcuboot_profile,
            "format": "mcuboot-cp-ap-pair" if mcuboot_profile else "seed",
            "reason": (
                "board-owned BL2 may select the validated CP/AP pair"
                if mcuboot_profile else
                "layout-migration seed only; the unsigned profile never "
                "selects the secondary pair"
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
            "arguments": [item["bkfil"] for item in
                          (*primary_segments, *bl2_segments)],
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
    if mcuboot_profile:
        manifest["bl2_segments"] = bl2_segments
        manifest["trust_chain"] = trust_chain_entry
        manifest["factory_image"]["bl2_segments"] = bl2_segments
        manifest["factory_image"]["bl2_write_range"] = [
            BL2_START, BL2_SECONDARY_END
        ]
    manifest_path = args.output / "bk7258-dual-image.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(manifest, sort_keys=True))


if __name__ == "__main__":
    main()
