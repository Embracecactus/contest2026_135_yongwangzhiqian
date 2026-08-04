#!/usr/bin/env python3
"""Verify the portable BK7258 N15-D one-trial state machine."""

from __future__ import annotations

import argparse
import errno
import hashlib
import json
import re
import shutil
import struct
import subprocess
import tempfile
import zlib
from pathlib import Path

from bk7258_ab_layout import BOOT_SIZE, OTA_METADATA_SIZE
from bk7258_crc_expand import PACKET_DATA, PACKET_TOTAL, decode, expand
from pack_bk7258_ota_metadata import (
    BOOT_METADATA_RECORD_SIZE,
    META_CONFIRMED_B,
    META_PENDING_B,
    META_ROLLBACK_A,
    META_TRIAL_STARTED,
    append_record,
    build_boot_metadata,
    build_record,
)
from pack_bk7258_ota_pair import S_APP_FILE, build_bundle, write_bundle
from pack_bk7258_ota_stage import STAGE_DESCRIPTOR_SIZE
from verify_bk7258_ota_pair import synthetic_component


DECISION_A_FAILSAFE = 1
DECISION_A_ROLLBACK = 2
DECISION_B_TRIAL_CANDIDATE = 3
DECISION_B_CONFIRMED = 4

REASON_METADATA_INVALID = 2
REASON_TRIAL_CONSUMED = 4
REASON_ROLLBACK_REQUESTED = 5
REASON_PENDING_VALID = 6
REASON_CONFIRMED_VALID = 7

META_STATE_OFFSET = 12
META_SEQUENCE_OFFSET = 16
META_GENERATION_OFFSET = 24
META_VERSION_OFFSET = 44
META_PRIMARY_SHA_OFFSET = 92
META_DESCRIPTOR_OFFSET = 124
META_CRC_OFFSET = 508

PROGRAM_CHUNKS = BOOT_METADATA_RECORD_SIZE // 32
BOOT_LOGICAL_SIZE = BOOT_SIZE // PACKET_TOTAL * PACKET_DATA

OFFICIAL_HASHES = {
    "cp/middleware/driver/flash/flash_driver.c": (
        "fc198e01c3bf0507453ed922bf9898ccb56b110d2d58ad387daa65f424f34cfc"
    ),
    "cp/middleware/soc/bk7258/hal/flash_ll.h": (
        "ea011023988c2f3aa80f8c78c5bb9f4f6e1ebcb4499c2288a26561ed79ab3e35"
    ),
    "cp/middleware/driver/flash/flash_driver.h": (
        "d6400e36b3b1485fe7b27fda1da15a9849203710626e78b24810d866fda7388e"
    ),
    "cp/include/driver/hal/hal_flash_types.h": (
        "111b6db7db73d8987dd984b168c6a66196fd110e85b5f88805fea45be121959b"
    ),
    "ap/components/ota/ota_common.c": (
        "d8f316f6edbc57b52e7e5e4f4a29ab17762799613155a8261fb50893adb2ab97"
    ),
    "cp/components/bk_libs/bk7258/bootloader/ab_bootloader/bootloader.bin": (
        "3b27958ef78cbb7e56b57695585008465c759a7671cfd776334fec49d3164047"
    ),
    "cp/middleware/driver/wdt/wdt_driver.c": (
        "72154ef672d8ca831fb955909549e43c826b7dff1188ae950f31c6203018e37a"
    ),
    "cp/include/driver/wdt.h": (
        "fe205e70f4fdda657af46aa0be6569cac772a968495b3a0ad1f382e462cef981"
    ),
}


class TrialVerificationError(RuntimeError):
    """Raised when an N15-D invariant fails."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise TrialVerificationError(message)


def canonical_record_args(metadata: bytes) -> dict[str, object]:
    version = metadata[META_VERSION_OFFSET : META_VERSION_OFFSET + 24].split(b"\0", 1)[0]
    base_offset = META_VERSION_OFFSET + 24
    base = metadata[base_offset : base_offset + 24].split(b"\0", 1)[0]
    return {
        "generation": struct.unpack_from("<Q", metadata, META_GENERATION_OFFSET)[0],
        "timestamp": struct.unpack_from("<I", metadata, 32)[0],
        "cp_physical_length": struct.unpack_from("<I", metadata, 36)[0],
        "ap_physical_length": struct.unpack_from("<I", metadata, 40)[0],
        "version": version.decode("ascii"),
        "base_version": base.decode("ascii"),
        "primary_sha256": metadata[META_PRIMARY_SHA_OFFSET : META_PRIMARY_SHA_OFFSET + 32],
        "descriptor": metadata[
            META_DESCRIPTOR_OFFSET : META_DESCRIPTOR_OFFSET + STAGE_DESCRIPTOR_SIZE
        ],
    }


def metadata_for_states(pending: bytes, states: tuple[int, ...], *, first_sequence: int = 1) -> bytes:
    args = canonical_record_args(pending)
    output = b"\xff" * OTA_METADATA_SIZE
    for index, state in enumerate(states):
        output = append_record(
            output,
            build_record(state=state, sequence=first_sequence + index, **args),
        )
    return output


def record_crc(metadata: bytearray, index: int) -> None:
    start = index * BOOT_METADATA_RECORD_SIZE
    struct.pack_into(
        "<I",
        metadata,
        start + META_CRC_OFFSET,
        zlib.crc32(metadata[start : start + META_CRC_OFFSET]) & 0xFFFFFFFF,
    )


def compile_harness(repo: Path, output: Path, *, analyzer: bool = False) -> None:
    compiler = shutil.which("cc")
    pkg_config = shutil.which("pkg-config")
    if compiler is None or pkg_config is None:
        raise TrialVerificationError("host cc/pkg-config is unavailable")
    openssl = subprocess.run(
        [pkg_config, "--cflags", "--libs", "openssl"],
        check=True,
        capture_output=True,
        text=True,
        timeout=10,
    ).stdout.split()
    board = repo / "board/bk7258_t5ai"
    command = [
        compiler,
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        f"-I{board / 'bootloader'}",
        f"-I{board / 'chip/cp'}",
        f"-I{board / 'chip/include'}",
        str(board / "chip/cp/bk7258_ota_staging_core.c"),
        str(board / "bootloader/boot_ota_select_core.c"),
        str(board / "bootloader/boot_ota_trial_core.c"),
        str(board / "scripts/host/bk7258_boot_ota_trial_harness.c"),
        *openssl,
    ]
    if analyzer:
        command.extend(["-fanalyzer", "-fsyntax-only"])
    else:
        command.extend(["-o", str(output)])
    subprocess.run(command, check=True, timeout=60)


def source_contract(repo: Path) -> None:
    board = repo / "board/bk7258_t5ai"
    paths = {
        "selector_header": board / "bootloader/boot_ota_select_core.h",
        "selector": board / "bootloader/boot_ota_select_core.c",
        "trial_header": board / "bootloader/boot_ota_trial_core.h",
        "trial": board / "bootloader/boot_ota_trial_core.c",
        "harness": board / "scripts/host/bk7258_boot_ota_trial_harness.c",
        "boot_adapter": board / "bootloader/boot_ota_select.c",
        "boot_writer": board / "bootloader/boot_ota_flash_program.c",
        "boot_linker": board / "bootloader/bootloader.ld",
        "cp_adapter": board / "chip/cp/bk7258_ota_trial.c",
        "guard": board / "chip/cp/bk7258_flash_guard.c",
        "kconfig": board / "chip/Kconfig",
        "defconfig": board / "configs/cp_nsh_psram/defconfig",
    }
    texts = {name: path.read_text(encoding="utf-8") for name, path in paths.items()}
    required = {
        "selector_header": (
            "bk7258_boot_ota_metadata_inspect",
            "bk7258_boot_ota_prepare_transition",
            "struct bk7258_boot_ota_transition_s",
        ),
        "selector": (
            "scan_metadata(metadata, &scan);",
            "scan.identity.sequence == UINT64_MAX",
            "putle32(record + META_STATE_OFFSET",
            "putle64(record + META_SEQUENCE_OFFSET",
            "crc32_bytes(record, META_CRC_OFFSET)",
        ),
        "trial_header": (
            "BK7258_ROLE_OTA_METADATA_PRIMARY_OFFSET",
            "BK7258_BOOT_OTA_PROGRAM_GRANULE      32u",
            "BK7258_BOOT_OTA_TRIAL_SCRATCH_SIZE",
            "current_boot_trial",
        ),
        "trial": (
            "ops->compile_write_enabled",
            "ops->runtime_write_enabled",
            "BK7258_BOOT_OTA_PROGRAM_CHUNKS",
            "BK7258_BOOT_OTA_PROGRAM_GRANULE",
            "bk7258_boot_ota_metadata_inspect",
            "expected_state == BK7258_BOOT_OTA_META_PENDING_B",
            "next_state == BK7258_BOOT_OTA_META_TRIAL_STARTED",
        ),
        "harness": (
            "context->flash[offset + index] &= data[index]",
            '"write-torn-error"',
            '"chunk-read-mismatch"',
            "bk7258_boot_ota_select_core",
        ),
        "boot_adapter": (
            "BK7258_BOOT_OTA_TRIAL_COMPILE_GATE 0u",
            "BK7258_BOOT_OTA_TRIAL_RUNTIME_GATE 0u",
            "g_bk7258_boot_ota_trial_compile_gate",
            "g_bk7258_boot_ota_trial_runtime_gate",
            "boot_ota_install_ramfunc",
            "boot_ota_flash_program32",
            "trial_result.current_boot_trial",
            "g_bk7258_boot_ota_remap_compile_gate",
            "g_bk7258_boot_ota_remap_runtime_gate",
        ),
        "boot_writer": (
            'section(".boot_ota_ramfunc.text")',
            "FLASH_COMMAND_PROGRAM   12u",
            "FLASH_EXPECTED_ID       0x00c86517u",
            "METADATA_PRIMARY_START  BK7258_ROLE_OTA_METADATA_PRIMARY_OFFSET",
            "METADATA_MIRROR_START   BK7258_ROLE_OTA_METADATA_MIRROR_OFFSET",
            "boot_ota_ram_fail_reset",
            "FLASH_PROTECT_MASK",
        ),
        "boot_linker": (
            ".boot_ota_ramfunc 0x2800C000",
            "N15 boot OTA SRAM writer exceeds 4 KiB",
            "__boot_ota_ramfunc_load_start",
        ),
        "cp_adapter": (
            "CONFIG_BK7258_OTA_TRIAL_WRITE",
            "BK7258_FLASH_GUARD_OTA_METADATA",
            "bk7258_ota_trial_confirm",
            "bk7258_ota_trial_rollback",
            "BK7258_FLASH_REMAP_ENABLE",
            "bk7258_boot_ota_rotation_control_transition",
            "bk7258_boot_ota_rotation_publish_pending",
            "bk7258_boot_ota_rotation_health_update",
        ),
        "guard": (
            "BK7258_FLASH_GUARD_OTA_METADATA",
            "BK7258_ROLE_OTA_METADATA_PRIMARY_OFFSET",
            "BK7258_ROLE_OTA_METADATA_MIRROR_OFFSET",
            "CONFIG_BK7258_OTA_TRIAL_WRITE",
        ),
        "kconfig": (
            "config BK7258_OTA_TRIAL",
            "config BK7258_OTA_TRIAL_WRITE",
            "depends on BK7258_OTA_TRIAL && BK7258_OTA_STAGING_WRITE",
        ),
        "defconfig": ("CONFIG_BK7258_OTA_TRIAL=y",),
    }
    for name, fragments in required.items():
        for fragment in fragments:
            require(fragment in texts[name], f"N15-D source closure missing: {name}: {fragment}")
    require("erase_sector" not in texts["trial"], "state append must not erase metadata")
    require(
        "CONFIG_BK7258_OTA_TRIAL_WRITE=y" not in texts["defconfig"],
        "final CP profile must keep the trial write compile gate off",
    )


def official_contract(sdk_source: Path | None) -> dict[str, object]:
    if sdk_source is None:
        return {"status": "not-run", "release": "v3.1.1.9", "reason": "--sdk-source not provided"}
    require(
        sdk_source.name == "bk_avdk_smp-release-v3.1.1.9",
        "SDK source must be exact v3.1.1.9",
    )
    payloads: dict[str, bytes] = {}
    verified: list[dict[str, str]] = []
    for relative, expected in OFFICIAL_HASHES.items():
        payload = (sdk_source / relative).read_bytes()
        observed = hashlib.sha256(payload).hexdigest()
        require(observed == expected, f"official N15-D input hash drift: {relative}")
        payloads[relative] = payload
        verified.append({"path": relative, "sha256": observed})

    driver = payloads["cp/middleware/driver/flash/flash_driver.c"].decode()
    driver_header = payloads["cp/middleware/driver/flash/flash_driver.h"].decode()
    hal_types = payloads["cp/include/driver/hal/hal_flash_types.h"].decode()
    ll = payloads["cp/middleware/soc/bk7258/hal/flash_ll.h"].decode()
    ota = payloads["ap/components/ota/ota_common.c"].decode()
    wdt = payloads["cp/middleware/driver/wdt/wdt_driver.c"].decode()
    wdt_header = payloads["cp/include/driver/wdt.h"].decode()
    for fragment in (
        "os_memset(pb, 0xFF, FLASH_BYTES_CNT);",
        "flash_hal_write_data(&s_flash.hal, buf[i]);",
        "flash_hal_set_op_cmd_write(&s_flash.hal, addr);",
    ):
        require(fragment in driver, f"official Flash write contract drift: {fragment}")
    require(
        "#define FLASH_BYTES_CNT                  32" in driver_header,
        "official Flash program granule drift",
    )
    require(
        "FLASH_OP_CMD_PP    = 12" in hal_types,
        "official Flash page-program command drift",
    )
    for fragment in (
        "hw->op_cmd.op_type_sw = FLASH_OP_CMD_PP;",
        "hw->data_sw_flash = data;",
        "while (flash_ll_is_busy(hw));",
    ):
        require(fragment in ll, f"official Flash LL contract drift: {fragment}")
    for fragment in (
        "bk_ota_double_check_for_execution",
        "bk_ota_confirm_update_partition(CONFIRM_EXEC_A);",
        "bk_ota_confirm_update_partition(CONFIRM_EXEC_B);",
        "cust_confirm_flag= 0x1",
    ):
        require(fragment in ota, f"official one-trial/confirm contract drift: {fragment}")
    for fragment in (
        "wdt_deinit_common();",
        "s_wdt.init_bits &= ~BIT(0);",
        "WDT_RETURN_ON_NOT_INIT();",
        "wdt_hal_init_wdt(&s_wdt.hal, s_wdt_period);",
    ):
        require(fragment in wdt, f"official WDT stop/feed contract drift: {fragment}")
    for fragment in (
        "bk_err_t bk_wdt_stop(void);",
        "bk_err_t bk_wdt_feed(void);",
    ):
        require(fragment in wdt_header, f"official WDT ABI drift: {fragment}")
    return {"status": "pass", "release": "v3.1.1.9", "source_hashes": verified}


def parse_nm(nm: str, elf: Path) -> dict[str, tuple[int, int, str]]:
    output = subprocess.run(
        [nm, "-S", "--defined-only", str(elf)],
        check=True,
        capture_output=True,
        text=True,
        timeout=30,
    ).stdout
    symbols: dict[str, tuple[int, int, str]] = {}
    for line in output.splitlines():
        fields = line.split()
        if len(fields) == 4:
            symbols[fields[3]] = (int(fields[0], 16), int(fields[1], 16), fields[2])
    return symbols


def section_info(objdump: str, elf: Path, section: str) -> tuple[int, int, int]:
    output = subprocess.run(
        [objdump, "-h", str(elf)],
        check=True,
        capture_output=True,
        text=True,
        timeout=30,
    ).stdout
    pattern = re.compile(
        rf"^\s*\d+\s+{re.escape(section)}\s+([0-9a-fA-F]+)\s+"
        r"([0-9a-fA-F]+)\s+([0-9a-fA-F]+)",
        re.MULTILINE,
    )
    match = pattern.search(output)
    require(match is not None, f"ELF section missing: {section}")
    return tuple(int(value, 16) for value in match.groups())  # type: ignore[return-value]


def verify_gate(
    symbols: dict[str, tuple[int, int, str]],
    logical: bytes,
    name: str,
    expected: int,
) -> int:
    require(name in symbols, f"ELF gate symbol missing: {name}")
    address, size, kind = symbols[name]
    require(size == 4 and kind.upper() == "R", f"{name} is not immutable Flash data")
    offset = address - 0x02000000
    require(0 <= offset <= len(logical) - 4, f"{name} is outside boot image")
    value = struct.unpack_from("<I", logical, offset)[0]
    require(value == expected, f"{name} must be {expected}, got {value}")
    return value


def verify_boot_elf(
    elf: Path,
    boot_bin: Path,
    boot_crc: Path,
    *,
    expected_gate_value: int = 0,
) -> dict[str, object]:
    nm = shutil.which("arm-none-eabi-nm")
    objdump = shutil.which("arm-none-eabi-objdump")
    objcopy = shutil.which("arm-none-eabi-objcopy")
    if nm is None or objdump is None or objcopy is None:
        raise TrialVerificationError("Arm GNU binutils are unavailable")
    symbols = parse_nm(nm, elf)
    required = {
        "boot_ota_select_app",
        "bk7258_boot_ota_rotation_select_core",
        "bk7258_boot_ota_rotation_inspect",
        "bk7258_boot_ota_rotation_select",
        "bk7258_boot_ota_rotation_latest",
        "bk7258_boot_ota_rotation_prepare_transition",
        "bk7258_boot_ota_rotation_trial_transition",
        "boot_ota_flash_program32",
        "g_bk7258_boot_ota_trial_compile_gate",
        "g_bk7258_boot_ota_trial_runtime_gate",
        "g_bk7258_boot_ota_select_compile_gate",
        "g_bk7258_boot_ota_select_runtime_gate",
        "g_bk7258_boot_ota_remap_compile_gate",
        "g_bk7258_boot_ota_remap_runtime_gate",
    }
    missing = sorted(required - symbols.keys())
    require(not missing, f"N15-D boot ELF closure missing symbols: {missing}")
    forbidden = {
        "bk7258_boot_ota_set_trial_enabled",
        "bk7258_boot_ota_set_selection_enabled",
        "bk7258_boot_ota_set_remap_enabled",
        "bk7258_ota_core_stage",
    }
    present = sorted(forbidden & symbols.keys())
    require(not present, f"forbidden mutating/setter symbols in boot ELF: {present}")
    undefined = subprocess.run(
        [nm, "-u", str(elf)], check=True, capture_output=True, text=True, timeout=30
    ).stdout.strip()
    require(not undefined, f"boot ELF has undefined symbols: {undefined}")

    logical = boot_bin.read_bytes()
    encoded = boot_crc.read_bytes()
    require(
        0 < len(logical) <= BOOT_LOGICAL_SIZE,
        "boot logical image exceeds its CSV-defined slot",
    )
    require(
        len(encoded) == BOOT_SIZE,
        f"boot encoded image must be exactly 0x{BOOT_SIZE:x}",
    )
    decoded = decode(encoded)
    require(decoded[: len(logical)] == logical, "boot encoded image does not contain bl.bin")
    require(
        decoded[len(logical) :] == b"\xff" * (BOOT_LOGICAL_SIZE - len(logical)),
        "boot encoded padding is not erased",
    )

    gate_names = (
        "g_bk7258_boot_ota_select_compile_gate",
        "g_bk7258_boot_ota_select_runtime_gate",
        "g_bk7258_boot_ota_remap_compile_gate",
        "g_bk7258_boot_ota_remap_runtime_gate",
        "g_bk7258_boot_ota_trial_compile_gate",
        "g_bk7258_boot_ota_trial_runtime_gate",
    )
    gates = {
        name: verify_gate(symbols, logical, name, expected_gate_value)
        for name in gate_names
    }

    size, vma, lma = section_info(objdump, elf, ".boot_ota_ramfunc")
    require(vma == 0x2800C000 and 0 < size <= 0x1000, "SRAM writer VMA/size drift")
    require(0x02000000 <= lma and lma + size <= 0x02010000, "SRAM writer LMA drift")
    entry, entry_size, _ = symbols["boot_ota_flash_program32"]
    require(vma <= entry and entry + entry_size <= vma + size, "program entry escaped SRAM")
    disassembly = subprocess.run(
        [objdump, "-d", "-j", ".boot_ota_ramfunc", str(elf)],
        check=True,
        capture_output=True,
        text=True,
        timeout=30,
    ).stdout
    for target in re.findall(r"\bblx?\s+([0-9a-fA-F]+)", disassembly):
        address = int(target, 16)
        require(vma <= address < vma + size, f"SRAM writer calls outside closure: 0x{address:x}")
    with tempfile.TemporaryDirectory(prefix="bk7258-n15d-elf-") as directory:
        raw = Path(directory) / "ramfunc.bin"
        subprocess.run(
            [objcopy, "--dump-section", f".boot_ota_ramfunc={raw}", str(elf)],
            check=True,
            timeout=30,
        )
        closure = raw.read_bytes()
    require(len(closure) == size, "SRAM writer extraction size drift")
    # Do not interpret every four instruction bytes as a literal: valid Thumb
    # opcodes can coincidentally form a 32-bit number in the XIP address range.
    # Objdump honours the ELF mapping symbols ($t/$d), so inspect its actual
    # data words and independently resolve every PC-relative literal load.
    xip_literals: list[tuple[int, int]] = []
    for match in re.finditer(
        r"(?m)^\s*([0-9a-fA-F]+):.*?\.word\s+0x([0-9a-fA-F]+)\s*$",
        disassembly,
    ):
        address = int(match.group(1), 16)
        value = int(match.group(2), 16)
        if 0x02000000 <= value < 0x02260000:
            xip_literals.append((address - vma, value))

    literal_targets: set[int] = set()
    for target in re.findall(
        r"(?m)\bldr(?:\.w)?\s+[^\n]*?\[pc[^\]]*\][^\n]*?;\s*\(?([0-9a-fA-F]+)",
        disassembly,
    ):
        address = int(target, 16)
        require(
            vma <= address and address + 4 <= vma + size and address % 4 == 0,
            f"SRAM writer literal load escaped closure: 0x{address:x}",
        )
        literal_targets.add(address)
        value = struct.unpack_from("<I", closure, address - vma)[0]
        if 0x02000000 <= value < 0x02260000:
            xip_literals.append((address - vma, value))
    require(not xip_literals, f"SRAM writer contains XIP literals: {xip_literals}")
    return {
        "status": "pass",
        "elf": str(elf.resolve()),
        "elf_sha256": hashlib.sha256(elf.read_bytes()).hexdigest(),
        "logical_size": len(logical),
        "logical_sha256": hashlib.sha256(logical).hexdigest(),
        "physical_size": len(encoded),
        "physical_sha256": hashlib.sha256(encoded).hexdigest(),
        "gates": gates,
        "expected_gate_value": expected_gate_value,
        "ramfunc": {
            "vma": vma,
            "lma": lma,
            "size": size,
            "literal_loads": len(literal_targets),
            "xip_literals": 0,
        },
    }


def verify_cp_elf(
    elf: Path, config: Path, *, validation_profile: bool = False
) -> dict[str, object]:
    nm = shutil.which("arm-none-eabi-nm")
    objdump = shutil.which("arm-none-eabi-objdump")
    if nm is None or objdump is None:
        raise TrialVerificationError("Arm GNU binutils are unavailable")
    symbols = parse_nm(nm, elf)
    required = {
        "bk7258_ota_trial_initialize",
        "bk7258_ota_trial_confirm",
        "bk7258_ota_trial_rollback",
        "bk7258_ota_trial_get_status",
        "bk7258_ota_publish_pending",
        "bk7258_ota_trial_write_enabled",
        "bk7258_boot_ota_rotation_control_transition",
        "bk7258_boot_ota_rotation_publish_pending",
        "bk7258_boot_ota_rotation_health_update",
        "bk7258_boot_ota_rotation_prepare_transition",
        "bk7258_boot_ota_rotation_inspect",
        "bk7258_boot_ota_rotation_select",
        "__wrap_bk_flash_partition_write_perm_check_by_addr",
        "g_bk7258_ota_trial_runtime_write",
    }
    if validation_profile:
        required.update(
            {
                "bk7258_ota_staging_set_write_enabled",
                "bk7258_ota_trial_set_write_enabled",
                "bk7258_ota_fault_initialize",
                "bk7258_ota_fault_arm",
                "bk7258_ota_fault_begin",
                "bk7258_ota_fault_before",
                "bk7258_ota_fault_get_status",
                "bk7258_ota_fault_finish",
                "bk7258_ota_fault_core_arm",
                "bk7258_ota_fault_core_begin",
                "bk7258_ota_fault_core_before",
                "bkota_main",
            }
        )
    missing = sorted(required - symbols.keys())
    require(not missing, f"N15-D CP ELF closure missing symbols: {missing}")
    forbidden = {"bktrial", "bktrial_main"}
    if not validation_profile:
        forbidden.update(
            {
                "bk7258_ota_trial_set_write_enabled",
                "bk7258_ota_fault_initialize",
                "bk7258_ota_fault_arm",
                "bk7258_ota_fault_begin",
                "bk7258_ota_fault_before",
                "bkota_fault_arm_command",
                "bkota_corrupt_memory",
            }
        )
    present = sorted(forbidden & symbols.keys())
    require(not present, f"trial runtime setter/command leaked into CP ELF: {present}")
    config_text = config.read_text(encoding="utf-8")
    require("CONFIG_BK7258_OTA_TRIAL=y" in config_text, "CP trial closure config is absent")
    require(
        "CONFIG_BK7258_OTA_HEALTH_STABLE_MS=5000" in config_text,
        "CP trial health window is not the reviewed 5000 ms",
    )
    require(
        "CONFIG_BK7258_OTA_HEALTH_POLL_MS=250" in config_text,
        "CP trial health poll interval is not the reviewed 250 ms",
    )
    if validation_profile:
        for setting in (
            "CONFIG_BK7258_OTA_STAGING_WRITE=y",
            "CONFIG_BK7258_OTA_TRIAL_WRITE=y",
            "CONFIG_BK7258_OTA_VALIDATION=y",
            "CONFIG_BK7258_OTA_FAULT_INJECTION=y",
        ):
            require(setting in config_text, f"validation CP config omits {setting}")
    else:
        require(
            "CONFIG_BK7258_OTA_TRIAL_WRITE=y" not in config_text,
            "CP trial write compile gate must remain off",
        )
        require(
            "CONFIG_BK7258_OTA_STAGING_WRITE=y" not in config_text,
            "CP staging write compile gate must remain off",
        )
        require(
            "CONFIG_BK7258_OTA_FAULT_INJECTION=y" not in config_text,
            "CP deterministic fault injection must remain absent",
        )
    compile_gate_disassembly = subprocess.run(
        [objdump, "-d", f"--disassemble=bk7258_ota_trial_compile_write", str(elf)],
        check=True,
        capture_output=True,
        text=True,
        timeout=30,
    ).stdout
    expected_compile = 1 if validation_profile else 0
    require(
        re.search(
            rf"movs?\s+r0,\s*#{expected_compile}", compile_gate_disassembly
        )
        is not None,
        f"CP compiled trial gate does not return {expected_compile}",
    )
    if validation_profile:
        staging_gate_disassembly = subprocess.run(
            [
                objdump,
                "-d",
                "--disassemble=bk7258_ota_compile_write_enabled",
                str(elf),
            ],
            check=True,
            capture_output=True,
            text=True,
            timeout=30,
        ).stdout
        require(
            re.search(r"movs?\s+r0,\s*#1", staging_gate_disassembly)
            is not None,
            "CP compiled staging gate does not return one",
        )
    return {
        "status": "pass",
        "elf": str(elf.resolve()),
        "elf_sha256": hashlib.sha256(elf.read_bytes()).hexdigest(),
        "required_symbols": sorted(required),
        "forbidden_symbols_present": present,
        "validation_profile": validation_profile,
        "compile_trial_write_enabled": validation_profile,
        "compile_staging_write_enabled": validation_profile,
        "runtime_setter_present": validation_profile,
        "runtime_gates_initialize_false": True,
    }


def run_case(
    harness: Path,
    root: Path,
    name: str,
    metadata: bytes,
    primary_path: Path,
    secondary_path: Path,
    *,
    generation: int,
    previous: int,
    next_state: int,
    mode: str,
    chunk: int,
    status: int,
    current: bool,
    decision: int,
    reason: int,
) -> tuple[bytes, str]:
    input_path = root / f"{name}-input.bin"
    output_path = root / f"{name}-output.bin"
    input_path.write_bytes(metadata)
    result = subprocess.run(
        [
            str(harness),
            str(input_path),
            str(primary_path),
            str(secondary_path),
            str(generation),
            str(previous),
            str(next_state),
            mode,
            str(chunk),
            str(status),
            "1" if current else "0",
            str(decision),
            str(reason),
            str(output_path),
        ],
        check=False,
        capture_output=True,
        text=True,
        timeout=60,
    )
    require(
        result.returncode == 0 and "BK7258 N15-D harness PASS" in result.stdout,
        f"host harness failed for {name}: {result.stdout}{result.stderr}",
    )
    return output_path.read_bytes(), result.stdout.strip()


def state_decision(state: int) -> tuple[int, int]:
    if state == META_PENDING_B:
        return DECISION_B_TRIAL_CANDIDATE, REASON_PENDING_VALID
    if state == META_TRIAL_STARTED:
        return DECISION_A_ROLLBACK, REASON_TRIAL_CONSUMED
    if state == META_CONFIRMED_B:
        return DECISION_B_CONFIRMED, REASON_CONFIRMED_VALID
    if state == META_ROLLBACK_A:
        return DECISION_A_ROLLBACK, REASON_ROLLBACK_REQUESTED
    raise TrialVerificationError(f"unhandled metadata state {state}")


def committed_decision(next_state: int) -> tuple[int, int]:
    return state_decision(next_state)


def self_test(repo: Path, sdk_source: Path | None) -> dict[str, object]:
    generation = 17
    version = "n15-d-test"
    base_version = "n15-c-test"
    timestamp = 0x12345678
    candidate_cp = synthetic_component("cp")
    candidate_ap = synthetic_component("ap")
    primary_cp = bytearray(candidate_cp)
    primary_ap = bytearray(candidate_ap)
    primary_cp[0x180] ^= 0x5A
    primary_ap[0x180] ^= 0xA5
    positive = 0
    negative = 0
    reset_boundaries = 0

    source_contract(repo)
    official = official_contract(sdk_source)
    with tempfile.TemporaryDirectory(prefix="bk7258-n15d-") as directory:
        root = Path(directory)
        harness = root / "trial-harness"
        compile_harness(repo, harness)
        compile_harness(repo, root / "unused", analyzer=True)
        bundle = root / "bundle"
        files, _ = build_bundle(
            candidate_cp,
            candidate_ap,
            generation=generation,
            version=version,
            base_version=base_version,
            timestamp=timestamp,
        )
        write_bundle(bundle, files)
        cp_path = root / "primary-cp.bin"
        ap_path = root / "primary-ap.bin"
        cp_path.write_bytes(expand(bytes(primary_cp)))
        ap_path.write_bytes(expand(bytes(primary_ap)))
        pending, primary, _ = build_boot_metadata(
            bundle,
            cp_path,
            ap_path,
            generation=generation,
            version=version,
            base_version=base_version,
            timestamp=timestamp,
            sdk_source=sdk_source,
        )
        primary_path = root / "primary.bin"
        secondary_path = root / "secondary.bin"
        primary_path.write_bytes(primary)
        secondary_path.write_bytes(files[S_APP_FILE])
        trial = metadata_for_states(pending, (META_PENDING_B, META_TRIAL_STARTED))
        confirmed = metadata_for_states(
            pending, (META_PENDING_B, META_TRIAL_STARTED, META_CONFIRMED_B)
        )
        rollback = metadata_for_states(
            pending, (META_PENDING_B, META_TRIAL_STARTED, META_ROLLBACK_A)
        )

        positive_cases = (
            ("trial", pending, META_PENDING_B, META_TRIAL_STARTED, True, trial),
            ("confirm", trial, META_TRIAL_STARTED, META_CONFIRMED_B, False, confirmed),
            ("rollback", trial, META_TRIAL_STARTED, META_ROLLBACK_A, False, rollback),
        )
        for name, initial, previous, next_state, current, expected in positive_cases:
            decision, reason = committed_decision(next_state)
            observed, _ = run_case(
                harness,
                root,
                name,
                initial,
                primary_path,
                secondary_path,
                generation=generation,
                previous=previous,
                next_state=next_state,
                mode="normal",
                chunk=-1,
                status=0,
                current=current,
                decision=decision,
                reason=reason,
            )
            require(observed == expected, f"{name} append is not byte-exact canonical metadata")
            positive += 1

        repeated, _ = run_case(
            harness,
            root,
            "trial-repeat",
            pending,
            primary_path,
            secondary_path,
            generation=generation,
            previous=META_PENDING_B,
            next_state=META_TRIAL_STARTED,
            mode="normal",
            chunk=-1,
            status=0,
            current=True,
            decision=DECISION_A_ROLLBACK,
            reason=REASON_TRIAL_CONSUMED,
        )
        require(repeated == trial, "trial transition deterministic rebuild drift")
        positive += 1

        unchanged_cases = (
            ("compile-gate", "compile-disabled", -errno.EACCES),
            ("runtime-gate", "runtime-disabled", -errno.EACCES),
            ("lock-timeout", "lock-error", -errno.ETIMEDOUT),
            ("lock-short", "lock-short", -errno.EIO),
            ("initial-read-timeout", "initial-read-error", -errno.ETIMEDOUT),
            ("initial-read-short", "initial-read-short", -errno.EIO),
        )
        for name, mode, status in unchanged_cases:
            observed, _ = run_case(
                harness,
                root,
                name,
                pending,
                primary_path,
                secondary_path,
                generation=generation,
                previous=META_PENDING_B,
                next_state=META_TRIAL_STARTED,
                mode=mode,
                chunk=-1,
                status=status,
                current=False,
                decision=DECISION_B_TRIAL_CANDIDATE,
                reason=REASON_PENDING_VALID,
            )
            require(observed == pending, f"{name} changed metadata before mutation")
            negative += 1

        structural_cases: list[tuple[str, bytes, int, int, int, int, int]] = []
        corrupt = bytearray(pending)
        corrupt[20] ^= 1
        structural_cases.append(
            ("bad-crc", bytes(corrupt), generation, META_PENDING_B, META_TRIAL_STARTED,
             -errno.EBADMSG, DECISION_A_FAILSAFE)
        )
        dirty_gap = bytearray(pending)
        dirty_gap[2 * BOOT_METADATA_RECORD_SIZE] = 0
        structural_cases.append(
            ("dirty-gap", bytes(dirty_gap), generation, META_PENDING_B, META_TRIAL_STARTED,
             -errno.EBADMSG, DECISION_A_FAILSAFE)
        )
        identity = bytearray(trial)
        identity[BOOT_METADATA_RECORD_SIZE + META_VERSION_OFFSET] ^= 1
        record_crc(identity, 1)
        structural_cases.append(
            ("identity-drift", bytes(identity), generation, META_TRIAL_STARTED,
             META_CONFIRMED_B, -errno.EBADMSG, DECISION_A_FAILSAFE)
        )
        max_sequence = bytearray(pending)
        struct.pack_into("<Q", max_sequence, META_SEQUENCE_OFFSET, 0xFFFFFFFFFFFFFFFF)
        record_crc(max_sequence, 0)
        structural_cases.append(
            ("sequence-overflow", bytes(max_sequence), generation, META_PENDING_B,
             META_TRIAL_STARTED, -errno.EOVERFLOW, DECISION_B_TRIAL_CANDIDATE)
        )
        full_dirty = bytearray(pending)
        full_dirty[BOOT_METADATA_RECORD_SIZE:] = b"\0" * (
            OTA_METADATA_SIZE - BOOT_METADATA_RECORD_SIZE
        )
        structural_cases.append(
            ("no-erased-slot", bytes(full_dirty), generation, META_PENDING_B,
             META_TRIAL_STARTED, -errno.EBADMSG, DECISION_A_FAILSAFE)
        )
        structural_cases.extend(
            (
                ("generation-stale", pending, generation + 1, META_PENDING_B,
                 META_TRIAL_STARTED, -errno.ESTALE, DECISION_B_TRIAL_CANDIDATE),
                ("state-mismatch", pending, generation, META_TRIAL_STARTED,
                 META_CONFIRMED_B, -errno.EPERM, DECISION_B_TRIAL_CANDIDATE),
                ("pending-direct-confirm", pending, generation, META_PENDING_B,
                 META_CONFIRMED_B, -errno.EINVAL, DECISION_B_TRIAL_CANDIDATE),
                ("confirmed-terminal", confirmed, generation, META_CONFIRMED_B,
                 META_ROLLBACK_A, -errno.EINVAL, DECISION_B_CONFIRMED),
                ("rollback-terminal", rollback, generation, META_ROLLBACK_A,
                 META_CONFIRMED_B, -errno.EINVAL, DECISION_A_ROLLBACK),
            )
        )
        for name, payload, expected_generation, previous, next_state, status, decision in structural_cases:
            if decision == DECISION_A_FAILSAFE:
                reason = REASON_METADATA_INVALID
            elif decision == DECISION_B_TRIAL_CANDIDATE:
                reason = REASON_PENDING_VALID
            elif decision == DECISION_B_CONFIRMED:
                reason = REASON_CONFIRMED_VALID
            else:
                reason = REASON_ROLLBACK_REQUESTED
            run_case(
                harness,
                root,
                name,
                payload,
                primary_path,
                secondary_path,
                generation=expected_generation,
                previous=previous,
                next_state=next_state,
                mode="normal",
                chunk=-1,
                status=status,
                current=False,
                decision=decision,
                reason=reason,
            )
            negative += 1

        transitions = (
            ("trial", pending, META_PENDING_B, META_TRIAL_STARTED),
            ("confirm", trial, META_TRIAL_STARTED, META_CONFIRMED_B),
            ("rollback", trial, META_TRIAL_STARTED, META_ROLLBACK_A),
        )
        for transition_name, initial, previous, next_state in transitions:
            for chunk in range(PROGRAM_CHUNKS):
                if chunk == PROGRAM_CHUNKS - 1:
                    decision, reason = committed_decision(next_state)
                else:
                    decision, reason = DECISION_A_FAILSAFE, REASON_METADATA_INVALID
                run_case(
                    harness,
                    root,
                    f"reset-{transition_name}-{chunk}",
                    initial,
                    primary_path,
                    secondary_path,
                    generation=generation,
                    previous=previous,
                    next_state=next_state,
                    mode="write-after-error",
                    chunk=chunk,
                    status=-errno.ETIMEDOUT,
                    current=False,
                    decision=decision,
                    reason=reason,
                )
                negative += 1
                reset_boundaries += 1

        for chunk in range(PROGRAM_CHUNKS):
            run_case(
                harness,
                root,
                f"torn-trial-{chunk}",
                pending,
                primary_path,
                secondary_path,
                generation=generation,
                previous=META_PENDING_B,
                next_state=META_TRIAL_STARTED,
                mode="write-torn-error",
                chunk=chunk,
                status=-errno.ETIMEDOUT,
                current=False,
                decision=DECISION_A_FAILSAFE,
                reason=REASON_METADATA_INVALID,
            )
            negative += 1

        for chunk in range(PROGRAM_CHUNKS):
            if chunk == PROGRAM_CHUNKS - 1:
                decision, reason = DECISION_A_ROLLBACK, REASON_TRIAL_CONSUMED
            else:
                decision, reason = DECISION_A_FAILSAFE, REASON_METADATA_INVALID
            run_case(
                harness,
                root,
                f"chunk-read-timeout-{chunk}",
                pending,
                primary_path,
                secondary_path,
                generation=generation,
                previous=META_PENDING_B,
                next_state=META_TRIAL_STARTED,
                mode="chunk-read-error",
                chunk=chunk,
                status=-errno.ETIMEDOUT,
                current=False,
                decision=decision,
                reason=reason,
            )
            negative += 1

        representative = (
            ("write-before-error", -errno.ETIMEDOUT),
            ("write-after-short", -errno.EIO),
            ("chunk-read-short", -errno.EIO),
            ("chunk-read-mismatch", -errno.EIO),
        )
        for mode, status in representative:
            for chunk in (0, PROGRAM_CHUNKS - 1):
                if mode == "write-before-error" and chunk == 0:
                    decision, reason = DECISION_B_TRIAL_CANDIDATE, REASON_PENDING_VALID
                elif mode == "write-before-error":
                    decision, reason = DECISION_A_FAILSAFE, REASON_METADATA_INVALID
                elif chunk == PROGRAM_CHUNKS - 1:
                    decision, reason = DECISION_A_ROLLBACK, REASON_TRIAL_CONSUMED
                else:
                    decision, reason = DECISION_A_FAILSAFE, REASON_METADATA_INVALID
                run_case(
                    harness,
                    root,
                    f"{mode}-{chunk}",
                    pending,
                    primary_path,
                    secondary_path,
                    generation=generation,
                    previous=META_PENDING_B,
                    next_state=META_TRIAL_STARTED,
                    mode=mode,
                    chunk=chunk,
                    status=status,
                    current=False,
                    decision=decision,
                    reason=reason,
                )
                negative += 1

        final_modes = (
            ("final-read-error", -errno.ETIMEDOUT),
            ("final-read-short", -errno.EIO),
            ("final-read-mismatch", -errno.EIO),
        )
        for transition_name, initial, previous, next_state in transitions:
            decision, reason = committed_decision(next_state)
            for mode, status in final_modes:
                run_case(
                    harness,
                    root,
                    f"{transition_name}-{mode}",
                    initial,
                    primary_path,
                    secondary_path,
                    generation=generation,
                    previous=previous,
                    next_state=next_state,
                    mode=mode,
                    chunk=-1,
                    status=status,
                    current=False,
                    decision=decision,
                    reason=reason,
                )
                negative += 1

    return {
        "format": 1,
        "status": "pass",
        "positive_cases": positive,
        "negative_cases": negative,
        "program_granule": 32,
        "program_chunks_per_record": PROGRAM_CHUNKS,
        "reset_boundaries": reset_boundaries,
        "source_contract": True,
        "static_analyzer": True,
        "official_contract": official,
        "exact_sdk_source": sdk_source is not None,
        "compile_trial_write_enabled": False,
        "runtime_trial_write_enabled": False,
        "board_write_authorized": False,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--sdk-source", type=Path)
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--boot-elf", type=Path)
    parser.add_argument("--boot-bin", type=Path)
    parser.add_argument("--boot-crc", type=Path)
    parser.add_argument("--cp-elf", type=Path)
    parser.add_argument("--cp-config", type=Path)
    parser.add_argument("--elf-only", action="store_true")
    parser.add_argument("--validation-profile", action="store_true")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[3]

    try:
        if args.elf_only:
            required = (
                args.boot_elf,
                args.boot_bin,
                args.boot_crc,
                args.cp_elf,
                args.cp_config,
            )
            if any(path is None for path in required):
                parser.error(
                    "--elf-only requires --boot-elf/--boot-bin/--boot-crc/"
                    "--cp-elf/--cp-config"
                )
            source_contract(repo)
            result = {
                "format": 1,
                "status": "pass",
                "source_contract": True,
                "official_contract": official_contract(args.sdk_source),
                "boot_elf": verify_boot_elf(
                    args.boot_elf,
                    args.boot_bin,
                    args.boot_crc,
                    expected_gate_value=1 if args.validation_profile else 0,
                ),
                "cp_elf": verify_cp_elf(
                    args.cp_elf,
                    args.cp_config,
                    validation_profile=args.validation_profile,
                ),
                "validation_profile": args.validation_profile,
                "compile_trial_write_enabled": args.validation_profile,
                "runtime_trial_write_enabled": False,
                "board_write_authorized": False,
            }
        elif args.self_test:
            result = self_test(repo, args.sdk_source)
        else:
            parser.error("choose --self-test or --elf-only")
        if args.output is not None:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(
                json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
            )
    except (OSError, subprocess.SubprocessError, ValueError, TrialVerificationError) as error:
        print(f"BK7258 N15-D trial verification FAIL: {error}")
        return 1

    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        if args.elf_only:
            if args.validation_profile:
                print(
                    "BK7258 N15-F validation ELF verification PASS: "
                    "compile_gates=true runtime_initial=false board_authorized=false"
                )
            else:
                print("BK7258 N15-D trial ELF verification PASS: writes_enabled=false")
        else:
            print(
                "BK7258 N15-D trial verification PASS: "
                f"positive={result['positive_cases']} negative={result['negative_cases']} "
                f"reset_boundaries={result['reset_boundaries']} writes_enabled=false"
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
