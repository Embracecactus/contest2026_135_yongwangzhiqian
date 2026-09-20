#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Declarative AIDK product HIL cases for the official OpenVela fixture.

This module intentionally contains no serial-port implementation.  The pytest
consumer uses the official ``p.sendCommand`` fixture so UART0 has one owner and
each command waits for its own target-side evidence before the next command is
sent.
"""

import re
import shlex
from dataclasses import dataclass
from typing import Iterable, Tuple


AIDK_BOARD = "aidk_ai_toy"
ACTIVE_ENV = "BK7258_AIDK_HIL_ACTIVE"

REQUIRED_COMMANDS = (
    "apctl",
    "bkdisplay",
    "bkhealth",
    "bkmotion",
    "bknfc",
    "bkvision",
    "bkvoice",
    "bkwifi",
)


@dataclass(frozen=True)
class HilStep:
    command: str
    expected_regex: Tuple[str, ...]
    timeout_sec: int = 20


@dataclass(frozen=True)
class HilCase:
    case_id: str
    steps: Tuple[HilStep, ...]
    repetitions: int
    active: bool
    physical_gate: str


SAFE_CASES = (
    HilCase(
        case_id="ap-health",
        steps=(
            HilStep(
                command="apctl health",
                expected_regex=(
                    r"AP supervisor state=HEALTHY\([0-9]+\) " r"reason=NONE\([0-9]+\)",
                ),
            ),
        ),
        repetitions=1,
        active=False,
        physical_gate="none; UART proves the AP supervisor state",
    ),
    HilCase(
        case_id="wifi-native-route",
        steps=(
            HilStep(
                command="bkwifi status 5000",
                expected_regex=(
                    r"BKWIFI RESULT operation=status status=0 link=3 "
                    r"rssi=-?[0-9]+ "
                    r"ip=(?!0\.0\.0\.0)(?:[0-9]{1,3}\.){3}[0-9]{1,3} "
                    r"mask=(?!0\.0\.0\.0)(?:[0-9]{1,3}\.){3}[0-9]{1,3} "
                    r"router=(?!0\.0\.0\.0)(?:[0-9]{1,3}\.){3}[0-9]{1,3}",
                ),
            ),
            HilStep(
                command="bkwifi ping 5000",
                expected_regex=(
                    r"BKWIFI RESULT operation=ping status=0 link=3 "
                    r"rssi=-?[0-9]+ "
                    r"ip=(?!0\.0\.0\.0)(?:[0-9]{1,3}\.){3}[0-9]{1,3} "
                    r"mask=(?!0\.0\.0\.0)(?:[0-9]{1,3}\.){3}[0-9]{1,3} "
                    r"router=(?!0\.0\.0\.0)(?:[0-9]{1,3}\.){3}[0-9]{1,3}",
                ),
            ),
        ),
        repetitions=1,
        active=False,
        physical_gate=(
            "UART proves the CP lease was applied to the native NuttX "
            "interface and the configured gateway answered ICMP"
        ),
    ),
    HilCase(
        case_id="voice-status",
        steps=(
            HilStep(
                command="bkvoice status",
                expected_regex=(
                    r"BKVOICE STATUS provider=ap ready=1 configured=[01] "
                    r"connected=[01] gateway_ready=[01] tls_available=1",
                    r"BKVOICE PTT ready=1 link=[01] pressed=0 "
                    r"turn=[0-9]+ presses=[0-9]+ last_error=-?[0-9]+",
                    r"BKVOICE FRAMES tx=[0-9]+ rx=[0-9]+",
                ),
            ),
        ),
        repetitions=1,
        active=False,
        physical_gate="status only; microphone and speaker paths are not exercised",
    ),
    HilCase(
        case_id="display-status",
        steps=(
            HilStep(
                command="bkdisplay status",
                expected_regex=(
                    r"BKDISPLAY STATUS service=ready state=READY\([0-9]+\) "
                    r"last_error=0 screens=2 expression=[^ ]+ "
                    r"pack=shaniu-default-v1 revision=[1-9][0-9]* "
                    r"mapping=(?:unverified|verified) sequence=[1-9][0-9]* "
                    r"storage=/dev/mmcsd0",
                ),
            ),
        ),
        repetitions=1,
        active=False,
        physical_gate="two physical panels and left/right mapping require observation",
    ),
    HilCase(
        case_id="health-status",
        steps=(
            HilStep(
                command="bkhealth status",
                expected_regex=(
                    r"BKHEALTH STATUS operation=0 "
                    r"battery_state=(?:full|charging|discharging)\([0-9]+\) "
                    r"voltage_mV=[0-9]+ percent=unavailable "
                    r"temperature_raw=-?[0-9]+ reference_raw=-?[0-9]+ "
                    r"generation=[0-9]+ sequence=[0-9]+ "
                    r"temperature_mC=(?:-?[0-9]+|unavailable) "
                    r"calibrated=(?:yes|no)",
                ),
            ),
        ),
        repetitions=2,
        active=False,
        physical_gate=(
            "voltage and temperature accuracy require reference instruments; "
            "battery percentage is intentionally unavailable"
        ),
    ),
    HilCase(
        case_id="motion-sample",
        steps=(
            HilStep(
                command="bkmotion sample",
                expected_regex=(
                    r"BKMOTION SAMPLE PASS timestamp_us=[1-9][0-9]* "
                    r"x_mms2=-?[0-9]+ y_mms2=-?[0-9]+ z_mms2=-?[0-9]+ "
                    r"status=-?[0-9]+ unit=mm_s2 privacy=telemetry-only",
                ),
            ),
        ),
        repetitions=2,
        active=False,
        physical_gate=(
            "axis orientation, scale and motion response require moving the "
            "board and comparison with a reference"
        ),
    ),
    HilCase(
        case_id="nfc-scan",
        steps=(
            HilStep(
                command="bknfc scan",
                expected_regex=(
                    r"BKNFC SCAN present=(?:yes|no) privacy=uid-not-exported",
                ),
            ),
        ),
        repetitions=2,
        active=False,
        physical_gate="separate no-card and known-card observations are required",
    ),
)


ACTIVE_CASES = (
    HilCase(
        case_id="vision-snapshot",
        steps=(
            HilStep(
                command="bkvision snapshot",
                expected_regex=(
                    r"BKVISION SNAPSHOT width=640 height=480 "
                    r"fourcc=[0-9a-fA-F]{8} bytes_used=[1-9][0-9]* "
                    r"capture_sequence=[0-9]+ soi=yes eoi=yes "
                    r"v4l2_error=no privacy=metadata-only storage=disabled",
                ),
                timeout_sec=30,
            ),
        ),
        repetitions=2,
        active=True,
        physical_gate="scene content and the camera privacy indicator require observation",
    ),
    HilCase(
        case_id="display-mood-cycle",
        steps=(
            HilStep(
                command="bkdisplay mood happy",
                expected_regex=(
                    r"BKDISPLAY MOOD PASS requested=happy applied=happy "
                    r"sequence=[1-9][0-9]* screens=2 "
                    r"mapping=(?:unverified|verified)",
                ),
            ),
            HilStep(
                command="bkdisplay mood neutral",
                expected_regex=(
                    r"BKDISPLAY MOOD PASS requested=neutral applied=neutral "
                    r"sequence=[1-9][0-9]* screens=2 "
                    r"mapping=(?:unverified|verified)",
                ),
            ),
        ),
        repetitions=1,
        active=True,
        physical_gate="both eye images and their physical left/right mapping require observation",
    ),
)


ALL_CASES = SAFE_CASES + ACTIVE_CASES


def validate_contract(cases: Iterable[HilCase] = ALL_CASES) -> None:
    """Fail closed on malformed or target-control HIL declarations."""

    seen = set()
    forbidden_commands = {"reset", "reboot", "flash", "usbmode"}
    for case in cases:
        if not case.case_id or case.case_id in seen:
            raise ValueError("HIL case ids must be non-empty and unique")
        seen.add(case.case_id)
        if case.repetitions < 1 or not case.steps or not case.physical_gate:
            raise ValueError("HIL cases require steps, repetitions and a physical gate")
        for step in case.steps:
            command = step.command.strip()
            if not command or command != step.command or "\n" in command:
                raise ValueError("HIL commands must be one normalized console line")
            command_tokens = shlex.split(command)
            if not command_tokens or command_tokens[0].lower() in forbidden_commands:
                raise ValueError(
                    "HIL app cases must not reset, flash or export storage"
                )
            if step.timeout_sec < 1 or not step.expected_regex:
                raise ValueError("HIL steps require a timeout and target evidence")
            for pattern in step.expected_regex:
                re.compile(pattern)


validate_contract()
