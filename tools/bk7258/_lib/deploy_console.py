#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""BK7258 CH340 CP console: status polling, reboot and paired confirmation."""

from __future__ import annotations

import re
import sys
import time

try:
    import serial
except ImportError:
    serial = None


CH340_VID = 0x1A86
CH340_PID = 0x7523


def _require_pyserial() -> None:
    if serial is None:
        raise RuntimeError(
            "pyserial is required: install it in the Python environment that "
            "owns the serial ports"
        )


def reboot_and_confirm(
    port_name: str,
    baud: int,
    version: str,
    counter: int,
    timeout: float,
    reboot: bool = True,
) -> None:
    _require_pyserial()
    assert serial is not None
    expected = f"pair=confirmed version={version} counter={counter}"
    healthy = (
        re.compile(r"ap magic=[0-9a-fA-F]{8} version=\d+ state=2 error=0\b"),
        re.compile(
            r"cpu2 magic=[0-9a-fA-F]{8} state=8 error=0 .*"
            r"ready=1 online=00000003\b"
        ),
        re.compile(r"rptun magic=[0-9a-fA-F]{8} version=\d+ state=4 error=0\b"),
        re.compile(r"manager state=0\b.*error=0\b"),
        re.compile(r"supervisor state=2\b"),
    )
    deadline = time.monotonic() + timeout
    next_status = time.monotonic() + (12.0 if reboot else 0.0)
    transcript = ""
    with serial.Serial(port_name, baud, timeout=0.2, write_timeout=3.0) as port:
        if reboot:
            port.reset_input_buffer()
            port.write(b"\r\nbkota reboot\r\n")
            port.flush()
        while time.monotonic() < deadline:
            data = port.read(4096)
            if data:
                text = data.decode("utf-8", errors="replace")
                transcript = (transcript + text)[-16384:]
                sys.stdout.write(text)
                sys.stdout.flush()
                if expected in transcript and all(
                    pattern.search(transcript) is not None for pattern in healthy
                ):
                    result = "reboot and automatic confirmation" if reboot else \
                             "running generation confirmation"
                    print(f"BK7258 OTA: {result} PASS")
                    return
            now = time.monotonic()
            if now >= next_status:
                port.write(b"\r\nbkota status\r\n")
                port.flush()
                next_status = now + 5.0
    raise TimeoutError(
        f"did not observe healthy {expected!r} on {port_name}; "
        "required AP READY, CPU2 online, RPTUN connected, manager idle, "
        "and supervisor healthy"
    )
