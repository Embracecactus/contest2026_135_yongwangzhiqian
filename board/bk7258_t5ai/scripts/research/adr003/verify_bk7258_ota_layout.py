#!/usr/bin/env python3
"""Calculate and fail-closed verify the proposed BK7258 N15 OTA layout.

This tool is read-only.  It distinguishes raw physical flash offsets from
CRC-decoded XIP offsets, detects the current packer/MTD boundary mismatch,
calculates a conditional safe staging layout, and proves whether the official
single-offset A/B remap can preserve the current layout.  It never reads from
or writes to a board.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import importlib.util
import json
import re
import sys
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any


FLASH_SIZE = 0x800000
ERASE_SIZE = 0x1000

BOOT_START = 0x000000
BOOT_END = 0x011000
CP_A_START = 0x011000
CP_A_LEGACY_DECLARED_END = 0x110000

# bk7258_flash_mtd.c passes BK7258_DATA_FLASH_OFFSET directly to the official
# raw bk_flash_* APIs.  The official v3.1.1.9 partition implementation and
# generated partition table prove that those APIs consume physical offsets.
# Therefore the persisted LittleFS bytes start at raw physical 0x100000, not
# at the CRC-expanded 0x110000 coordinate used by the original packer guard.

LITTLEFS_RAW_START = 0x100000
LITTLEFS_RAW_END = 0x200000
CP_A_SAFE_END = LITTLEFS_RAW_START
CRC_ADDRESS_GAP_START = LITTLEFS_RAW_END
CRC_ADDRESS_GAP_END = 0x220000
AP_A_START = 0x220000
AP_A_END = 0x440000

CP_B_START = 0x440000
CP_B_END = 0x52F000
AP_B_START = 0x52F000
AP_B_END = 0x74F000
JOURNAL_COPY_SIZE = 0x013000
FORWARD_JOURNAL_0_START = 0x74F000
FORWARD_JOURNAL_1_START = 0x762000
REVERSE_JOURNAL_0_START = 0x775000
REVERSE_JOURNAL_1_START = 0x788000
SCRATCH_START = 0x79B000
OTA_RESERVED_START = 0x79C000

OFFICIAL_TAIL_START = 0x7FA000
EASYFLASH_START = 0x7FA000
EASYFLASH_AP_START = 0x7FC000
SYS_RF_START = 0x7FE000
SYS_NET_START = 0x7FF000

SDK_RELEASE = "v3.1.1.9"
SDK_NORMAL_BOOT_SHA256 = (
    "105161bb603eedafbffcb5efb8f7c06a0c8503e42ba4da46490c2c21ed813de6"
)
SDK_AB_BOOT_SHA256 = "3b27958ef78cbb7e56b57695585008465c759a7671cfd776334fec49d3164047"


class VerificationError(RuntimeError):
    """Raised when an N15 layout or provenance gate fails."""


@dataclass(frozen=True)
class Region:
    name: str
    start: int
    end: int
    policy: str

    @property
    def size(self) -> int:
        return self.end - self.start

    def to_json(self) -> dict[str, str | int]:
        result = asdict(self)
        result.update(
            {
                "start_hex": f"0x{self.start:06x}",
                "end_hex": f"0x{self.end:06x}",
                "size": self.size,
                "size_hex": f"0x{self.size:x}",
            }
        )
        return result


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def parse_size(value: str) -> int:
    match = re.fullmatch(r"\s*(\d+)\s*([KkMm]?)\s*", value)
    if not match:
        raise VerificationError(f"unsupported size: {value!r}")
    scale = {"": 1, "k": 1024, "m": 1024 * 1024}[match.group(2).lower()]
    return int(match.group(1)) * scale


def require(condition: bool, message: str) -> None:
    if not condition:
        raise VerificationError(message)


def require_aligned(region: Region) -> None:
    require(region.start % ERASE_SIZE == 0, f"{region.name} start is unaligned")
    require(region.end % ERASE_SIZE == 0, f"{region.name} end is unaligned")
    require(region.start < region.end, f"{region.name} is empty or reversed")


def load_python_module(script: Path, name: str) -> Any:
    spec = importlib.util.spec_from_file_location(name, script)
    if spec is None or spec.loader is None:
        raise VerificationError(f"cannot import Python module: {script}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def verify_dual_packer(script: Path) -> dict[str, str]:
    require(script.is_file(), f"dual-image packer missing: {script}")
    packer = load_python_module(script, "bk7258_dual_packer")
    expected = {
        "BOOT_PHYSICAL_OFFSET": BOOT_START,
        "BOOT_PHYSICAL_SIZE": BOOT_END - BOOT_START,
        "CP_PHYSICAL_OFFSET": CP_A_START,
        "DATA_PHYSICAL_OFFSET": LITTLEFS_RAW_START,
        "AP_PHYSICAL_OFFSET": AP_A_START,
        "AP_PHYSICAL_END": AP_A_END,
        "FLASH_ERASE_SIZE": ERASE_SIZE,
    }
    for name, value in expected.items():
        observed = getattr(packer, name, None)
        require(
            observed == value,
            f"pack_dual_image.py {name}: expected 0x{value:x}, got {observed!r}",
        )
    return {
        "path": str(script),
        "sha256": sha256(script),
        "raw_littlefs_boundary": f"0x{LITTLEFS_RAW_START:06x}",
        "safe_for_raw_mtd": "true",
    }


def verify_mtd_raw_range(board: Path) -> dict[str, str | int]:
    amp = board / "chip" / "include" / "bk7258_amp.h"
    mtd = board / "chip" / "cp" / "bk7258_flash_mtd.c"
    require(amp.is_file(), f"BK7258 AMP layout header missing: {amp}")
    require(mtd.is_file(), f"BK7258 MTD wrapper missing: {mtd}")

    amp_text = amp.read_text(encoding="utf-8")
    mtd_text = mtd.read_text(encoding="utf-8")
    require(
        re.search(
            r"#define\s+BK7258_DATA_RAW_PHYSICAL_OFFSET\s+0x00100000u",
            amp_text,
        )
        is not None,
        "BK7258_DATA_RAW_PHYSICAL_OFFSET drifted from 0x100000",
    )
    require(
        re.search(
            r"#define\s+BK7258_DATA_RAW_PHYSICAL_SIZE\s+0x00100000u",
            amp_text,
        )
        is not None,
        "BK7258_DATA_RAW_PHYSICAL_SIZE drifted from 1 MiB",
    )
    require(
        "#define BK7258_DATA_PART_BASE       "
        "BK7258_DATA_RAW_PHYSICAL_OFFSET" in mtd_text,
        "MTD no longer derives its base from the raw physical constant",
    )
    for api in (
        "bk_flash_read_bytes",
        "bk_flash_erase_sector",
        "bk_flash_write_bytes",
    ):
        require(api in mtd_text, f"MTD no longer calls official {api}")

    return {
        "amp_header": str(amp),
        "amp_header_sha256": sha256(amp),
        "mtd_wrapper": str(mtd),
        "mtd_wrapper_sha256": sha256(mtd),
        "physical_start": LITTLEFS_RAW_START,
        "physical_end": LITTLEFS_RAW_END,
    }


def verify_team_safety_gates(board: Path) -> dict[str, str | int | bool]:
    postbuild = board / "scripts" / "postbuild.sh"
    debugger = board / "scripts" / "bk7258_auto_debug.sh"
    engine = board / "bootloader" / "boot_ota_engine.h"
    for path in (postbuild, debugger, engine):
        require(path.is_file(), f"team safety gate missing: {path}")

    postbuild_text = postbuild.read_text(encoding="utf-8")
    debugger_text = debugger.read_text(encoding="utf-8")
    engine_text = engine.read_text(encoding="utf-8")
    require(
        'MAX_SIZE="0x000e0f00"' in postbuild_text,
        "CP post-build raw-image maximum is not the safe 0xe0f00",
    )
    require(
        "0x11000 + CP_IMAGE_SIZE <= 0x100000" in debugger_text,
        "sparse Flash guard does not stop CP at raw LittleFS 0x100000",
    )
    require(
        re.search(
            r"^#define\s+BK7258_OTA_ENGINE_WRITE_GATE\s+0u\s*$",
            engine_text,
            re.MULTILINE,
        )
        is not None,
        "N15 SRAM Flash mutation gate is not hard-disabled",
    )
    return {
        "postbuild": str(postbuild),
        "postbuild_sha256": sha256(postbuild),
        "debug_sop": str(debugger),
        "debug_sop_sha256": sha256(debugger),
        "ota_engine_header": str(engine),
        "ota_engine_header_sha256": sha256(engine),
        "ota_engine_write_gate": 0,
        "writes_enabled": False,
    }


def verify_journal_model(board: Path) -> dict[str, str | int]:
    script = board / "scripts" / "simulate_bk7258_ota_journal.py"
    require(script.is_file(), f"OTA journal model missing: {script}")
    model = load_python_module(script, "bk7258_ota_journal_model")
    expected = {
        "ERASE_SIZE": ERASE_SIZE,
        "CP_SLOT_SIZE": CP_A_SAFE_END - CP_A_START,
        "AP_SLOT_SIZE": AP_A_END - AP_A_START,
        "PAIR_SECTORS": 0x30F,
        "PHASE_MARKERS": 0x92D,
        "JOURNAL_COPY_SIZE": JOURNAL_COPY_SIZE,
    }
    for name, value in expected.items():
        observed = getattr(model, name, None)
        require(
            observed == value,
            f"journal model {name}: expected 0x{value:x}, got {observed!r}",
        )
    result = model.run_self_test()
    require(
        result.get("status") == "pass-read-only-host-model",
        "journal model did not report the read-only PASS state",
    )
    require(
        result.get("writes_enabled") is False,
        "journal model unexpectedly enables board writes",
    )
    reset_cases = result.get("reset_cases", {})
    require(
        reset_cases.get("total") == 32915,
        "journal reset/torn-write coverage drifted from 32,915 cases",
    )
    metadata = result.get("metadata_abi", {})
    require(
        metadata.get("status") == "pass-read-only-metadata-abi",
        "journal metadata ABI self-test did not pass",
    )
    require(
        metadata.get("writes_enabled") is False,
        "journal metadata ABI unexpectedly enables board writes",
    )
    return {
        "path": str(script),
        "sha256": sha256(script),
        "status": result["status"],
        "writes_enabled": False,
        "reset_and_torn_write_cases": reset_cases["total"],
        "metadata_abi_status": metadata["status"],
        "metadata_header_size": metadata["header_size"],
        "metadata_marker_size": metadata["marker_size"],
        "phase_markers_per_direction": expected["PHASE_MARKERS"],
        "copy_size": JOURNAL_COPY_SIZE,
        "copy_count": 4,
    }


def verify_sdk_flash_semantics(sdk_source: Path) -> dict[str, Any]:
    sources = {
        "partition_driver": (
            sdk_source / "cp/middleware/driver/flash/flash_partition.c"
        ),
        "flash_driver": (sdk_source / "cp/middleware/driver/flash/flash_driver.c"),
        "flash_header": sdk_source / "cp/include/driver/flash.h",
        "cpu_boot": sdk_source / "cp/components/bk_startup/system_main.c",
        "security_generator": (
            sdk_source / "tools/env_tools/beken_utils/scripts/gen_security.py"
        ),
    }
    for name, path in sources.items():
        require(path.is_file(), f"official Flash evidence missing ({name}): {path}")

    partition_text = sources["partition_driver"].read_text(encoding="utf-8")
    flash_text = sources["flash_driver"].read_text(encoding="utf-8")
    header_text = sources["flash_header"].read_text(encoding="utf-8")
    cpu_boot_text = sources["cpu_boot"].read_text(encoding="utf-8")
    security_text = sources["security_generator"].read_text(encoding="utf-8")

    require(
        "fun_flash_phy_addr = FLASH_LOGICAL_2_PHY(fun_flash_logical_addr)"
        in partition_text,
        "official permission check no longer distinguishes logical XIP and physical",
    )
    for call in (
        "bk_flash_erase_sector(erase_addr)",
        "bk_flash_write_bytes(start_addr, buffer, buffer_len)",
        "bk_flash_read_bytes(start_addr, out_buffer, buffer_len)",
    ):
        require(call in partition_text, f"official raw Flash call drifted: {call}")
    require(
        "addr = (pt->partition_start_addr / 34) * 32" in cpu_boot_text,
        "official AP boot no longer converts physical partition offsets to XIP",
    )
    require(
        '"((((virtual_addr) >> 5) * 34) + ((virtual_addr) & 31))' in security_text,
        "official 32-to-34 physical mapping formula drifted",
    )
    require(
        'section(".itcm_sec_code")' in header_text
        and "bk_flash_erase_sector" in header_text,
        "official erase API lost its ITCM section declaration",
    )
    for symbol in (
        "bk_flash_erase_sector",
        "bk_flash_write_bytes",
        "bk_flash_read_bytes",
    ):
        require(
            symbol in flash_text, f"official Flash implementation missing: {symbol}"
        )

    return {
        "address_domain": "bk_flash_* consumes raw physical offsets",
        "xip_conversion": "32 logical bytes map to 34 physical bytes",
        "files": {
            name: {"path": str(path), "sha256": sha256(path)}
            for name, path in sources.items()
        },
    }


def parse_official_csv(path: Path) -> list[Region]:
    regions: list[Region] = []
    cursor = 0
    with path.open(newline="", encoding="utf-8") as stream:

        def normalized_rows() -> Any:
            for line in stream:
                stripped = line.lstrip()
                if stripped.startswith("#Name,"):
                    yield stripped[1:]
                elif not stripped.startswith("#"):
                    yield line

        rows = normalized_rows()
        for row in csv.DictReader(rows):
            name = (row.get("Name") or "").strip()
            if not name:
                continue
            offset_text = (row.get("Offset") or "").strip()
            start = int(offset_text, 0) if offset_text else cursor
            size = parse_size(row["Size"])
            regions.append(Region(name, start, start + size, "official"))
            cursor = start + size
    return regions


def verify_sdk_source(sdk_source: Path) -> dict[str, Any]:
    normal = (
        sdk_source
        / "cp/components/bk_libs/bk7258/bootloader/normal_bootloader/bootloader.bin"
    )
    ab = (
        sdk_source
        / "cp/components/bk_libs/bk7258/bootloader/ab_bootloader/bootloader.bin"
    )
    csv_path = sdk_source / "projects/app_ab/partitions/bk7258/auto_partitions.csv"
    for path in (normal, ab, csv_path):
        require(path.is_file(), f"official {SDK_RELEASE} input missing: {path}")

    normal_hash = sha256(normal)
    ab_hash = sha256(ab)
    require(
        normal_hash == SDK_NORMAL_BOOT_SHA256,
        "normal bootloader is not the pinned official v3.1.1.9 binary",
    )
    require(
        ab_hash == SDK_AB_BOOT_SHA256,
        "AB bootloader is not the pinned official v3.1.1.9 binary",
    )

    official = parse_official_csv(csv_path)
    by_name = {region.name: region for region in official}
    expected = {
        "primary_bootloader": (0x000000, 0x011000),
        "primary_cp_app": (0x011000, 0x165000),
        "primary_ap_app": (0x165000, 0x286000),
        "s_app": (0x286000, 0x4FB000),
        "ota_fina_executive": (0x4FB000, 0x4FC000),
        "usr_config": (0x4FC000, 0x50A000),
        "easyflash": (0x7FA000, 0x7FC000),
        "easyflash_ap": (0x7FC000, 0x7FE000),
        "sys_rf": (0x7FE000, 0x7FF000),
        "sys_net": (0x7FF000, 0x800000),
    }
    for name, (start, end) in expected.items():
        region = by_name.get(name)
        require(region is not None, f"official AB partition missing: {name}")
        require(
            (region.start, region.end) == (start, end),
            f"official AB partition drift: {name} is "
            f"0x{region.start:x}..0x{region.end:x}",
        )

    require(
        by_name["primary_cp_app"].size + by_name["primary_ap_app"].size
        == by_name["s_app"].size,
        "official AB primary pair and s_app sizes differ",
    )
    return {
        "release": SDK_RELEASE,
        "source": str(sdk_source),
        "normal_bootloader": {
            "size": normal.stat().st_size,
            "sha256": normal_hash,
        },
        "ab_bootloader": {"size": ab.stat().st_size, "sha256": ab_hash},
        "ab_partitions": [region.to_json() for region in official],
        "flash_address_contract": verify_sdk_flash_semantics(sdk_source),
    }


def calculate(board: Path, sdk_source: Path | None) -> dict[str, Any]:
    regions = [
        Region("bootloader", BOOT_START, BOOT_END, "preserve"),
        Region("cp_a_safe", CP_A_START, CP_A_SAFE_END, "active-slot-safe"),
        Region(
            "littlefs_raw",
            LITTLEFS_RAW_START,
            LITTLEFS_RAW_END,
            "preserve-raw-physical",
        ),
        Region(
            "crc_address_gap",
            CRC_ADDRESS_GAP_START,
            CRC_ADDRESS_GAP_END,
            "preserve-unallocated",
        ),
        Region("ap_a", AP_A_START, AP_A_END, "active-slot"),
        Region("cp_b", CP_B_START, CP_B_END, "proposed-staging"),
        Region("ap_b", AP_B_START, AP_B_END, "proposed-staging"),
        Region(
            "ota_forward_journal_0",
            FORWARD_JOURNAL_0_START,
            FORWARD_JOURNAL_0_START + JOURNAL_COPY_SIZE,
            "proposed-forward-metadata",
        ),
        Region(
            "ota_forward_journal_1",
            FORWARD_JOURNAL_1_START,
            FORWARD_JOURNAL_1_START + JOURNAL_COPY_SIZE,
            "proposed-forward-metadata",
        ),
        Region(
            "ota_reverse_journal_0",
            REVERSE_JOURNAL_0_START,
            REVERSE_JOURNAL_0_START + JOURNAL_COPY_SIZE,
            "proposed-reverse-metadata",
        ),
        Region(
            "ota_reverse_journal_1",
            REVERSE_JOURNAL_1_START,
            REVERSE_JOURNAL_1_START + JOURNAL_COPY_SIZE,
            "proposed-reverse-metadata",
        ),
        Region(
            "ota_scratch",
            SCRATCH_START,
            SCRATCH_START + ERASE_SIZE,
            "proposed-swap-scratch",
        ),
        Region(
            "ota_reserved",
            OTA_RESERVED_START,
            OFFICIAL_TAIL_START,
            "unallocated-reserve",
        ),
        Region("easyflash", EASYFLASH_START, EASYFLASH_AP_START, "preserve"),
        Region("easyflash_ap", EASYFLASH_AP_START, SYS_RF_START, "preserve"),
        Region("sys_rf", SYS_RF_START, SYS_NET_START, "preserve"),
        Region("sys_net", SYS_NET_START, FLASH_SIZE, "preserve"),
    ]
    for region in regions:
        require_aligned(region)
    for left, right in zip(regions, regions[1:], strict=False):
        require(
            left.end == right.start,
            f"layout gap/overlap between {left.name} and {right.name}",
        )

    require(
        CP_A_SAFE_END - CP_A_START == CP_B_END - CP_B_START,
        "safe CP slots differ",
    )
    require(AP_A_END - AP_A_START == AP_B_END - AP_B_START, "AP slots differ")
    pair_size = (CP_A_SAFE_END - CP_A_START) + (AP_A_END - AP_A_START)
    staging_size = AP_B_END - CP_B_START
    require(pair_size == staging_size, "staging cannot hold a complete CP/AP pair")

    cp_delta = CP_B_START - CP_A_START
    ap_delta = AP_B_START - AP_A_START
    require(cp_delta != ap_delta, "candidate layout unexpectedly supports one remap")

    mirrored_delta = AP_A_END - CP_A_START
    mirrored_end = AP_A_END + mirrored_delta
    remap_span = AP_A_END - CP_A_START
    free_before_tail = OFFICIAL_TAIL_START - AP_A_END
    legacy_declared_overlap = CP_A_LEGACY_DECLARED_END - LITTLEFS_RAW_START
    require(
        legacy_declared_overlap == 0x10000,
        "expected historical packer/MTD mismatch changed; re-audit the layout",
    )
    max_cp_crc_blob = ((CP_A_SAFE_END - CP_A_START) // 34) * 34
    max_cp_logical_image = (max_cp_crc_blob // 34) * 32
    require(
        mirrored_end > OFFICIAL_TAIL_START,
        "single-offset mirror unexpectedly fits before the official tail",
    )

    result: dict[str, Any] = {
        "format": 1,
        "status": "r2-technical-closure-complete-read-only",
        "flash_size": FLASH_SIZE,
        "erase_size": ERASE_SIZE,
        "regions": [region.to_json() for region in regions],
        "layout_domain_correction": {
            "legacy_packer_littlefs_boundary": CP_A_LEGACY_DECLARED_END,
            "mtd_raw_littlefs_start": LITTLEFS_RAW_START,
            "historical_cp_littlefs_overlap": legacy_declared_overlap,
            "safe_cp_slot_end": CP_A_SAFE_END,
            "safe_cp_crc_blob_max": max_cp_crc_blob,
            "safe_cp_logical_image_max": max_cp_logical_image,
            "packer_guard_corrected": True,
        },
        "pair": {
            "cp_slot_size": CP_A_SAFE_END - CP_A_START,
            "ap_slot_size": AP_A_END - AP_A_START,
            "combined_size": pair_size,
            "staging_size": staging_size,
        },
        "official_single_offset_remap": {
            "compatible_with_current_layout": False,
            "cp_candidate_delta": cp_delta,
            "ap_candidate_delta": ap_delta,
            "required_mirror_span": remap_span,
            "available_before_official_tail": free_before_tail,
            "required_mirrored_end": mirrored_end,
            "deficit_before_official_tail": mirrored_end - OFFICIAL_TAIL_START,
            "reason": (
                "LittleFS separates CP A and AP A; one hardware offset cannot map "
                "the two contiguous staging images to both active addresses"
            ),
        },
        "recommended_candidate": {
            "strategy": "paired-physical-sector-swap",
            "writes_enabled": False,
            "journal_copies": 4,
            "journal_sectors_per_copy": JOURNAL_COPY_SIZE // ERASE_SIZE,
            "journal_total_sectors": 4 * JOURNAL_COPY_SIZE // ERASE_SIZE,
            "scratch_sectors": 1,
            "reserved_bytes": OFFICIAL_TAIL_START - OTA_RESERVED_START,
            "precondition": (
                "retain all post-link/packer/SRAM hard gates, obtain explicit "
                "owner acceptance of ADR-003, and complete the later N15 safety "
                "gates before any board Flash write"
            ),
        },
        "dual_image_packer": verify_dual_packer(
            board / "scripts" / "pack_dual_image.py"
        ),
        "littlefs_raw_owner": verify_mtd_raw_range(board),
        "team_safety_gates": verify_team_safety_gates(board),
        "journal_model": verify_journal_model(board),
    }
    if sdk_source is not None:
        result["official_sdk"] = verify_sdk_source(sdk_source.resolve())
    return result


def main() -> int:
    script = Path(__file__).resolve()
    board = script.parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--sdk-source",
        type=Path,
        help="verify exact official v3.1.1.9 source/binaries and AB CSV",
    )
    parser.add_argument("--json", action="store_true", help="emit JSON only")
    args = parser.parse_args()

    try:
        result = calculate(board, args.sdk_source)
    except VerificationError as error:
        print(f"BK7258 N15 OTA layout FAIL: {error}")
        return 1

    encoded = json.dumps(result, indent=2, sort_keys=True)
    if args.json:
        print(encoded)
    else:
        print(
            "BK7258 N15 OTA layout PASS (read-only): the corrected CP guard "
            "stops at raw LittleFS, and a bounded safe staging pair fits"
        )
        print(encoded)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
