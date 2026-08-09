#!/usr/bin/env python3
"""Fail-closed verifier for the accepted BK7258 contiguous A/B migration."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from bk7258_ab_layout import LAYOUT_ID, LayoutError, report as layout_report


SCRIPT_DIR = Path(__file__).resolve().parent
BOARD_DIR = SCRIPT_DIR.parent


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require_fragments(path: Path, fragments: tuple[str, ...]) -> dict[str, str]:
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as error:
        raise LayoutError(f"cannot read {path}: {error}") from error
    missing = [fragment for fragment in fragments if fragment not in text]
    if missing:
        raise LayoutError(f"{path.name} layout drift; missing {missing!r}")
    return {"path": str(path), "sha256": sha256(path)}


def verify_team_contracts() -> dict[str, object]:
    contracts = {
        "partition_csv": require_fragments(
            BOARD_DIR / "partitions/bk7258/auto_partitions.csv",
            (
                "# SDK_RELEASE=v3.1.1.9",
                "FLASH_CAPACITY=8M",
                "primary_cp_app,,1360K,code,TRUE,TRUE,slot_a_cp",
                "ota_fina_mirror,,4K,data,TRUE,TRUE,ota_metadata_mirror",
                "ota_manifest_a,,4K,data,TRUE,TRUE,ota_manifest_a",
                "ota_manifest_b,,4K,data,TRUE,TRUE,ota_manifest_b",
                "ota_auth_policy,,4K,data,TRUE,FALSE,ota_auth_policy",
            ),
        ),
        "partition_generator": require_fragments(
            SCRIPT_DIR / "gen_bk7258_partitions.py",
            (
                'SDK_RELEASE = "v3.1.1.9"',
                "verify_sdk_compatibility",
                "render_sdk_csv",
                "BK7258_PARTITION_LAYOUT_ID",
            ),
        ),
        "generated_header": require_fragments(
            BOARD_DIR / "chip/include/bk7258_partition_layout.h",
            (
                f'#define BK7258_PARTITION_LAYOUT_ID "{LAYOUT_ID}"',
                "#define BK7258_ROLE_SLOT_A_CP_OFFSET",
                "#define BK7258_ROLE_OTA_METADATA_MIRROR_OFFSET",
                "#define BK7258_ROLE_OTA_MANIFEST_A_OFFSET",
                "#define BK7258_ROLE_OTA_MANIFEST_B_OFFSET",
                "#define BK7258_ROLE_OTA_AUTH_POLICY_OFFSET",
                "#define BK7258_ROLE_CALIBRATION_NET_END",
            ),
        ),
        "amp_header": require_fragments(
            BOARD_DIR / "chip/include/bk7258_amp.h",
            (
                "#define BK7258_CP_FLASH_SIZE             BK7258_ROLE_SLOT_A_CP_LOGICAL_SIZE",
                "#define BK7258_AP_FLASH_OFFSET           BK7258_ROLE_SLOT_A_AP_LOGICAL_OFFSET",
                "#define BK7258_AP_FLASH_SIZE             BK7258_ROLE_SLOT_A_AP_LOGICAL_SIZE",
                "#define BK7258_AB_SECONDARY_START        BK7258_ROLE_SLOT_B_PAIR_OFFSET",
                "#define BK7258_DATA_RAW_PHYSICAL_OFFSET  BK7258_ROLE_LITTLEFS_OFFSET",
                "#define BK7258_CALIBRATION_TAIL_START    BK7258_ROLE_EASYFLASH_CP_OFFSET",
            ),
        ),
        "cp_linker": require_fragments(
            SCRIPT_DIR / "ld.script",
            (
                '#include "../chip/include/bk7258_partition_layout.h"',
                "#  define BK7258_CP_IMAGE_FLASH_BASE BK7258_ROLE_SLOT_A_CP_XIP_START",
                "#  define BK7258_CP_IMAGE_FLASH_SIZE BK7258_ROLE_SLOT_A_CP_LOGICAL_SIZE",
                "ORIGIN = BK7258_CP_IMAGE_FLASH_ORIGIN",
                "LENGTH = BK7258_CP_IMAGE_FLASH_LENGTH",
            ),
        ),
        "ap_linker": require_fragments(
            SCRIPT_DIR / "ld_ap.script",
            (
                '#include "../chip/include/bk7258_partition_layout.h"',
                "ORIGIN = BK7258_ROLE_SLOT_A_AP_XIP_START + 0x200",
                "LENGTH = BK7258_ROLE_SLOT_A_AP_LOGICAL_SIZE - 0x200",
                "ORIGIN = BK7258_ROLE_SLOT_A_AP_XIP_START",
                "LENGTH = BK7258_ROLE_SLOT_A_AP_LOGICAL_SIZE",
                "AP __vector_core0_table must be at generated FLASH origin",
            ),
        ),
        "postbuild": require_fragments(
            SCRIPT_DIR / "postbuild.sh",
            (
                'python3 "${PARTITION_GENERATOR}" --check',
                '--get "${PARTITION_ROLE}.xip_start"',
                '--get "${PARTITION_ROLE}.logical_size"',
                '--get "${PARTITION_ROLE}.offset"',
            ),
        ),
        "debug_sop": require_fragments(
            SCRIPT_DIR / "bk7258_auto_debug.sh",
            (
                'EXPECTED_LAYOUT_ID=$(python3 "$PARTITION_GENERATOR" --get layout_id)',
                'verify_bk7258_factory_layout.py" --package "$DUAL_DIR"',
                "CP_IMAGE_SIZE <= CP_SIZE",
                "AP_IMAGE_SIZE <= AP_SIZE",
                "AP_IMAGE_WIN}@${AP_OFFSET}-${AP_LENGTH_HEX}",
            ),
        ),
        "build_wrapper": require_fragments(
            SCRIPT_DIR / "build_dual_image.sh",
            (
                'BK7258_SDK_BUNDLE_VERSION}" != "v3.1.1.9"',
                'python3 "${PARTITION_GENERATOR}"',
                'verify_bk7258_partitions.py"',
                'bk7258-partitions.json"',
                'verify_bk7258_factory_layout.py"',
                'verify_bk7258_ota_pair.py"',
                'verify_bk7258_ota_staging.py"',
                'verify_bk7258_ota_boot.py"',
                'verify_bk7258_ota_trial.py"',
                'verify_bk7258_ota_rotation.py"',
                'verify_bk7258_ota_rotation_select.py"',
                'verify_bk7258_ota_rotation_trial.py"',
                'verify_bk7258_ota_rotation_publish.py"',
                'verify_bk7258_ota_rotation_control.py"',
                'verify_bk7258_ota_rotation_health.py"',
                'pack_bk7258_ota_rotation.py"',
                'bk7258-factory-layout.json"',
                'bk7258-ota-boot.json"',
                'bk7258-ota-trial.json"',
                "N15_OTA_BOARD_WRITE_AUTHORIZED=false",
            ),
        ),
        "boot_table": require_fragments(
            BOARD_DIR / "bootloader/boot_main.c",
            (
                "BK7258_ROLE_SLOT_A_CP_LOGICAL_OFFSET",
                "BK7258_ROLE_SLOT_A_CP_LOGICAL_SIZE",
                "BK7258_ROLE_SLOT_A_AP_LOGICAL_OFFSET",
                "BK7258_ROLE_SLOT_A_AP_LOGICAL_SIZE",
            ),
        ),
        "mtd": require_fragments(
            BOARD_DIR / "chip/cp/bk7258_flash_mtd.c",
            (
                "BK7258_DATA_RAW_PHYSICAL_OFFSET",
                "BK7258_DATA_RAW_PHYSICAL_SIZE",
                "BK7258_FLASH_GUARD_DATA",
            ),
        ),
        "flash_guard": require_fragments(
            BOARD_DIR / "chip/cp/bk7258_flash_guard.c",
            (
                "BK7258_ROLE_SLOT_A_CP_OFFSET",
                "BK7258_ROLE_SLOT_B_PAIR_OFFSET",
                "BK7258_ROLE_SLOT_B_PAIR_SIZE",
                "BK7258_ROLE_OTA_METADATA_PRIMARY_OFFSET",
                "BK7258_ROLE_OTA_METADATA_MIRROR_OFFSET",
                "CONFIG_BK7258_OTA_STAGING_WRITE",
                "CONFIG_BK7258_OTA_TRIAL_WRITE",
            ),
        ),
        "packer": require_fragments(
            SCRIPT_DIR / "pack_dual_image.py",
            ("LAYOUT_ID", "PAIR_B_START", "CALIBRATION_TAIL_START"),
        ),
        "factory_verifier": require_fragments(
            SCRIPT_DIR / "verify_bk7258_factory_layout.py",
            (
                "len(factory) == FACTORY_PREFIX_END",
                "B seed is not a byte-exact A pair copy",
                "factory image contains unexpected bytes",
                '"included_in_image": False',
                '"included_in_images": False',
            ),
        ),
        "pair_packer": require_fragments(
            SCRIPT_DIR / "pack_bk7258_ota_pair.py",
            (
                'PAIR_SCHEMA = "bk7258-cp-ap-pair-v1"',
                'RBL_CURRENT_VERSION = "00010203040506070809"',
                "OFFICIAL_SOURCE_HASHES",
                '"staging_writes_enabled": False',
                '"trial_metadata_mutation_enabled": False',
                '"publisher_authenticated": False',
            ),
        ),
        "pair_verifier": require_fragments(
            SCRIPT_DIR / "verify_bk7258_ota_pair.py",
            (
                "decode(physical_rbl)",
                "expected_generation",
                '"mixed generation"',
                '"AP size overflow"',
                '"writes_enabled": False',
            ),
        ),
        "staging_descriptor": require_fragments(
            SCRIPT_DIR / "pack_bk7258_ota_stage.py",
            (
                'STAGE_MAGIC = b"BKOTA15B"',
                "STAGE_DESCRIPTOR_SIZE = 384",
                '"compile_write_enabled": False',
                '"board_write_authorized": False',
            ),
        ),
        "staging_core": require_fragments(
            BOARD_DIR / "chip/cp/bk7258_ota_staging_core.c",
            (
                "BK7258_ROLE_SLOT_A_CP_OFFSET",
                "BK7258_ROLE_SLOT_B_PAIR_OFFSET",
                "BK7258_ROLE_SLOT_B_PAIR_SIZE",
                "validate_rbl_header",
                "BK7258_OTA_STAGE_FINAL_DIGEST",
                "bk7258_ota_core_stage_inactive",
            ),
        ),
        "rotation_metadata_packer": require_fragments(
            SCRIPT_DIR / "pack_bk7258_ota_rotation.py",
            (
                'ROTATION_MAGIC = b"BKOTA15R"',
                "ROTATION_FORMAT = 2",
                'ROTATION_RECORD = struct.Struct("<8sHHIQQIII24s24s32s384sI")',
                'target_slot not in {"a", "b"}',
                "OTA_METADATA_MIRROR_START",
                '"board_write_authorized": False',
            ),
        ),
        "rotation_core": require_fragments(
            BOARD_DIR / "bootloader/boot_ota_rotation_core.c",
            (
                '#define ROTATION_MAGIC                 "BKOTA15R"',
                "ROTATION_FORMAT                2u",
                "BK7258_BOOT_OTA_ROTATION_PENDING_A",
                "BK7258_BOOT_OTA_ROTATION_PENDING_B",
                "bk7258_boot_ota_rotation_select",
                "bk7258_boot_ota_rotation_prepare_transition",
            ),
        ),
        "rotation_selector_core": require_fragments(
            BOARD_DIR / "bootloader/boot_ota_rotation_select_core.c",
            (
                "BK7258_ROLE_OTA_METADATA_PRIMARY_OFFSET",
                "BK7258_ROLE_OTA_METADATA_MIRROR_OFFSET",
                "bk7258_boot_ota_rotation_select_core",
                "BK7258_BOOT_OTA_ROTATION_DECISION_TARGET_TRIAL",
                "BK7258_BOOT_OTA_ROTATION_DECISION_TARGET_CONFIRMED",
            ),
        ),
        "boot_selector_adapter": require_fragments(
            BOARD_DIR / "bootloader/boot_ota_select.c",
            (
                "BK7258_BOOT_OTA_SELECT_COMPILE_GATE 0u",
                "BK7258_BOOT_OTA_SELECT_RUNTIME_GATE 0u",
                "BK7258_BOOT_OTA_REMAP_COMPILE_GATE 0u",
                "BK7258_BOOT_OTA_REMAP_RUNTIME_GATE 0u",
                "BK7258_BOOT_OTA_TRIAL_COMPILE_GATE 0u",
                "BK7258_BOOT_OTA_TRIAL_RUNTIME_GATE 0u",
                "FLASH_REMAP_BEGIN           (FLASH_CONTROLLER_BASE + 0x58u)",
                "BK7258_ROLE_SLOT_B_PAIR_OFFSET / BK7258_FLASH_CRC_TOTAL_SIZE",
                "bk7258_boot_ota_rotation_select",
                "bk7258_boot_ota_rotation_trial_transition",
                "boot_ota_select_app",
            ),
        ),
        "rotation_trial_core": require_fragments(
            BOARD_DIR / "bootloader/boot_ota_rotation_trial_core.c",
            (
                "BK7258_ROLE_OTA_METADATA_PRIMARY_OFFSET",
                "BK7258_ROLE_OTA_METADATA_MIRROR_OFFSET",
                "BK7258_BOOT_OTA_ROTATION_RECORD_SIZE",
                "bk7258_boot_ota_rotation_prepare_transition",
                "bk7258_boot_ota_rotation_trial_transition",
            ),
        ),
        "trial_cp_adapter": require_fragments(
            BOARD_DIR / "chip/cp/bk7258_ota_trial.c",
            (
                "CONFIG_BK7258_OTA_TRIAL_WRITE",
                "BK7258_FLASH_GUARD_OTA_METADATA",
                "bk7258_ota_trial_confirm",
                "bk7258_ota_trial_rollback",
                "bk7258_boot_ota_rotation_control_transition",
                "bk7258_boot_ota_rotation_publish_pending",
                "bk7258_boot_ota_rotation_health_update",
            ),
        ),
        "trial_verifier": require_fragments(
            SCRIPT_DIR / "verify_bk7258_ota_trial.py",
            (
                '"reset_boundaries": reset_boundaries',
                '"compile_trial_write_enabled": False',
                '"board_write_authorized": False',
                "verify_boot_elf",
                "verify_cp_elf",
            ),
        ),
        "boot_sram_writer": require_fragments(
            BOARD_DIR / "bootloader/boot_ota_flash_program.c",
            (
                'section(".boot_ota_ramfunc.text")',
                "FLASH_COMMAND_PROGRAM   12u",
                "METADATA_PRIMARY_START  BK7258_ROLE_OTA_METADATA_PRIMARY_OFFSET",
                "METADATA_MIRROR_START   BK7258_ROLE_OTA_METADATA_MIRROR_OFFSET",
                "boot_ota_ram_fail_reset",
            ),
        ),
        "boot_sha256": require_fragments(
            BOARD_DIR / "bootloader/boot_sha256.c",
            (
                "boot_sha256_init",
                "boot_sha256_update",
                "boot_sha256_final",
            ),
        ),
        "boot_verifier": require_fragments(
            SCRIPT_DIR / "verify_bk7258_ota_boot.py",
            (
                "OFFICIAL_CONTRACT_HASHES",
                "OFFICIAL_ENABLE_SLICE",
                "OFFICIAL_REMAP_SLICE",
                '"positive_cases": positive_count',
                '"compile_selection_enabled": False',
                '"board_write_authorized": False',
            ),
        ),
        "boot_linker": require_fragments(
            BOARD_DIR / "bootloader/bootloader.ld",
            (
                ".boot_ota_workspace 0x2800D000 (NOLOAD)",
                ".boot_ota_ramfunc 0x2800C000",
                "N15 boot OTA workspace exceeds 12 KiB",
                "N15 boot OTA SRAM writer exceeds 4 KiB",
                "bootloader must not require initialized RAM data",
                "bootloader must not retain hidden BSS state",
            ),
        ),
    }
    return {"layout_id": LAYOUT_ID, "files": contracts}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk-source", type=Path)
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        result = layout_report(args.sdk_source)
        result["team_contracts"] = verify_team_contracts()
    except (LayoutError, OSError, ValueError) as error:
        print(f"BK7258 accepted A/B layout verification FAIL: {error}")
        return 1
    encoded = json.dumps(result, indent=2, sort_keys=True)
    if args.output is not None:
        args.output.write_text(encoded + "\n", encoding="utf-8")
    if args.json:
        print(encoded)
    else:
        print(
            "BK7258 accepted A/B layout verification PASS: "
            f"layout_id={LAYOUT_ID} writes_enabled=false"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
