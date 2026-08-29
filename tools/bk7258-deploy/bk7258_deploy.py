#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Send one verified BK7258 OTA package to a device over native USB CDC.

Peer of ``tools/bk7258/bk7258.py``: that tool builds and signs the package,
this one delivers it.  Only the USB transport exists today; the chip layer
also owns file and HTTPS sources, so a future transport belongs here as one
more module beside ``deploy_usb``.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from deploy_console import CH340_PID, CH340_VID, reboot_and_confirm  # noqa: E402
from deploy_usb import (  # noqa: E402
    NATIVE_PID,
    NATIVE_VID,
    PackageError,
    PackageObjects,
    open_package,
    stream_package,
    unique_port,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Stream a signed BK7258 CP/AP OTA package over the native USB0 CDC port, then use "
            "the CH340 CP console for a whole-device reboot and confirmation."
        )
    )
    parser.add_argument("--package", type=Path)
    parser.add_argument(
        "--ota-port",
        help="native USB CDC port; auto-select VID 1209:0001 when omitted",
    )
    parser.add_argument(
        "--control-port",
        help=(
            "CH340 CP console; auto-select VID 1a86:7523 when omitted, or "
            "use 'none' to stage without reboot"
        ),
    )
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--connect-timeout", type=float, default=45.0)
    parser.add_argument(
        "--frame-timeout",
        type=float,
        default=120.0,
        help=(
            "maximum silence while the target erases/programs before the "
            "next OTA protocol frame (default: 120 seconds)"
        ),
    )
    parser.add_argument("--confirm-timeout", type=float, default=120.0)
    parser.add_argument(
        "--status-only",
        action="store_true",
        help="only poll bkota status on the CH340 console",
    )
    parser.add_argument(
        "--reboot-only",
        action="store_true",
        help=(
            "reboot through the CH340 console and strictly confirm the "
            "currently accepted generation without opening native USB"
        ),
    )
    parser.add_argument("--expected-version")
    parser.add_argument("--expected-counter", type=int)
    parser.add_argument(
        "--expected-board",
        help=(
            "require the signed catalog to target this physical board; "
            "omit it to accept any BK7258 board the package declares"
        ),
    )
    parser.add_argument(
        "--inspect-only",
        action="store_true",
        help="validate package structure/hashes without opening serial ports",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.status_only or args.reboot_only:
        if args.status_only and args.reboot_only:
            raise ValueError("--status-only and --reboot-only are mutually exclusive")
        if args.expected_version is None or args.expected_counter is None:
            raise ValueError(
                "status/reboot mode requires --expected-version and "
                "--expected-counter"
            )
        if args.control_port == "none":
            raise ValueError("status/reboot mode requires a CH340 control port")
        control_port = unique_port(
            args.control_port, CH340_VID, CH340_PID, "BK7258 CH340 console"
        )
        reboot_and_confirm(
            control_port,
            args.baud,
            args.expected_version,
            args.expected_counter,
            args.confirm_timeout,
            reboot=args.reboot_only,
        )
        return 0

    if args.package is None:
        raise ValueError("--package is required for USB OTA")
    if args.inspect_only:
        objects = open_package(args.package.resolve(), args.expected_board)
        if (
            args.expected_version is not None
            and objects.version != args.expected_version
        ):
            raise ValueError(
                f"package version {objects.version} does not match "
                f"{args.expected_version}"
            )
        if (
            args.expected_counter is not None
            and objects.counter != args.expected_counter
        ):
            raise ValueError(
                f"package counter {objects.counter} does not match "
                f"{args.expected_counter}"
            )
        print(
            f"BK7258 USB OTA: package PASS version={objects.version} "
            f"counter={objects.counter} target={objects.target}"
        )
        return 0
    ota_port = unique_port(args.ota_port, NATIVE_VID, NATIVE_PID, "BK7258 native USB CDC")
    control_port = None
    if args.control_port != "none":
        control_port = unique_port(
            args.control_port, CH340_VID, CH340_PID, "BK7258 CH340 console"
        )
    objects = open_package(args.package.resolve(), args.expected_board)
    stream_package(
        ota_port,
        objects,
        args.connect_timeout,
        args.frame_timeout,
    )
    if control_port is None:
        print("BK7258 USB OTA: staged only; run 'bkota reboot' on the CP console")
    else:
        reboot_and_confirm(
            control_port,
            args.baud,
            objects.version,
            objects.counter,
            args.confirm_timeout,
        )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (
        OSError,
        RuntimeError,
        TimeoutError,
        ValueError,
        PackageError,
    ) as error:
        print(f"BK7258 USB OTA: FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
