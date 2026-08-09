#!/usr/bin/env python3
"""Verify the separately gated BK7258 N15-F validation package."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

from verify_bk7258_ota_staging import verify_elf as verify_staging_elf
from verify_bk7258_ota_trial import (
    official_contract,
    parse_nm,
    verify_boot_elf,
    verify_cp_elf,
)


class ValidationVerificationError(RuntimeError):
    """Raised when a validation-package safety invariant fails."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValidationVerificationError(message)


def source_contract(repo: Path) -> None:
    board = repo / "board/bk7258"
    paths = {
        "app": repo / "app/hello_app/bk7258_ota_main.c",
        "app_cmake": repo / "app/hello_app/CMakeLists.txt",
        "app_make": repo / "app/hello_app/Makefile",
        "app_defs": repo / "app/hello_app/Make.defs",
        "kconfig": board / "chip/Kconfig",
        "staging_header": board / "chip/include/bk7258_ota_staging.h",
        "fault_header": board / "chip/include/bk7258_ota_fault.h",
        "fault_verify": board / "scripts/verify_bk7258_ota_fault.py",
        "normal": board / "configs/cp_nsh_psram/defconfig",
        "validation": board / "configs/cp_nsh_ota/defconfig",
        "boot_make": board / "bootloader/Makefile",
        "build": board / "scripts/build_dual_image.sh",
        "metadata_pack": board / "scripts/pack_bk7258_ota_rotation.py",
        "campaign_pack": board / "scripts/pack_bk7258_ota_campaign.py",
        "campaign_verify": board / "scripts/verify_bk7258_ota_campaign.py",
        "transfer_verify": board / "scripts/verify_bk7258_ota_transfer.py",
        "transfer_loader": board / "scripts/load_bk7258_ota_psram.sh",
    }
    text = {name: path.read_text(encoding="utf-8") for name, path in paths.items()}
    for token in (
        'BKOTA_TOKEN_PREFIX "N15-WRITE-"',
        "bkota_authorize",
        "bkota_disarm();",
        "bk7258_ota_staging_set_write_enabled(true)",
        "bk7258_ota_staging_set_write_enabled(false)",
        "bk7258_ota_trial_set_write_enabled(true)",
        "bk7258_ota_trial_set_write_enabled(false)",
        "bk7258_ota_staging_validate",
        "bk7258_ota_staging_stage",
        "bk7258_ota_publish_pending",
        "bk7258_ota_trial_confirm",
        "bk7258_ota_trial_rollback",
        "bkota_transfer_ready",
        "BK7258_OTA_TRANSFER_CANDIDATE_ADDRESS",
        "BK7258_OTA_TRANSFER_DESCRIPTOR_ADDRESS",
        "BK7258_OTA_TRANSFER_RECORD_ADDRESS",
        "bkota_memory_read",
        "bkota validate-mem",
        "bkota stage-mem",
        "bkota publish-mem",
        "bkota prepare-transfer",
        "bkota fault-arm",
        "bkota fault-status",
        "bkota fault-clear",
        "bkota corrupt-mem",
        "bk7258_ota_fault_begin",
        "bkota_fault_finish_session",
        "WDIOC_STOP",
        "watchdog_active=0 action=load-psram-then-stage-publish-reset",
        "unsigned artifacts; do not deploy",
    ):
        require(token in text["app"], f"bkota safety contract missing {token}")
    for forbidden in ("boardctl(", "reboot(", "system(", "exec"):
        require(forbidden not in text["app"], f"bkota performs forbidden automatic action {forbidden}")
    require("CONFIG_BK7258_OTA_VALIDATION" in text["app_cmake"], "CMake omits bkota")
    require("CONFIG_BK7258_OTA_VALIDATION" in text["app_make"], "Makefile omits bkota")
    require("CONFIG_BK7258_OTA_VALIDATION" in text["app_defs"], "Make.defs omits bkota")
    for token in (
        "config BK7258_OTA_VALIDATION",
        "config BK7258_OTA_FAULT_INJECTION",
        "depends on BK7258_OTA_STAGING_WRITE && BK7258_OTA_TRIAL_WRITE",
        "depends on BK7258_PSRAM",
        "depends on BK7258_OTA_VALIDATION",
        "generation-bound authorization token",
    ):
        require(token in text["kconfig"], f"validation Kconfig contract missing {token}")
    for setting in (
        "CONFIG_BK7258_OTA_STAGING_WRITE=y",
        "CONFIG_BK7258_OTA_TRIAL_WRITE=y",
        "CONFIG_BK7258_OTA_VALIDATION=y",
        "CONFIG_BK7258_OTA_FAULT_INJECTION=y",
        "CONFIG_NSH_MAXARGUMENTS=10",
    ):
        require(setting in text["validation"], f"validation defconfig omits {setting}")
        require(setting not in text["normal"], f"normal defconfig leaks {setting}")
    for macro in (
        "BK7258_BOOT_OTA_SELECT_COMPILE_GATE=1u",
        "BK7258_BOOT_OTA_SELECT_RUNTIME_GATE=1u",
        "BK7258_BOOT_OTA_REMAP_COMPILE_GATE=1u",
        "BK7258_BOOT_OTA_REMAP_RUNTIME_GATE=1u",
        "BK7258_BOOT_OTA_TRIAL_COMPILE_GATE=1u",
        "BK7258_BOOT_OTA_TRIAL_RUNTIME_GATE=1u",
    ):
        require(macro in text["boot_make"], f"validation Boot gate missing {macro}")
    for token in (
        'N15_OTA_VALIDATION="${N15_OTA_VALIDATION:-NO}"',
        '"${CP_CONFIG_NAME}" != "cp_nsh_ota"',
        '"${N15_OTA_HOST_BUNDLE_ENABLED}" != "true"',
        'OUTPUT="${TOPDIR}/bk7258-dual-ota-validation"',
        '"${TMPDIR}/bootloader.elf"',
        '"${TMPDIR}/nuttx-${role}.config"',
        'N15_OTA_BOARD_WRITE_AUTHORIZED=false',
        '--descriptor-output "${N15_OTA_OUTPUT}/bk7258-ota-stage.bin"',
        'verify_bk7258_ota_transfer.py',
        'verify_bk7258_ota_fault.py',
        'N15_OTA_FAULT_INJECTION_ENABLED=${N15_OTA_VALIDATION_ENABLED}',
        '--validation-profile',
        '--expected-gate-value "${BOOT_GATE_VALUE}"',
    ):
        require(token in text["build"], f"validation build fail-closed contract missing {token}")

    for token in (
        "#define BK7258_OTA_TRANSFER_CANDIDATE_ADDRESS  0x60800000u",
        "#define BK7258_OTA_TRANSFER_CANDIDATE_SIZE",
        "BK7258_ROLE_SLOT_B_PAIR_SIZE",
        "#define BK7258_OTA_TRANSFER_DESCRIPTOR_ADDRESS",
        "#define BK7258_OTA_TRANSFER_RECORD_ADDRESS",
        "BK7258_FLASH_ERASE_SIZE",
        "#define BK7258_OTA_TRANSFER_RECORD_SIZE        512u",
        "#define BK7258_OTA_TRANSFER_END",
    ):
        require(
            token in text["staging_header"],
            f"validation transfer ABI source drift: {token}",
        )
    for token in (
        "BK7258_OTA_FAULT_STAGE_MASK",
        "BK7258_OTA_FAULT_PUBLISH_MASK",
        "BK7258_OTA_FAULT_TRIAL_MASK",
        "BK7258_OTA_FAULT_MAX_ORDINAL 65535u",
    ):
        require(token in text["fault_header"], f"fault ABI missing {token}")
    require(
        "normal_profile_enabled" in text["fault_verify"],
        "fault verifier omits normal-profile closure",
    )
    for token in (
        "ROTATION_DESCRIPTOR_OFFSET == 124",
        '"bk7258-ota-stage.bin"',
        'report["descriptor_path"]',
    ):
        require(token in text["metadata_pack"], f"descriptor export missing {token}")
    for token in (
        "EXPECTED_ABI",
        "standalone descriptor differs from pending record",
        '"board_write_authorized": False',
        '"flash_write_performed": False',
    ):
        require(token in text["transfer_verify"], f"transfer verifier missing {token}")

    for token in (
        'SDK_RELEASE = "v3.1.1.9"',
        '"board_write_authorized": False',
        '"automatic_reset": False',
        '"flash_write_performed": False',
        '"physical_execution_performed": False',
        '"controlled_power_cycle_required": True',
        '"mid_flash_pulse_brownout_tested": False',
        '"boundary_model": "fail-before callback, quiescent return, controlled power cycle"',
        '"ordered_execution_required": True',
        '"terminal_case_last":',
        '"successful-confirm"',
        '"successful-return-a"',
        'target_slot="a"',
        '"publish-erase-fault"',
        '"publish-readback-fault"',
        '"trial-write-fault"',
    ):
        require(token in text["campaign_pack"], f"campaign packer missing {token}")

    for token in (
        "CAMPAIGN_FORMAT = 2",
        "EXPECTED_CASES",
        "CONTROLLED POWER CYCLE",
        "package.parent == root",
        '"loader_dry_runs": len(cases)',
        '"board_write_authorized": False',
        '"physical_execution_performed": False',
        '"mid_flash_pulse_brownout_tested": False',
        '"successful-return-a"',
        '"--expected-target-slot"',
        'f"{expected_key} loader dry-run"',
    ):
        require(token in text["campaign_verify"], f"campaign verifier missing {token}")

    for token in (
        '"jlink_write_command": "loadfile-noreset"',
        '"implicit_reset_forbidden": True',
        '"--check-only"',
        '"existing J-Link plan drift"',
        ' noreset',
    ):
        require(token in text["transfer_verify"],
                f"transfer verifier reset-safety contract missing {token}")

    loader = text["transfer_loader"]
    for token in (
        "--watchdog-stopped",
        "--execute",
        "--check-only",
        "verify_bk7258_ota_transfer.py",
        '[[ $TOKEN == "N15-WRITE-${GENERATION}" ]]',
        "((WATCHDOG_STOPPED == 1))",
        "CANDIDATE_CHUNK_SIZE=$((64 * 1024))",
        "CANDIDATE_BATCH_CHUNKS=1",
        "CANDIDATE_CAPACITY=$((DESCRIPTOR_ADDRESS - CANDIDATE_ADDRESS))",
        '[[ $CANDIDATE_SIZE -eq $CANDIDATE_CAPACITY ]]',
        "--additional-suffix=.bin",
        'for COMMAND_FILE in "${COMMAND_DIR}"/batch-*.jlink',
        '-ExitOnError 1 < "$COMMAND_FILE"',
        "JLINK_STATUS=$?",
        '[[ $BATCH_INDEX -eq $BATCH_COUNT ]]',
        "DRY RUN:",
        "Do not reset before validate-mem/stage-mem/publish-mem",
    ):
        require(token in loader, f"transfer loader safety contract missing {token}")

    load_format = "printf 'loadfile \"%s\" 0x%08x noreset\\n'"
    verify_format = "printf 'verifybin \"%s\", 0x%08x\\n'"
    require(
        loader.count(load_format) == 3,
        "loader must build one candidate and two control-artifact load commands",
    )
    require(
        loader.count(verify_format) == 3,
        "loader must verify every generated PSRAM load command",
    )
    require(
        "printf '%s\\n' go exit |" in loader,
        "loader error path must resume the target without reset",
    )
    for line in loader.splitlines():
        command_line = line.strip().lower()
        if not command_line.startswith("printf "):
            continue
        command_line = command_line.replace("noreset", "")
        for forbidden in ("erase", "loadbin", "reset", "rsettype", "mem32", "w4"):
            require(
                forbidden not in command_line,
                f"loader command construction contains forbidden {forbidden}",
            )


def verify_runtime_bss(cp_elf: Path) -> dict[str, object]:
    nm = shutil.which("arm-none-eabi-nm")
    if nm is None:
        raise ValidationVerificationError("arm-none-eabi-nm is unavailable")
    symbols = parse_nm(nm, cp_elf)
    expected_sizes = {
        "g_bk7258_ota_staging_runtime_write": 1,
        "g_bk7258_ota_trial_runtime_write": 1,
        "g_bk7258_ota_fault_initialized": 1,
        "g_bk7258_ota_fault_plan": 24,
    }
    report: dict[str, object] = {}
    for name, expected_size in expected_sizes.items():
        require(name in symbols, f"validation CP ELF omits {name}")
        address, size, kind = symbols[name]
        require(
            size == expected_size and kind in "Bb",
            f"{name} is not the expected zero-initialized BSS object",
        )
        report[name] = {"address": address, "size": size, "section": kind}
    return report


def verify_transfer_elf(cp_elf: Path) -> dict[str, object]:
    nm = shutil.which("arm-none-eabi-nm")
    if nm is None:
        raise ValidationVerificationError("arm-none-eabi-nm is unavailable")
    symbols = parse_nm(nm, cp_elf)
    required = (
        "bkota_main",
        "bkota_memory_read",
        "bkota_transfer_ready",
        "bkota_validate_or_stage_memory",
        "bkota_publish_memory",
        "bk7258_ota_fault_arm",
        "bk7258_ota_fault_before",
        "bk7258_ota_fault_get_status",
        "bk7258_ota_fault_finish",
    )
    for name in required:
        require(name in symbols, f"validation CP ELF omits transfer symbol {name}")

    payload = cp_elf.read_bytes()
    strings = (
        b"BKOTA TRANSFER READY generation=",
        b"watchdog_active=0 action=load-psram-then-stage-publish-reset",
        b"validate-mem",
        b"stage-mem",
        b"publish-mem",
        b"fault-arm",
        b"fault-status",
        b"fault-clear",
        b"corrupt-mem",
        b"BKOTA CORRUPT-MEM ret=0",
    )
    for value in strings:
        require(value in payload, f"validation CP ELF omits transfer payload {value!r}")
    return {
        "symbols": list(required),
        "fixed_addresses": True,
        "requires_16m_psram": True,
        "watchdog_stop_requires_generation_token": True,
        "automatic_reset": False,
        "deterministic_fault_injection": True,
        "bounded_psram_corruption": True,
    }


def verify(
    repo: Path,
    boot_elf: Path,
    boot_bin: Path,
    boot_crc: Path,
    cp_elf: Path,
    cp_config: Path,
    sdk_source: Path | None,
) -> dict[str, object]:
    source_contract(repo)
    boot = verify_boot_elf(
        boot_elf, boot_bin, boot_crc, expected_gate_value=1
    )
    cp = verify_cp_elf(cp_elf, cp_config, validation_profile=True)
    staging = verify_staging_elf(cp_elf, cp_config, validation_profile=True)
    runtime_bss = verify_runtime_bss(cp_elf)
    transfer = verify_transfer_elf(cp_elf)
    return {
        "format": 2,
        "status": "pass",
        "profile": "cp_nsh_ota+ap_smp_psram",
        "source_contract": True,
        "official_contract": official_contract(sdk_source),
        "boot": boot,
        "cp": cp,
        "staging": staging,
        "runtime_gate_bss": runtime_bss,
        "psram_transfer": transfer,
        "cp_elf_sha256": hashlib.sha256(cp_elf.read_bytes()).hexdigest(),
        "boot_compile_and_runtime_gates": 1,
        "cp_compile_write_gates": True,
        "cp_runtime_write_gates_initial": False,
        "generation_bound_operator_token": True,
        "generation_bound_one_shot_failpoints": True,
        "bounded_psram_corruption": True,
        "automatic_reset": False,
        "publisher_authenticated": False,
        "anti_rollback": False,
        "board_write_authorized": False,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--boot-elf", type=Path, required=True)
    parser.add_argument("--boot-bin", type=Path, required=True)
    parser.add_argument("--boot-crc", type=Path, required=True)
    parser.add_argument("--cp-elf", type=Path, required=True)
    parser.add_argument("--cp-config", type=Path, required=True)
    parser.add_argument("--sdk-source", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[3]
    try:
        result = verify(
            repo,
            args.boot_elf,
            args.boot_bin,
            args.boot_crc,
            args.cp_elf,
            args.cp_config,
            args.sdk_source,
        )
        if args.output is not None:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(
                json.dumps(result, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
    except (
        OSError,
        RuntimeError,
        subprocess.SubprocessError,
        ValueError,
        ValidationVerificationError,
    ) as error:
        print(f"BK7258 N15-F validation verification FAIL: {error}")
        return 1
    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        print(
            "BK7258 N15-F validation verification PASS: "
            "boot_gates=1 cp_runtime_initial=false board_authorized=false"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
