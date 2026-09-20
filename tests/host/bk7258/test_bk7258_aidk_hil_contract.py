#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Host checks for the declarative AIDK UART HIL contract."""

import re
import sys
import unittest
from pathlib import Path


HIL_ROOT = Path(__file__).resolve().parents[2] / "pytest" / "test_bk7258"
sys.path.insert(0, str(HIL_ROOT))

from aidk_hil_contract import (  # noqa: E402
    ACTIVE_CASES,
    ALL_CASES,
    HilCase,
    HilStep,
    REQUIRED_COMMANDS,
    SAFE_CASES,
    validate_contract,
)


class AidkHilContractTest(unittest.TestCase):
    def test_contract_is_well_formed_and_has_expected_cases(self):
        validate_contract()
        self.assertEqual(
            [case.case_id for case in ALL_CASES],
            [
                "ap-health",
                "wifi-native-route",
                "voice-status",
                "display-status",
                "health-status",
                "motion-sample",
                "nfc-scan",
                "vision-snapshot",
                "display-mood-cycle",
            ],
        )
        self.assertTrue(all(not case.active for case in SAFE_CASES))
        self.assertTrue(all(case.active for case in ACTIVE_CASES))
        self.assertEqual(
            REQUIRED_COMMANDS,
            (
                "apctl",
                "bkdisplay",
                "bkhealth",
                "bkmotion",
                "bknfc",
                "bkvision",
                "bkvoice",
                "bkwifi",
            ),
        )

    def test_cases_never_control_reset_or_flash(self):
        commands = [step.command.lower() for case in ALL_CASES for step in case.steps]
        for command in commands:
            self.assertNotIn("reset", command)
            self.assertNotIn("reboot", command)
            self.assertNotIn("flash", command)
            self.assertNotEqual(command, "usbmode msc")

        for index, command in enumerate(
            ("reset reboot", "reboot", "flash write", "usbmode   msc", "usbmode\tmsc")
        ):
            unsafe = HilCase(
                case_id=f"unsafe-{index}",
                steps=(HilStep(command=command, expected_regex=("PASS",)),),
                repetitions=1,
                active=False,
                physical_gate="not applicable",
            )
            with self.subTest(command=command):
                with self.assertRaises(ValueError):
                    validate_contract((unsafe,))

    def test_success_regexes_are_valid_and_do_not_accept_fail_lines(self):
        for case in ALL_CASES:
            for step in case.steps:
                for pattern in step.expected_regex:
                    re.compile(pattern)
                    self.assertIsNone(re.search(pattern, "TARGET FAIL ret=-5"))

    def test_wifi_success_regex_rejects_connecting_and_disconnected_links(self):
        wifi_case = next(
            case for case in ALL_CASES if case.case_id == "wifi-native-route"
        )
        for link_state in (1, 2):
            with self.subTest(link_state=link_state):
                for step in wifi_case.steps:
                    output = (
                        f"BKWIFI RESULT operation={step.command.split()[1]} "
                        f"status=0 link={link_state} rssi=-47 "
                        "ip=192.168.0.101 mask=255.255.255.0 "
                        "router=192.168.0.1"
                    )
                    self.assertTrue(
                        all(
                            re.search(pattern, output) is None
                            for pattern in step.expected_regex
                        )
                    )

    def test_representative_target_output_matches_every_step(self):
        samples = {
            "ap-health": (
                "AP supervisor state=HEALTHY(2) reason=NONE(0) generation=1",
            ),
            "wifi-native-route": (
                "BKWIFI RESULT operation=status status=0 link=3 rssi=-47 "
                "ip=192.168.0.101 mask=255.255.255.0 router=192.168.0.1",
                "BKWIFI RESULT operation=ping status=0 link=3 rssi=-47 "
                "ip=192.168.0.101 mask=255.255.255.0 router=192.168.0.1",
            ),
            "voice-status": (
                "BKVOICE STATUS provider=ap ready=1 configured=1 connected=0 "
                "gateway_ready=0 tls_available=1\n"
                "BKVOICE PTT ready=1 link=1 pressed=0 turn=0 presses=0 "
                "last_error=0\n"
                "BKVOICE FRAMES tx=0 rx=0",
            ),
            "display-status": (
                "BKDISPLAY STATUS service=ready state=READY(3) last_error=0 "
                "screens=2 expression=neutral pack=shaniu-default-v1 "
                "revision=1 mapping=unverified sequence=3 storage=/dev/mmcsd0",
            ),
            "health-status": (
                "BKHEALTH STATUS operation=0 battery_state=charging(4) "
                "voltage_mV=4102 percent=unavailable temperature_raw=530 "
                "reference_raw=565 generation=1 sequence=4 "
                "temperature_mC=unavailable calibrated=no",
            ),
            "motion-sample": (
                "BKMOTION SAMPLE PASS timestamp_us=123456 x_mms2=120 "
                "y_mms2=-340 z_mms2=9806 status=0 unit=mm_s2 "
                "privacy=telemetry-only",
            ),
            "nfc-scan": ("BKNFC SCAN present=no privacy=uid-not-exported",),
            "vision-snapshot": (
                "BKVISION SNAPSHOT width=640 height=480 fourcc=4745504a "
                "bytes_used=32768 capture_sequence=1 soi=yes eoi=yes "
                "v4l2_error=no privacy=metadata-only storage=disabled",
            ),
            "display-mood-cycle": (
                "BKDISPLAY MOOD PASS requested=happy applied=happy sequence=4 "
                "screens=2 mapping=unverified",
                "BKDISPLAY MOOD PASS requested=neutral applied=neutral sequence=5 "
                "screens=2 mapping=unverified",
            ),
        }
        for case in ALL_CASES:
            self.assertEqual(len(samples[case.case_id]), len(case.steps))
            for step, output in zip(case.steps, samples[case.case_id]):
                for pattern in step.expected_regex:
                    self.assertIsNotNone(
                        re.search(pattern, output),
                        f"{case.case_id} did not match {pattern}",
                    )


if __name__ == "__main__":
    unittest.main()
