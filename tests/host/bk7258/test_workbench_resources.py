#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Independent RJI1/RJS1 vectors and externally controlled transport failures."""
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
EPOCH = "01020304000000000000000000000000"
NONCE = "05060708000000000000000000000000"
BEGIN = bytes.fromhex(
    "524a493100000001"
    + EPOCH
    + NONCE
    + "000000000000000700000080000013880000000000000000"
)
SNAPSHOT = bytes.fromhex(
    "524a533100000003"
    + EPOCH
    + NONCE
    + "000000000000000800000000000000010000008000000003"
    + "00000000000000000000000600001388"
) + bytes(48)


class ResourcesTest(unittest.TestCase):
    def module(self):
        return importlib.import_module("_lib.workbench_resources")

    def test_golden(self):
        m = self.module()
        self.assertEqual(m.encode("begin", EPOCH, NONCE, 7, 128, 5000), BEGIN)
        state = m.decode(SNAPSHOT)
        self.assertEqual(
            (state["state"], state["id"], state["written"]), ("receiving", 8, 3)
        )
        self.assertTrue(state["volatile"])
        self.assertFalse(state["installed"])
        self.assertNotIn("activated", state)

    def test_invalid(self):
        m = self.module()
        for values in [
            ("begin", EPOCH, NONCE, 0, 127, 1),
            ("begin", EPOCH, NONCE, 0, 128, 0),
            ("begin", EPOCH, NONCE, True, 128, 1),
            ("cancel", EPOCH, NONCE, 0, 0, 0),
            ("finish", EPOCH, NONCE, 8, 1, 0),
            ("append", EPOCH, NONCE, 8, 0, 1, b"a"),
            ("append", EPOCH, NONCE, 8, 0, 0, b"a" * 4097),
            ("begin", "00" * 16, NONCE, 0, 128, 1),
        ]:
            with self.subTest(values=values[:6]), self.assertRaises(ValueError):
                m.encode(*values)
        for offset, data in [
            (0, b"BAD!"),
            (4, struct.pack(">I", 12)),
            (72, struct.pack(">I", 7)),
            (120, b"x"),
            (60, struct.pack(">I", 129)),
        ]:
            bad = bytearray(SNAPSHOT)
            bad[offset : offset + len(data)] = data
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                m.decode(bad)
        with self.assertRaises(ValueError):
            m.decode(SNAPSHOT[:-1])

    def client(self, failure=None):
        c = w.ControlClient.__new__(w.ControlClient)
        c.closed, c.authenticated, c._timeout = False, True, 10
        c._clock = lambda: 100.0
        c._last = 100.0
        c.channel = type("Channel", (), {"close": lambda self: None})()
        calls = []

        def exchange(command, payload, deadline):
            calls.append((command, bytes(payload), deadline))
            if command == failure:
                raise w.ControlError("peer disconnected")
            if command == 15:
                self.assertEqual(len(payload), 20)
                value = struct.unpack(">I", payload[:4])[0]
                self.assertEqual(value >> 16, 16)
                offset = value & 65535
                return (128, *struct.unpack(">4I", SNAPSHOT[offset : offset + 16]))
            return (0, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0)

        c._exchange = exchange
        return c, calls

    def test_snapshot(self):
        c, calls = self.client()
        result = c.resource_status()
        self.assertEqual(result["id"], 8)
        self.assertEqual(
            [struct.unpack(">I", p[:4])[0] & 65535 for _, p, _ in calls],
            list(range(0, 128, 16)),
        )
        self.assertEqual(len({p[4:] for _, p, _ in calls}), 1)
        first = calls[0][1][4:]
        calls.clear()
        c.resource_status()
        self.assertNotEqual(first, calls[0][1][4:])
        self.assertEqual({d for _, _, d in calls}, {110.0})

    def test_staging(self):
        c, calls = self.client()
        result = c.resource_request("begin", EPOCH, NONCE, 7, 128, 5000)
        self.assertEqual(
            [(cmd, p) for cmd, p, _ in calls],
            [
                (16, struct.pack(">II", 16, 64)),
                (17, BEGIN[:32]),
                (17, BEGIN[32:]),
                (18, b""),
            ],
        )
        self.assertEqual(result, {"accepted": True, "completion_verified": False})
        self.assertEqual({d for _, _, d in calls}, {110.0})

    def test_failure(self):
        c, calls = self.client(17)
        with self.assertRaises(w.ControlError):
            c.resource_request("begin", EPOCH, NONCE, 7, 128, 5000)
        self.assertTrue(c.closed)
        self.assertEqual([cmd for cmd, _, _ in calls], [16, 17])
        c, calls = self.client()
        c.authenticated = False
        with self.assertRaises(w.ControlError):
            c.resource_status()
        with self.assertRaises(w.ControlError):
            c.resource_request("begin", EPOCH, NONCE, 7, 128, 5000)
        self.assertEqual(calls, [])

    def test_deadline(self):
        for invalid in (float("nan"), float("inf"), 99.0):
            c, calls = self.client()
            with self.subTest(deadline=invalid), self.assertRaises(w.ControlError):
                c.resource_status(deadline=invalid)
            self.assertEqual(calls, [])
            self.assertTrue(c.closed)

    def test_cli(self):
        parser = argparse.ArgumentParser()
        w.add_arguments(parser)
        args = parser.parse_args(["resource-status"])
        self.assertEqual(args.operation, "resource-status")
        args = parser.parse_args(
            [
                "resource-upload",
                "--file",
                "missing.bkep",
                "--receipt",
                "receipt.json",
                "--ttl-ms",
                "5000",
            ]
        )
        with patch.object(w, "SerialChannel") as channel, patch.object(
            w, "_credentials"
        ) as creds:
            with self.assertRaises(ValueError):
                w.run(args)
            channel.assert_not_called()
            creds.assert_not_called()


if __name__ == "__main__":
    unittest.main()
