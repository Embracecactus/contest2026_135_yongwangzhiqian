#!/usr/bin/env python3
"""Compatibility API backed by the generated BK7258 partition CSV model.

New code should prefer roles from :mod:`gen_bk7258_partitions`.  The exported
constants come from the repository-owned CSV instead of a second hand-written
layout table.
"""

from __future__ import annotations

import argparse
import json
import os
from dataclasses import asdict, dataclass
from pathlib import Path

from gen_bk7258_partitions import (
    DEFAULT_INPUT,
    PartitionLayoutError,
    REPOSITORY_ROOT,
    load_layout,
    parse_size,
    verify_sdk_compatibility,
)


class LayoutError(PartitionLayoutError):
    """Backward-compatible name for partition/layout validation failures."""


def _load_active_layout():
    source = os.environ.get("BK7258_PARTITION_LAYOUT_SOURCE") or None
    expected_id = os.environ.get("BK7258_PARTITION_LAYOUT_ID") or None
    expected_sha256 = os.environ.get("BK7258_PARTITION_LAYOUT_SHA256") or None
    supplied = tuple(value is not None for value in (
        source, expected_id, expected_sha256
    ))
    if any(supplied) and not all(supplied):
        raise LayoutError(
            "BK7258_PARTITION_LAYOUT_SOURCE, BK7258_PARTITION_LAYOUT_ID and "
            "BK7258_PARTITION_LAYOUT_SHA256 must be exported together"
        )

    if source is None:
        active_input = DEFAULT_INPUT.resolve()
    else:
        selected = Path(source)
        active_input = (
            selected if selected.is_absolute() else REPOSITORY_ROOT / selected
        ).resolve()
    try:
        layout = load_layout(active_input)
    except PartitionLayoutError as error:
        raise LayoutError(str(error)) from error
    if expected_id is not None and (
        layout.layout_id != expected_id
        or layout.layout_sha256 != expected_sha256
    ):
        raise LayoutError(
            "active partition layout identity mismatch: "
            f"expected={expected_id}/{expected_sha256} "
            f"observed={layout.layout_id}/{layout.layout_sha256}"
        )
    return active_input, layout


LAYOUT_INPUT, _LAYOUT = _load_active_layout()
_BOOT = _LAYOUT.by_role("boot")
_CP_A = _LAYOUT.by_role("slot_a_cp")
_AP_A = _LAYOUT.by_role("slot_a_ap")
_PAIR_B = _LAYOUT.by_role("slot_b_pair")
_USR_CONFIG = _LAYOUT.by_role("vendor_config")
_MANIFEST_PRIMARY = _LAYOUT.by_role("bl1_primary_manifest")
_MANIFEST_SECONDARY = _LAYOUT.by_role("bl1_secondary_manifest")
_BL2 = _LAYOUT.by_role("bl2")
_LITTLEFS = _LAYOUT.by_role("littlefs")
_EASYFLASH = _LAYOUT.by_role("easyflash_cp")

LAYOUT_ID = _LAYOUT.layout_id
LAYOUT_SHA256 = _LAYOUT.layout_sha256
LAYOUT_SOURCE = str(_LAYOUT.report()["source"])
FLASH_SIZE = _LAYOUT.flash_size
ERASE_SIZE = _LAYOUT.erase_size

BOOT_START = _BOOT.offset
BOOT_SIZE = _BOOT.size
CP_A_START = _CP_A.offset
CP_A_SIZE = _CP_A.size
AP_A_START = _AP_A.offset
AP_A_SIZE = _AP_A.size
PAIR_B_START = _PAIR_B.offset
PAIR_B_SIZE = _PAIR_B.size
USR_CONFIG_START = _USR_CONFIG.offset
USR_CONFIG_SIZE = _USR_CONFIG.size
BL1_MANIFEST_PRIMARY_START = _MANIFEST_PRIMARY.offset
BL1_MANIFEST_PRIMARY_SIZE = _MANIFEST_PRIMARY.size
BL1_MANIFEST_SECONDARY_START = _MANIFEST_SECONDARY.offset
BL1_MANIFEST_SECONDARY_SIZE = _MANIFEST_SECONDARY.size
BL2_START = _BL2.offset
BL2_SIZE = _BL2.size
BL2_SECONDARY_START = BL2_START + BL2_SIZE
BL2_SECONDARY_SIZE = BL2_SIZE
BL2_SECONDARY_END = BL2_SECONDARY_START + BL2_SECONDARY_SIZE
LITTLEFS_START = _LITTLEFS.offset
LITTLEFS_SIZE = _LITTLEFS.size
CALIBRATION_TAIL_START = _EASYFLASH.offset
FACTORY_PREFIX_END = _PAIR_B.end
MIGRATION_WRITE_END = _LITTLEFS.end

CP_XIP_START = _LAYOUT.xip_base + _LAYOUT.logical_offset(_CP_A)
CP_XIP_SIZE = _LAYOUT.logical_size(_CP_A)
AP_XIP_START = _LAYOUT.xip_base + _LAYOUT.logical_offset(_AP_A)
AP_XIP_SIZE = _LAYOUT.logical_size(_AP_A)


@dataclass(frozen=True)
class Region:
    name: str
    start: int
    size: int
    policy: str

    @property
    def end(self) -> int:
        return self.start + self.size

    def report(self) -> dict[str, object]:
        result = asdict(self)
        result.update(
            {
                "start_hex": f"0x{self.start:06x}",
                "end": self.end,
                "end_hex": f"0x{self.end:06x}",
                "size_hex": f"0x{self.size:x}",
            }
        )
        return result


REGIONS = (
    Region(_BOOT.name, _BOOT.offset, _BOOT.size, "official-envelope"),
    Region(_CP_A.name, _CP_A.offset, _CP_A.size, "primary-a"),
    Region(_AP_A.name, _AP_A.offset, _AP_A.size, "primary-a"),
    Region(_PAIR_B.name, _PAIR_B.offset, _PAIR_B.size, "paired-b"),
    Region(
        "reserved_before_usr_config",
        _PAIR_B.end,
        _USR_CONFIG.offset - _PAIR_B.end,
        "unallocated",
    ),
    Region(
        _USR_CONFIG.name,
        _USR_CONFIG.offset,
        _USR_CONFIG.size,
        "vendor-reserved",
    ),
    Region(
        "reserved_before_bl1_manifest",
        _USR_CONFIG.end,
        _MANIFEST_PRIMARY.offset - _USR_CONFIG.end,
        "unallocated",
    ),
    Region(
        _MANIFEST_PRIMARY.name,
        _MANIFEST_PRIMARY.offset,
        _MANIFEST_PRIMARY.size,
        "bl1-primary-manifest",
    ),
    Region(
        _MANIFEST_SECONDARY.name,
        _MANIFEST_SECONDARY.offset,
        _MANIFEST_SECONDARY.size,
        "bl1-secondary-manifest",
    ),
    Region(
        "reserved_before_littlefs",
        _MANIFEST_SECONDARY.end,
        _LITTLEFS.offset - _MANIFEST_SECONDARY.end,
        "unallocated",
    ),
    Region(_LITTLEFS.name, _LITTLEFS.offset, _LITTLEFS.size, "cp-raw-owner"),
    Region(
        "reserved_after_littlefs",
        _LITTLEFS.end,
        _EASYFLASH.offset - _LITTLEFS.end,
        "unallocated",
    ),
    Region(
        "official_tail",
        _EASYFLASH.offset,
        FLASH_SIZE - _EASYFLASH.offset,
        "immutable-to-project-flash",
    ),
)

OFFICIAL_ROWS = tuple(
    (name, _LAYOUT.by_name(name).offset, _LAYOUT.by_name(name).size)
    for name in (
        "primary_bootloader",
        "primary_cp_app",
        "primary_ap_app",
        "s_app",
        "usr_config",
        "easyflash",
        "easyflash_ap",
        "sys_rf",
        "sys_net",
    )
)


def crc_physical_size(logical_size: int) -> int:
    if logical_size < 0 or logical_size % _LAYOUT.crc_data_size:
        raise LayoutError(
            "CRC-expanded logical sizes must be 32-byte aligned"
        )
    return (
        logical_size // _LAYOUT.crc_data_size * _LAYOUT.crc_total_size
    )


def verify_layout() -> None:
    """Re-evaluate the CSV and compatibility aliases before packaging."""

    try:
        observed = load_layout(LAYOUT_INPUT)
    except PartitionLayoutError as error:
        raise LayoutError(str(error)) from error
    if observed.layout_id != LAYOUT_ID or observed.layout_sha256 != LAYOUT_SHA256:
        raise LayoutError("partition CSV changed after this module was imported")
    expected_start = 0
    for region in REGIONS:
        if region.start != expected_start:
            raise LayoutError(
                f"layout gap/overlap before {region.name}: "
                f"0x{expected_start:x} != 0x{region.start:x}"
            )
        expected_start = region.end
    if expected_start != FLASH_SIZE:
        raise LayoutError("layout does not cover the exact Flash capacity")


def verify_contract(
    source: Path | None = None,
    expected_id: str | None = None,
    expected_sha256: str | None = None,
) -> None:
    """Bind an explicit tool invocation to the already selected layout."""

    if source is not None and source.resolve() != LAYOUT_INPUT:
        raise LayoutError(
            f"partition source mismatch: {source.resolve()} != {LAYOUT_INPUT}"
        )
    if (expected_id is None) != (expected_sha256 is None):
        raise LayoutError("expected layout ID and SHA-256 must be supplied together")
    if expected_id is not None and (
        expected_id != LAYOUT_ID or expected_sha256 != LAYOUT_SHA256
    ):
        raise LayoutError(
            "partition identity mismatch: "
            f"expected={expected_id}/{expected_sha256} "
            f"active={LAYOUT_ID}/{LAYOUT_SHA256}"
        )
    verify_layout()


def verify_official_sdk(source: Path) -> dict[str, object]:
    try:
        result = verify_sdk_compatibility(_LAYOUT, source)
    except PartitionLayoutError as error:
        raise LayoutError(str(error)) from error
    # Keep historical result keys for callers and archived evidence readers.
    result["partition_csv"] = result["reference_csv"]
    result["partition_csv_sha256"] = result["reference_csv_sha256"]
    return result


def report(sdk_source: Path | None = None) -> dict[str, object]:
    verify_layout()
    result: dict[str, object] = {
        "format": 2,
        "layout_id": LAYOUT_ID,
        "layout_sha256": LAYOUT_SHA256,
        "layout_source": LAYOUT_SOURCE,
        "flash_size": FLASH_SIZE,
        "erase_size": ERASE_SIZE,
        "regions": [region.report() for region in REGIONS],
        "partitions": _LAYOUT.report()["partitions"],
        "xip": {
            "cp": [CP_XIP_START, CP_XIP_START + CP_XIP_SIZE],
            "ap": [AP_XIP_START, AP_XIP_START + AP_XIP_SIZE],
        },
        "pair": {
            "primary_start": CP_A_START,
            "primary_end": AP_A_START + AP_A_SIZE,
            "secondary_start": PAIR_B_START,
            "size": PAIR_B_SIZE,
            "single_offset_compatible": True,
        },
        "bl1_manifest_sectors": [
            [
                BL1_MANIFEST_PRIMARY_START,
                BL1_MANIFEST_PRIMARY_START + BL1_MANIFEST_PRIMARY_SIZE,
            ],
            [
                BL1_MANIFEST_SECONDARY_START,
                BL1_MANIFEST_SECONDARY_START + BL1_MANIFEST_SECONDARY_SIZE,
            ],
        ],
        "migration": {
            "write_ranges": [
                [BOOT_START, FACTORY_PREFIX_END],
                [LITTLEFS_START, MIGRATION_WRITE_END],
            ],
            "project_write_end": MIGRATION_WRITE_END,
            "calibration_tail_start": CALIBRATION_TAIL_START,
            "chip_erase_allowed": False,
            "preserve_usr_config": True,
            "destructive_factory_requires_fresh_owner_authority": True,
        },
        "status": "accepted-layout-csv-host-verified",
        "writes_enabled": False,
    }
    if sdk_source is not None:
        result["official_sdk"] = verify_official_sdk(sdk_source)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk-source", type=Path)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    try:
        result = report(args.sdk_source)
    except (LayoutError, OSError, ValueError) as error:
        print(f"BK7258 accepted A/B layout FAIL: {error}")
        return 1
    encoded = json.dumps(result, indent=2, sort_keys=True)
    if args.json:
        print(encoded)
    else:
        print(
            "BK7258 accepted A/B layout PASS: "
            f"layout_id={LAYOUT_ID} writes_enabled=false"
        )
        print(encoded)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
