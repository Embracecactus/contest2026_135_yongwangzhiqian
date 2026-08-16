#!/usr/bin/env python3
"""Verify every byte of a packaged BK7258 ADR-004 migration image."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from bk7258_ab_layout import (
    AP_A_SIZE,
    AP_A_START,
    BL2_SECONDARY_START,
    BL2_SIZE,
    BL2_START,
    BOOT_SIZE,
    BOOT_START,
    CALIBRATION_TAIL_START,
    CP_A_SIZE,
    CP_A_START,
    ERASE_SIZE,
    FACTORY_PREFIX_END,
    LAYOUT_ID,
    LAYOUT_INPUT,
    LAYOUT_SHA256,
    LAYOUT_SOURCE,
    LITTLEFS_SIZE,
    LITTLEFS_START,
    MIGRATION_WRITE_END,
    PAIR_B_SIZE,
    PAIR_B_START,
    verify_contract as verify_partition_contract,
)
from bk7258_trust_chain import (
    TrustChainError,
    load_contract,
    verify_contract_artifacts,
)


class VerificationError(RuntimeError):
    """Raised when a packaged byte or manifest contract is unsafe."""


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def read_file(package: Path, name: str) -> bytes:
    path = package / name
    try:
        return path.read_bytes()
    except OSError as error:
        raise VerificationError(f"cannot read {path}: {error}") from error


def require(condition: bool, message: str) -> None:
    if not condition:
        raise VerificationError(message)


def require_ff(payload: bytes, start: int, end: int, name: str) -> None:
    require(start <= end <= len(payload), f"invalid {name} range")
    non_ff = next(
        (offset for offset in range(start, end) if payload[offset] != 0xFF), None
    )
    if non_ff is not None:
        raise VerificationError(
            f"{name} contains non-FF byte at physical 0x{non_ff:06x}"
        )


def check_manifest_file(
    package: Path, entry: dict[str, object], expected_name: str
) -> bytes:
    require(entry.get("file") == expected_name, f"manifest file drift: {expected_name}")
    payload = read_file(package, expected_name)
    require(
        entry.get("sha256") == sha256_bytes(payload),
        f"manifest SHA-256 mismatch: {expected_name}",
    )
    return payload


def verify(
    package: Path,
    partition: Path = LAYOUT_INPUT,
    expected_id: str | None = None,
    expected_sha256: str | None = None,
) -> dict[str, object]:
    verify_partition_contract(partition, expected_id, expected_sha256)
    manifest_path = package / "bk7258-dual-image.json"
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise VerificationError(f"cannot load {manifest_path}: {error}") from error

    require(manifest.get("format") == 2, "manifest format must be 2")
    require(manifest.get("layout_id") == LAYOUT_ID, "manifest layout ID drift")
    require(
        manifest.get("layout_sha256") == LAYOUT_SHA256,
        "manifest layout SHA-256 drift",
    )
    require(manifest.get("writes_enabled") is False, "writes_enabled must stay false")
    embedded_layout = manifest.get("layout")
    require(isinstance(embedded_layout, dict), "embedded layout is missing")
    require(embedded_layout.get("layout_id") == LAYOUT_ID, "embedded layout ID drift")
    require(
        embedded_layout.get("layout_sha256") == LAYOUT_SHA256,
        "embedded layout SHA-256 drift",
    )
    require(
        embedded_layout.get("writes_enabled") is False,
        "embedded writes_enabled must stay false",
    )

    segments = manifest.get("segments")
    require(isinstance(segments, list) and len(segments) == 3, "expected 3 A segments")
    segment_by_name = {
        str(item.get("name")): item for item in segments if isinstance(item, dict)
    }
    require(len(segment_by_name) == 3, "A segment names are invalid or duplicated")

    expected_segments = (
        ("primary_bootloader", "bl_crc.bin", BOOT_START, BOOT_SIZE),
        ("primary_cp_app", "app_crc_flash.bin", CP_A_START, CP_A_SIZE),
        ("primary_ap_app", "app1_crc_flash.bin", AP_A_START, AP_A_SIZE),
    )
    primary_payloads: dict[str, bytes] = {}
    for name, file_name, start, capacity in expected_segments:
        entry = segment_by_name.get(name)
        require(isinstance(entry, dict), f"missing manifest segment: {name}")
        payload = check_manifest_file(package, entry, file_name)
        require(entry.get("physical_offset") == start, f"{name} offset drift")
        require(entry.get("length") == len(payload), f"{name} length drift")
        require(entry.get("physical_end") == start + len(payload), f"{name} end drift")
        require(0 < len(payload) <= capacity, f"{name} exceeds its partition")
        require(len(payload) % ERASE_SIZE == 0, f"{name} is not erase aligned")
        primary_payloads[name] = payload

    boot = primary_payloads["primary_bootloader"]
    cp = primary_payloads["primary_cp_app"]
    ap = primary_payloads["primary_ap_app"]
    require(len(boot) == BOOT_SIZE, "bootloader must fill its exact envelope")

    # Older packages may already contain BL2 segments while still carrying the
    # migration-only `secondary_seed` metadata.  The explicit key is the
    # profile discriminator so a rebuild can validate an old package before
    # replacing it.
    mcuboot_profile = manifest.get("secondary_pair") is not None
    secondary_key = "secondary_pair" if mcuboot_profile else "secondary_seed"
    secondary_file = "s_app_mcuboot.bin" if mcuboot_profile else "s_app_seed.bin"
    secondary_entry = manifest.get(secondary_key)
    require(isinstance(secondary_entry, dict), f"{secondary_key} manifest entry missing")
    secondary = check_manifest_file(package, secondary_entry, secondary_file)
    require(len(secondary) == PAIR_B_SIZE, "secondary seed size drift")
    require(secondary_entry.get("physical_offset") == PAIR_B_START, "B offset drift")
    require(
        secondary_entry.get("physical_end") == PAIR_B_START + PAIR_B_SIZE, "B end drift"
    )
    require(secondary_entry.get("same_pair_as_primary") is True, "B pair marker drift")
    require(secondary_entry.get("rbl_header_present") is False, "unexpected RBL marker")
    require(
        secondary_entry.get("boot_selectable") is mcuboot_profile,
        "B selection metadata does not match package profile",
    )
    require(
        secondary_entry.get("format") ==
        ("mcuboot-cp-ap-pair" if mcuboot_profile else "seed"),
        "B format metadata drift",
    )

    trust_result = None
    trust_entry = manifest.get("trust_chain")
    if mcuboot_profile:
        require(isinstance(trust_entry, dict), "MCUboot trust-chain entry missing")
        trust_payload = check_manifest_file(
            package, trust_entry, "bk7258-trust-chain.json"
        )
        require(
            trust_entry.get("length") == len(trust_payload),
            "trust-chain contract length drift",
        )
        require(
            trust_entry.get("preflash_target_match_required") is True,
            "MCUboot package must require target trust matching",
        )
        trust_document = load_contract(package / "bk7258-trust-chain.json")
        verify_contract_artifacts(trust_document, package)
        trust_result = {
            "contract": "bk7258-trust-chain.json",
            "sha256": sha256_bytes(trust_payload),
            "preflash_target_match_required": True,
        }
    else:
        require(trust_entry is None, "raw package must not carry a trust contract")

    # MCUboot packages carry two board-owned BL2 copies.  Older non-MCUboot
    # packages legitimately omit this optional list, so keep the check
    # conditional while making a present pair byte/offset bounded.
    bl2_entries = manifest.get("bl2_segments")
    if mcuboot_profile:
        require(bl2_entries is not None, "MCUboot BL2 segments are missing")
    if bl2_entries is not None:
        require(
            isinstance(bl2_entries, list) and len(bl2_entries) == 2,
            "expected primary and secondary BL2 segments",
        )
        for entry, expected_name, expected_file, expected_offset in (
            (bl2_entries[0], "primary_bl2", "bl2_crc.bin", BL2_START),
            (bl2_entries[1], "secondary_bl2", "bl2_secondary_crc.bin",
             BL2_SECONDARY_START),
        ):
            require(isinstance(entry, dict), f"invalid {expected_name} entry")
            payload = check_manifest_file(package, entry, expected_file)
            require(entry.get("name") == expected_name, f"{expected_name} name drift")
            require(entry.get("physical_offset") == expected_offset,
                    f"{expected_name} offset drift")
            require(entry.get("length") == len(payload),
                    f"{expected_name} length drift")
            require(entry.get("physical_end") == expected_offset + len(payload),
                    f"{expected_name} end drift")
            require(0 < len(payload) <= BL2_SIZE,
                    f"{expected_name} exceeds its envelope")
            require(len(payload) % ERASE_SIZE == 0,
                    f"{expected_name} is not erase aligned")

    expected_secondary = bytearray(b"\xff" * PAIR_B_SIZE)
    expected_secondary[: len(cp)] = cp
    expected_secondary[CP_A_SIZE : CP_A_SIZE + len(ap)] = ap
    # Keep the historical diagnostic text for the source-level layout gate;
    # it applies to both the migration seed and the MCUboot B pair.
    require(secondary == expected_secondary, "B seed is not a byte-exact A pair copy")

    factory_entry = manifest.get("factory_image")
    require(isinstance(factory_entry, dict), "factory_image manifest entry missing")
    factory = check_manifest_file(package, factory_entry, "all-app-factory.bin")
    require(
        len(factory) == FACTORY_PREFIX_END,
        f"factory prefix must end before usr_config at 0x{FACTORY_PREFIX_END:x}",
    )
    require(factory_entry.get("length") == len(factory), "factory length drift")
    require(
        factory_entry.get("project_write_end") == MIGRATION_WRITE_END,
        "write-end drift",
    )
    require(
        factory_entry.get("calibration_tail_start") == CALIBRATION_TAIL_START,
        "calibration-tail boundary drift",
    )
    require(MIGRATION_WRITE_END < CALIBRATION_TAIL_START, "migration reaches tail")
    require(factory_entry.get("layout_migration") is True, "migration marker missing")
    require(
        factory_entry.get("preserves_usr_config") is True, "usr_config gate missing"
    )
    require(
        factory_entry.get("preserves_reserved_ranges") is True,
        "reserved-range gate missing",
    )
    require(
        factory_entry.get("requires_explicit_owner_gate") is True,
        "owner confirmation gate missing",
    )

    expected_factory = bytearray(b"\xff" * FACTORY_PREFIX_END)
    expected_factory[BOOT_START : BOOT_START + len(boot)] = boot
    expected_factory[CP_A_START : CP_A_START + len(cp)] = cp
    expected_factory[AP_A_START : AP_A_START + len(ap)] = ap
    expected_factory[PAIR_B_START : PAIR_B_START + len(secondary)] = secondary
    require(factory == expected_factory, "factory image contains unexpected bytes")

    require_ff(factory, CP_A_START + len(cp), AP_A_START, "CP-A padding")
    require_ff(factory, AP_A_START + len(ap), PAIR_B_START, "AP-A padding")
    require_ff(
        factory,
        PAIR_B_START + PAIR_B_SIZE,
        FACTORY_PREFIX_END,
        "reserved factory padding",
    )

    migration_segments = manifest.get("migration_segments")
    require(
        isinstance(migration_segments, list) and len(migration_segments) == 2,
        "expected two bounded migration segments",
    )
    migration_by_name = {
        str(item.get("name")): item
        for item in migration_segments
        if isinstance(item, dict)
    }
    prefix_entry = migration_by_name.get("factory_prefix")
    clear_entry = migration_by_name.get("littlefs_clear")
    require(isinstance(prefix_entry, dict), "factory_prefix segment missing")
    require(isinstance(clear_entry, dict), "littlefs_clear segment missing")
    prefix_again = check_manifest_file(package, prefix_entry, "all-app-factory.bin")
    require(prefix_again == factory, "factory prefix segment mismatch")
    require(prefix_entry.get("physical_offset") == BOOT_START, "prefix offset drift")
    require(
        prefix_entry.get("physical_end") == FACTORY_PREFIX_END,
        "prefix end crosses usr_config",
    )
    littlefs_clear = check_manifest_file(
        package, clear_entry, "littlefs_factory_clear.bin"
    )
    require(len(littlefs_clear) == LITTLEFS_SIZE, "LittleFS clear size drift")
    require(
        clear_entry.get("physical_offset") == LITTLEFS_START, "LittleFS offset drift"
    )
    require(
        clear_entry.get("physical_end") == MIGRATION_WRITE_END,
        "LittleFS clear end drift",
    )
    require_ff(littlefs_clear, 0, len(littlefs_clear), "LittleFS clear image")

    expected_ranges = [
        [BOOT_START, FACTORY_PREFIX_END],
        [LITTLEFS_START, MIGRATION_WRITE_END],
    ]
    require(factory_entry.get("write_ranges") == expected_ranges, "write ranges drift")
    expected_loader_entries = [prefix_entry, clear_entry]
    if isinstance(bl2_entries, list):
        expected_loader_entries.extend(bl2_entries)
    expected_loader_entries.sort(key=lambda entry: entry.get("physical_offset"))
    expected_loader_arguments = [
        entry.get("bkfil") for entry in expected_loader_entries
    ]
    require(
        factory_entry.get("loader_arguments") == expected_loader_arguments,
        "loader argument manifest drift",
    )

    raw_images = manifest.get("raw_images")
    crc_images = manifest.get("crc_images")
    require(isinstance(raw_images, dict), "raw image manifest missing")
    require(isinstance(crc_images, dict), "CRC image manifest missing")
    for table, role, expected_name in (
        (raw_images, "cp", "app.bin"),
        (raw_images, "ap", "app1.bin"),
        (crc_images, "cp", "app_crc.bin"),
        (crc_images, "ap", "app1_crc.bin"),
    ):
        entry = table.get(role)
        require(isinstance(entry, dict), f"missing {expected_name} manifest entry")
        check_manifest_file(package, entry, expected_name)

    result = {
        "format": 1,
        "status": "pass",
        "layout_source": LAYOUT_SOURCE,
        "layout_id": LAYOUT_ID,
        "layout_sha256": LAYOUT_SHA256,
        "package": str(package.resolve()),
        "factory_image": {
            "length": len(factory),
            "sha256": sha256_bytes(factory),
            "write_ranges": expected_ranges,
            "write_end": MIGRATION_WRITE_END,
        },
        "littlefs_clear": {
            "length": len(littlefs_clear),
            "sha256": sha256_bytes(littlefs_clear),
        },
        "primary": {
            "boot_length": len(boot),
            "cp_length": len(cp),
            "ap_length": len(ap),
        },
        "secondary": {
            "length": len(secondary),
            "same_pair_as_primary": True,
            "boot_selectable": mcuboot_profile,
        },
        "calibration_tail": {
            "start": CALIBRATION_TAIL_START,
            "included_in_image": False,
        },
        "usr_config": {"included_in_images": False},
        "writes_enabled": False,
    }
    if trust_result is not None:
        result["trust_chain"] = trust_result
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--partition", type=Path, default=LAYOUT_INPUT)
    parser.add_argument("--expect-layout-id")
    parser.add_argument("--expect-layout-sha256")
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    try:
        result = verify(
            args.package,
            args.partition,
            args.expect_layout_id,
            args.expect_layout_sha256,
        )
    except (TrustChainError, VerificationError, OSError, ValueError) as error:
        print(f"FAIL bk7258-factory-layout: {error}")
        return 1

    if args.json is not None:
        args.json.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    mcuboot_profile = bool(result["secondary"]["boot_selectable"])
    print(
        "PASS bk7258-factory-layout: "
        f"prefix=0x{FACTORY_PREFIX_END:x} fs-clear=0x{LITTLEFS_SIZE:x} "
        + ("B=mcuboot-selectable " if mcuboot_profile else
           "B=same-pair/non-selectable ")
        + "usr_config=preserved "
        f"tail=0x{CALIBRATION_TAIL_START:x} untouched"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
