#!/usr/bin/env python3
"""Verify the portable BK7258 N15-F continuous-health policy."""

from __future__ import annotations

import argparse
import errno
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

from bk7258_crc_expand import expand
from pack_bk7258_ota_metadata import (
    BOOT_METADATA_RECORD_SIZE,
    META_CONFIRMED_B,
    META_PENDING_B,
    META_ROLLBACK_A,
    META_TRIAL_STARTED,
    build_boot_metadata,
)
from pack_bk7258_ota_pair import build_bundle, write_bundle
from verify_bk7258_ota_pair import synthetic_component
from verify_bk7258_ota_trial import metadata_for_states, official_contract


HEALTH_METADATA_INVALID = 1
HEALTH_GENERATION_STALE = 2
HEALTH_NOT_TRIAL = 3
HEALTH_PRIMARY_MAPPING = 4
HEALTH_SUPERVISOR_UNHEALTHY = 5
HEALTH_CLOCK_REGRESSED = 6
HEALTH_STABILIZING = 7
HEALTH_READY = 8


class HealthVerificationError(RuntimeError):
    """Raised when an N15-F health-confirm invariant fails."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise HealthVerificationError(message)


def compile_harness(repo: Path, output: Path, *, analyzer: bool = False) -> None:
    compiler = shutil.which("cc")
    if compiler is None:
        raise HealthVerificationError("host cc is unavailable")
    board = repo / "board/bk7258"
    command = [
        compiler,
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        f"-I{board / 'bootloader'}",
        f"-I{board / 'chip/cp'}",
        str(board / "chip/cp/bk7258_ota_staging_core.c"),
        str(board / "bootloader/boot_ota_select_core.c"),
        str(board / "bootloader/boot_ota_health_core.c"),
        str(board / "scripts/host/bk7258_boot_ota_health_harness.c"),
    ]
    if analyzer:
        command.extend(["-fanalyzer", "-fsyntax-only"])
    else:
        command.extend(["-o", str(output)])
    subprocess.run(command, check=True, timeout=60)


def source_contract(repo: Path) -> None:
    board = repo / "board/bk7258"
    paths = {
        "header": board / "bootloader/boot_ota_health_core.h",
        "core": board / "bootloader/boot_ota_health_core.c",
        "harness": board / "scripts/host/bk7258_boot_ota_health_harness.c",
        "adapter": board / "chip/cp/bk7258_ota_trial.c",
        "public": board / "chip/include/bk7258_ota_trial.h",
        "kconfig": board / "chip/Kconfig",
        "make": board / "chip/Make.defs",
        "cmake": board / "chip/CMakeLists.txt",
        "config": board / "configs/cp_nsh_psram/defconfig",
    }
    text = {name: path.read_text(encoding="utf-8") for name, path in paths.items()}
    for token in (
        "bk7258_boot_ota_health_update",
        "BK7258_BOOT_OTA_HEALTH_CLOCK_REGRESSED",
        "expected_generation == 0",
        "required_stable_ms == 0",
        "info.state != BK7258_BOOT_OTA_META_TRIAL_STARTED",
        "sample->secondary_mapping_active",
        "sample->supervisor_healthy",
        "sample->supervisor_fault_free",
        "tracker->supervisor_generation != sample->supervisor_generation",
        "tracker->supervisor_fault_count != sample->supervisor_fault_count",
        "sample->now_ms - tracker->stable_since_ms",
    ):
        require(token in text["header"] or token in text["core"], f"health core contract missing {token}")
    for forbidden in (
        "<nuttx/",
        "<driver/",
        "malloc(",
        "free(",
        "printf(",
        "memcpy(",
        "memset(",
    ):
        require(forbidden not in text["core"], f"portable health core uses forbidden dependency {forbidden}")
    for token in (
        "CONFIG_BK7258_OTA_HEALTH_STABLE_MS",
        "CONFIG_BK7258_OTA_HEALTH_POLL_MS",
        "bk7258_ap_supervisor_get_status",
        "BK7258_AP_SUPERVISOR_FLAG_PRIMARY",
        "BK7258_AP_SUPERVISOR_FLAG_SECONDARY",
        "BK7258_AP_SUPERVISOR_FLAG_RPMSG_OK",
        "BK7258_AP_SUPERVISOR_INJECT_NONE",
        "bk7258_boot_ota_health_update",
    ):
        require(token in text["adapter"], f"CP health adapter contract missing {token}")
    for token in (
        "config BK7258_OTA_HEALTH_STABLE_MS",
        "config BK7258_OTA_HEALTH_POLL_MS",
        "depends on BK7258_AP_SUPERVISOR",
    ):
        require(token in text["kconfig"], f"health Kconfig contract missing {token}")
    require("boot_ota_health_core.c" in text["make"], "Make.defs omits health core")
    require("boot_ota_health_core.c" in text["cmake"], "CMake omits health core")
    require("CONFIG_SYSTEM_TIME64=y" in text["config"], "health profile requires 64-bit system time")
    require("CONFIG_BK7258_AP_SUPERVISOR=y" in text["config"], "health profile omits AP supervisor")
    require("CONFIG_BK7258_OTA_TRIAL=y" in text["config"], "health profile omits trial closure")
    require("CONFIG_BK7258_OTA_TRIAL_WRITE=y" not in text["config"], "main profile must keep metadata writes off")
    require("bk7258_ota_trial_get_status" in text["public"], "public trial observability is absent")


def sample(
    now_ms: int,
    generation: int = 11,
    faults: int = 0,
    secondary: bool = True,
    healthy: bool = True,
    fault_free: bool = True,
) -> str:
    return ",".join(
        str(value)
        for value in (
            now_ms,
            generation,
            faults,
            int(secondary),
            int(healthy),
            int(fault_free),
        )
    )


def parse_steps(output: str) -> list[dict[str, int]]:
    steps: list[dict[str, int]] = []
    for line in output.splitlines():
        if not line.startswith("STEP "):
            continue
        fields = line.split()
        observed = {"index": int(fields[1])}
        for field in fields[2:]:
            key, value = field.split("=", 1)
            observed[key] = int(value)
        steps.append(observed)
    return steps


def run_case(
    harness: Path,
    root: Path,
    name: str,
    metadata: bytes,
    *,
    expected_generation: int,
    stable_ms: int,
    samples: tuple[str, ...],
    mode: str = "normal",
) -> tuple[list[dict[str, int]], str]:
    path = root / f"{name}.bin"
    path.write_bytes(metadata)
    result = subprocess.run(
        [
            str(harness),
            str(path),
            str(expected_generation),
            str(stable_ms),
            mode,
            *samples,
        ],
        check=False,
        capture_output=True,
        text=True,
        timeout=15,
    )
    require(result.returncode == 0, f"health harness failed for {name}: {result.stdout}{result.stderr}")
    return parse_steps(result.stdout), result.stdout


def require_step(step: dict[str, int], **expected: int) -> None:
    for key, value in expected.items():
        require(step.get(key) == value, f"step {step.get('index')} {key}: expected {value}, got {step.get(key)}")


def build_fixture(root: Path, sdk_source: Path | None, generation: int) -> bytes:
    candidate_cp = synthetic_component("cp")
    candidate_ap = synthetic_component("ap")
    bundle = root / "bundle"
    files, _ = build_bundle(
        candidate_cp,
        candidate_ap,
        generation=generation,
        version="n15-f-test",
        base_version="n15-base",
        timestamp=0x12345678,
    )
    write_bundle(bundle, files)
    cp_path = root / "cp-crc.bin"
    ap_path = root / "ap-crc.bin"
    cp_path.write_bytes(expand(candidate_cp))
    ap_path.write_bytes(expand(candidate_ap))
    pending, _, _ = build_boot_metadata(
        bundle,
        cp_path,
        ap_path,
        generation=generation,
        version="n15-f-test",
        base_version="n15-base",
        timestamp=0x12345678,
        sdk_source=sdk_source,
    )
    return pending


def self_test(repo: Path, sdk_source: Path | None) -> dict[str, object]:
    generation = 41
    stable_ms = 1000
    positive = 0
    negative = 0
    continuity_resets = 0

    source_contract(repo)
    official = official_contract(sdk_source)
    with tempfile.TemporaryDirectory(prefix="bk7258-n15f-health-") as directory:
        root = Path(directory)
        harness = root / "health-harness"
        compile_harness(repo, harness)
        compile_harness(repo, root / "unused", analyzer=True)
        pending = build_fixture(root, sdk_source, generation)
        trial = metadata_for_states(pending, (META_PENDING_B, META_TRIAL_STARTED))
        confirmed = metadata_for_states(
            pending,
            (META_PENDING_B, META_TRIAL_STARTED, META_CONFIRMED_B),
        )
        rollback = metadata_for_states(
            pending,
            (META_PENDING_B, META_TRIAL_STARTED, META_ROLLBACK_A),
        )
        erased = b"\xff" * len(trial)
        corrupt = bytearray(trial)
        corrupt[BOOT_METADATA_RECORD_SIZE + 9] ^= 1

        steps, _ = run_case(
            harness,
            root,
            "exact-boundary",
            trial,
            expected_generation=generation,
            stable_ms=stable_ms,
            samples=(sample(1000), sample(1999), sample(2000)),
        )
        require(len(steps) == 3, "exact-boundary step count drift")
        require_step(steps[0], status=-errno.EAGAIN, reason=HEALTH_STABILIZING, stable_ms=0, ready=0, tracking=1)
        require_step(steps[1], status=-errno.EAGAIN, reason=HEALTH_STABILIZING, stable_ms=999, ready=0)
        require_step(steps[2], status=0, reason=HEALTH_READY, stable_ms=1000, ready=1)
        positive += 1

        wrap_start = (1 << 64) - 500
        steps, _ = run_case(
            harness,
            root,
            "uint64-wrap",
            trial,
            expected_generation=generation,
            stable_ms=stable_ms,
            samples=(sample(wrap_start), sample(499), sample(500)),
        )
        require_step(steps[1], status=-errno.EAGAIN, reason=HEALTH_STABILIZING, stable_ms=999)
        require_step(steps[2], status=0, reason=HEALTH_READY, stable_ms=1000, ready=1)
        positive += 1

        reset_sequences = (
            (
                "generation-drift",
                (sample(0), sample(500, generation=12), sample(1499, generation=12), sample(1500, generation=12)),
                -errno.EAGAIN,
                HEALTH_STABILIZING,
            ),
            (
                "fault-drift",
                (sample(0), sample(500, faults=1), sample(1499, faults=1), sample(1500, faults=1)),
                -errno.EAGAIN,
                HEALTH_STABILIZING,
            ),
            (
                "unhealthy-interrupt",
                (sample(0), sample(500, healthy=False), sample(600), sample(1599), sample(1600)),
                -errno.EAGAIN,
                HEALTH_SUPERVISOR_UNHEALTHY,
            ),
            (
                "mapping-interrupt",
                (sample(0), sample(500, secondary=False), sample(600), sample(1599), sample(1600)),
                -errno.EPERM,
                HEALTH_PRIMARY_MAPPING,
            ),
            (
                "clock-regression",
                (sample(1000), sample(900), sample(1899), sample(1900)),
                -errno.EAGAIN,
                HEALTH_CLOCK_REGRESSED,
            ),
        )
        for name, samples, interrupt_status, interrupt_reason in reset_sequences:
            steps, _ = run_case(
                harness,
                root,
                name,
                trial,
                expected_generation=generation,
                stable_ms=stable_ms,
                samples=samples,
            )
            require_step(steps[1], status=interrupt_status, reason=interrupt_reason, ready=0)
            require_step(steps[-2], status=-errno.EAGAIN, stable_ms=999, ready=0)
            require_step(steps[-1], status=0, reason=HEALTH_READY, stable_ms=1000, ready=1)
            positive += 1
            continuity_resets += 1

        negative_states = (
            ("pending", pending, generation, -errno.EPERM, HEALTH_NOT_TRIAL),
            ("confirmed", confirmed, generation, -errno.EALREADY, HEALTH_NOT_TRIAL),
            ("rollback", rollback, generation, -errno.EPERM, HEALTH_NOT_TRIAL),
            ("erased", erased, generation, -errno.EBADMSG, HEALTH_METADATA_INVALID),
            ("corrupt", bytes(corrupt), generation, -errno.EBADMSG, HEALTH_METADATA_INVALID),
            ("stale-generation", trial, generation + 1, -errno.ESTALE, HEALTH_GENERATION_STALE),
        )
        for name, metadata, expected_generation, status, reason in negative_states:
            steps, _ = run_case(
                harness,
                root,
                name,
                metadata,
                expected_generation=expected_generation,
                stable_ms=stable_ms,
                samples=(sample(0),),
            )
            require(len(steps) == 1, f"{name} step count drift")
            require_step(steps[0], status=status, reason=reason, ready=0, tracking=0)
            negative += 1

        unhealthy_samples = (
            ("primary-mapping", sample(0, secondary=False), -errno.EPERM, HEALTH_PRIMARY_MAPPING),
            ("supervisor-state", sample(0, healthy=False), -errno.EAGAIN, HEALTH_SUPERVISOR_UNHEALTHY),
            ("supervisor-fault", sample(0, fault_free=False), -errno.EAGAIN, HEALTH_SUPERVISOR_UNHEALTHY),
        )
        for name, current_sample, status, reason in unhealthy_samples:
            steps, _ = run_case(
                harness,
                root,
                name,
                trial,
                expected_generation=generation,
                stable_ms=stable_ms,
                samples=(current_sample,),
            )
            require_step(steps[0], status=status, reason=reason, ready=0, tracking=0)
            negative += 1

        invalid_calls = (
            ("null-tracker", generation, stable_ms, "null-tracker"),
            ("null-metadata", generation, stable_ms, "null-metadata"),
            ("null-sample", generation, stable_ms, "null-sample"),
            ("zero-generation", 0, stable_ms, "normal"),
            ("zero-window", generation, 0, "normal"),
        )
        for name, expected_generation, window, mode in invalid_calls:
            steps, _ = run_case(
                harness,
                root,
                name,
                trial,
                expected_generation=expected_generation,
                stable_ms=window,
                samples=(sample(0),),
                mode=mode,
            )
            require_step(steps[0], status=-errno.EINVAL, reason=0, ready=0, tracking=0)
            negative += 1

        _, output = run_case(
            harness,
            root,
            "null-result",
            trial,
            expected_generation=generation,
            stable_ms=stable_ms,
            samples=(sample(0),),
            mode="null-result",
        )
        require(f"NULL_RESULT status={-errno.EINVAL} tracking=0" in output, "null-result contract drift")
        negative += 1

    return {
        "format": 1,
        "status": "pass",
        "positive_cases": positive,
        "negative_cases": negative,
        "continuity_resets": continuity_resets,
        "stable_window_ms": stable_ms,
        "uint64_wrap_checked": True,
        "clock_regression_fail_closed": True,
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
    except (OSError, subprocess.SubprocessError, ValueError, HealthVerificationError) as error:
        print(f"BK7258 N15-F health verification FAIL: {error}")
        return 1
    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        print(
            "BK7258 N15-F health verification PASS: "
            f"positive={result['positive_cases']} negative={result['negative_cases']} "
            f"continuity_resets={result['continuity_resets']} "
            "writes_enabled=false"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
