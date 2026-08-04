#!/usr/bin/env python3
"""Verify N15-E pending publication and metadata-sector reclamation."""

from __future__ import annotations

import argparse
import errno
import json
import shutil
import struct
import subprocess
import tempfile
import zlib
from pathlib import Path

from bk7258_ab_layout import OTA_METADATA_SIZE
from bk7258_crc_expand import expand
from pack_bk7258_ota_metadata import (
    BOOT_METADATA_RECORD_SIZE,
    META_CONFIRMED_B,
    META_PENDING_B,
    META_ROLLBACK_A,
    META_TRIAL_STARTED,
    build_boot_metadata,
)
from pack_bk7258_ota_pair import S_APP_FILE, build_bundle, write_bundle
from verify_bk7258_ota_pair import synthetic_component
from verify_bk7258_ota_trial import metadata_for_states, official_contract


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

PROGRAM_CHUNKS = BOOT_METADATA_RECORD_SIZE // 32


class PublishVerificationError(RuntimeError):
    """Raised when an N15-E publication invariant fails."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise PublishVerificationError(message)


def compile_harness(repo: Path, output: Path, *, analyzer: bool = False) -> None:
    compiler = shutil.which("cc")
    pkg_config = shutil.which("pkg-config")
    if compiler is None or pkg_config is None:
        raise PublishVerificationError("host cc/pkg-config is unavailable")
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
        str(board / "bootloader/boot_ota_publish_core.c"),
        str(board / "scripts/host/bk7258_boot_ota_publish_harness.c"),
        *openssl,
    ]
    if analyzer:
        command.extend(["-fanalyzer", "-fsyntax-only"])
    else:
        command.extend(["-o", str(output)])
    subprocess.run(command, check=True, timeout=90)


def source_contract(repo: Path) -> None:
    board = repo / "board/bk7258_t5ai"
    paths = {
        "header": board / "bootloader/boot_ota_publish_core.h",
        "core": board / "bootloader/boot_ota_publish_core.c",
        "harness": board / "scripts/host/bk7258_boot_ota_publish_harness.c",
        "adapter": board / "chip/cp/bk7258_ota_trial.c",
        "public": board / "chip/include/bk7258_ota_trial.h",
        "guard": board / "chip/cp/bk7258_flash_guard.c",
        "kconfig": board / "chip/Kconfig",
        "make": board / "chip/Make.defs",
        "cmake": board / "chip/CMakeLists.txt",
        "config": board / "configs/cp_nsh_psram/defconfig",
    }
    text = {name: path.read_text(encoding="utf-8") for name, path in paths.items()}
    for token in (
        "bk7258_boot_ota_publish_pending",
        "BK7258_BOOT_OTA_PUBLISH_WORKSPACE_SIZE",
        "primary_mapping_active",
        "BK7258_BOOT_OTA_META_CONFIRMED_B",
        "expected_generation <= previous_info.generation",
        "ops->erase_sector",
        "bytes_erased(current_metadata",
        "BK7258_BOOT_OTA_PROGRAM_GRANULE",
        "BK7258_BOOT_OTA_DECISION_B_TRIAL_CANDIDATE",
        "select_result.primary_full_verified",
        "select_result.secondary_verified",
    ):
        require(token in text["core"] or token in text["header"], f"core contract missing {token}")
    for forbidden in ("<nuttx/", "<driver/", "malloc(", "free(", "printf(", "memcpy(", "memset("):
        require(forbidden not in text["core"], f"portable publisher uses forbidden dependency {forbidden}")
    for token in (
        "bk7258_ota_publish_pending",
        "bk7258_ota_publish_primary",
        "bk7258_flash_guard_lock(BK7258_FLASH_GUARD_OTA_METADATA",
        "bk_flash_erase_sector(address)",
        "BK7258_BOOT_OTA_PUBLISH_WORKSPACE_SIZE",
    ):
        require(token in text["adapter"], f"CP adapter contract missing {token}")
    require("boot_ota_publish_core.c" in text["make"], "Make.defs omits publication core")
    require("boot_ota_publish_core.c" in text["cmake"], "CMake omits publication core")
    require("CONFIG_BK7258_OTA_TRIAL=y" in text["config"], "read-only N15-E closure is absent")
    require("CONFIG_BK7258_OTA_TRIAL_WRITE=y" not in text["config"], "metadata write gate must remain off")
    require("CONFIG_BK7258_OTA_STAGING_WRITE=y" not in text["config"], "staging write gate must remain off")
    require("BK7258_AB_METADATA_START" in text["guard"], "metadata guard range is absent")


def run_case(
    harness: Path,
    root: Path,
    name: str,
    current: bytes,
    pending_record: bytes,
    primary_path: Path,
    secondary_path: Path,
    *,
    generation: int,
    mode: str,
    chunk: int,
    status: int,
    decision: int,
    reason: int,
    reclaimed: bool,
    idempotent: bool = False,
) -> bytes:
    current_path = root / f"{name}-current.bin"
    record_path = root / f"{name}-record.bin"
    output_path = root / f"{name}-output.bin"
    current_path.write_bytes(current)
    record_path.write_bytes(pending_record)
    result = subprocess.run(
        [
            str(harness),
            str(current_path),
            str(record_path),
            str(primary_path),
            str(secondary_path),
            str(generation),
            mode,
            str(chunk),
            str(status),
            str(decision),
            str(reason),
            "1" if reclaimed else "0",
            "1" if idempotent else "0",
            str(output_path),
        ],
        check=False,
        capture_output=True,
        text=True,
        timeout=90,
    )
    require(
        result.returncode == 0 and "BK7258 N15-E harness PASS" in result.stdout,
        f"host harness failed for {name}: {result.stdout}{result.stderr}",
    )
    return output_path.read_bytes()


def state_decision(state: int) -> tuple[int, int]:
    if state == META_PENDING_B:
        return DECISION_B_TRIAL_CANDIDATE, REASON_PENDING_VALID
    if state == META_TRIAL_STARTED:
        return DECISION_A_ROLLBACK, REASON_TRIAL_CONSUMED
    if state == META_CONFIRMED_B:
        return DECISION_B_CONFIRMED, REASON_CONFIRMED_VALID
    if state == META_ROLLBACK_A:
        return DECISION_A_ROLLBACK, REASON_ROLLBACK_REQUESTED
    raise PublishVerificationError(f"unknown state {state}")


def make_pending(
    root: Path,
    label: str,
    candidate_cp: bytes,
    candidate_ap: bytes,
    primary_cp_path: Path,
    primary_ap_path: Path,
    *,
    generation: int,
    version: str,
    base_version: str,
    timestamp: int,
    sdk_source: Path | None,
) -> tuple[bytes, bytes]:
    bundle = root / f"bundle-{label}"
    files, _ = build_bundle(
        candidate_cp,
        candidate_ap,
        generation=generation,
        version=version,
        base_version=base_version,
        timestamp=timestamp,
    )
    write_bundle(bundle, files)
    pending, _, _ = build_boot_metadata(
        bundle,
        primary_cp_path,
        primary_ap_path,
        generation=generation,
        version=version,
        base_version=base_version,
        timestamp=timestamp,
        sdk_source=sdk_source,
    )
    return pending, files[S_APP_FILE]


def self_test(repo: Path, sdk_source: Path | None) -> dict[str, object]:
    generation = 32
    prior_generation = generation - 1
    version = "n15-e-test"
    base_version = "n15-base"
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
    erase_boundaries = 0

    source_contract(repo)
    official = official_contract(sdk_source)
    with tempfile.TemporaryDirectory(prefix="bk7258-n15e-") as directory:
        root = Path(directory)
        harness = root / "publish-harness"
        compile_harness(repo, harness)
        compile_harness(repo, root / "unused", analyzer=True)
        cp_path = root / "primary-cp.bin"
        ap_path = root / "primary-ap.bin"
        cp_path.write_bytes(expand(bytes(primary_cp)))
        ap_path.write_bytes(expand(bytes(primary_ap)))
        pending, secondary = make_pending(
            root,
            "new",
            candidate_cp,
            candidate_ap,
            cp_path,
            ap_path,
            generation=generation,
            version=version,
            base_version=base_version,
            timestamp=timestamp,
            sdk_source=sdk_source,
        )
        old_pending, old_secondary = make_pending(
            root,
            "old",
            candidate_cp,
            candidate_ap,
            cp_path,
            ap_path,
            generation=prior_generation,
            version=version,
            base_version=base_version,
            timestamp=timestamp,
            sdk_source=sdk_source,
        )
        require(secondary == old_secondary, "generation changed candidate bytes")
        primary_path = root / "primary.bin"
        secondary_path = root / "secondary.bin"
        primary_path.write_bytes(
            build_boot_metadata(
                root / "bundle-new",
                cp_path,
                ap_path,
                generation=generation,
                version=version,
                base_version=base_version,
                timestamp=timestamp,
                sdk_source=sdk_source,
            )[1]
        )
        secondary_path.write_bytes(secondary)
        pending_record = pending[:BOOT_METADATA_RECORD_SIZE]
        erased = b"\xff" * OTA_METADATA_SIZE
        old_trial = metadata_for_states(
            old_pending, (META_PENDING_B, META_TRIAL_STARTED)
        )
        old_confirmed = metadata_for_states(
            old_pending,
            (META_PENDING_B, META_TRIAL_STARTED, META_CONFIRMED_B),
        )
        old_rollback = metadata_for_states(
            old_pending,
            (META_PENDING_B, META_TRIAL_STARTED, META_ROLLBACK_A),
        )
        invalid = bytearray(old_trial)
        invalid[BOOT_METADATA_RECORD_SIZE + 9] ^= 1

        positive_cases = (
            ("erased", erased, False, False),
            ("trial-reclaim", old_trial, True, False),
            ("rollback-reclaim", old_rollback, True, False),
            ("invalid-recover", bytes(invalid), True, False),
            ("idempotent", pending, False, True),
        )
        for name, current, reclaimed, idempotent in positive_cases:
            observed = run_case(
                harness,
                root,
                name,
                current,
                pending_record,
                primary_path,
                secondary_path,
                generation=generation,
                mode="normal",
                chunk=-1,
                status=0,
                decision=DECISION_B_TRIAL_CANDIDATE,
                reason=REASON_PENDING_VALID,
                reclaimed=reclaimed,
                idempotent=idempotent,
            )
            require(observed == pending, f"{name} did not commit canonical pending metadata")
            positive += 1

        unchanged = (
            ("compile-gate", erased, "compile-disabled", -errno.EACCES, DECISION_A_BASELINE, REASON_METADATA_ERASED),
            ("runtime-gate", erased, "runtime-disabled", -errno.EACCES, DECISION_A_BASELINE, REASON_METADATA_ERASED),
            ("remap-active", erased, "remap-active", -errno.EPERM, DECISION_A_BASELINE, REASON_METADATA_ERASED),
            ("lock-error", erased, "lock-error", -errno.ETIMEDOUT, DECISION_A_BASELINE, REASON_METADATA_ERASED),
            ("lock-short", erased, "lock-short", -errno.EIO, DECISION_A_BASELINE, REASON_METADATA_ERASED),
            ("initial-read-error", erased, "initial-read-error", -errno.ETIMEDOUT, DECISION_A_BASELINE, REASON_METADATA_ERASED),
            ("initial-read-short", erased, "initial-read-short", -errno.EIO, DECISION_A_BASELINE, REASON_METADATA_ERASED),
            ("raw-read-error", erased, "raw-read-error", -errno.ETIMEDOUT, DECISION_A_BASELINE, REASON_METADATA_ERASED),
            ("raw-read-short", erased, "raw-read-short", -errno.EIO, DECISION_A_BASELINE, REASON_METADATA_ERASED),
            ("existing-pending", old_pending, "normal", -errno.EALREADY, DECISION_B_TRIAL_CANDIDATE, REASON_PENDING_VALID),
            ("existing-confirmed", old_confirmed, "normal", -errno.EBUSY, DECISION_B_CONFIRMED, REASON_CONFIRMED_VALID),
            ("stale-trial", old_trial, "normal", -errno.ESTALE, DECISION_A_ROLLBACK, REASON_TRIAL_CONSUMED),
            ("stale-rollback", old_rollback, "normal", -errno.ESTALE, DECISION_A_ROLLBACK, REASON_ROLLBACK_REQUESTED),
        )
        for name, current, mode, status, decision, reason in unchanged:
            expected_generation = prior_generation if name.startswith("stale-") else generation
            record = (
                old_pending[:BOOT_METADATA_RECORD_SIZE]
                if name.startswith("stale-")
                else pending_record
            )
            observed = run_case(
                harness,
                root,
                name,
                current,
                record,
                primary_path,
                secondary_path,
                generation=expected_generation,
                mode=mode,
                chunk=-1,
                status=status,
                decision=decision,
                reason=reason,
                reclaimed=False,
            )
            require(observed == current, f"{name} mutated metadata")
            negative += 1

        malformed_records: list[tuple[str, bytes]] = []
        for name, offset in (
            ("record-crc", 8),
            ("record-state", 12),
            ("record-generation", 24),
        ):
            record = bytearray(pending_record)
            record[offset] ^= 1
            malformed_records.append((name, bytes(record)))
        sequence = bytearray(pending_record)
        struct.pack_into("<Q", sequence, 16, 2)
        struct.pack_into("<I", sequence, 508, zlib.crc32(sequence[:508]) & 0xFFFFFFFF)
        malformed_records.append(("record-sequence", bytes(sequence)))
        for name, record in malformed_records:
            observed = run_case(
                harness,
                root,
                name,
                erased,
                record,
                primary_path,
                secondary_path,
                generation=generation,
                mode="normal",
                chunk=-1,
                status=-errno.EBADMSG,
                decision=DECISION_A_BASELINE,
                reason=REASON_METADATA_ERASED,
                reclaimed=False,
            )
            require(observed == erased, f"{name} mutated erased metadata")
            negative += 1

        corrupt_primary = root / "primary-corrupt.bin"
        primary_bytes = bytearray(primary_path.read_bytes())
        primary_bytes[0x800] ^= 1
        corrupt_primary.write_bytes(primary_bytes)
        corrupt_secondary = root / "secondary-corrupt.bin"
        secondary_bytes = bytearray(secondary_path.read_bytes())
        secondary_bytes[0x800] ^= 1
        corrupt_secondary.write_bytes(secondary_bytes)
        for name, primary_fixture, secondary_fixture in (
            ("primary-corrupt", corrupt_primary, secondary_path),
            ("secondary-corrupt", primary_path, corrupt_secondary),
        ):
            observed = run_case(
                harness,
                root,
                name,
                erased,
                pending_record,
                primary_fixture,
                secondary_fixture,
                generation=generation,
                mode="normal",
                chunk=-1,
                status=-errno.EBADMSG,
                decision=DECISION_A_BASELINE,
                reason=REASON_METADATA_ERASED,
                reclaimed=False,
            )
            require(observed == erased, f"{name} mutated metadata before validation")
            negative += 1

        erase_faults = (
            ("erase-before-error", -errno.ETIMEDOUT, DECISION_A_ROLLBACK, REASON_ROLLBACK_REQUESTED, False),
            ("erase-after-error", -errno.ETIMEDOUT, DECISION_A_BASELINE, REASON_METADATA_ERASED, False),
            ("erase-incomplete-error", -errno.ETIMEDOUT, DECISION_A_FAILSAFE, REASON_METADATA_INVALID, False),
            ("erase-short", -errno.EIO, DECISION_A_BASELINE, REASON_METADATA_ERASED, False),
            ("erase-read-error", -errno.ETIMEDOUT, DECISION_A_BASELINE, REASON_METADATA_ERASED, True),
            ("erase-read-short", -errno.EIO, DECISION_A_BASELINE, REASON_METADATA_ERASED, True),
            ("erase-read-mismatch", -errno.EIO, DECISION_A_BASELINE, REASON_METADATA_ERASED, True),
            ("timeout-after-erase", -errno.ETIMEDOUT, DECISION_A_BASELINE, REASON_METADATA_ERASED, True),
        )
        for mode, status, decision, reason, reclaimed in erase_faults:
            run_case(
                harness,
                root,
                mode,
                old_rollback,
                pending_record,
                primary_path,
                secondary_path,
                generation=generation,
                mode=mode,
                chunk=-1,
                status=status,
                decision=decision,
                reason=reason,
                reclaimed=reclaimed,
            )
            negative += 1
            erase_boundaries += 1

        program_modes = (
            ("write-before-error", -errno.ETIMEDOUT),
            ("write-after-error", -errno.ETIMEDOUT),
            ("write-after-short", -errno.EIO),
            ("write-torn-error", -errno.ETIMEDOUT),
            ("chunk-read-error", -errno.ETIMEDOUT),
            ("chunk-read-short", -errno.EIO),
            ("chunk-read-mismatch", -errno.EIO),
        )
        for mode, status in program_modes:
            for chunk in range(PROGRAM_CHUNKS):
                complete = mode != "write-before-error" and mode != "write-torn-error"
                complete = complete and chunk == PROGRAM_CHUNKS - 1
                if complete:
                    decision, reason = DECISION_B_TRIAL_CANDIDATE, REASON_PENDING_VALID
                elif mode == "write-before-error" and chunk == 0:
                    decision, reason = DECISION_A_BASELINE, REASON_METADATA_ERASED
                else:
                    decision, reason = DECISION_A_FAILSAFE, REASON_METADATA_INVALID
                run_case(
                    harness,
                    root,
                    f"{mode}-{chunk}",
                    old_rollback,
                    pending_record,
                    primary_path,
                    secondary_path,
                    generation=generation,
                    mode=mode,
                    chunk=chunk,
                    status=status,
                    decision=decision,
                    reason=reason,
                    reclaimed=True,
                )
                negative += 1
                reset_boundaries += 1

        for mode, status in (
            ("final-read-error", -errno.ETIMEDOUT),
            ("final-read-short", -errno.EIO),
            ("final-read-mismatch", -errno.EIO),
        ):
            run_case(
                harness,
                root,
                mode,
                old_rollback,
                pending_record,
                primary_path,
                secondary_path,
                generation=generation,
                mode=mode,
                chunk=-1,
                status=status,
                decision=DECISION_B_TRIAL_CANDIDATE,
                reason=REASON_PENDING_VALID,
                reclaimed=True,
            )
            negative += 1

    return {
        "format": 1,
        "status": "pass",
        "positive_cases": positive,
        "negative_cases": negative,
        "erase_boundaries": erase_boundaries,
        "program_reset_boundaries": reset_boundaries,
        "program_chunks": PROGRAM_CHUNKS,
        "static_analyzer": True,
        "source_contract": True,
        "official_contract": official,
        "compile_metadata_write_enabled": False,
        "runtime_metadata_write_enabled": False,
        "board_write_authorized": False,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--sdk-source", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    if not args.self_test:
        parser.error("choose --self-test")
    repo = Path(__file__).resolve().parents[3]
    try:
        result = self_test(repo, args.sdk_source)
        if args.output is not None:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(
                json.dumps(result, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
    except (OSError, subprocess.SubprocessError, ValueError, PublishVerificationError) as error:
        print(f"BK7258 N15-E publication verification FAIL: {error}")
        return 1
    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        print(
            "BK7258 N15-E publication verification PASS: "
            f"positive={result['positive_cases']} negative={result['negative_cases']} "
            f"erase_boundaries={result['erase_boundaries']} "
            f"program_boundaries={result['program_reset_boundaries']} "
            "writes_enabled=false"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
