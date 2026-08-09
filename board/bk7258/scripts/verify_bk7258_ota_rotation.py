#!/usr/bin/env python3
"""Verify the portable N15 symmetric dual-bank metadata foundation."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import tempfile
from pathlib import Path

from bk7258_ab_layout import (
    LITTLEFS_START,
    OTA_METADATA_MIRROR_SIZE,
    OTA_METADATA_MIRROR_START,
    OTA_METADATA_SIZE,
    OTA_METADATA_START,
    USR_CONFIG_SIZE,
    USR_CONFIG_START,
    report as layout_report,
)


SCRIPT_DIR = Path(__file__).resolve().parent
BOARD_DIR = SCRIPT_DIR.parent
BOOT_DIR = BOARD_DIR / "bootloader"
CORE = BOOT_DIR / "boot_ota_rotation_core.c"
HEADER = BOOT_DIR / "boot_ota_rotation_core.h"
HARNESS = SCRIPT_DIR / "host" / "bk7258_boot_ota_rotation_harness.c"

SDK_RELEASE = "v3.1.1.9"
OFFICIAL_PARTITIONS = Path("projects/app_ab/partitions/bk7258/auto_partitions.csv")
OFFICIAL_PARTITIONS_SHA256 = (
    "78b104c2b27e1b4fb450605c3e3a3c454c5325a3073d66b5795a5306d3595947"
)

PASS_PATTERN = re.compile(
    r"BK7258 OTA rotation harness PASS: positive=(?P<positive>\d+) "
    r"negative=(?P<negative>\d+)"
)


class RotationVerificationError(RuntimeError):
    """Raised when a format-2 rotation contract drifts."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RotationVerificationError(message)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_official_partition_boundary(
    sdk_source: Path | None,
) -> dict[str, object]:
    layout = layout_report(sdk_source)
    csv_path: Path | None = None
    observed_sha: str | None = None
    if sdk_source is not None:
        sdk = layout["official_sdk"]
        csv_path = Path(str(sdk["reference_csv"]))
        observed_sha = str(sdk["reference_csv_sha256"])
        require(
            observed_sha == OFFICIAL_PARTITIONS_SHA256,
            "official v3.1.1.9 app_ab partition table hash drift",
        )
        require(
            sdk["official_reference_geometry_match"] is True,
            "project default geometry no longer matches official v3.1.1.9",
        )
    usr_config_end = USR_CONFIG_START + USR_CONFIG_SIZE
    require(
        OTA_METADATA_MIRROR_START == usr_config_end
        and OTA_METADATA_MIRROR_START + OTA_METADATA_MIRROR_SIZE
        <= LITTLEFS_START,
        "CSV metadata mirror is not inside the unallocated span",
    )
    return {
        "path": None if csv_path is None else str(csv_path),
        "sha256": observed_sha,
        "source_verified": sdk_source is not None,
        "official_assigned_end": usr_config_end,
        "metadata_bank0_start": OTA_METADATA_START,
        "metadata_bank0_size": OTA_METADATA_SIZE,
        "metadata_bank1_start": OTA_METADATA_MIRROR_START,
        "metadata_bank1_end": (
            OTA_METADATA_MIRROR_START + OTA_METADATA_MIRROR_SIZE
        ),
        "littlefs_start": LITTLEFS_START,
        "layout_id": layout["layout_id"],
    }


def verify_source_contract() -> dict[str, object]:
    for path in (CORE, HEADER, HARNESS):
        require(path.is_file(), f"missing rotation source: {path}")

    header = HEADER.read_text(encoding="utf-8")
    core = CORE.read_text(encoding="utf-8")
    for fragment in (
        "BK7258_BOOT_OTA_ROTATION_PENDING_B = 1",
        "BK7258_BOOT_OTA_ROTATION_CONFIRMED_B = 3",
        "BK7258_BOOT_OTA_ROTATION_PENDING_A = 5",
        "BK7258_BOOT_OTA_ROTATION_CONFIRMED_A = 7",
        "BK7258_BOOT_OTA_ROTATION_NO_BANK",
    ):
        require(fragment in header, f"rotation header drift: {fragment}")
    for fragment in (
        '#define ROTATION_MAGIC                 "BKOTA15R"',
        "#define ROTATION_FORMAT                2u",
        "SLOT_A_START                 BK7258_ROLE_SLOT_A_CP_OFFSET",
        "SLOT_B_START                 BK7258_ROLE_SLOT_B_PAIR_OFFSET",
        "if (banks[0].generation == banks[1].generation)",
        "view->stable_slot = confirmed_state(selected->state)",
        "current.sequence != previous.sequence + 1u",
    ):
        require(fragment in core, f"rotation core drift: {fragment}")

    return {
        "core": {"path": str(CORE), "sha256": sha256(CORE)},
        "header": {"path": str(HEADER), "sha256": sha256(HEADER)},
        "harness": {"path": str(HARNESS), "sha256": sha256(HARNESS)},
    }


def compile_and_run(cc: str) -> dict[str, object]:
    with tempfile.TemporaryDirectory(prefix="bk7258-ota-rotation-") as directory:
        output = Path(directory) / "rotation-harness"
        command = [
            cc,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fanalyzer",
            f"-I{BOOT_DIR}",
            f"-I{BOARD_DIR / 'chip/include'}",
            str(CORE),
            str(HARNESS),
            "-o",
            str(output),
        ]
        compiled = subprocess.run(command, text=True, capture_output=True, check=False)
        require(
            compiled.returncode == 0,
            "rotation harness compilation failed:\n"
            + compiled.stdout
            + compiled.stderr,
        )
        executed = subprocess.run(
            [str(output)], text=True, capture_output=True, check=False
        )
        require(
            executed.returncode == 0,
            "rotation harness execution failed:\n"
            + executed.stdout
            + executed.stderr,
        )
        match = PASS_PATTERN.search(executed.stdout)
        require(match is not None, "rotation harness PASS line is missing")
        positive = int(match.group("positive"))
        negative = int(match.group("negative"))
        require(positive >= 9, "rotation positive coverage is too small")
        require(negative >= 6, "rotation negative coverage is too small")
        return {
            "compiler": cc,
            "compile_command": command[:-2] + ["<temporary-output>"],
            "positive": positive,
            "negative": negative,
            "stdout": executed.stdout.strip(),
        }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk-source", type=Path)
    parser.add_argument("--cc", default="gcc")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()

    try:
        sdk_source = args.sdk_source.resolve() if args.sdk_source else None
        if sdk_source is not None:
            require(
                sdk_source.name == "bk_avdk_smp-release-v3.1.1.9",
                "only the official v3.1.1.9 SDK source is accepted",
            )
        report = {
            "format": 2,
            "status": "pass",
            "sdk_release": SDK_RELEASE,
            "official_partition": verify_official_partition_boundary(sdk_source),
            "source": verify_source_contract(),
            "harness": compile_and_run(args.cc),
            "board_execution": False,
            "flash_write_performed": False,
        }
        if args.report is not None:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(
                json.dumps(report, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
    except (OSError, ValueError, RotationVerificationError) as error:
        print(f"BK7258 symmetric OTA rotation verification FAIL: {error}")
        return 1

    print(
        "BK7258 symmetric OTA rotation verification PASS: "
        f"positive={report['harness']['positive']} "
        f"negative={report['harness']['negative']} "
        "sdk=v3.1.1.9 board_execution=false"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
