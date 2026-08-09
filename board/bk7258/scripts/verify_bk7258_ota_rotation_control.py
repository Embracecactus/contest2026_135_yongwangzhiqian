#!/usr/bin/env python3
"""Verify race-free format-2 confirm/rollback selection on both banks."""

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
HARNESS = SCRIPT_DIR / "host/bk7258_boot_ota_rotation_control_harness.c"


class RotationControlError(RuntimeError):
    """Raised when selected-bank transition invariants drift."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RotationControlError(message)


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
        str(BOOT_DIR / "boot_ota_rotation_control_core.c"),
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
    with tempfile.TemporaryDirectory(prefix="bk7258-rotation-control-") as directory:
        root = Path(directory)
        executable = root / "harness"
        compile_command = compile_harness(executable)
        fixture = build_fixtures(root)
        cases = (
            ("confirm-b", fixture["trial_b"], fixture["erased"], 1, 2, 3, "normal", "ok", 0),
            ("rollback-a", fixture["trial_b"], fixture["erased"], 1, 2, 4, "normal", "ok", 0),
            ("confirm-a", fixture["confirmed_b"], fixture["trial_a"], 2, 6, 7, "normal", "ok", 1),
            ("rollback-b", fixture["confirmed_b"], fixture["trial_a"], 2, 6, 8, "normal", "ok", 1),
            ("compile-gate", fixture["trial_b"], fixture["erased"], 1, 2, 3, "compile-off", "error", -1),
            ("runtime-gate", fixture["confirmed_b"], fixture["trial_a"], 2, 6, 7, "runtime-off", "error", -1),
            ("write-error", fixture["confirmed_b"], fixture["trial_a"], 2, 6, 7, "write-error", "error", 1),
            ("readback-corrupt", fixture["trial_b"], fixture["erased"], 1, 2, 3, "readback-corrupt", "error", 0),
            ("stale-generation", fixture["confirmed_b"], fixture["trial_a"], 1, 6, 7, "normal", "error", 1),
            ("wrong-state", fixture["confirmed_b"], fixture["trial_a"], 2, 2, 3, "normal", "error", 1),
            ("equal-generation", fixture["confirmed_b"], fixture["equal_a"], 1, 6, 7, "normal", "error", -1),
            ("bank-read-error", fixture["confirmed_b"], fixture["trial_a"], 2, 6, 7, "read-error", "error", -1),
        )
        outputs: list[str] = []
        for name, bank0, bank1, generation, expected, next_state, mode, outcome, selected in cases:
            path0 = root / f"{name}-bank0.bin"
            path1 = root / f"{name}-bank1.bin"
            path0.write_bytes(bank0)
            path1.write_bytes(bank1)
            result = subprocess.run(
                [
                    str(executable), str(path0), str(path1), str(generation),
                    str(expected), str(next_state), mode, outcome,
                    str(selected), "end",
                ],
                capture_output=True,
                text=True,
                timeout=30,
            )
            require(
                result.returncode == 0
                and "BK7258 format-2 control harness PASS" in result.stdout,
                f"{name} failed: {result.stdout}{result.stderr}",
            )
            outputs.append(result.stdout.strip())

        return {
            "format": 2,
            "status": "pass",
            "sdk_release": "v3.1.1.9",
            "layout_id": layout["layout_id"],
            "compile_command": compile_command,
            "positive_cases": 4,
            "negative_cases": 8,
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
    except (OSError, RotationControlError, subprocess.CalledProcessError, ValueError) as error:
        print(f"BK7258 format-2 control verification FAIL: {error}")
        return 1
    if args.json:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print(
            "BK7258 format-2 control verification PASS: "
            f"positive={report['positive_cases']} "
            f"negative={report['negative_cases']} board_execution=false"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
