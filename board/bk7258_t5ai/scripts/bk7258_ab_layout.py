#!/usr/bin/env python3
"""Compatibility API backed by the generated BK7258 partition CSV model.

New code should prefer roles from :mod:`gen_bk7258_partitions`.  The exported
constants remain stable for existing N15 packers and verifiers while their
values now come from the repository-owned CSV instead of a second hand-written
layout table.
"""

from __future__ import annotations

import argparse
import json
from dataclasses import asdict, dataclass
from pathlib import Path

from gen_bk7258_partitions import (
    DEFAULT_INPUT,
    PartitionLayoutError,
    load_layout,
    parse_size,
    verify_sdk_compatibility,
)


class LayoutError(PartitionLayoutError):
    """Backward-compatible name for partition/layout validation failures."""


_LAYOUT = load_layout(DEFAULT_INPUT)
_BOOT = _LAYOUT.by_role("boot")
_CP_A = _LAYOUT.by_role("slot_a_cp")
_AP_A = _LAYOUT.by_role("slot_a_ap")
_PAIR_B = _LAYOUT.by_role("slot_b_pair")
_METADATA_PRIMARY = _LAYOUT.by_role("ota_metadata_primary")
_USR_CONFIG = _LAYOUT.by_role("vendor_config")
_METADATA_MIRROR = _LAYOUT.by_role("ota_metadata_mirror")
_LITTLEFS = _LAYOUT.by_role("littlefs")
_EASYFLASH = _LAYOUT.by_role("easyflash_cp")

LAYOUT_ID = _LAYOUT.layout_id
LAYOUT_SHA256 = _LAYOUT.layout_sha256
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
OTA_METADATA_START = _METADATA_PRIMARY.offset
OTA_METADATA_SIZE = _METADATA_PRIMARY.size
USR_CONFIG_START = _USR_CONFIG.offset
USR_CONFIG_SIZE = _USR_CONFIG.size
OTA_METADATA_MIRROR_START = _METADATA_MIRROR.offset
OTA_METADATA_MIRROR_SIZE = _METADATA_MIRROR.size
LITTLEFS_START = _LITTLEFS.offset
LITTLEFS_SIZE = _LITTLEFS.size
CALIBRATION_TAIL_START = _EASYFLASH.offset
FACTORY_PREFIX_END = _METADATA_PRIMARY.end
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
        _METADATA_PRIMARY.name,
        _METADATA_PRIMARY.offset,
        _METADATA_PRIMARY.size,
        "trial-metadata-primary",
    ),
    Region(
        _USR_CONFIG.name,
        _USR_CONFIG.offset,
        _USR_CONFIG.size,
        "vendor-reserved",
    ),
    Region(
        _METADATA_MIRROR.name,
        _METADATA_MIRROR.offset,
        _METADATA_MIRROR.size,
        "trial-metadata-mirror",
    ),
    Region(
        "reserved_before_littlefs",
        _METADATA_MIRROR.end,
        _LITTLEFS.offset - _METADATA_MIRROR.end,
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
        "ota_fina_executive",
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
        observed = load_layout(DEFAULT_INPUT)
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
        "layout_source": "board/bk7258_t5ai/partitions/bk7258/auto_partitions.csv",
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
        "metadata_banks": [
            [OTA_METADATA_START, OTA_METADATA_START + OTA_METADATA_SIZE],
            [
                OTA_METADATA_MIRROR_START,
                OTA_METADATA_MIRROR_START + OTA_METADATA_MIRROR_SIZE,
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
            "preserve_metadata_mirror_until_n15_o": True,
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
