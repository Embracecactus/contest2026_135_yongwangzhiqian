#!/usr/bin/env python3
"""Verify N15-B descriptors and run portable staging fault injection."""

from __future__ import annotations

import argparse
import json
import shutil
import struct
import subprocess
import tempfile
import zlib
from pathlib import Path

from bk7258_crc_expand import PACKET_DATA, PACKET_TOTAL, crc16
from pack_bk7258_ota_pair import (
    S_APP_FILE,
    build_bundle,
    parse_int,
    write_bundle,
)
from pack_bk7258_ota_stage import (
    STAGE_DESCRIPTOR_SIZE,
    STAGE_HEADER,
    build_stage_descriptor,
)
from verify_bk7258_ota_pair import synthetic_component


PHYSICAL_OFFSET_FIELD = 108
PHYSICAL_SIZE_FIELD = 112
GENERATION_FIELD = 96
VERSION_FIELD = 160
HEADER_CRC_FIELD = 380


class StageVerificationError(RuntimeError):
    """Raised when a descriptor or staging test violates N15-B."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise StageVerificationError(message)


def recalculate_descriptor_crc(descriptor: bytearray) -> None:
    struct.pack_into(
        "<I", descriptor, HEADER_CRC_FIELD, zlib.crc32(descriptor[:-4]) & 0xFFFFFFFF
    )


def verify_descriptor(
    bundle: Path,
    descriptor_path: Path,
    *,
    expected_generation: int,
    expected_version: str,
    expected_base_version: str,
    expected_timestamp: int,
    sdk_source: Path | None,
) -> dict[str, object]:
    actual = descriptor_path.read_bytes()
    require(len(actual) == STAGE_DESCRIPTOR_SIZE, "stage descriptor size drift")
    expected, report = build_stage_descriptor(
        bundle,
        expected_generation=expected_generation,
        expected_version=expected_version,
        expected_base_version=expected_base_version,
        expected_timestamp=expected_timestamp,
        sdk_source=sdk_source,
    )
    require(actual == expected, "stage descriptor is non-canonical")
    require(
        zlib.crc32(actual[:-4]) & 0xFFFFFFFF
        == struct.unpack_from("<I", actual, HEADER_CRC_FIELD)[0],
        "stage descriptor CRC32 mismatch",
    )
    require(len(STAGE_HEADER.unpack(actual)) == 30, "stage descriptor field count drift")
    return {
        "format": 1,
        "status": "pass",
        "descriptor": str(descriptor_path.resolve()),
        "descriptor_size": len(actual),
        "generation": report["generation"],
        "version": report["version"],
        "physical_offset": report["physical_offset"],
        "physical_size": report["physical_size"],
        "compile_write_enabled": False,
        "runtime_write_enabled": False,
        "remap_enabled": False,
        "trial_metadata_mutation_enabled": False,
        "board_write_authorized": False,
    }


def compile_harness(repo: Path, output: Path) -> None:
    compiler = shutil.which("cc")
    if compiler is None:
        raise StageVerificationError("host C compiler 'cc' is unavailable")
    pkg_config = shutil.which("pkg-config")
    if pkg_config is None:
        raise StageVerificationError("pkg-config is unavailable")
    openssl = subprocess.run(
        [pkg_config, "--cflags", "--libs", "openssl"],
        check=True,
        capture_output=True,
        text=True,
        timeout=10,
    ).stdout.split()
    chip = repo / "board/bk7258/chip"
    harness = repo / "board/bk7258/scripts/host/bk7258_ota_staging_harness.c"
    command = [
        compiler,
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        f"-I{chip / 'cp'}",
        f"-I{chip / 'include'}",
        str(chip / "cp/bk7258_ota_staging_core.c"),
        str(harness),
        *openssl,
        "-o",
        str(output),
    ]
    subprocess.run(command, check=True, timeout=30)


def run_harness(
    harness: Path,
    descriptor: Path,
    image: Path,
    *,
    generation: int,
    version: str,
    base_version: str,
    timestamp: int,
    mode: str,
) -> str:
    result = subprocess.run(
        [
            str(harness),
            str(descriptor),
            str(image),
            str(generation),
            version,
            base_version,
            str(timestamp),
            mode,
        ],
        check=False,
        capture_output=True,
        text=True,
        timeout=60,
    )
    require(
        result.returncode == 0 and "BK7258 N15-B harness PASS" in result.stdout,
        f"host harness failed for {mode}: {result.stdout}{result.stderr}",
    )
    return result.stdout.strip()


def source_contract(repo: Path) -> None:
    paths = {
        "core": repo / "board/bk7258/chip/cp/bk7258_ota_staging_core.c",
        "adapter": repo / "board/bk7258/chip/cp/bk7258_ota_staging.c",
        "guard": repo / "board/bk7258/chip/cp/bk7258_flash_guard.c",
        "kconfig": repo / "board/bk7258/chip/Kconfig",
        "make": repo / "board/bk7258/chip/Make.defs",
        "cmake": repo / "board/bk7258/chip/CMakeLists.txt",
        "defconfig": repo / "board/bk7258/configs/cp_nsh_psram/defconfig",
    }
    texts = {name: path.read_text(encoding="utf-8") for name, path in paths.items()}
    required = {
        "core": (
            "BK7258_OTA_PHYSICAL_START          BK7258_ROLE_SLOT_B_PAIR_OFFSET",
            "BK7258_OTA_PHYSICAL_SIZE           BK7258_ROLE_SLOT_B_PAIR_SIZE",
            "BK7258_OTA_STAGE_FINAL_DIGEST",
            "crc16_packet(packet)",
            "validate_rbl_header",
            "flash_ops->runtime_write_enabled",
            "bk7258_ota_core_stage_inactive",
        ),
        "adapter": (
            "CONFIG_BK7258_OTA_STAGING_WRITE",
            "bk7258_flash_guard_lock(BK7258_FLASH_GUARD_OTA_STAGING",
            "sha256init",
            "g_bk7258_ota_staging_active",
            "__atomic_compare_exchange_n",
            "g_bk7258_ota_staging_link_closure",
            "bk7258_ota_active_start",
            "bk7258_ota_staging_stage_inactive",
        ),
        "guard": (
            "BK7258_FLASH_REMAP_ENABLE",
            "BK7258_ROLE_SLOT_A_CP_OFFSET",
            "BK7258_ROLE_SLOT_B_PAIR_OFFSET",
            "CONFIG_BK7258_OTA_STAGING_WRITE",
        ),
        "kconfig": ("config BK7258_OTA_STAGING", "config BK7258_OTA_STAGING_WRITE"),
        "make": ("bk7258_ota_staging_core.c", "bk7258_ota_staging.c"),
        "cmake": ("cp/bk7258_ota_staging_core.c", "cp/bk7258_ota_staging.c"),
        "defconfig": ("CONFIG_BK7258_OTA_STAGING=y",),
    }
    for name, fragments in required.items():
        for fragment in fragments:
            require(fragment in texts[name], f"N15-B source closure missing: {name}: {fragment}")
    require(
        "CONFIG_BK7258_OTA_STAGING_WRITE=y" not in texts["defconfig"],
        "N15-B compile write gate must remain zero",
    )
    mtd = (repo / "board/bk7258/chip/cp/bk7258_flash_mtd.c").read_text(
        encoding="utf-8"
    )
    require(
        "BK7258_FLASH_GUARD_DATA, write, 0" in mtd,
        "read-only MTD transactions must not acquire SDK write permission",
    )


def verify_elf(
    elf: Path, config: Path, *, validation_profile: bool = False
) -> dict[str, object]:
    nm = shutil.which("arm-none-eabi-nm")
    if nm is None:
        raise StageVerificationError("arm-none-eabi-nm is unavailable")
    symbols = subprocess.run(
        [nm, "--defined-only", str(elf)],
        check=True,
        capture_output=True,
        text=True,
        timeout=30,
    ).stdout
    names = {line.split()[-1] for line in symbols.splitlines() if line.split()}
    required = {
        "__wrap_bk_flash_partition_write_perm_check_by_addr",
        "bk7258_flash_guard_lock",
        "bk7258_flash_guard_unlock",
        "bk7258_ota_core_stage_inactive",
        "bk7258_ota_core_validate",
        "bk7258_ota_core_validate_at",
        "bk7258_ota_staging_initialize",
        "bk7258_ota_staging_stage",
        "bk7258_ota_staging_stage_inactive",
        "bk7258_ota_staging_validate",
        "bk7258_ota_staging_validate_slot",
        "bk7258_ota_staging_write_enabled",
        "sha256final",
        "sha256init",
        "sha256update",
    }
    missing = sorted(required - names)
    require(not missing, f"N15-B CP ELF closure missing symbols: {missing}")
    setter_present = "bk7258_ota_staging_set_write_enabled" in names
    require(
        setter_present == validation_profile,
        "staging write-enable setter/profile mismatch",
    )
    config_text = config.read_text(encoding="utf-8")
    require("CONFIG_BK7258_OTA_STAGING=y" in config_text, "staging config is off")
    require(
        ("CONFIG_BK7258_OTA_STAGING_WRITE=y" in config_text)
        == validation_profile,
        "staging compile write gate/profile mismatch",
    )
    if validation_profile:
        require(
            "CONFIG_BK7258_OTA_VALIDATION=y" in config_text,
            "validation command config is absent",
        )
    return {
        "status": "pass",
        "elf": str(elf.resolve()),
        "config": str(config.resolve()),
        "required_symbols": sorted(required),
        "validation_profile": validation_profile,
        "compile_write_enabled": validation_profile,
        "runtime_enable_setter_present": setter_present,
        "runtime_write_initial": False,
    }


def self_test(repo: Path, sdk_source: Path | None) -> dict[str, object]:
    generation = 16
    version = "n15-b-test"
    base_version = "n15-a-test"
    timestamp = 0x12345678
    cp = synthetic_component("cp")
    ap = synthetic_component("ap")
    positive_count = 0
    negative_count = 0

    source_contract(repo)
    with tempfile.TemporaryDirectory(prefix="bk7258-n15b-") as directory:
        root = Path(directory)
        bundle = root / "bundle"
        files, _ = build_bundle(
            cp,
            ap,
            generation=generation,
            version=version,
            base_version=base_version,
            timestamp=timestamp,
        )
        write_bundle(bundle, files)
        descriptor_bytes, _ = build_stage_descriptor(
            bundle,
            expected_generation=generation,
            expected_version=version,
            expected_base_version=base_version,
            expected_timestamp=timestamp,
            sdk_source=sdk_source,
        )
        second_descriptor, _ = build_stage_descriptor(
            bundle,
            expected_generation=generation,
            expected_version=version,
            expected_base_version=base_version,
            expected_timestamp=timestamp,
            sdk_source=sdk_source,
        )
        require(descriptor_bytes == second_descriptor, "descriptor rebuild drift")
        descriptor = root / "descriptor.bin"
        descriptor.write_bytes(descriptor_bytes)
        descriptor_a_bytes, _ = build_stage_descriptor(
            bundle,
            expected_generation=generation,
            expected_version=version,
            expected_base_version=base_version,
            expected_timestamp=timestamp,
            sdk_source=sdk_source,
            target_slot="a",
        )
        descriptor_a = root / "descriptor-a.bin"
        descriptor_a.write_bytes(descriptor_a_bytes)
        harness = root / "staging-harness"
        compile_harness(repo, harness)

        for mode in ("validate", "success"):
            run_harness(
                harness,
                descriptor,
                bundle / S_APP_FILE,
                generation=generation,
                version=version,
                base_version=base_version,
                timestamp=timestamp,
                mode=mode,
            )
            positive_count += 1

        for mode in ("validate-a", "success-a"):
            run_harness(
                harness,
                descriptor_a,
                bundle / S_APP_FILE,
                generation=generation,
                version=version,
                base_version=base_version,
                timestamp=timestamp,
                mode=mode,
            )
            positive_count += 1

        for mode, slot_descriptor in (
            ("active-reject-a", descriptor_a),
            ("active-reject-b", descriptor),
        ):
            run_harness(
                harness,
                slot_descriptor,
                bundle / S_APP_FILE,
                generation=generation,
                version=version,
                base_version=base_version,
                timestamp=timestamp,
                mode=mode,
            )
            negative_count += 1

        fault_modes = (
            "compile-gate",
            "runtime-gate",
            "timeout",
            "lock-timeout",
            "erase-fail",
            "erase-verify",
            "write-fail",
            "readback",
            "final-digest",
            "source-short",
            "source-swap",
        )
        for mode in fault_modes:
            run_harness(
                harness,
                descriptor,
                bundle / S_APP_FILE,
                generation=generation,
                version=version,
                base_version=base_version,
                timestamp=timestamp,
                mode=mode,
            )
            negative_count += 1

        descriptor_cases: list[tuple[str, bytearray]] = []
        corrupt_crc = bytearray(descriptor_bytes)
        corrupt_crc[20] ^= 1
        descriptor_cases.append(("descriptor-crc", corrupt_crc))
        for name, field, value in (
            ("start-low", PHYSICAL_OFFSET_FIELD, 0x00285000),
            ("start-high", PHYSICAL_OFFSET_FIELD, 0x00287000),
            ("size-short", PHYSICAL_SIZE_FIELD, 0x00274000),
            ("size-long", PHYSICAL_SIZE_FIELD, 0x00276000),
        ):
            payload = bytearray(descriptor_bytes)
            struct.pack_into("<I", payload, field, value)
            recalculate_descriptor_crc(payload)
            descriptor_cases.append((name, payload))
        generation_bad = bytearray(descriptor_bytes)
        struct.pack_into("<Q", generation_bad, GENERATION_FIELD, generation + 1)
        recalculate_descriptor_crc(generation_bad)
        descriptor_cases.append(("generation", generation_bad))
        version_bad = bytearray(descriptor_bytes)
        version_bad[VERSION_FIELD : VERSION_FIELD + 24] = b"other\0".ljust(24, b"\0")
        recalculate_descriptor_crc(version_bad)
        descriptor_cases.append(("version", version_bad))

        for name, payload in descriptor_cases:
            path = root / f"{name}.bin"
            path.write_bytes(payload)
            run_harness(
                harness,
                path,
                bundle / S_APP_FILE,
                generation=generation,
                version=version,
                base_version=base_version,
                timestamp=timestamp,
                mode="validate-fail",
            )
            negative_count += 1

        run_harness(
            harness,
            descriptor,
            bundle / S_APP_FILE,
            generation=generation + 1,
            version=version,
            base_version=base_version,
            timestamp=timestamp,
            mode="validate-fail",
        )
        negative_count += 1

        packet_bad = bytearray((bundle / S_APP_FILE).read_bytes())
        packet_bad[1] ^= 1
        packet_bad_path = root / "packet-bad.bin"
        packet_bad_path.write_bytes(packet_bad)
        run_harness(
            harness,
            descriptor,
            packet_bad_path,
            generation=generation,
            version=version,
            base_version=base_version,
            timestamp=timestamp,
            mode="validate-fail",
        )
        negative_count += 1

        header_bad = bytearray((bundle / S_APP_FILE).read_bytes())
        logical_header_offset = 0x24F000
        physical_header_offset = (logical_header_offset // PACKET_DATA) * PACKET_TOTAL
        header_bad[physical_header_offset + 4] ^= 1
        block = header_bad[
            physical_header_offset : physical_header_offset + PACKET_DATA
        ]
        struct.pack_into(
            ">H", header_bad, physical_header_offset + PACKET_DATA, crc16(block)
        )
        header_bad_path = root / "header-bad.bin"
        header_bad_path.write_bytes(header_bad)
        run_harness(
            harness,
            descriptor,
            header_bad_path,
            generation=generation,
            version=version,
            base_version=base_version,
            timestamp=timestamp,
            mode="validate-fail",
        )
        negative_count += 1

    return {
        "format": 1,
        "status": "pass",
        "positive_cases": positive_count,
        "negative_cases": negative_count,
        "source_contract": True,
        "exact_sdk_source": sdk_source is not None,
        "compile_write_enabled": False,
        "runtime_write_enabled": False,
        "board_write_authorized": False,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", type=Path)
    parser.add_argument("--descriptor", type=Path)
    parser.add_argument("--expected-generation", type=parse_int)
    parser.add_argument("--expected-version")
    parser.add_argument("--expected-base-version")
    parser.add_argument("--expected-timestamp", type=parse_int)
    parser.add_argument("--sdk-source", type=Path)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--elf", type=Path)
    parser.add_argument("--config", type=Path)
    parser.add_argument("--elf-only", action="store_true")
    parser.add_argument("--validation-profile", action="store_true")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[3]

    try:
        if args.elf_only:
            if args.elf is None or args.config is None:
                parser.error("--elf-only requires --elf and --config")
            source_contract(repo)
            result = verify_elf(
                args.elf, args.config, validation_profile=args.validation_profile
            )
        elif args.self_test:
            result = self_test(repo, args.sdk_source)
        else:
            if args.bundle is None or args.descriptor is None:
                parser.error("--bundle and --descriptor are required")
            if (
                args.expected_generation is None
                or args.expected_version is None
                or args.expected_base_version is None
                or args.expected_timestamp is None
            ):
                parser.error("descriptor verification requires all --expected-* fields")
            result = verify_descriptor(
                args.bundle,
                args.descriptor,
                expected_generation=args.expected_generation,
                expected_version=args.expected_version,
                expected_base_version=args.expected_base_version,
                expected_timestamp=args.expected_timestamp,
                sdk_source=args.sdk_source,
            )
        if not args.elf_only and args.elf is not None:
            if args.config is None:
                parser.error("--elf requires --config")
            result["elf_verification"] = verify_elf(
                args.elf,
                args.config,
                validation_profile=args.validation_profile,
            )
    except (
        OSError,
        KeyError,
        TypeError,
        ValueError,
        subprocess.SubprocessError,
        StageVerificationError,
    ) as error:
        print(f"BK7258 N15-B staging verification FAIL: {error}")
        return 1

    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        print(
            "BK7258 N15-B staging verification PASS: "
            f"positive={result.get('positive_cases', 1)} "
            f"negative={result.get('negative_cases', 0)} "
            f"validation_profile={str(args.validation_profile).lower()} "
            "runtime_initial=false"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
