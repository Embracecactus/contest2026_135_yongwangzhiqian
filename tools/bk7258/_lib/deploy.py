#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Deploy one verified BK7258 OTA package over native USB CDC.

Only the USB transport exists today.  File and HTTPS OTA sources remain chip
sources; future host transports belong beside :mod:`deploy_usb`.
"""

from __future__ import annotations

import argparse
from pathlib import Path

from .deploy_console import CH340_PID, CH340_VID, reboot_and_confirm
from .deploy_usb import (
    NATIVE_PID,
    NATIVE_VID,
    PackageError,
    PackageObjects,
    open_package,
    stream_package,
    unique_port,
)


def add_arguments(parser: argparse.ArgumentParser) -> None:
    """Add the deploy contract to the sole public BK7258 CLI."""
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
def run(args: argparse.Namespace) -> None:
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
        return

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
        return
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
