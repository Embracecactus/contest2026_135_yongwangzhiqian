# SPDX-License-Identifier: Apache-2.0
import json
import importlib
from pathlib import Path
import struct
import sys
import unittest
from contextlib import contextmanager
from types import SimpleNamespace
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools/bk7258"))

hil_test = importlib.import_module("_lib.hil_test")
workbench = importlib.import_module("_lib.workbench")


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
    def test_long_key_cli_waits_for_independent_terminal_power_evidence(self):
        class Client(FakeClient):
            def info(self):
                return {"major": 0, "minor": 7, "revision": 47, "build": 692}

            def status(self):
                return {"ready": True}

        @contextmanager
        def authorized(unused):
            yield Client()

        terminal = {
            "timeline": [
                {"contract_state": "WAIT_CP_OR_UNKNOWN", "raw_state": 258},
                {"contract_state": "FAILED", "raw_state": 3, "error": -110},
            ],
            "final": {"contract_state": "FAILED", "raw_state": 3, "error": -110},
        }
        args = SimpleNamespace(
            operation="key",
            git_sha="1" * 40,
            session=5,
            held_ms=3000,
            pm_mode="pending",
            console_port="COM9",
        )
        factory = importlib.import_module("_lib.factory_diagnostics")
        with patch.object(workbench, "authorized_client", side_effect=authorized), patch.object(
            factory, "wait_power", return_value=terminal, create=True
        ) as wait:
            result = hil_test.run(args, ["bk7258.py", "hil-test", "key"])

        wait.assert_called_once_with("COM9", timeout=35)
        self.assertEqual(result["result"], "PASS")
        self.assertEqual(result["observation"]["final"]["contract_state"], "FAILED")

    def test_audio_client_uses_authenticated_kind20_without_external_media(self):
        wire = struct.pack(
            ">4s15I",
            b"BAS1", 1, 2, 23, 1, 3, 24576, 0, 0, 0,
            (-125) & 0xFFFFFFFF, (-125) & 0xFFFFFFFF, 0, 0, 0, 0,
        )

        class Stub:
            engineering_audio_run = workbench.ControlClient.engineering_audio_run
            engineering_audio_status = workbench.ControlClient.engineering_audio_status

            def __init__(self):
                self.closed = False
                self.authenticated = True
                self._timeout = 1
                self.calls = []

            def _now(self):
                return 10

            def _exchange(self, command, payload, deadline):
                self.calls.append((command, bytes(payload), deadline))
                if command == 15:
                    offset = struct.unpack(">I", payload)[0] & 0xFFFF
                    return (64, *struct.unpack(">4I", wire[offset : offset + 16]))
                return (0, 0, 0, 0, 0)

            def close(self):
                self.closed = True

        client = Stub()
        accepted = client.engineering_audio_run(23)
        self.assertTrue(accepted["accepted"])
        self.assertEqual(struct.unpack(">II", client.calls[0][1]), (20, 32))
        self.assertEqual(hil_test.decode_audio_command(client.calls[1][1])["session"], 23)
        status = client.engineering_audio_status()
        self.assertEqual(status["state"], "complete")
        self.assertFalse(client.closed)

    def test_audio_run_is_fixed_and_status_covers_all_three_sessions(self):
        record = hil_test.encode_audio_run(session=17)
        self.assertEqual(len(record), 32)
        self.assertEqual(
            hil_test.decode_audio_command(record),
            {"operation": "run", "session": 17, "sequence": 1},
        )
        wire = struct.pack(
            ">4s15I",
            b"BAS1", 1, 2, 17, 1, 3, 24576, 0, 0, 0,
            (-125) & 0xFFFFFFFF, (-125) & 0xFFFFFFFF, 0, 0, 0, 0,
        )
        status = hil_test.decode_audio_status(wire)
        self.assertEqual(status["state"], "complete")
        self.assertEqual(status["accepted_bytes"], 24576)
        self.assertEqual(status["cancel_write"], -125)
        self.assertEqual(status["cancel_drain"], -125)
        self.assertEqual(status["next_result"], 0)

    def test_audio_codec_rejects_caller_media_and_false_success(self):
        with self.assertRaises(hil_test.HilTestError):
            hil_test.encode_audio_run(session=0)
        with self.assertRaises(hil_test.HilTestError):
            hil_test.decode_audio_command(b"BKA1" + bytes(28))
        failed = struct.pack(
            ">4s15I",
            b"BAS1", 1, 3, 9, 1, 1, 8192, (-110) & 0xFFFFFFFF,
            0, 0, 0, 0, 0, 0, 0, 0,
        )
        self.assertEqual(hil_test.decode_audio_status(failed)["result"], -110)

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

    def test_key_result_requires_real_terminal_power_evidence(self):
        short = {
            "power_intent": False,
            "pm_requests": 0,
            "power_state": "idle",
            "power_error": 0,
        }
        self.assertEqual(hil_test.validate_key_result(2999, short)["result"], "PASS")

        long = {
            "timeline": [
                {"contract_state": "WAIT_CP_OR_UNKNOWN", "raw_state": 258},
                {"contract_state": "FAILED", "raw_state": 3, "error": -110},
            ],
            "final": {"contract_state": "FAILED", "raw_state": 3, "error": -110},
        }
        self.assertEqual(hil_test.validate_key_result(3000, long)["result"], "PASS")
        with self.assertRaisesRegex(hil_test.HilTestError, "terminal"):
            hil_test.validate_key_result(
                3001,
                {
                    "timeline": [
                        {"contract_state": "WAIT_CP_OR_UNKNOWN", "raw_state": 258}
                    ],
                    "final": {
                        "contract_state": "WAIT_CP_OR_UNKNOWN",
                        "raw_state": 258,
                    },
                },
            )


if __name__ == "__main__":
    unittest.main()
