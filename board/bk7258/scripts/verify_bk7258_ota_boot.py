#!/usr/bin/env python3
"""Verify N15-C metadata and run portable boot-selection fault injection."""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import shutil
import struct
import subprocess
import tempfile
import zlib
from pathlib import Path

from bk7258_ab_layout import (
    AP_A_SIZE,
    BOOT_SIZE,
    CP_A_SIZE,
    OTA_METADATA_SIZE,
    PAIR_B_SIZE,
)
from bk7258_crc_expand import PACKET_DATA, PACKET_TOTAL, crc16, decode, expand
from pack_bk7258_ota_metadata import (
    BOOT_METADATA_FILE,
    BOOT_METADATA_RECORD_SIZE,
    META_CONFIRMED_B,
    META_PENDING_B,
    META_ROLLBACK_A,
    META_TRIAL_STARTED,
    BootMetadataError,
    append_record,
    build_boot_metadata,
    build_record,
)
from pack_bk7258_ota_pair import S_APP_FILE, build_bundle, parse_int, write_bundle
from pack_bk7258_ota_rotation import (
    PENDING_A,
    PENDING_B,
    ROTATION_MAGIC,
    build_rotation_bank,
)
from pack_bk7258_ota_stage import STAGE_DESCRIPTOR_SIZE
from verify_bk7258_ota_pair import synthetic_component


DECISION_A_BASELINE = 0
DECISION_A_FAILSAFE = 1
DECISION_A_ROLLBACK = 2
DECISION_B_TRIAL_CANDIDATE = 3
DECISION_B_CONFIRMED = 4

REASON_METADATA_ERASED = 1
REASON_METADATA_INVALID = 2
REASON_CANDIDATE_INVALID = 3
REASON_TRIAL_CONSUMED = 4
REASON_ROLLBACK_REQUESTED = 5
REASON_PENDING_VALID = 6
REASON_CONFIRMED_VALID = 7

META_STATE_OFFSET = 12
META_SEQUENCE_OFFSET = 16
META_GENERATION_OFFSET = 24
META_CP_LENGTH_OFFSET = 36
META_AP_LENGTH_OFFSET = 40
META_VERSION_OFFSET = 44
META_PRIMARY_SHA_OFFSET = 92
META_DESCRIPTOR_OFFSET = 124
META_CRC_OFFSET = 508

STAGE_GENERATION_OFFSET = 96
STAGE_PHYSICAL_OFFSET = 108
STAGE_RBL_PHYSICAL_OFFSET = 124
STAGE_VERSION_OFFSET = 160
STAGE_CRC_OFFSET = 380
BOOT_LOGICAL_SIZE = BOOT_SIZE // PACKET_TOTAL * PACKET_DATA

OFFICIAL_CONTRACT_HASHES = {
    "cp/middleware/driver/flash/flash_driver.c": (
        "fc198e01c3bf0507453ed922bf9898ccb56b110d2d58ad387daa65f424f34cfc"
    ),
    "cp/middleware/soc/bk7258/hal/flash_ll.h": (
        "ea011023988c2f3aa80f8c78c5bb9f4f6e1ebcb4499c2288a26561ed79ab3e35"
    ),
    "cp/middleware/soc/bk7258/hal/sys_pm_hal.c": (
        "ed28f9fd42e345a9ac94469ab395095ebca802797d06b2853fdeee6840cf3dba"
    ),
    "cp/middleware/driver/flash/flash_partition.c": (
        "b47b36757a6f278ea06e0a69fdeefeada4e64a9f5614fd2d8f1b7e4083b38b8f"
    ),
    "projects/app_ab/partitions/bk7258/auto_partitions.csv": (
        "78b104c2b27e1b4fb450605c3e3a3c454c5325a3073d66b5795a5306d3595947"
    ),
    "cp/components/bk_libs/bk7258/bootloader/ab_bootloader/bootloader.bin": (
        "3b27958ef78cbb7e56b57695585008465c759a7671cfd776334fec49d3164047"
    ),
}

OFFICIAL_ENABLE_SLICE = bytes.fromhex(
    "70b50446064d07486e6e31463443fef7d3fe21460448fef7cffe6c6670bd00bf"
)
OFFICIAL_REMAP_SLICE = bytes.fromhex(
    "38b51448fff710fe04461348fff70cfe616b426b0546521a1048fef7c1fd2220"
    "636b590191fbf0f20d4902f100738b656a6b530193fbf0f202f10073cb656d6b"
    "646b2a1b530193fbf0f000f100750d6638bd00bfab3d0002b03d000252410002"
    "00000344"
)


class BootVerificationError(RuntimeError):
    """Raised when an N15-C artifact or selector invariant fails."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise BootVerificationError(message)


def record_crc(metadata: bytearray, index: int = 0) -> None:
    start = index * BOOT_METADATA_RECORD_SIZE
    struct.pack_into(
        "<I",
        metadata,
        start + META_CRC_OFFSET,
        zlib.crc32(metadata[start : start + META_CRC_OFFSET]) & 0xFFFFFFFF,
    )


def descriptor_crc(metadata: bytearray, index: int = 0) -> None:
    start = index * BOOT_METADATA_RECORD_SIZE + META_DESCRIPTOR_OFFSET
    struct.pack_into(
        "<I",
        metadata,
        start + STAGE_CRC_OFFSET,
        zlib.crc32(metadata[start : start + STAGE_CRC_OFFSET]) & 0xFFFFFFFF,
    )
    record_crc(metadata, index)


def compile_harness(repo: Path, output: Path) -> None:
    compiler = shutil.which("cc")
    pkg_config = shutil.which("pkg-config")
    if compiler is None or pkg_config is None:
        raise BootVerificationError("host cc/pkg-config is unavailable")
    openssl = subprocess.run(
        [pkg_config, "--cflags", "--libs", "openssl"],
        check=True,
        capture_output=True,
        text=True,
        timeout=10,
    ).stdout.split()
    boot = repo / "board/bk7258/bootloader"
    chip = repo / "board/bk7258/chip"
    harness = repo / "board/bk7258/scripts/host/bk7258_boot_ota_select_harness.c"
    subprocess.run(
        [
            compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            f"-I{boot}",
            f"-I{chip / 'cp'}",
            f"-I{chip / 'include'}",
            str(chip / "cp/bk7258_ota_staging_core.c"),
            str(boot / "boot_ota_select_core.c"),
            str(harness),
            *openssl,
            "-o",
            str(output),
        ],
        check=True,
        timeout=30,
    )


def run_harness(
    harness: Path,
    root: Path,
    name: str,
    metadata: bytes,
    primary: bytes,
    secondary: bytes,
    *,
    expect_error: bool,
    decision: int,
    reason: int,
    metadata_valid: bool,
    primary_full: bool,
    secondary_verified: bool,
    mode: str = "normal",
) -> str:
    case = root / name
    case.mkdir()
    metadata_path = case / BOOT_METADATA_FILE
    primary_path = case / "primary.bin"
    secondary_path = case / "secondary.bin"
    metadata_path.write_bytes(metadata)
    primary_path.write_bytes(primary)
    secondary_path.write_bytes(secondary)
    result = subprocess.run(
        [
            str(harness),
            str(metadata_path),
            str(primary_path),
            str(secondary_path),
            "error" if expect_error else "ok",
            str(decision),
            str(reason),
            "1" if metadata_valid else "0",
            "1" if primary_full else "0",
            "1" if secondary_verified else "0",
            mode,
        ],
        check=False,
        capture_output=True,
        text=True,
        timeout=60,
    )
    require(
        result.returncode == 0 and "BK7258 N15-C harness PASS" in result.stdout,
        f"host harness failed for {name}: {result.stdout}{result.stderr}",
    )
    return result.stdout.strip()


def source_contract(repo: Path) -> None:
    paths = {
        "core": repo / "board/bk7258/bootloader/boot_ota_select_core.c",
        "header": repo / "board/bk7258/bootloader/boot_ota_select_core.h",
        "packer": repo / "board/bk7258/scripts/pack_bk7258_ota_rotation.py",
        "adapter": repo / "board/bk7258/bootloader/boot_ota_select.c",
        "main": repo / "board/bk7258/bootloader/boot_main.c",
        "wdt": repo / "board/bk7258/bootloader/boot_wdt.h",
        "make": repo / "board/bk7258/bootloader/Makefile",
        "linker": repo / "board/bk7258/bootloader/bootloader.ld",
    }
    texts = {name: path.read_text(encoding="utf-8") for name, path in paths.items()}
    required = {
        "core": (
            'META_MAGIC                  "BKOTA15C"',
            "CP_PHYSICAL_SIZE            BK7258_ROLE_SLOT_A_CP_SIZE",
            "AP_PHYSICAL_SIZE            BK7258_ROLE_SLOT_A_AP_SIZE",
            "SECONDARY_START             BK7258_ROLE_SLOT_B_PAIR_OFFSET",
            "BK7258_FLASH_ERASE_SIZE",
            "validate_primary_component",
            "validate_secondary",
            "BK7258_BOOT_OTA_META_TRIAL_STARTED",
        ),
        "header": (
            "BK7258_BOOT_OTA_RECORD_SIZE    512u",
            "BK7258_BOOT_OTA_DECISION_B_TRIAL_CANDIDATE",
            "primary_full_verified",
        ),
        "packer": (
            'ROTATION_MAGIC = b"BKOTA15R"',
            "ROTATION_FORMAT = 2",
            'target_slot not in {"a", "b"}',
            '"metadata_publish_enabled": False',
            '"board_write_authorized": False',
        ),
        "adapter": (
            "BK7258_BOOT_OTA_SELECT_COMPILE_GATE 0u",
            "BK7258_BOOT_OTA_SELECT_RUNTIME_GATE 0u",
            "BK7258_BOOT_OTA_REMAP_COMPILE_GATE 0u",
            "BK7258_BOOT_OTA_REMAP_RUNTIME_GATE 0u",
            "FLASH_COMMAND_READ          5u",
            "REMAP_ADDRESS_BEGIN         BK7258_ROLE_SLOT_A_CP_XIP_START",
            "REMAP_ADDRESS_END           BK7258_ROLE_SLOT_A_AP_XIP_END",
            "BK7258_ROLE_SLOT_B_PAIR_OFFSET / BK7258_FLASH_CRC_TOTAL_SIZE",
            "bk7258_boot_ota_rotation_select_core(",
            "bk7258_boot_ota_rotation_trial_transition(",
            "boot_select_slot",
        ),
        "main": ("app_vec = boot_ota_select_app(app_vec);", "ota select"),
        "wdt": (
            "REG32(WDT_APB_CTRL) = ctrl1;",
            "REG32(WDT_AON_CTRL) = ctrl1;",
        ),
        "make": (
            "boot_ota_select_core.o",
            "boot_ota_rotation_core.o",
            "boot_ota_rotation_select_core.o",
            "boot_ota_rotation_trial_core.o",
            "boot_ota_staging_core.o",
            "boot_sha256.o",
        ),
        "linker": (
            ".boot_ota_workspace 0x2800D000 (NOLOAD)",
            "N15 boot OTA workspace exceeds 12 KiB",
            "bootloader exceeds its 64 KiB logical slot",
        ),
    }
    for name, fragments in required.items():
        for fragment in fragments:
            require(fragment in texts[name], f"N15-C source closure missing: {name}: {fragment}")

    require(
        texts["wdt"].count("REG32(WDT_AON_CTRL) = ctrl1;") == 2
        and texts["wdt"].count("REG32(WDT_AON_CTRL) = ctrl2;") == 2,
        "both boot WDT init and feed must re-arm AON_WDT",
    )

    require("set_write_enabled" not in texts["adapter"], "boot selector must have no gate setter")


def verify_official_contract(sdk_source: Path) -> dict[str, object]:
    require(
        sdk_source.name == "bk_avdk_smp-release-v3.1.1.9",
        "SDK source must be the exact v3.1.1.9 release",
    )
    verified: list[dict[str, str]] = []
    payloads: dict[str, bytes] = {}
    for relative, expected in OFFICIAL_CONTRACT_HASHES.items():
        payload = (sdk_source / relative).read_bytes()
        observed = hashlib.sha256(payload).hexdigest()
        require(observed == expected, f"official N15-C input hash drift: {relative}")
        payloads[relative] = payload
        verified.append({"path": relative, "sha256": observed})

    flash_driver = payloads["cp/middleware/driver/flash/flash_driver.c"].decode()
    flash_ll = payloads["cp/middleware/soc/bk7258/hal/flash_ll.h"].decode()
    sys_pm = payloads["cp/middleware/soc/bk7258/hal/sys_pm_hal.c"].decode()
    partitions = payloads[
        "projects/app_ab/partitions/bk7258/auto_partitions.csv"
    ].decode()
    for fragment in (
        "uint32_t addr = address & (~FLASH_ADDRESS_MASK);",
        "flash_hal_set_op_cmd_read(&s_flash.hal, addr);",
        "for (uint32_t i = 0; i < FLASH_BUFFER_LEN; i++)",
    ):
        require(fragment in flash_driver, f"official raw-read contract drift: {fragment}")
    for fragment in (
        "hw->op_cmd.op_type_sw = FLASH_OP_CMD_READ;",
        "hw->op_ctrl.op_sw = 1;",
        "return hw->data_flash_sw;",
    ):
        require(fragment in flash_ll, f"official Flash LL contract drift: {fragment}")
    for fragment in (
        "FLASH_OFFSET_ADDR_BEGIN*4",
        "FLASH_OFFSET_ADDR_END*4",
        "FLASH_ADDR_OFFSET*4",
        "FLASH_OFFSET_ENABLE*4",
    ):
        require(fragment in sys_pm, f"official remap register contract drift: {fragment}")
    for fragment in (
        "primary_cp_app,,1360k,code,TRUE,TRUE",
        "primary_ap_app,,1156k,code,TRUE,TRUE",
        "s_app,,2516k,data,TRUE,TRUE",
        "ota_fina_executive,,4K,data,TRUE,TRUE",
    ):
        require(fragment in partitions, f"official AB partition geometry drift: {fragment}")

    binary = payloads[
        "cp/components/bk_libs/bk7258/bootloader/ab_bootloader/bootloader.bin"
    ]
    require(binary[0x22D8 : 0x22D8 + len(OFFICIAL_ENABLE_SLICE)] == OFFICIAL_ENABLE_SLICE,
            "official remap-enable routine bytes drift")
    require(binary[0x24F0 : 0x24F0 + len(OFFICIAL_REMAP_SLICE)] == OFFICIAL_REMAP_SLICE,
            "official one-offset routine bytes drift")
    return {
        "release": "v3.1.1.9",
        "source_hashes": verified,
        "enable_function": "0x020022d8",
        "remap_function": "0x020024f0",
        "remap_registers": ["0x44030058", "0x4403005c", "0x44030060", "0x44030064"],
    }


def verify_boot_sha256(repo: Path, root: Path) -> int:
    compiler = shutil.which("cc")
    if compiler is None:
        raise BootVerificationError("host C compiler is unavailable")
    source = repo / "board/bk7258/bootloader/boot_sha256.c"
    library = root / "libbk7258-boot-sha256.so"
    subprocess.run(
        [
            compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fPIC",
            "-shared",
            str(source),
            "-o",
            str(library),
        ],
        check=True,
        timeout=30,
    )
    sha = ctypes.CDLL(str(library))
    sha.boot_sha256_init.argtypes = [ctypes.c_void_p]
    sha.boot_sha256_update.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]
    sha.boot_sha256_final.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    messages = (b"", b"abc", bytes(range(256)) * 1000, b"a" * 1000000)
    for message in messages:
        context = ctypes.create_string_buffer(256)
        digest = ctypes.create_string_buffer(32)
        sha.boot_sha256_init(context)
        offset = 0
        while offset < len(message):
            count = min(73, len(message) - offset)
            chunk = ctypes.create_string_buffer(message[offset : offset + count])
            sha.boot_sha256_update(context, chunk, count)
            offset += count
        if not message:
            empty = ctypes.create_string_buffer(1)
            sha.boot_sha256_update(context, empty, 0)
        sha.boot_sha256_final(context, digest)
        require(digest.raw == hashlib.sha256(message).digest(), "boot SHA-256 vector mismatch")
    return len(messages)


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


def verify_boot_elf(
    elf: Path,
    boot_bin: Path,
    boot_crc: Path,
    *,
    expected_gate_value: int = 0,
) -> dict[str, object]:
    nm = shutil.which("arm-none-eabi-nm")
    if nm is None:
        raise BootVerificationError("arm-none-eabi-nm is unavailable")
    symbols = parse_nm(nm, elf)
    required = {
        "boot_ota_select_app",
        "bk7258_boot_ota_rotation_select_core",
        "bk7258_boot_ota_rotation_trial_transition",
        "bk7258_boot_ota_rotation_select",
        "bk7258_boot_ota_validate_base_pair",
        "bk7258_boot_ota_validate_candidate_pair",
        "bk7258_ota_core_validate_at",
        "boot_sha256_init",
        "boot_sha256_update",
        "boot_sha256_final",
        "g_bk7258_boot_ota_select_compile_gate",
        "g_bk7258_boot_ota_select_runtime_gate",
        "g_bk7258_boot_ota_remap_compile_gate",
        "g_bk7258_boot_ota_remap_runtime_gate",
        "g_bk7258_boot_ota_trial_compile_gate",
        "g_bk7258_boot_ota_trial_runtime_gate",
        "g_boot_ota_metadata",
        "g_boot_ota_scratch",
    }
    missing = sorted(required - symbols.keys())
    require(not missing, f"N15-C boot ELF closure missing symbols: {missing}")
    forbidden = {
        "bk7258_ota_core_stage",
        "bk7258_ota_staging_stage",
        "bk7258_boot_ota_set_selection_enabled",
        "bk7258_boot_ota_set_remap_enabled",
    }
    present = sorted(forbidden & symbols.keys())
    require(not present, f"mutating/staging symbols leaked into boot ELF: {present}")
    undefined = subprocess.run(
        [nm, "-u", str(elf)],
        check=True,
        capture_output=True,
        text=True,
        timeout=30,
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
        f"boot physical image must be exactly 0x{BOOT_SIZE:x}",
    )
    decoded = decode(encoded)
    require(decoded[: len(logical)] == logical, "boot CRC image does not contain bl.bin")
    require(decoded[len(logical) :] == b"\xff" * (BOOT_LOGICAL_SIZE - len(logical)),
            "boot CRC image padding is not erased")

    gate_names = (
        "g_bk7258_boot_ota_select_compile_gate",
        "g_bk7258_boot_ota_select_runtime_gate",
        "g_bk7258_boot_ota_remap_compile_gate",
        "g_bk7258_boot_ota_remap_runtime_gate",
        "g_bk7258_boot_ota_trial_compile_gate",
        "g_bk7258_boot_ota_trial_runtime_gate",
    )
    gate_report: dict[str, int] = {}
    for name in gate_names:
        address, size, kind = symbols[name]
        require(size == 4 and kind.upper() == "R", f"{name} is not immutable Flash data")
        offset = address - 0x02000000
        require(0 <= offset <= len(logical) - 4, f"{name} is outside bl.bin")
        value = struct.unpack_from("<I", logical, offset)[0]
        require(
            value == expected_gate_value,
            f"{name} must be {expected_gate_value}, got {value}",
        )
        gate_report[name] = value

    require(symbols["g_boot_ota_scratch"][:2] == (0x2800D000, 0x2000),
            "boot selector scratch range drift")
    require(symbols["g_boot_ota_metadata"][:2] == (0x2800F000, 0x1000),
            "boot selector metadata range drift")
    return {
        "status": "pass",
        "elf": str(elf.resolve()),
        "elf_sha256": hashlib.sha256(elf.read_bytes()).hexdigest(),
        "logical_size": len(logical),
        "logical_sha256": hashlib.sha256(logical).hexdigest(),
        "physical_size": len(encoded),
        "physical_sha256": hashlib.sha256(encoded).hexdigest(),
        "required_symbols": sorted(required),
        "forbidden_symbols_present": present,
        "gates": gate_report,
        "expected_gate_value": expected_gate_value,
        "workspace": [0x2800D000, 0x28010000],
    }


def canonical_record_args(
    metadata: bytes,
) -> dict[str, object]:
    descriptor = metadata[
        META_DESCRIPTOR_OFFSET : META_DESCRIPTOR_OFFSET + STAGE_DESCRIPTOR_SIZE
    ]
    version = metadata[META_VERSION_OFFSET : META_VERSION_OFFSET + 24].split(b"\0", 1)[0]
    base_offset = META_VERSION_OFFSET + 24
    base_version = metadata[base_offset : base_offset + 24].split(b"\0", 1)[0]
    return {
        "generation": struct.unpack_from("<Q", metadata, META_GENERATION_OFFSET)[0],
        "timestamp": struct.unpack_from("<I", metadata, 32)[0],
        "cp_physical_length": struct.unpack_from("<I", metadata, META_CP_LENGTH_OFFSET)[0],
        "ap_physical_length": struct.unpack_from("<I", metadata, META_AP_LENGTH_OFFSET)[0],
        "version": version.decode("ascii"),
        "base_version": base_version.decode("ascii"),
        "primary_sha256": metadata[META_PRIMARY_SHA_OFFSET : META_PRIMARY_SHA_OFFSET + 32],
        "descriptor": descriptor,
    }


def metadata_for_states(pending: bytes, states: tuple[int, ...]) -> bytes:
    args = canonical_record_args(pending)
    metadata = b"\xff" * OTA_METADATA_SIZE
    for sequence, state in enumerate(states, 1):
        metadata = append_record(
            metadata,
            build_record(state=state, sequence=sequence, **args),
        )
    return metadata


def verify_metadata(
    bundle: Path,
    cp_crc: Path,
    ap_crc: Path,
    metadata_path: Path,
    *,
    generation: int,
    version: str,
    base_version: str,
    timestamp: int,
    sdk_source: Path | None,
    base_pair: Path | None = None,
    expected_target_slot: str | None = None,
) -> dict[str, object]:
    actual = metadata_path.read_bytes()
    if actual.startswith(ROTATION_MAGIC):
        state = struct.unpack_from("<I", actual, 12)[0]
        if state == PENDING_A:
            target_slot = "a"
        elif state == PENDING_B:
            target_slot = "b"
        else:
            raise BootVerificationError(
                "format-2 artifact must begin with a pending A/B record"
            )
        if expected_target_slot is not None:
            require(
                target_slot == expected_target_slot,
                "format-2 target slot differs from the caller-pinned slot",
            )
        expected, record, descriptor, report = build_rotation_bank(
            bundle,
            cp_crc,
            ap_crc,
            target_slot=target_slot,
            bank=0,
            generation=generation,
            version=version,
            base_version=base_version,
            timestamp=timestamp,
            sdk_source=sdk_source,
            base_pair_path=base_pair,
        )
        second, second_record, second_descriptor, second_report = (
            build_rotation_bank(
                bundle,
                cp_crc,
                ap_crc,
                target_slot=target_slot,
                bank=0,
                generation=generation,
                version=version,
                base_version=base_version,
                timestamp=timestamp,
                sdk_source=sdk_source,
                base_pair_path=base_pair,
            )
        )
        require(actual == expected, "format-2 boot metadata is non-canonical")
        require(expected == second, "format-2 deterministic rebuild drift")
        require(record == second_record, "format-2 record rebuild drift")
        require(descriptor == second_descriptor,
                "format-2 descriptor rebuild drift")
        require(report == second_report, "format-2 report rebuild drift")
        return report

    expected, primary, report = build_boot_metadata(
        bundle,
        cp_crc,
        ap_crc,
        generation=generation,
        version=version,
        base_version=base_version,
        timestamp=timestamp,
        sdk_source=sdk_source,
    )
    second, second_primary, second_report = build_boot_metadata(
        bundle,
        cp_crc,
        ap_crc,
        generation=generation,
        version=version,
        base_version=base_version,
        timestamp=timestamp,
        sdk_source=sdk_source,
    )
    require(actual == expected, "boot metadata is non-canonical")
    require(expected == second, "boot metadata deterministic rebuild drift")
    require(primary == second_primary, "primary pair deterministic rebuild drift")
    require(report == second_report, "boot metadata report deterministic rebuild drift")
    return report


def optional_official_contract(sdk_source: Path | None) -> dict[str, object]:
    """Verify the external exact SDK tree when the caller makes it available."""

    if sdk_source is None:
        return {
            "status": "not-run",
            "release": "v3.1.1.9",
            "reason": "--sdk-source not provided",
        }
    return verify_official_contract(sdk_source)


def self_test(repo: Path, sdk_source: Path | None) -> dict[str, object]:
    generation = 17
    version = "n15-c-test"
    base_version = "n15-b-test"
    timestamp = 0x12345678
    candidate_cp = synthetic_component("cp")
    candidate_ap = synthetic_component("ap")
    primary_cp = bytearray(candidate_cp)
    primary_ap = bytearray(candidate_ap)
    primary_cp[0x180] ^= 0x5A
    primary_ap[0x180] ^= 0xA5
    cp_encoded = expand(bytes(primary_cp))
    ap_encoded = expand(bytes(primary_ap))
    positive_count = 0
    negative_count = 0

    source_contract(repo)
    official_report = optional_official_contract(sdk_source)
    with tempfile.TemporaryDirectory(prefix="bk7258-n15c-") as directory:
        root = Path(directory)
        sha_vector_count = verify_boot_sha256(repo, root)
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
        cp_path.write_bytes(cp_encoded)
        ap_path.write_bytes(ap_encoded)
        pending, primary, report = build_boot_metadata(
            bundle,
            cp_path,
            ap_path,
            generation=generation,
            version=version,
            base_version=base_version,
            timestamp=timestamp,
            sdk_source=sdk_source,
        )
        pending2, primary2, report2 = build_boot_metadata(
            bundle,
            cp_path,
            ap_path,
            generation=generation,
            version=version,
            base_version=base_version,
            timestamp=timestamp,
            sdk_source=sdk_source,
        )
        require((pending, primary, report) == (pending2, primary2, report2), "metadata rebuild drift")
        secondary = files[S_APP_FILE]
        harness = root / "boot-select-harness"
        compile_harness(repo, harness)

        positive_cases = (
            (
                "erased",
                b"\xff" * OTA_METADATA_SIZE,
                DECISION_A_BASELINE,
                REASON_METADATA_ERASED,
                True,
                False,
                False,
            ),
            (
                "pending",
                pending,
                DECISION_B_TRIAL_CANDIDATE,
                REASON_PENDING_VALID,
                True,
                True,
                True,
            ),
            (
                "trial-started",
                metadata_for_states(pending, (META_PENDING_B, META_TRIAL_STARTED)),
                DECISION_A_ROLLBACK,
                REASON_TRIAL_CONSUMED,
                True,
                True,
                False,
            ),
            (
                "confirmed",
                metadata_for_states(
                    pending,
                    (META_PENDING_B, META_TRIAL_STARTED, META_CONFIRMED_B),
                ),
                DECISION_B_CONFIRMED,
                REASON_CONFIRMED_VALID,
                True,
                True,
                True,
            ),
            (
                "rollback",
                metadata_for_states(
                    pending,
                    (META_PENDING_B, META_TRIAL_STARTED, META_ROLLBACK_A),
                ),
                DECISION_A_ROLLBACK,
                REASON_ROLLBACK_REQUESTED,
                True,
                True,
                False,
            ),
        )
        for name, metadata, decision, reason, valid, full, secondary_ok in positive_cases:
            run_harness(
                harness,
                root,
                name,
                metadata,
                primary,
                secondary,
                expect_error=False,
                decision=decision,
                reason=reason,
                metadata_valid=valid,
                primary_full=full,
                secondary_verified=secondary_ok,
            )
            positive_count += 1

        def failsafe_metadata(name: str, payload: bytes, *, full: bool) -> None:
            nonlocal negative_count
            run_harness(
                harness,
                root,
                name,
                payload,
                primary,
                secondary,
                expect_error=False,
                decision=DECISION_A_FAILSAFE,
                reason=REASON_METADATA_INVALID,
                metadata_valid=False,
                primary_full=full,
                secondary_verified=False,
            )
            negative_count += 1

        corrupt = bytearray(pending)
        corrupt[20] ^= 1
        failsafe_metadata("metadata-crc", bytes(corrupt), full=False)

        torn = bytearray(b"\xff" * OTA_METADATA_SIZE)
        torn[:100] = pending[:100]
        failsafe_metadata("metadata-first-torn", bytes(torn), full=False)

        dirty_gap = bytearray(b"\xff" * OTA_METADATA_SIZE)
        dirty_gap[BOOT_METADATA_RECORD_SIZE] = 0
        failsafe_metadata("metadata-dirty-gap", bytes(dirty_gap), full=False)

        trailing_torn = bytearray(pending)
        trailing_torn[BOOT_METADATA_RECORD_SIZE] = 0
        failsafe_metadata("metadata-trailing-torn", bytes(trailing_torn), full=True)

        trial = bytearray(metadata_for_states(pending, (META_PENDING_B, META_TRIAL_STARTED)))
        struct.pack_into("<Q", trial, BOOT_METADATA_RECORD_SIZE + META_SEQUENCE_OFFSET, 3)
        record_crc(trial, 1)
        failsafe_metadata("metadata-sequence-gap", bytes(trial), full=True)

        generation_drift = bytearray(trial)
        struct.pack_into(
            "<Q", generation_drift, BOOT_METADATA_RECORD_SIZE + META_SEQUENCE_OFFSET, 2
        )
        struct.pack_into(
            "<Q", generation_drift, BOOT_METADATA_RECORD_SIZE + META_GENERATION_OFFSET, generation + 1
        )
        record_crc(generation_drift, 1)
        failsafe_metadata("metadata-generation-drift", bytes(generation_drift), full=True)

        confirmed_direct = bytearray(metadata_for_states(pending, (META_PENDING_B, META_TRIAL_STARTED)))
        struct.pack_into(
            "<I", confirmed_direct, BOOT_METADATA_RECORD_SIZE + META_STATE_OFFSET, META_CONFIRMED_B
        )
        record_crc(confirmed_direct, 1)
        failsafe_metadata("metadata-invalid-transition", bytes(confirmed_direct), full=True)

        duplicate = bytearray(metadata_for_states(pending, (META_PENDING_B, META_TRIAL_STARTED)))
        struct.pack_into(
            "<I", duplicate, BOOT_METADATA_RECORD_SIZE + META_STATE_OFFSET, META_PENDING_B
        )
        record_crc(duplicate, 1)
        failsafe_metadata("metadata-duplicate-pending", bytes(duplicate), full=True)

        for name, offset, value in (
            ("metadata-cp-length-misaligned", META_CP_LENGTH_OFFSET, len(cp_encoded) - 1),
            ("metadata-cp-length-high", META_CP_LENGTH_OFFSET, CP_A_SIZE + PACKET_TOTAL),
            ("metadata-ap-length-zero", META_AP_LENGTH_OFFSET, 0),
        ):
            invalid = bytearray(pending)
            struct.pack_into("<I", invalid, offset, value)
            record_crc(invalid)
            failsafe_metadata(name, bytes(invalid), full=False)

        address_drift = bytearray(pending)
        struct.pack_into(
            "<I",
            address_drift,
            META_DESCRIPTOR_OFFSET + STAGE_PHYSICAL_OFFSET,
            0x00285000,
        )
        descriptor_crc(address_drift)
        failsafe_metadata("descriptor-address-drift", bytes(address_drift), full=False)

        rbl_address_drift = bytearray(pending)
        struct.pack_into(
            "<I",
            rbl_address_drift,
            META_DESCRIPTOR_OFFSET + STAGE_RBL_PHYSICAL_OFFSET,
            0x00273F22,
        )
        descriptor_crc(rbl_address_drift)
        failsafe_metadata("descriptor-rbl-address-drift", bytes(rbl_address_drift), full=False)

        descriptor_generation = bytearray(pending)
        struct.pack_into(
            "<Q",
            descriptor_generation,
            META_DESCRIPTOR_OFFSET + STAGE_GENERATION_OFFSET,
            generation + 1,
        )
        descriptor_crc(descriptor_generation)
        failsafe_metadata("descriptor-generation-drift", bytes(descriptor_generation), full=False)

        version_drift = bytearray(pending)
        version_drift[META_VERSION_OFFSET : META_VERSION_OFFSET + 24] = b"stale\0".ljust(24, b"\0")
        record_crc(version_drift)
        failsafe_metadata("metadata-version-drift", bytes(version_drift), full=False)

        zero_digest = bytearray(pending)
        zero_digest[META_PRIMARY_SHA_OFFSET : META_PRIMARY_SHA_OFFSET + 32] = bytes(32)
        record_crc(zero_digest)
        failsafe_metadata("metadata-zero-primary-digest", bytes(zero_digest), full=False)

        def candidate_failsafe(name: str, candidate: bytes, mode: str = "normal") -> None:
            nonlocal negative_count
            run_harness(
                harness,
                root,
                name,
                pending,
                primary,
                candidate,
                expect_error=False,
                decision=DECISION_A_FAILSAFE,
                reason=REASON_CANDIDATE_INVALID,
                metadata_valid=True,
                primary_full=True,
                secondary_verified=False,
                mode=mode,
            )
            negative_count += 1

        candidate_crc = bytearray(secondary)
        candidate_crc[1] ^= 1
        candidate_failsafe("candidate-crc", bytes(candidate_crc))

        header_physical = (0x24F000 // PACKET_DATA) * PACKET_TOTAL
        candidate_rbl = bytearray(secondary)
        candidate_rbl[header_physical + 4] ^= 1
        block = candidate_rbl[header_physical : header_physical + PACKET_DATA]
        struct.pack_into(">H", candidate_rbl, header_physical + PACKET_DATA, crc16(block))
        candidate_failsafe("candidate-rbl", bytes(candidate_rbl))

        candidate_vector = bytearray(secondary)
        struct.pack_into("<I", candidate_vector, 4, 0x02000001)
        struct.pack_into(">H", candidate_vector, PACKET_DATA, crc16(candidate_vector[:PACKET_DATA]))
        candidate_failsafe("candidate-vector", bytes(candidate_vector))
        candidate_failsafe("candidate-read-error", secondary, mode="read-error-secondary")

        def primary_fatal(
            name: str,
            primary_payload: bytes,
            metadata_payload: bytes = pending,
            mode: str = "normal",
        ) -> None:
            nonlocal negative_count
            run_harness(
                harness,
                root,
                name,
                metadata_payload,
                primary_payload,
                secondary,
                expect_error=True,
                decision=DECISION_A_FAILSAFE,
                reason=REASON_METADATA_INVALID,
                metadata_valid=False,
                primary_full=False,
                secondary_verified=False,
                mode=mode,
            )
            negative_count += 1

        primary_crc = bytearray(primary)
        primary_crc[1] ^= 1
        primary_fatal("primary-crc", bytes(primary_crc))

        primary_cp_padding = bytearray(primary)
        primary_cp_padding[len(cp_encoded)] = 0
        primary_fatal("primary-cp-padding", bytes(primary_cp_padding))

        primary_ap_padding = bytearray(primary)
        primary_ap_padding[CP_A_SIZE + len(ap_encoded)] = 0
        primary_fatal("primary-ap-padding", bytes(primary_ap_padding))

        primary_sha = bytearray(pending)
        primary_sha[META_PRIMARY_SHA_OFFSET] ^= 1
        record_crc(primary_sha)
        primary_fatal("primary-sha", primary, bytes(primary_sha))

        primary_vector = bytearray(primary)
        struct.pack_into("<I", primary_vector, 4, 0x02000001)
        struct.pack_into(">H", primary_vector, PACKET_DATA, crc16(primary_vector[:PACKET_DATA]))
        vector_metadata = bytearray(pending)
        vector_metadata[META_PRIMARY_SHA_OFFSET : META_PRIMARY_SHA_OFFSET + 32] = hashlib.sha256(
            primary_vector
        ).digest()
        record_crc(vector_metadata)
        primary_fatal("primary-vector", bytes(primary_vector), bytes(vector_metadata))

        shorter = bytearray(pending)
        struct.pack_into("<I", shorter, META_CP_LENGTH_OFFSET, len(cp_encoded) - PACKET_TOTAL)
        record_crc(shorter)
        primary_fatal("primary-length-short", primary, bytes(shorter))
        primary_fatal("primary-read-error", primary, mode="read-error-primary")
        primary_fatal("primary-short-read", primary, mode="short-read")

    return {
        "format": 1,
        "active_format": 2,
        "status": "pass",
        "positive_cases": positive_count,
        "negative_cases": negative_count,
        "sha256_vectors": sha_vector_count,
        "source_contract": True,
        "official_contract": official_report,
        "exact_sdk_source": sdk_source is not None,
        "compile_selection_enabled": False,
        "runtime_selection_enabled": False,
        "compile_remap_enabled": False,
        "runtime_remap_enabled": False,
        "trial_metadata_mutation_enabled": False,
        "board_write_authorized": False,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", type=Path)
    parser.add_argument("--cp-crc", type=Path)
    parser.add_argument("--ap-crc", type=Path)
    parser.add_argument("--metadata", type=Path)
    parser.add_argument("--base-pair", type=Path)
    parser.add_argument("--expected-target-slot", choices=("a", "b"))
    parser.add_argument("--expected-generation", type=parse_int)
    parser.add_argument("--expected-version")
    parser.add_argument("--expected-base-version")
    parser.add_argument("--expected-timestamp", type=parse_int)
    parser.add_argument("--sdk-source", type=Path)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--boot-elf", type=Path)
    parser.add_argument("--boot-bin", type=Path)
    parser.add_argument("--boot-crc", type=Path)
    parser.add_argument("--elf-only", action="store_true")
    parser.add_argument("--expected-gate-value", type=int, choices=(0, 1), default=0)
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[3]

    try:
        boot_artifacts = (args.boot_elf, args.boot_bin, args.boot_crc)
        if args.elf_only:
            if any(path is None for path in boot_artifacts):
                parser.error("--elf-only requires --boot-elf/--boot-bin/--boot-crc")
            source_contract(repo)
            result = {
                "format": 2,
                "status": "pass",
                "exact_sdk_source": args.sdk_source is not None,
                "official_contract": optional_official_contract(args.sdk_source),
                "elf_verification": verify_boot_elf(
                    args.boot_elf,
                    args.boot_bin,
                    args.boot_crc,
                    expected_gate_value=args.expected_gate_value,
                ),
            }
        elif args.self_test:
            result = self_test(repo, args.sdk_source)
        else:
            required_paths = (args.bundle, args.cp_crc, args.ap_crc, args.metadata)
            if any(path is None for path in required_paths):
                parser.error("artifact verification requires --bundle/--cp-crc/--ap-crc/--metadata")
            if (
                args.expected_generation is None
                or args.expected_version is None
                or args.expected_base_version is None
                or args.expected_timestamp is None
            ):
                parser.error("artifact verification requires all --expected-* fields")
            result = verify_metadata(
                args.bundle,
                args.cp_crc,
                args.ap_crc,
                args.metadata,
                generation=args.expected_generation,
                version=args.expected_version,
                base_version=args.expected_base_version,
                timestamp=args.expected_timestamp,
                sdk_source=args.sdk_source,
                base_pair=args.base_pair,
                expected_target_slot=args.expected_target_slot,
            )
        if not args.elf_only and any(path is not None for path in boot_artifacts):
            if any(path is None for path in boot_artifacts):
                parser.error("boot ELF verification requires all three boot artifacts")
            result["elf_verification"] = verify_boot_elf(
                args.boot_elf,
                args.boot_bin,
                args.boot_crc,
                expected_gate_value=args.expected_gate_value,
            )
    except (
        OSError,
        KeyError,
        TypeError,
        ValueError,
        subprocess.SubprocessError,
        BootMetadataError,
        BootVerificationError,
    ) as error:
        print(f"BK7258 N15-C boot verification FAIL: {error}")
        return 1

    encoded = json.dumps(result, indent=2, sort_keys=True)
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded + "\n", encoding="utf-8")
    if args.json:
        print(encoded)
    else:
        print(
            "BK7258 N15-C boot verification PASS: "
            f"positive={result.get('positive_cases', 1)} "
            f"negative={result.get('negative_cases', 0)} "
            f"expected_gate_value={args.expected_gate_value}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
