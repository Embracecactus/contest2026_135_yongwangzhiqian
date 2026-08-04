#!/usr/bin/env python3
"""Verify format-2 A/B selection against real encoded pair fixtures."""

from __future__ import annotations

import argparse
import json
import shutil
import struct
import subprocess
import tempfile
import zlib
from pathlib import Path

from bk7258_ab_layout import OTA_METADATA_SIZE, PAIR_B_SIZE, report as layout_report
from bk7258_crc_expand import expand
from pack_bk7258_ota_metadata import build_primary_pair
from pack_bk7258_ota_pair import S_APP_FILE, build_bundle, write_bundle
from pack_bk7258_ota_rotation import build_rotation_bank
from verify_bk7258_ota_pair import synthetic_component


SCRIPT_DIR = Path(__file__).resolve().parent
BOARD_DIR = SCRIPT_DIR.parent
BOOT_DIR = BOARD_DIR / "bootloader"
CHIP_DIR = BOARD_DIR / "chip"
HARNESS_SOURCE = SCRIPT_DIR / "host/bk7258_boot_ota_rotation_select_harness.c"

RECORD_SIZE = 512
STATE_OFFSET = 12
SEQUENCE_OFFSET = 16
CRC_OFFSET = 508

DECISION_A_BASELINE = 0
DECISION_BASE_STABLE = 1
DECISION_TARGET_TRIAL = 2
DECISION_TARGET_CONFIRMED = 3
DECISION_BASE_RECOVERY = 4
DECISION_A_METADATA_RECOVERY = 5

REASON_ERASED = 0
REASON_PENDING = 1
REASON_TRIAL = 2
REASON_CONFIRMED = 3
REASON_ROLLBACK = 4
REASON_CANDIDATE_INVALID = 5
REASON_METADATA_INVALID = 6


class RotationSelectError(RuntimeError):
    """Raised when a symmetric selector invariant drifts."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RotationSelectError(message)


def append_state(bank: bytes, state: int) -> bytes:
    require(len(bank) == OTA_METADATA_SIZE, "bank size drift")
    output = bytearray(bank)
    for index in range(8):
        offset = index * RECORD_SIZE
        if output[offset : offset + RECORD_SIZE] == b"\xff" * RECORD_SIZE:
            require(index > 0, "cannot append without a pending record")
            previous = bytes(output[offset - RECORD_SIZE : offset])
            record = bytearray(previous)
            sequence = struct.unpack_from("<Q", previous, SEQUENCE_OFFSET)[0] + 1
            struct.pack_into("<I", record, STATE_OFFSET, state)
            struct.pack_into("<Q", record, SEQUENCE_OFFSET, sequence)
            struct.pack_into(
                "<I", record, CRC_OFFSET, zlib.crc32(record[:CRC_OFFSET]) & 0xFFFFFFFF
            )
            output[offset : offset + RECORD_SIZE] = record
            return bytes(output)
    raise RotationSelectError("metadata bank has no erased record")


def compile_harness(output: Path) -> list[str]:
    compiler = shutil.which("cc")
    pkg_config = shutil.which("pkg-config")
    if compiler is None or pkg_config is None:
        raise RotationSelectError("host cc/pkg-config is unavailable")
    openssl = subprocess.run(
        [pkg_config, "--cflags", "--libs", "openssl"],
        check=True,
        capture_output=True,
        text=True,
        timeout=10,
    ).stdout.split()
    command = [
        compiler,
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-fanalyzer",
        f"-I{BOOT_DIR}",
        f"-I{CHIP_DIR / 'cp'}",
        f"-I{CHIP_DIR / 'include'}",
        str(CHIP_DIR / "cp/bk7258_ota_staging_core.c"),
        str(BOOT_DIR / "boot_ota_select_core.c"),
        str(BOOT_DIR / "boot_ota_rotation_core.c"),
        str(BOOT_DIR / "boot_ota_rotation_select_core.c"),
        str(HARNESS_SOURCE),
        *openssl,
        "-o",
        str(output),
    ]
    subprocess.run(command, check=True, capture_output=True, text=True, timeout=60)
    return command[:-2] + ["<temporary-output>"]


def run_case(
    harness: Path,
    root: Path,
    name: str,
    bank0: bytes,
    bank1: bytes,
    slot_a: bytes,
    slot_b: bytes,
    *,
    expect_error: bool = False,
    decision: int,
    reason: int,
    boot_slot: int,
    metadata_valid: bool,
    degraded: bool,
    base_verified: bool,
    target_verified: bool,
    trial_required: bool,
    mode: str = "normal",
) -> str:
    case = root / name
    case.mkdir()
    paths = []
    for file_name, payload in (
        ("bank0.bin", bank0),
        ("bank1.bin", bank1),
        ("slot-a.bin", slot_a),
        ("slot-b.bin", slot_b),
    ):
        path = case / file_name
        path.write_bytes(payload)
        paths.append(path)
    command = [
        str(harness),
        *(str(path) for path in paths),
        "error" if expect_error else "ok",
        str(decision),
        str(reason),
        str(boot_slot),
        "1" if metadata_valid else "0",
        "1" if degraded else "0",
        "1" if base_verified else "0",
        "1" if target_verified else "0",
        "1" if trial_required else "0",
        mode,
    ]
    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
    require(
        result.returncode == 0
        and "BK7258 format-2 selector harness PASS" in result.stdout,
        f"{name} failed: {result.stdout}{result.stderr}",
    )
    return result.stdout.strip()


def build_fixtures(root: Path) -> dict[str, bytes]:
    cp1 = synthetic_component("cp")
    ap1 = synthetic_component("ap")
    cp2 = bytearray(cp1)
    ap2 = bytearray(ap1)
    cp3 = bytearray(cp1)
    ap3 = bytearray(ap1)
    cp2[0x180] ^= 0x31
    ap2[0x180] ^= 0x42
    cp3[0x180] ^= 0x53
    ap3[0x180] ^= 0x64
    cp1_crc = expand(cp1)
    ap1_crc = expand(ap1)
    cp2_crc = expand(bytes(cp2))
    ap2_crc = expand(bytes(ap2))

    bundle_b = root / "bundle-b"
    files_b, _ = build_bundle(
        bytes(cp2),
        bytes(ap2),
        generation=1,
        version="2.0.0",
        base_version="1.0.0",
        timestamp=101,
    )
    write_bundle(bundle_b, files_b)
    bundle_a = root / "bundle-a"
    files_a, _ = build_bundle(
        bytes(cp3),
        bytes(ap3),
        generation=2,
        version="3.0.0",
        base_version="2.0.0",
        timestamp=202,
    )
    write_bundle(bundle_a, files_a)
    bundle_b_next = root / "bundle-b-next"
    files_b_next, _ = build_bundle(
        bytes(cp2),
        bytes(ap2),
        generation=3,
        version="2.1.0",
        base_version="1.0.0",
        timestamp=303,
    )
    write_bundle(bundle_b_next, files_b_next)
    bundle_equal = root / "bundle-equal"
    files_equal, _ = build_bundle(
        bytes(cp3),
        bytes(ap3),
        generation=1,
        version="3.0.0",
        base_version="2.0.0",
        timestamp=202,
    )
    write_bundle(bundle_equal, files_equal)

    cp1_path = root / "cp1.crc"
    ap1_path = root / "ap1.crc"
    cp2_path = root / "cp2.crc"
    ap2_path = root / "ap2.crc"
    base_b_path = root / "base-b.bin"
    cp1_path.write_bytes(cp1_crc)
    ap1_path.write_bytes(ap1_crc)
    cp2_path.write_bytes(cp2_crc)
    ap2_path.write_bytes(ap2_crc)
    base_b_path.write_bytes(files_b[S_APP_FILE])

    pending_b, _, _, _ = build_rotation_bank(
        bundle_b,
        cp1_path,
        ap1_path,
        target_slot="b",
        bank=0,
        generation=1,
        version="2.0.0",
        base_version="1.0.0",
        timestamp=101,
    )
    pending_a, _, _, _ = build_rotation_bank(
        bundle_a,
        cp2_path,
        ap2_path,
        target_slot="a",
        bank=1,
        generation=2,
        version="3.0.0",
        base_version="2.0.0",
        timestamp=202,
        base_pair_path=base_b_path,
    )
    pending_b_next, _, _, _ = build_rotation_bank(
        bundle_b_next,
        cp1_path,
        ap1_path,
        target_slot="b",
        bank=1,
        generation=3,
        version="2.1.0",
        base_version="1.0.0",
        timestamp=303,
    )
    equal_a, _, _, _ = build_rotation_bank(
        bundle_equal,
        cp2_path,
        ap2_path,
        target_slot="a",
        bank=1,
        generation=1,
        version="3.0.0",
        base_version="2.0.0",
        timestamp=202,
        base_pair_path=base_b_path,
    )
    return {
        "erased": b"\xff" * OTA_METADATA_SIZE,
        "slot_a_factory": build_primary_pair(cp1_crc, ap1_crc),
        "slot_b_v2": files_b[S_APP_FILE],
        "slot_b_next": files_b_next[S_APP_FILE],
        "slot_a_v3": files_a[S_APP_FILE],
        "pending_b": pending_b,
        "pending_b_next": pending_b_next,
        "trial_b": append_state(pending_b, 2),
        "confirmed_b": append_state(append_state(pending_b, 2), 3),
        "rollback_a": append_state(append_state(pending_b, 2), 4),
        "pending_a": pending_a,
        "trial_a": append_state(pending_a, 6),
        "confirmed_a": append_state(append_state(pending_a, 6), 7),
        "rollback_b": append_state(append_state(pending_a, 6), 8),
        "equal_a": equal_a,
    }


def verify(sdk_source: Path | None) -> dict[str, object]:
    layout = layout_report(sdk_source)
    if sdk_source is not None:
        require(
            sdk_source.name == "bk_avdk_smp-release-v3.1.1.9",
            "only official v3.1.1.9 is accepted",
        )
        require(
            layout["official_sdk"]["official_reference_geometry_match"] is True,
            "official v3.1.1.9 geometry drift",
        )
    with tempfile.TemporaryDirectory(prefix="bk7258-rotation-select-") as directory:
        root = Path(directory)
        harness = root / "harness"
        compile_command = compile_harness(harness)
        fixture = build_fixtures(root)
        erased = fixture["erased"]
        a1 = fixture["slot_a_factory"]
        b2 = fixture["slot_b_v2"]
        a3 = fixture["slot_a_v3"]
        require(len(a1) == len(b2) == len(a3) == PAIR_B_SIZE, "slot size drift")
        outputs: list[str] = []

        def case(name: str, bank0: bytes, bank1: bytes, sa: bytes, sb: bytes, **kw: object) -> None:
            outputs.append(run_case(harness, root, name, bank0, bank1, sa, sb, **kw))

        case("erased", erased, erased, a1, b2, decision=DECISION_A_BASELINE,
             reason=REASON_ERASED, boot_slot=0, metadata_valid=False,
             degraded=False, base_verified=True, target_verified=False,
             trial_required=False)
        case("pending-b", fixture["pending_b"], erased, a1, b2,
             decision=DECISION_TARGET_TRIAL, reason=REASON_PENDING, boot_slot=0,
             metadata_valid=True, degraded=False, base_verified=True,
             target_verified=True, trial_required=True)
        case("trial-b", fixture["trial_b"], erased, a1, b2,
             decision=DECISION_BASE_STABLE, reason=REASON_TRIAL, boot_slot=0,
             metadata_valid=True, degraded=False, base_verified=True,
             target_verified=False, trial_required=False)
        case("rollback-a", fixture["rollback_a"], erased, a1, b2,
             decision=DECISION_BASE_STABLE, reason=REASON_ROLLBACK, boot_slot=0,
             metadata_valid=True, degraded=False, base_verified=True,
             target_verified=False, trial_required=False)
        case("confirmed-b", fixture["confirmed_b"], erased, a1, b2,
             decision=DECISION_TARGET_CONFIRMED, reason=REASON_CONFIRMED,
             boot_slot=1, metadata_valid=True, degraded=False,
             base_verified=True, target_verified=True, trial_required=False)
        case("pending-a", fixture["confirmed_b"], fixture["pending_a"], a3, b2,
             decision=DECISION_TARGET_TRIAL, reason=REASON_PENDING, boot_slot=1,
             metadata_valid=True, degraded=False, base_verified=True,
             target_verified=True, trial_required=True)
        case("trial-a", fixture["confirmed_b"], fixture["trial_a"], a3, b2,
             decision=DECISION_BASE_STABLE, reason=REASON_TRIAL, boot_slot=1,
             metadata_valid=True, degraded=False, base_verified=True,
             target_verified=False, trial_required=False)
        case("rollback-b", fixture["confirmed_b"], fixture["rollback_b"], a3, b2,
             decision=DECISION_BASE_STABLE, reason=REASON_ROLLBACK, boot_slot=1,
             metadata_valid=True, degraded=False, base_verified=True,
             target_verified=False, trial_required=False)
        case("confirmed-a", fixture["confirmed_b"], fixture["confirmed_a"], a3, b2,
             decision=DECISION_TARGET_CONFIRMED, reason=REASON_CONFIRMED,
             boot_slot=0, metadata_valid=True, degraded=False,
             base_verified=True, target_verified=True, trial_required=False)

        corrupt_bank = bytearray(fixture["pending_a"])
        corrupt_bank[0] ^= 1
        case("torn-newer", fixture["confirmed_b"], bytes(corrupt_bank), a3, b2,
             decision=DECISION_TARGET_CONFIRMED, reason=REASON_CONFIRMED,
             boot_slot=1, metadata_valid=True, degraded=True,
             base_verified=False, target_verified=True, trial_required=False)
        corrupt_a = bytearray(a3)
        corrupt_a[0x220] ^= 1
        case("bad-a-candidate", fixture["confirmed_b"], fixture["pending_a"],
             bytes(corrupt_a), b2, decision=DECISION_BASE_RECOVERY,
             reason=REASON_CANDIDATE_INVALID, boot_slot=1, metadata_valid=True,
             degraded=False, base_verified=True, target_verified=False,
             trial_required=False)
        case("equal-generation", fixture["confirmed_b"], fixture["equal_a"],
             a3, b2, decision=DECISION_A_METADATA_RECOVERY,
             reason=REASON_METADATA_INVALID, boot_slot=0, metadata_valid=False,
             degraded=True, base_verified=True, target_verified=False,
             trial_required=False)
        case("bank1-read-error", fixture["confirmed_b"], fixture["pending_a"],
             a3, b2, decision=DECISION_TARGET_CONFIRMED,
             reason=REASON_CONFIRMED, boot_slot=1, metadata_valid=True,
             degraded=True, base_verified=False, target_verified=True,
             trial_required=False, mode="read-error-bank1")

        return {
            "format": 2,
            "status": "pass",
            "sdk_release": "v3.1.1.9",
            "layout_id": layout["layout_id"],
            "compile_command": compile_command,
            "positive_cases": 9,
            "recovery_cases": 4,
            "outputs": outputs,
            "board_execution": False,
            "flash_write_performed": False,
        }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk-source", type=Path)
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    try:
        report = verify(args.sdk_source.resolve() if args.sdk_source else None)
        if args.report is not None:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(
                json.dumps(report, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
    except (
        OSError,
        RotationSelectError,
        subprocess.CalledProcessError,
        ValueError,
    ) as error:
        print(f"BK7258 format-2 selector verification FAIL: {error}")
        return 1

    if args.json:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print(
            "BK7258 format-2 selector verification PASS: "
            f"positive={report['positive_cases']} "
            f"recovery={report['recovery_cases']} "
            "sdk=v3.1.1.9 board_execution=false"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
