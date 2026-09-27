#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""ETC1/ETS1 public golden vectors and client failure boundaries."""
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

START = bytes.fromhex(
    "455443310000000100000000000003e801020304050607080000000200000000"
)
ACTIVE = bytes.fromhex(
    "4554533100000003000000010000000000000000000002580102030405060708"
)


class TrialTest(unittest.TestCase):
    def codec(self):
        return importlib.import_module("_lib.workbench_trial")

    def client(self, fault=None, torn=False):
        c = w.ControlClient.__new__(w.ControlClient)
        c.closed = False
        c.authenticated = True
        c._now = lambda: 1.0
        c._timeout = 10
        c.channel = type("Channel", (), {"close": lambda s: None})()
        calls = []

        def exchange(command, payload, deadline):
            calls.append((command, payload))
            if command == fault:
                raise w.ControlError("controlled transport failure")
            if command == 15:
                offset = struct.unpack(">I", payload)[0] & 65535
                data = bytearray(ACTIVE[offset : offset + 16])
                if torn and len(calls) == 3:
                    data[11] ^= 1
                return (32, *struct.unpack(">4I", data))
            return (0, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0)

        c._exchange = exchange
        return c, calls

    def test_golden(self):
        t = self.codec()
        self.assertEqual(t.encode("start", 0, "0102030405060708", "happy", 1000), START)
        self.assertEqual(
            t.encode("cancel", 1, "0102030405060709"),
            bytes.fromhex(
                "4554433100000002000000010000000001020304050607090000000000000000"
            ),
        )

    def test_invalid(self):
        t = self.codec()
        for args in [
            ("start", True, "0102030405060708", "happy", 1000),
            ("start", 0, "0" * 16, "happy", 1000),
            ("start", 0, "0102030405060708", "../bad", 1),
            ("start", 0, "0102030405060708", "happy", 0),
            ("cancel", 0, "0102030405060708"),
            ("cancel", 1, "0102030405060708", "happy", 1),
        ]:
            with self.subTest(args=args), self.assertRaises(ValueError):
                t.encode(*args)

    def test_pack_golden(self):
        expected = (
            bytes.fromhex(
                "455443320000000100000000000003e801020304050607080000000200000000"
            )
            + b"shaniu-upload-v1.bkep"
            + bytes(19)
        )
        self.assertEqual(len(expected), 72)
        self.assertEqual(
            self.codec().encode(
                "start",
                0,
                "0102030405060708",
                "happy",
                1000,
                pack_filename="shaniu-upload-v1.bkep",
            ),
            expected,
        )

    def test_pack_invalid(self):
        for name in (
            "",
            "../x.bkep",
            "a/b.bkep",
            "A.bkep",
            "x.txt",
            "a" * 35 + ".bkep",
            "a\\b.bkep",
            "a\x00.bkep",
        ):
            with self.subTest(name=name), self.assertRaises(ValueError):
                self.codec().encode(
                    "start", 0, "0102030405060708", "happy", 1, pack_filename=name
                )
        with self.assertRaises(ValueError):
            self.codec().encode("cancel", 1, "0102030405060708", pack_filename="x.bkep")

    def test_pack_staging(self):
        c, calls = self.client()
        c.trial_request(
            "start",
            0,
            "0102030405060708",
            "happy",
            1000,
            pack_filename="shaniu-upload-v1.bkep",
        )
        self.assertEqual([cmd for cmd, _ in calls], [16, 17, 17, 17, 18])
        self.assertEqual(calls[0][1], struct.pack(">II", 11, 72))
        self.assertEqual([len(p) for cmd, p in calls if cmd == 17], [32, 32, 8])
        expected = (
            bytes.fromhex(
                "455443320000000100000000000003e801020304050607080000000200000000"
            )
            + b"shaniu-upload-v1.bkep"
            + bytes(19)
        )
        self.assertEqual(b"".join(p for cmd, p in calls if cmd == 17), expected)

    def test_pack_old_firmware(self):
        c, calls = self.client(fault=16)
        with self.assertRaises(w.ControlError):
            c.trial_request(
                "start",
                0,
                "0102030405060708",
                "happy",
                1000,
                pack_filename="shaniu-upload-v1.bkep",
            )
        self.assertEqual([cmd for cmd, _ in calls], [16])
        self.assertTrue(c.closed)

    def test_pack_cli_preflight(self):
        parser = argparse.ArgumentParser()
        w.add_arguments(parser)
        for operation in ("trial-start", "trial-cancel", "trial-status"):
            args = parser.parse_args([operation, "--pack-filename", "../bad.bkep"])
            with patch.object(
                w, "_credentials", side_effect=AssertionError("key access")
            ) as borrow:
                with self.assertRaises(ValueError):
                    w.run(args)
                borrow.assert_not_called()

    def test_snapshot(self):
        t = self.codec()
        r = t.decode(ACTIVE)
        self.assertEqual((r["state"], r["id"], r["remaining_ms"]), ("active", 1, 600))
        self.assertTrue(r["device_reports_rendered"])
        for state in (1, 2, 4, 5, 6, 7, 8, 9):
            data = bytearray(ACTIVE)
            data[7] = state
            self.assertFalse(t.decode(data)["device_reports_rendered"])
        data = bytearray(ACTIVE)
        data[16:24] = bytes([255]) * 8
        self.assertIsNone(t.decode(data)["remaining_ms"])
        for bad in (
            ACTIVE[:-1],
            b"BAD!" + ACTIVE[4:],
            ACTIVE[:4] + bytes([255]) * 4 + ACTIVE[8:],
        ):
            with self.assertRaises(ValueError):
                t.decode(bad)

    def test_staging(self):
        c, calls = self.client()
        r = c.trial_request("start", 0, "0102030405060708", "happy", 1000)
        self.assertEqual(
            calls, [(16, struct.pack(">II", 11, 32)), (17, START), (18, b"")]
        )
        self.assertEqual(
            r,
            dict(
                accepted=True,
                operation_id="0102030405060708",
                completion_verified=False,
            ),
        )

    def test_coherent_read(self):
        c, calls = self.client()
        self.assertEqual(c.trial_status()["state"], "active")
        self.assertEqual(
            [struct.unpack(">I", p)[0] & 65535 for _, p in calls], [0, 16, 0]
        )
        c, calls = self.client(torn=True)
        with self.assertRaises(w.ControlError):
            c.trial_status()
        self.assertTrue(c.closed)

    def test_no_replay(self):
        c, calls = self.client(fault=17)
        with self.assertRaises(w.ControlError):
            c.trial_request("start", 0, "0102030405060708", "happy", 1000)
        self.assertEqual([a for a, _ in calls], [16, 17])
        self.assertTrue(c.closed)
        c, calls = self.client()
        c.authenticated = False
        with self.assertRaises(w.ControlError):
            c.trial_status()
        self.assertEqual(calls, [])

    def test_cli_preflight(self):
        parser = argparse.ArgumentParser()
        w.add_arguments(parser)
        args = parser.parse_args(
            [
                "trial-start",
                "--expected-trial-id",
                "0",
                "--operation-id",
                "0102030405060708",
                "--expression",
                "happy",
                "--ttl-ms",
                "0",
            ]
        )
        with patch.object(
            w, "_credentials", side_effect=AssertionError("must not read key")
        ) as borrow:
            with self.assertRaises(ValueError):
                w.run(args)
            borrow.assert_not_called()


class WirePeerBase(unittest.TestCase):
    def peer_command(self):
        return [str(ROOT / "tests/host/bk7258/build/test_shaniu_trial_wire"), "--peer"]

    def setUp(self):
        import subprocess

        self.process = subprocess.Popen(
            self.peer_command(),
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self.reply = b""
        self.frames = []
        c = w.ControlClient.__new__(w.ControlClient)
        c.closed = False
        c.authenticated = False
        c._sequence = 0
        c._timeout = 10
        c._now = lambda: 1.0
        c.channel = type("Channel", (), {"close": lambda s: None})()
        c._tls = self
        c._call = lambda function, deadline: function()
        self.c = c
        self.assertEqual(
            c._exchange(1, bytes([1]) + bytes(31), 11),
            (0, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0),
        )
        c.authenticated = True

    def command(self, line):
        import select

        self.process.stdin.write((line + "\n").encode())
        self.process.stdin.flush()
        self.assertTrue(
            select.select([self.process.stdout], [], [], 3)[0], "native peer stalled"
        )
        result = self.process.stdout.readline().decode().strip()
        self.assertTrue(result, "native peer ended before reply")
        return result

    def write(self, data):
        self.frames.append(bytes(data))
        self.reply = bytes.fromhex(self.command(bytes(data).hex()))
        return len(data)

    def read(self, count):
        result, self.reply = self.reply[:count], self.reply[count:]
        return result

    def tearDown(self):
        self.c.close()
        self.process.stdin.close()
        try:
            self.process.wait(timeout=3)
            detail = self.process.stderr.read().decode()
            self.assertEqual(self.process.returncode, 0, detail)
        finally:
            if self.process.poll() is None:
                self.process.kill()
                self.process.wait()
            self.process.stdout.close()
            self.process.stderr.close()


class WireTest(WirePeerBase):
    def test_lifecycle(self):
        c = self.c
        self.assertEqual(c.trial_status()["state"], "idle")
        self.assertFalse(
            c.trial_request("start", 0, "0102030405060708", "happy", 1000)[
                "completion_verified"
            ]
        )
        self.assertEqual(c.trial_status()["state"], "pending")
        self.assertEqual(self.command("step"), "STEP 1 happy")
        self.assertTrue(c.trial_status()["device_reports_rendered"])
        self.assertFalse(
            c.trial_request("cancel", 1, "0102030405060709")["completion_verified"]
        )
        self.assertEqual(c.trial_status()["state"], "cancel_pending")
        self.assertEqual(self.command("step"), "STEP 2 neutral")
        self.assertTrue(c.trial_status()["cancel_confirmed"])

    def test_retry_expiry(self):
        c = self.c
        c.trial_request("start", 0, "0102030405060708", "happy", 1000)
        self.assertEqual(self.command("step"), "STEP 1 happy")
        self.command("time 500")
        c.trial_request("start", 0, "0102030405060708", "happy", 1000)
        self.assertEqual(c.trial_status()["remaining_ms"], 600)
        self.command("time 1100")
        self.assertEqual(self.command("step"), "STEP 2 neutral")
        self.assertEqual(c.trial_status()["state"], "expired")
        c.trial_request("start", 0, "0102030405060708", "happy", 1000)
        self.assertEqual(self.command("step"), "STEP 2 neutral")

    def test_stale_cancel(self):
        c = self.c
        c.trial_request("start", 0, "0102030405060708", "happy", 1000)
        c.trial_request("cancel", 1, "0102030405060709")
        self.assertTrue(c.trial_status()["cancel_confirmed"])
        c.trial_request("start", 1, "0102030405060710", "sad", 1000)
        self.assertEqual(self.command("step"), "STEP 1 sad")
        before = len(self.frames)
        with self.assertRaises(w.ControlError):
            c.trial_request("cancel", 1, "0102030405060711")
        self.assertEqual(len(self.frames) - before, 3)
        self.assertEqual(self.command("step"), "STEP 1 sad")


class PackWireTest(WirePeerBase):
    def peer_command(self):
        import json
        import os

        return json.loads(os.environ["SHANIU_PACK_TEST_PEER"])

    def start_pack(self, filename="shaniu-upload-v1.bkep"):
        self.assertEqual(self.c.trial_status()["state"], "idle")
        result = self.c.trial_request(
            "start", 0, "0102030405060708", "happy", 1000, pack_filename=filename
        )
        self.assertFalse(result["completion_verified"])
        self.assertEqual(self.c.trial_status()["state"], "pending")

    def test_cancel(self):
        self.start_pack()
        self.assertEqual(self.command("step"), "STEP 4 shaniu-upload-v1 happy")
        self.assertTrue(self.c.trial_status()["device_reports_rendered"])
        self.c.trial_request("cancel", 1, "0102030405060709")
        self.assertEqual(self.c.trial_status()["state"], "cancel_pending")
        self.assertEqual(self.command("step"), "STEP 6 shaniu-default-v1 neutral")
        self.assertTrue(self.c.trial_status()["cancel_confirmed"])

    def test_expiry(self):
        self.start_pack()
        self.assertEqual(self.command("step"), "STEP 4 shaniu-upload-v1 happy")
        self.command("time 500")
        self.c.trial_request(
            "start",
            0,
            "0102030405060708",
            "happy",
            1000,
            pack_filename="shaniu-upload-v1.bkep",
        )
        self.assertEqual(self.c.trial_status()["remaining_ms"], 600)
        self.command("time 1100")
        self.assertEqual(self.command("step"), "STEP 6 shaniu-default-v1 neutral")
        self.assertEqual(self.c.trial_status()["state"], "expired")

    def test_missing(self):
        self.start_pack("missing.bkep")
        self.assertEqual(self.command("step"), "STEP 2 shaniu-default-v1 neutral")
        result = self.c.trial_status()
        self.assertEqual(result["state"], "failed")
        self.assertFalse(result["device_reports_rendered"])


if __name__ == "__main__":
    program = unittest.main(exit=False)
    raise SystemExit(
        2 if program.result.errors else int(not program.result.wasSuccessful())
    )
