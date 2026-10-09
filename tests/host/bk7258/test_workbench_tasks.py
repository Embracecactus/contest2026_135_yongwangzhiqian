#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Independent PTE1/PTS1 vectors; external SDC1 responses, real client methods."""
import argparse
import importlib
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools/bk7258"))
w = importlib.import_module("_lib.workbench")
TASK = "00112233445566778899aabbccddeeff"
EVENT = bytes.fromhex("5054453100000001" + TASK + "00000000000000010000ea6000000000")
SNAPSHOT = bytes.fromhex(
    "5054533100000003" + TASK + "0000000000000002000000000000ea600000000500000064"
)


class TasksTest(unittest.TestCase):
    def module(self):
        return importlib.import_module("_lib.workbench_tasks")

    def test_golden(self):
        m = self.module()
        self.assertEqual(m.encode(TASK, 1, "start", 60000, 0), EVENT)
        result = m.decode(SNAPSHOT)
        self.assertEqual(result["state"], "success")
        self.assertEqual(result["event_sequence"], 2)
        self.assertTrue(result["feedback_pending"])
        self.assertNotIn("rendered", result)
        empty = m.decode(b"PTS1" + bytes(36) + struct.pack(">II", 1, 0))
        self.assertEqual(empty["state"], "none")

    def test_invalid(self):
        m = self.module()
        for values in [
            (TASK, 0, "start", 60000, 0),
            ("00" * 16, 1, "start", 1, 0),
            (TASK, 1, "start", 1, 1),
            (TASK, 1, "bad", 1, 0),
            (TASK, 1, "progress", 0, 0),
            (TASK, 2**64, "success", 1, 100),
            (TASK, True, "start", 1, 0),
            (TASK, 1, "progress", 1, 101),
        ]:
            with self.subTest(values=values), self.assertRaises(ValueError):
                m.encode(*values)
        for offset, value in [
            (0, b"BAD!"),
            (4, struct.pack(">I", 6)),
            (8, bytes(16)),
            (24, bytes(8)),
            (40, struct.pack(">I", 0x85)),
            (44, struct.pack(">I", 101)),
        ]:
            bad = bytearray(SNAPSHOT)
            bad[offset : offset + len(value)] = value
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                m.decode(bad)
        with self.assertRaises(ValueError):
            m.decode(SNAPSHOT[:-1])

    def client(self, *, changed=False, fault=None):
        # Only replace the external response boundary. Real methods, staging,
        # parser and guard execute; production C integration is a separate gate.
        c = w.ControlClient.__new__(w.ControlClient)
        c.closed, c.authenticated, c._timeout = False, True, 10
        c._clock = lambda: 100.0
        c._last = 100.0
        c.channel = type("Channel", (), {"close": lambda self: None})()
        calls = []

        def exchange(command, payload, deadline):
            calls.append((command, payload))
            if command == fault:
                raise w.ControlError("external transport lost")
            if command == 15:
                offset = struct.unpack(">I", payload)[0] & 65535
                data = bytearray(SNAPSHOT[offset : offset + 16])
                if changed and len(calls) == 5:
                    data[-1] ^= 1
                return (48, *struct.unpack(">4I", data))
            return (0, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0)

        c._exchange = exchange
        return c, calls

    def test_staging(self):
        c, calls = self.client()
        result = c.task_event(TASK, 1, "start", 60000, 0)
        self.assertEqual(
            calls,
            [
                (16, struct.pack(">II", 15, 40)),
                (17, EVENT[:32]),
                (17, EVENT[32:]),
                (18, b""),
            ],
        )
        self.assertEqual(
            result, {"accepted": True, "event_sequence": 1, "feedback_verified": False}
        )

    def test_readback(self):
        c, calls = self.client()
        self.assertEqual(c.task_status()["state"], "success")
        self.assertEqual(
            [struct.unpack(">I", p)[0] & 65535 for _, p in calls], [0, 16, 32, 0, 16]
        )
        c, calls = self.client(changed=True)
        with self.assertRaises(w.ControlError):
            c.task_status()
        self.assertTrue(c.closed)

    def test_failure(self):
        c, calls = self.client(fault=17)
        with self.assertRaises(w.ControlError):
            c.task_event(TASK, 1, "start", 60000, 0)
        self.assertEqual([command for command, _ in calls], [16, 17])
        self.assertTrue(c.closed)
        c, calls = self.client()
        c.authenticated = False
        for action in [
            lambda: c.task_status(),
            lambda: c.task_event(TASK, 1, "start", 1, 0),
        ]:
            with self.assertRaises(w.ControlError):
                action()
        self.assertEqual(calls, [])

    def test_cli(self):
        parser = argparse.ArgumentParser()
        w.add_arguments(parser)
        args = parser.parse_args(
            [
                "task-event",
                "--task-id",
                TASK,
                "--event-sequence",
                "1",
                "--state",
                "start",
                "--ttl-ms",
                "60000",
                "--progress",
                "0",
            ]
        )
        # Bad local inputs must not open hardware or even borrow credentials.
        args.event_sequence = 0
        with patch.object(w, "SerialChannel") as channel, patch.object(
            w, "_credentials"
        ) as creds:
            with self.assertRaises(ValueError):
                w.run(args)
            channel.assert_not_called()
            creds.assert_not_called()


if __name__ == "__main__":
    unittest.main()
