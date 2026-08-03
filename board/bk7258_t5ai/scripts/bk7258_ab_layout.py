#!/usr/bin/env python3
"""Canonical host model for the accepted BK7258 v3.1.1.9 A/B layout."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
from dataclasses import asdict, dataclass
from pathlib import Path


LAYOUT_ID = "bk7258-v3.1.1.9-contiguous-ab-v1"
FLASH_SIZE = 0x800000
ERASE_SIZE = 0x1000

BOOT_START = 0x000000
BOOT_SIZE = 0x011000
CP_A_START = 0x011000
CP_A_SIZE = 0x154000
AP_A_START = 0x165000
AP_A_SIZE = 0x121000
PAIR_B_START = 0x286000
PAIR_B_SIZE = 0x275000
OTA_METADATA_START = 0x4FB000
OTA_METADATA_SIZE = 0x001000
USR_CONFIG_START = 0x4FC000
USR_CONFIG_SIZE = 0x00E000
LITTLEFS_START = 0x600000
LITTLEFS_SIZE = 0x100000
CALIBRATION_TAIL_START = 0x7FA000
FACTORY_PREFIX_END = OTA_METADATA_START + OTA_METADATA_SIZE
MIGRATION_WRITE_END = LITTLEFS_START + LITTLEFS_SIZE

CP_XIP_START = 0x02010000
CP_XIP_SIZE = 0x140000
AP_XIP_START = 0x02150000
AP_XIP_SIZE = 0x110000


class LayoutError(RuntimeError):
    """Raised when an address-domain or official-layout contract drifts."""


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
    Region("primary_bootloader", BOOT_START, BOOT_SIZE, "official-envelope"),
    Region("primary_cp_app", CP_A_START, CP_A_SIZE, "primary-a"),
    Region("primary_ap_app", AP_A_START, AP_A_SIZE, "primary-a"),
    Region("s_app", PAIR_B_START, PAIR_B_SIZE, "paired-b"),
    Region(
        "ota_fina_executive",
        OTA_METADATA_START,
        OTA_METADATA_SIZE,
        "trial-metadata",
    ),
    Region("usr_config", USR_CONFIG_START, USR_CONFIG_SIZE, "vendor-reserved"),
    Region(
        "reserved_before_littlefs",
        USR_CONFIG_START + USR_CONFIG_SIZE,
        LITTLEFS_START - (USR_CONFIG_START + USR_CONFIG_SIZE),
        "unallocated",
    ),
    Region("littlefs", LITTLEFS_START, LITTLEFS_SIZE, "cp-raw-owner"),
    Region(
        "reserved_after_littlefs",
        LITTLEFS_START + LITTLEFS_SIZE,
        CALIBRATION_TAIL_START - (LITTLEFS_START + LITTLEFS_SIZE),
        "unallocated",
    ),
    Region(
        "official_tail",
        CALIBRATION_TAIL_START,
        FLASH_SIZE - CALIBRATION_TAIL_START,
        "immutable-to-project-flash",
    ),
)


OFFICIAL_ROWS = (
    ("primary_bootloader", BOOT_START, BOOT_SIZE),
    ("primary_cp_app", CP_A_START, CP_A_SIZE),
    ("primary_ap_app", AP_A_START, AP_A_SIZE),
    ("s_app", PAIR_B_START, PAIR_B_SIZE),
    ("ota_fina_executive", OTA_METADATA_START, OTA_METADATA_SIZE),
    ("usr_config", USR_CONFIG_START, USR_CONFIG_SIZE),
    ("easyflash", 0x7FA000, 0x2000),
    ("easyflash_ap", 0x7FC000, 0x2000),
    ("sys_rf", 0x7FE000, 0x1000),
    ("sys_net", 0x7FF000, 0x1000),
)


def crc_physical_size(logical_size: int) -> int:
    if logical_size < 0 or logical_size % 32:
        raise LayoutError("CRC-expanded logical sizes must be 32-byte aligned")
    return logical_size // 32 * 34


def parse_size(value: str) -> int:
    normalized = value.strip().lower()
    multiplier = 1
    if normalized.endswith("k"):
        multiplier = 1024
        normalized = normalized[:-1]
    elif normalized.endswith("m"):
        multiplier = 1024 * 1024
        normalized = normalized[:-1]
    return int(normalized, 0) * multiplier


def verify_layout() -> None:
    expected_start = 0
    for region in REGIONS:
        if region.start != expected_start:
            raise LayoutError(
                f"layout gap/overlap before {region.name}: "
                f"0x{expected_start:x} != 0x{region.start:x}"
            )
        if region.start % ERASE_SIZE or region.size % ERASE_SIZE:
            raise LayoutError(f"{region.name} is not erase-sector aligned")
        expected_start = region.end
    if expected_start != FLASH_SIZE:
        raise LayoutError("layout does not cover the exact 8 MiB Flash")
    if CP_A_START != crc_physical_size(CP_XIP_START - 0x02000000):
        raise LayoutError("CP logical/raw start conversion drift")
    if CP_A_SIZE != crc_physical_size(CP_XIP_SIZE):
        raise LayoutError("CP logical/raw size conversion drift")
    if AP_A_START != crc_physical_size(AP_XIP_START - 0x02000000):
        raise LayoutError("AP logical/raw start conversion drift")
    if AP_A_SIZE != crc_physical_size(AP_XIP_SIZE):
        raise LayoutError("AP logical/raw size conversion drift")
    if CP_A_SIZE + AP_A_SIZE != PAIR_B_SIZE:
        raise LayoutError("primary pair and s_app sizes differ")


def verify_official_sdk(source: Path) -> dict[str, object]:
    if source.name != "bk_avdk_smp-release-v3.1.1.9":
        raise LayoutError("SDK source must be the exact v3.1.1.9 release directory")
    csv_path = source / "projects/app_ab/partitions/bk7258/auto_partitions.csv"
    position_path = (
        source / "projects/app_ab/partitions/bk7258/ab_position_independent.csv"
    )
    try:
        lines = [
            line
            for line in csv_path.read_text(encoding="utf-8").splitlines()
            if line and not line.startswith("#")
        ]
        position_text = position_path.read_text(encoding="utf-8")
    except OSError as error:
        raise LayoutError(f"cannot read official app_ab inputs: {error}") from error

    rows = list(
        csv.DictReader(
            lines, fieldnames=("name", "offset", "size", "type", "read", "write")
        )
    )
    cursor = 0
    observed: list[tuple[str, int, int]] = []
    for row in rows:
        offset_text = row["offset"].strip()
        start = int(offset_text, 0) if offset_text else cursor
        size = parse_size(row["size"])
        observed.append((row["name"].strip(), start, size))
        cursor = start + size
    if tuple(observed) != OFFICIAL_ROWS:
        raise LayoutError(f"official app_ab partition rows drifted: {observed!r}")
    if "pos_independent,TRUE" not in position_text.replace("\r", ""):
        raise LayoutError("official position-independent AB switch is not TRUE")

    return {
        "release": "v3.1.1.9",
        "source": str(source.resolve()),
        "partition_csv": str(csv_path.resolve()),
        "partition_csv_sha256": hashlib.sha256(csv_path.read_bytes()).hexdigest(),
        "position_csv_sha256": hashlib.sha256(position_path.read_bytes()).hexdigest(),
        "position_independent": True,
    }


def report(sdk_source: Path | None = None) -> dict[str, object]:
    verify_layout()
    result: dict[str, object] = {
        "format": 1,
        "layout_id": LAYOUT_ID,
        "flash_size": FLASH_SIZE,
        "erase_size": ERASE_SIZE,
        "regions": [region.report() for region in REGIONS],
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
        "status": "accepted-layout-host-verified",
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
