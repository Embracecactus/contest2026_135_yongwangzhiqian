#!/usr/bin/env python3
"""Verify format-2 health confirmation policy for A and B trials."""

from __future__ import annotations

import argparse
import errno
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
HARNESS = SCRIPT_DIR / "host/bk7258_boot_ota_rotation_health_harness.c"

HEALTH_METADATA_INVALID = 1
HEALTH_GENERATION_STALE = 2
HEALTH_NOT_TRIAL = 3
HEALTH_MAPPING_MISMATCH = 4
HEALTH_SUPERVISOR_UNHEALTHY = 5
HEALTH_STABILIZING = 7
HEALTH_READY = 8


class RotationHealthError(RuntimeError):
    """Raised when symmetric health-policy invariants drift."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RotationHealthError(message)


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
        str(BOOT_DIR / "boot_ota_rotation_health_core.c"),
        str(HARNESS),
        "-o",
        str(output),
    ]
    subprocess.run(command, check=True, capture_output=True, text=True, timeout=60)
    return command[:-2] + ["<temporary-output>"]


def sample(now: int, generation: int = 9, faults: int = 0,
           active: int = 1, healthy: int = 1, fault_free: int = 1) -> str:
    return f"{now},{generation},{faults},{active},{healthy},{fault_free}"


def parse(output: str) -> list[dict[str, int]]:
    rows: list[dict[str, int]] = []
    for line in output.splitlines():
        if not line.startswith("STEP "):
            continue
        fields = line.split()
        row = {"index": int(fields[1])}
        for field in fields[2:]:
            key, value = field.split("=", 1)
            row[key] = int(value)
        rows.append(row)
    return rows


def run(executable: Path, root: Path, name: str, bank: bytes,
        generation: int, samples: tuple[str, ...]) -> list[dict[str, int]]:
    path = root / f"{name}.bin"
    path.write_bytes(bank)
    result = subprocess.run(
        [str(executable), str(path), str(generation), "1000", *samples],
        capture_output=True,
        text=True,
        timeout=15,
    )
    require(
        result.returncode == 0,
        f"{name} failed: {result.stdout}{result.stderr}",
    )
    return parse(result.stdout)


def verify(sdk_source: Path | None) -> dict[str, object]:
    if sdk_source is not None:
        require(
            sdk_source.name == "bk_avdk_smp-release-v3.1.1.9",
            "only official v3.1.1.9 is accepted",
        )
    layout = layout_report(sdk_source)
    with tempfile.TemporaryDirectory(prefix="bk7258-rotation-health-") as directory:
        root = Path(directory)
        executable = root / "harness"
        compile_command = compile_harness(executable)
        fixture = build_fixtures(root)

        rows = run(executable, root, "trial-b", fixture["trial_b"], 1,
                   (sample(100, generation=9, active=1),
                    sample(1099, generation=9, active=1),
                    sample(1100, generation=9, active=1)))
        require(rows[-1]["status"] == 0 and rows[-1]["reason"] == HEALTH_READY,
                "B trial did not become healthy at the exact boundary")

        rows = run(executable, root, "trial-a", fixture["trial_a"], 2,
                   (sample(500, active=0), sample(1500, active=0)))
        require(rows[-1]["status"] == 0 and rows[-1]["target"] == 0,
                "A trial health direction failed")

        rows = run(executable, root, "mapping", fixture["trial_a"], 2,
                   (sample(0, active=1),))
        require(rows[0]["status"] == -errno.EPERM and
                rows[0]["reason"] == HEALTH_MAPPING_MISMATCH,
                "wrong active mapping was not rejected")

        rows = run(executable, root, "generation", fixture["trial_b"], 2,
                   (sample(0),))
        require(rows[0]["status"] == -errno.ESTALE and
                rows[0]["reason"] == HEALTH_GENERATION_STALE,
                "stale generation was not rejected")

        rows = run(executable, root, "pending", fixture["pending_b"], 1,
                   (sample(0),))
        require(rows[0]["status"] == -errno.EPERM and
                rows[0]["reason"] == HEALTH_NOT_TRIAL,
                "pending state was accepted as a running trial")

        rows = run(executable, root, "confirmed", fixture["confirmed_b"], 1,
                   (sample(0),))
        require(rows[0]["status"] == -errno.EALREADY and
                rows[0]["reason"] == HEALTH_NOT_TRIAL,
                "confirmed state did not report already complete")

        corrupt = bytearray(fixture["trial_b"])
        corrupt[512 + 9] ^= 1
        rows = run(executable, root, "corrupt", bytes(corrupt), 1,
                   (sample(0),))
        require(rows[0]["status"] < 0 and
                rows[0]["reason"] == HEALTH_METADATA_INVALID,
                "corrupt chain was not rejected")

        rows = run(executable, root, "continuity", fixture["trial_b"], 1,
                   (sample(0), sample(900, healthy=0), sample(1000),
                    sample(1999), sample(2000)))
        require(rows[1]["status"] == -errno.EAGAIN and
                rows[1]["reason"] == HEALTH_SUPERVISOR_UNHEALTHY and
                rows[-1]["status"] == 0,
                "unhealthy sample did not restart the stable window")

        return {
            "format": 2,
            "status": "pass",
            "sdk_release": "v3.1.1.9",
            "layout_id": layout["layout_id"],
            "compile_command": compile_command,
            "positive_cases": 2,
            "negative_cases": 6,
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
    except (OSError, RotationHealthError, subprocess.CalledProcessError, ValueError) as error:
        print(f"BK7258 format-2 health verification FAIL: {error}")
        return 1
    if args.json:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print(
            "BK7258 format-2 health verification PASS: "
            f"positive={report['positive_cases']} "
            f"negative={report['negative_cases']} board_execution=false"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
