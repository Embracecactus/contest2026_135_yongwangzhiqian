#!/usr/bin/env python3
"""Verify format-2 dual-bank publication and reset-safe bank rotation."""

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
CHIP_DIR = BOARD_DIR / "chip"
HARNESS = SCRIPT_DIR / "host/bk7258_boot_ota_rotation_publish_harness.c"


class RotationPublishError(RuntimeError):
    """Raised when dual-bank publication invariants drift."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RotationPublishError(message)


def compile_harness(output: Path) -> list[str]:
    compiler = shutil.which("cc")
    pkg_config = shutil.which("pkg-config")
    require(compiler is not None and pkg_config is not None,
            "host cc/pkg-config is unavailable")
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
        str(BOOT_DIR / "boot_ota_rotation_publish_core.c"),
        str(HARNESS),
        *openssl,
        "-o",
        str(output),
    ]
    subprocess.run(command, check=True, capture_output=True, text=True, timeout=90)
    return command[:-2] + ["<temporary-output>"]


def verify(sdk_source: Path | None) -> dict[str, object]:
    if sdk_source is not None:
        require(
            sdk_source.name == "bk_avdk_smp-release-v3.1.1.9",
            "only official v3.1.1.9 is accepted",
        )
    layout = layout_report(sdk_source)
    with tempfile.TemporaryDirectory(prefix="bk7258-rotation-publish-") as directory:
        root = Path(directory)
        executable = root / "harness"
        compile_command = compile_harness(executable)
        fixture = build_fixtures(root)
        torn = bytearray(fixture["equal_a"])
        torn[0] ^= 1
        corrupt_b = bytearray(fixture["slot_b_v2"])
        corrupt_b[0x220] ^= 1
        cases = (
            ("initial-b", fixture["erased"], fixture["erased"],
             fixture["slot_a_factory"], fixture["slot_b_v2"],
             fixture["pending_b"][:512], 1, 0, "normal", "ok", 0),
            ("idempotent-b", fixture["pending_b"], fixture["erased"],
             fixture["slot_a_factory"], fixture["slot_b_v2"],
             fixture["pending_b"][:512], 1, 0, "idempotent", "ok", 0),
            ("rotate-b-to-a", fixture["confirmed_b"], fixture["erased"],
             fixture["slot_a_v3"], fixture["slot_b_v2"],
             fixture["pending_a"][:512], 2, 1, "normal", "ok", 1),
            ("reclaim-torn-bank", fixture["confirmed_b"], bytes(torn),
             fixture["slot_a_v3"], fixture["slot_b_v2"],
             fixture["pending_a"][:512], 2, 1, "reclaim", "ok", 1),
            ("consumed-trial-next", fixture["trial_b"], fixture["erased"],
             fixture["slot_a_factory"], fixture["slot_b_next"],
             fixture["pending_b_next"][:512], 3, 0, "normal", "ok", 1),
            ("compile-gate", fixture["erased"], fixture["erased"],
             fixture["slot_a_factory"], fixture["slot_b_v2"],
             fixture["pending_b"][:512], 1, 0, "compile-off", "error", -1),
            ("runtime-gate", fixture["erased"], fixture["erased"],
             fixture["slot_a_factory"], fixture["slot_b_v2"],
             fixture["pending_b"][:512], 1, 0, "runtime-off", "error", -1),
            ("mapping-mismatch", fixture["confirmed_b"], fixture["erased"],
             fixture["slot_a_v3"], fixture["slot_b_v2"],
             fixture["pending_a"][:512], 2, 0, "normal", "error", -1),
            ("stale-generation", fixture["confirmed_b"], fixture["erased"],
             fixture["slot_a_v3"], fixture["slot_b_v2"],
             fixture["equal_a"][:512], 1, 1, "normal", "error", -1),
            ("busy-trial", fixture["trial_b"], fixture["erased"],
             fixture["slot_a_factory"], fixture["slot_b_v2"],
             fixture["pending_b"][:512], 1, 0, "normal", "error", 0),
            ("candidate-corrupt", fixture["erased"], fixture["erased"],
             fixture["slot_a_factory"], bytes(corrupt_b),
             fixture["pending_b"][:512], 1, 0, "normal", "error", -1),
            ("erase-error", fixture["confirmed_b"], bytes(torn),
             fixture["slot_a_v3"], fixture["slot_b_v2"],
             fixture["pending_a"][:512], 2, 1, "erase-error", "error", 0),
            ("write-error", fixture["confirmed_b"], fixture["erased"],
             fixture["slot_a_v3"], fixture["slot_b_v2"],
             fixture["pending_a"][:512], 2, 1, "write-error", "error", 0),
            ("readback-corrupt", fixture["confirmed_b"], fixture["erased"],
             fixture["slot_a_v3"], fixture["slot_b_v2"],
             fixture["pending_a"][:512], 2, 1, "readback-corrupt", "error", 0),
            ("metadata-read-error", fixture["confirmed_b"], fixture["erased"],
             fixture["slot_a_v3"], fixture["slot_b_v2"],
             fixture["pending_a"][:512], 2, 1, "metadata-read-error", "error", 0),
        )
        outputs: list[str] = []
        for (name, bank0, bank1, slot_a, slot_b, record, generation,
             active, mode, outcome, expected_bank) in cases:
            paths: list[Path] = []
            for suffix, payload in (
                ("bank0", bank0), ("bank1", bank1), ("slot-a", slot_a),
                ("slot-b", slot_b), ("record", record),
            ):
                path = root / f"{name}-{suffix}.bin"
                path.write_bytes(payload)
                paths.append(path)
            result = subprocess.run(
                [
                    str(executable), *(str(path) for path in paths),
                    str(generation), str(active), mode, outcome,
                    str(expected_bank), "end", "spare",
                ],
                capture_output=True,
                text=True,
                timeout=90,
            )
            require(
                result.returncode == 0
                and "BK7258 format-2 publish harness PASS" in result.stdout,
                f"{name} failed: {result.stdout}{result.stderr}",
            )
            outputs.append(result.stdout.strip())

        return {
            "format": 2,
            "status": "pass",
            "sdk_release": "v3.1.1.9",
            "layout_id": layout["layout_id"],
            "compile_command": compile_command,
            "positive_cases": 5,
            "negative_cases": 10,
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
    except (OSError, RotationPublishError, subprocess.CalledProcessError, ValueError) as error:
        print(f"BK7258 format-2 publish verification FAIL: {error}")
        return 1
    if args.json:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print(
            "BK7258 format-2 publish verification PASS: "
            f"positive={report['positive_cases']} "
            f"negative={report['negative_cases']} board_execution=false"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
