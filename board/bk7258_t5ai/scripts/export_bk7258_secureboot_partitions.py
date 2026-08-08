#!/usr/bin/env python3
"""Export the project secureboot profile to Beken partition CSV.

This is a build adapter, not a packer.  It converts the project-owned
seven-column profile into the six-column format consumed by the official
release/v2.0.1 BL1/BL2 tooling.  It deliberately has no signing, key, OTP,
eFuse, Flash, or device-control capability.  CP/AP naming is the only
BK7258-specific translation performed here.
"""

from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path

from gen_bk7258_partitions import (
    BOARD_DIR,
    SECUREBOOT_XIP_LAYOUT,
    PartitionLayoutError,
    format_sdk_size,
    load_layout,
)


DEFAULT_INPUT = BOARD_DIR / "partitions/bk7258/secureboot_xip_cp_ap.csv"
SUBTYPE_BY_NAME = {
    "sys_its": "its",
    "sys_ps": "ps",
}

# The project calls the physical CPU0 image "CP" so that it remains clear
# beside the independently booted AP image.  Beken TF-M/BL2 tooling, however,
# uses this exact pair of names to decide that TF-M must hand off to the
# Non-Secure CPU0 application.  Keep the project CSV vocabulary stable and
# translate only at this official-tool boundary.
OFFICIAL_NAME_BY_PROJECT_NAME = {
    "primary_cp_app": "primary_cpu0_app",
    "secondary_cp_app": "secondary_cpu0_app",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=DEFAULT_INPUT)
    parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args()


def export(layout_input: Path, output: Path) -> None:
    layout = load_layout(layout_input)
    if layout.layout_name != SECUREBOOT_XIP_LAYOUT:
        raise PartitionLayoutError(
            "Beken secureboot export requires the secureboot XIP profile"
        )

    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(("Name", "Type", "SubType", "Offset", "Size", "Flags"))
        for partition in layout.partitions:
            official_name = OFFICIAL_NAME_BY_PROJECT_NAME.get(
                partition.name, partition.name
            )
            # The project models bl1_control as raw data so the 12 KiB
            # control pages are never passed through the 32+2 code encoder.
            # Beken's public partition table nevertheless marks the reset
            # hand-off page executable.  Preserve each side's semantics at
            # this explicit adapter boundary.
            official_type = (
                "app"
                if partition.executable or partition.name == "bl1_control"
                else "data"
            )
            writer.writerow(
                (
                    official_name,
                    official_type,
                    SUBTYPE_BY_NAME.get(partition.name, ""),
                    f"0x{partition.offset:06x}",
                    format_sdk_size(partition.size),
                    "",
                )
            )


def main() -> int:
    args = parse_args()
    try:
        export(args.input, args.output)
    except (OSError, PartitionLayoutError, ValueError) as error:
        print(f"BK7258 secureboot partition export FAIL: {error}", file=sys.stderr)
        return 1

    print(f"BK7258 secureboot partition export PASS: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
