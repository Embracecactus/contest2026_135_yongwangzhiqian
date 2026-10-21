#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""BK7258 CH340 CP console: status polling, reboot and paired confirmation."""

from __future__ import annotations

import re
import secrets
import sys
import time

try:
    import serial
except ImportError:
    serial = None


CH340_VID = 0x1A86
CH340_PID = 0x7523
MAX_STATUS_BYTES = 16 * 1024


def _require_pyserial() -> None:
    if serial is None:
        raise RuntimeError(
            "pyserial is required: install it in the Python environment that "
            "owns the serial ports"
        )


def _snapshot_is_healthy(
    lines: list[str], generation: str, version: str, counter: int
) -> bool:
    """Check one complete, generation-stable ``bkota status`` response."""
    expected = re.compile(
        rf"pair=confirmed version={re.escape(version)} counter={counter}\Z"
    )
    healthy = (
        re.compile(
            rf"ap magic=[0-9a-fA-F]{{8}} version=\d+ state=2 error=0 "
            rf"generation={generation}\b"
        ),
        re.compile(
            rf"cpu2 magic=[0-9a-fA-F]{{8}} state=8 error=0 "
            rf"generation={generation}\b.*ready=1 online=00000003\b"
        ),
        re.compile(
            rf"rptun magic=[0-9a-fA-F]{{8}} version=\d+ state=4 error=0 "
            rf"generation={generation}\b"
        ),
        re.compile(r"manager state=0\b.*error=0\b"),
        re.compile(rf"supervisor state=2\b generation={generation}\b"),
    )
    return any(expected.fullmatch(line) for line in lines) and all(
        any(pattern.match(line) for line in lines) for pattern in healthy
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
    deadline = time.monotonic() + timeout
    next_status = time.monotonic() + (12.0 if reboot else 0.0)
    partial = ""
    snapshot: list[str] | None = None
    snapshot_size = 0
    request = ""
    begin = re.compile(r"bkota-status-begin request=([0-9a-f]{16}) generation=(\d+)\Z")
    end = re.compile(r"bkota-status-end request=([0-9a-f]{16}) generation=(\d+)\Z")
    with serial.Serial(port_name, baud, timeout=0.2, write_timeout=3.0) as port:
        if reboot:
            port.reset_input_buffer()
            port.write(b"\r\nbkota reboot\r\n")
            port.flush()
        while time.monotonic() < deadline:
            now = time.monotonic()
            if now >= next_status:
                request = secrets.token_hex(8)
                snapshot = None
                partial = ""
                port.write(f"\r\nbkota status {request}\r\n".encode("ascii"))
                port.flush()
                next_status = now + 5.0
            data = port.read(4096)
            if data:
                text = data.decode("utf-8", errors="replace")
                sys.stdout.write(text)
                sys.stdout.flush()
                partial += text
                complete, separator, partial = partial.rpartition("\n")
                if separator:
                    for line in complete.splitlines():
                        match = begin.fullmatch(line)
                        if match:
                            snapshot = [line] if match.group(1) == request else None
                            snapshot_size = len(line)
                            continue
                        if snapshot is None:
                            continue
                        snapshot_size += len(line) + 1
                        if snapshot_size > MAX_STATUS_BYTES:
                            snapshot = None
                            continue
                        snapshot.append(line)
                        match = end.fullmatch(line)
                        if not match:
                            continue
                        if (
                            match.group(1) == request
                            and begin.fullmatch(snapshot[0]).group(2) == match.group(2)
                            and _snapshot_is_healthy(
                                snapshot, match.group(2), version, counter
                            )
                        ):
                            result = (
                                "reboot and automatic confirmation"
                                if reboot
                                else "running generation confirmation"
                            )
                            print(f"BK7258 OTA: {result} PASS")
                            return
                        snapshot = None
                if len(partial) > MAX_STATUS_BYTES:
                    partial = ""
                    snapshot = None
    raise TimeoutError(
        f"did not observe a complete healthy {expected!r} status snapshot on "
        f"{port_name}; required AP READY, CPU2 online, RPTUN connected, "
        "manager idle, supervisor healthy, and stable generation"
    )
