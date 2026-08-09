#!/usr/bin/env python3
"""Verify format-2 one-trial append on both metadata banks."""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

from bk7258_ab_layout import report as layout_report
from verify_bk7258_ota_rotation_select import build_fixtures


SCRIPT_DIR = Path(__file__).resolve().parent
BOARD_DIR = SCRIPT_DIR.parent
BOOT_DIR = BOARD_DIR / "bootloader"
HARNESS = SCRIPT_DIR / "host/bk7258_boot_ota_rotation_trial_harness.c"


class RotationTrialError(RuntimeError):
    """Raised when a dual-bank transition invariant drifts."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RotationTrialError(message)


def compile_harness(output: Path) -> list[str]:
    compiler = shutil.which("cc")
    require(compiler is not None, "host C compiler is unavailable")
    command = [
        compiler,
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-fanalyzer",
        f"-I{BOOT_DIR}",
        f"-I{BOARD_DIR / 'chip/include'}",
        str(BOOT_DIR / "boot_ota_rotation_core.c"),
        str(BOOT_DIR / "boot_ota_rotation_trial_core.c"),
        str(HARNESS),
        "-o",
        str(output),
    ]
    subprocess.run(command, check=True, capture_output=True, text=True, timeout=60)
    return command[:-2] + ["<temporary-output>"]


def verify(sdk_source: Path | None) -> dict[str, object]:
    if sdk_source is not None:
        require(
            sdk_source.name == "bk_avdk_smp-release-v3.1.1.9",
            "only official v3.1.1.9 is accepted",
        )
    layout = layout_report(sdk_source)
    with tempfile.TemporaryDirectory(prefix="bk7258-rotation-trial-") as directory:
        root = Path(directory)
        executable = root / "harness"
        compile_command = compile_harness(executable)
        fixture = build_fixtures(root)
        cases = (
            ("pending-b", fixture["pending_b"], 0, 1, 1, 2, "normal", "ok"),
            ("pending-a", fixture["pending_a"], 1, 2, 5, 6, "normal", "ok"),
            ("compile-gate", fixture["pending_b"], 0, 1, 1, 2,
             "compile-gate-off", "error"),
            ("runtime-gate", fixture["pending_a"], 1, 2, 5, 6,
             "runtime-gate-off", "error"),
            ("write-error", fixture["pending_b"], 0, 1, 1, 2,
             "write-error", "error"),
            ("readback-corrupt", fixture["pending_a"], 1, 2, 5, 6,
             "readback-corrupt", "error"),
            ("wrong-bank", fixture["pending_b"], 2, 1, 1, 2,
             "normal", "error"),
            ("wrong-state", fixture["pending_b"], 0, 1, 5, 6,
             "normal", "error"),
        )
        outputs: list[str] = []
        for name, payload, bank, generation, expected, next_state, mode, outcome in cases:
            path = root / f"{name}.bin"
            path.write_bytes(payload)
            result = subprocess.run(
                [
                    str(executable), str(path), str(bank), str(generation),
                    str(expected), str(next_state), mode, outcome,
                ],
                capture_output=True,
                text=True,
                timeout=30,
            )
            require(
                result.returncode == 0
                and "BK7258 format-2 trial harness PASS" in result.stdout,
                f"{name} failed: {result.stdout}{result.stderr}",
            )
            outputs.append(result.stdout.strip())
        return {
            "format": 2,
            "status": "pass",
            "sdk_release": "v3.1.1.9",
            "layout_id": layout["layout_id"],
            "compile_command": compile_command,
            "positive_cases": 2,
            "negative_cases": 6,
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
        RotationTrialError,
        subprocess.CalledProcessError,
        ValueError,
    ) as error:
        print(f"BK7258 format-2 trial verification FAIL: {error}")
        return 1
    if args.json:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print(
            "BK7258 format-2 trial verification PASS: "
            f"positive={report['positive_cases']} "
            f"negative={report['negative_cases']} board_execution=false"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
