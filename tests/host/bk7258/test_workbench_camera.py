#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Camera wire contract; transport fixtures never represent board images."""
import importlib
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools/bk7258"))


class CameraTest(unittest.TestCase):
    def module(self):
        return importlib.import_module("_lib.workbench_camera")

    def test_status_golden_and_invalid(self):
        m = self.module()
        data = struct.pack(
            ">4sIQ16sQIIIIiIIIII",
            b"CCS1",
            3,
            7,
            b"n" * 16,
            12345,
            6,
            640,
            480,
            0x4745504A,
            0,
            3,
            30,
            17,
            65,
            120000,
        )
        value = m.decode(data)
        self.assertEqual(
            (value["state"], value["id"], value["width"]), ("ready", 7, 640)
        )
        self.assertEqual(value["capture_sequence"], 17)
        self.assertTrue(value["valid"])
        for offset, replacement in [
            (0, b"BAD!"),
            (4, struct.pack(">I", 9)),
            (40, struct.pack(">I", 102401)),
            (52, bytes(4)),
            (60, struct.pack(">I", 7)),
        ]:
            bad = bytearray(data)
            bad[offset : offset + 4] = replacement
            with self.assertRaises(ValueError):
                m.decode(bad)
        with self.assertRaises(ValueError):
            m.decode(data[:-1])

    def test_request_requires_identity(self):
        m = self.module()
        self.assertEqual(
            m.encode(1, 7, "6e" * 16), b"CCQ1" + struct.pack(">IQ", 1, 7) + b"n" * 16
        )
        for action, identity, nonce in [
            (0, 0, "6e" * 16),
            (1, -1, "6e" * 16),
            (1, 0, "00" * 16),
        ]:
            with self.assertRaises(ValueError):
                m.encode(action, identity, nonce)

    def test_no_capture_without_auth_or_invalid_input(self):
        from _lib.workbench import ControlClient, ControlError

        m = self.module()
        client = ControlClient.__new__(ControlClient)
        client.closed = False
        client.authenticated = False
        with self.assertRaises(ControlError):
            m.status(client)


if __name__ == "__main__":
    unittest.main()
