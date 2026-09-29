# SPDX-License-Identifier: Apache-2.0
import json
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))

from tools.bk7258._lib import hil_test


class FakeClient:
    def __init__(self):
        self.records = []
        self.snapshot = {
            "enabled": True,
            "session": 0,
            "sequence": 0,
            "elapsed_ms": 0,
            "key_mask": 0,
            "pm_mode": "blocked",
            "pm_requests": 0,
            "pm_queries": 0,
            "power_state": "idle",
            "power_unresolved": False,
            "power_error": 0,
            "last_result": 0,
        }

    def engineering_status(self):
        return dict(self.snapshot)

    def engineering_command(self, record):
        decoded = hil_test.decode_command(record)
        self.records.append(decoded)
        self.snapshot.update(
            session=decoded["session"],
            sequence=decoded["sequence"],
            elapsed_ms=decoded["elapsed_ms"],
        )
        if decoded["operation"] == "session":
            self.snapshot["pm_mode"] = decoded["pm_mode"]
        elif decoded["operation"] == "key":
            self.snapshot["key_mask"] = decoded["key_mask"]
        return {"accepted": True, "completion_verified": False}


class HilTestContract(unittest.TestCase):
    def test_command_is_bounded_versioned_and_round_trips(self):
        record = hil_test.encode_command(
            "key", session=7, sequence=4, value=2, elapsed_ms=3000
        )
        self.assertEqual(len(record), 32)
        self.assertEqual(
            hil_test.decode_command(record),
            {
                "operation": "key",
                "session": 7,
                "sequence": 4,
                "key_mask": 2,
                "elapsed_ms": 3000,
                "flags": 0,
            },
        )

    def test_no_held_sequence_uses_real_command_path(self):
        client = FakeClient()
        result = hil_test.key_sequence(
            client, session=23, held_ms=3000, pm_mode="pending"
        )
        self.assertEqual(
            [item["operation"] for item in client.records],
            ["session", "key", "advance", "key"],
        )
        self.assertEqual(client.records[1]["key_mask"], 2)
        self.assertEqual(client.records[-1]["key_mask"], 0)
        self.assertEqual(result["sequence"], 4)

    def test_status_requires_test_identity(self):
        client = FakeClient()
        client.snapshot["enabled"] = False
        with self.assertRaisesRegex(hil_test.HilTestError, "engineering test"):
            hil_test.require_status(client)

    def test_status_reports_retained_power_intent(self):
        wire = struct.pack(
            ">4s15I", b"BKS1", 1, 9, 42, 4, 3000, 0, 4,
            1, 0, 0x102, 0xFFFFFF8C, 0, 1, 1, 2,
        )
        status = hil_test.decode_status(wire)
        self.assertTrue(status["enabled"])
        self.assertTrue(status["power_intent"])
        self.assertEqual(status["pm_mode"], "late-ack")
        self.assertEqual(status["power_state"], "pending")
        self.assertTrue(status["power_unresolved"])
        self.assertEqual(status["power_error"], -116)
        self.assertEqual(status["voice_state"], "idle")
        self.assertEqual(status["storage_state"], "ready")
        self.assertEqual(status["network_state"], "ready")

    def test_json_result_keeps_identity_and_observation_layers(self):
        report = hil_test.report(
            test="k2_timeout",
            result="PASS",
            firmware={"version": "0.7.46+691", "build": 691},
            git_sha="1" * 40,
            command=["bk7258.py", "hil-test", "suite"],
            input_sequence=["down", "advance:3000", "up"],
            observation={"power_state": "failed", "power_error": -110},
        )
        encoded = json.dumps(report, sort_keys=True)
        self.assertIn('"git_sha": "1111111111111111111111111111111111111111"', encoded)
        self.assertEqual(report["test"], "k2_timeout")
        self.assertEqual(report["result"], "PASS")


if __name__ == "__main__":
    unittest.main()
