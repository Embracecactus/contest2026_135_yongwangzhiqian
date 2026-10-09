#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""ESC1/ESS1 independent vectors, client contracts and native peer cases."""
import argparse
import importlib
import struct
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools/bk7258"))
w = importlib.import_module("_lib.workbench")
EPOCH = "07000000000000000000000000000000"
NONCE = "01000000000000000000000000000000"
NAME = "shaniu-upload-v1.bkep"
# Approved ESC1 layout, independently specified; not produced by the codec.
SET = bytes.fromhex(
    "4553433100000001" + EPOCH + NONCE + "00000001000000000000000000000001"
) + NAME.encode().ljust(40, b"\0")


def snapshot():
    return (
        b"ESS1"
        + struct.pack(">I", 6)
        + bytes.fromhex(EPOCH)
        + struct.pack(">IiiIQQ", 2, 0, 0, 7, 2, 1)
        + bytes.fromhex(NONCE)
        + NAME.encode().ljust(40, b"\0")
        + struct.pack(">Q", 9)
        + bytes(8)
    )


class SelectionTest(unittest.TestCase):
    def codec(self):
        return importlib.import_module("_lib.workbench_selection")

    def client(self, fault=None, data=None, torn=False):
        c = w.ControlClient.__new__(w.ControlClient)
        c.closed = False
        c.authenticated = True
        c._now = lambda: 1.0
        c._timeout = 10
        c.channel = type("Channel", (), {"close": lambda s: None})()
        calls = []
        data = snapshot() if data is None else data

        def exchange(command, payload, deadline):
            calls.append((command, payload))
            if command == fault:
                raise w.ControlError("lost response")
            if command == 15:
                self.assertEqual(len(payload), 20)
                argument = struct.unpack(">I", payload[:4])[0]
                self.assertEqual(argument >> 16, 17)
                offset = argument & 65535
                part = data[offset : offset + 16]
                if torn and len(calls) > 8:
                    part = bytes(16)
                return (128, *struct.unpack(">4I", part))
            return (0, 0, 0, 0, 0)

        c._exchange = exchange
        return c, calls

    def test_golden(self):
        self.assertEqual(len(SET), 96)
        self.assertEqual(self.codec().encode("set", EPOCH, NONCE, 1, 1, NAME), SET)
        for action, number in (("refresh", 2), ("cancel", 3), ("recover", 4)):
            expected = (
                b"ESC1"
                + struct.pack(">I", number)
                + bytes.fromhex(EPOCH + NONCE)
                + struct.pack(">IIQ", 1, 0, 0)
                + bytes(40)
            )
            self.assertEqual(self.codec().encode(action, EPOCH, NONCE, 1), expected)

    def test_invalid(self):
        m = self.codec()
        for args in (
            ("set", EPOCH, NONCE, True, 1, NAME),
            ("set", "0" * 32, NONCE, 1, 1, NAME),
            ("set", EPOCH, "A" * 32, 1, 1, NAME),
            ("set", EPOCH, NONCE, 1, None, NAME),
            ("set", EPOCH, NONCE, 1, 2**64, NAME),
            ("set", EPOCH, NONCE, 1, 1, "../x.bkep"),
            ("cancel", EPOCH, NONCE, 0),
            ("refresh", EPOCH, NONCE, 1, 1, NAME),
        ):
            with self.subTest(args=args), self.assertRaises(ValueError):
                m.encode(*args)

    def test_decode(self):
        data = self.codec().decode(snapshot())
        self.assertEqual(data["state"], "done")
        self.assertEqual((data["id"], data["revision"], data["filename"]), (2, 2, NAME))
        self.assertTrue(
            data["device_reports_saved"] and data["device_reports_rendered"]
        )
        self.assertFalse(data["refresh_complete"])
        self.assertEqual(data["operation_nonce"], NONCE)
        pending = bytearray(snapshot())
        struct.pack_into(">I", pending, 4, 1)
        struct.pack_into(">I", pending, 36, 0)
        struct.pack_into(">Q", pending, 40, 0)
        queued = self.codec().decode(pending)
        self.assertFalse(
            queued["device_reports_saved"] or queued["device_reports_rendered"]
        )
        self.assertIsNone(queued["revision"])
        unknown = bytearray(snapshot())
        struct.pack_into(">I", unknown, 4, 9)
        struct.pack_into(">i", unknown, 28, -5)
        struct.pack_into(">I", unknown, 36, 3)
        value = self.codec().decode(unknown)
        self.assertTrue(value["device_reports_saved"])
        self.assertFalse(
            value["device_reports_rendered"] or value["selection_complete"]
        )

    def test_malformed(self):
        for state, flags in ((1, 3), (2, 3), (5, 3), (4, 1)):
            data = bytearray(snapshot())
            struct.pack_into(">I", data, 4, state)
            struct.pack_into(">I", data, 36, flags)
            with self.subTest(early_state=state), self.assertRaises(ValueError):
                self.codec().decode(data)
        for offset, value in ((0, 0), (7, 255), (35, 1), (39, 32), (120, 1), (111, 1)):
            data = bytearray(snapshot())
            data[offset] = value
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                self.codec().decode(data)
        for data in (snapshot()[:-1], bytes(128), "invalid"):
            with self.assertRaises(ValueError):
                self.codec().decode(data)

    def test_catalog_is_not_default_completion(self):
        data = bytearray(128)
        data[:4] = b"ESS1"
        struct.pack_into(">I", data, 4, 6)
        data[8:24] = bytes.fromhex(EPOCH)
        struct.pack_into(">IiiI", data, 24, 2, 0, 0, 32)
        struct.pack_into(">Q", data, 112, 1)
        value = self.codec().decode(data)
        self.assertTrue(value["catalog"])
        self.assertEqual(value["snapshot_of"], "latest_catalog_job")
        for field in (
            "selection_complete",
            "refresh_complete",
            "device_reports_saved",
            "device_reports_rendered",
        ):
            self.assertFalse(value[field])
        self.assertIsNone(value["revision"])
        self.assertIsNone(value["filename"])
        for offset, number in ((36, 33), (36, 34), (36, 40), (4, 3), (4, 4), (48, 1)):
            bad = bytearray(data)
            struct.pack_into(">I", bad, offset, number)
            with self.subTest(offset=offset, number=number), self.assertRaises(
                ValueError
            ):
                self.codec().decode(bad)

    def test_staging(self):
        c, calls = self.client()
        result = c.selection_request("set", EPOCH, NONCE, 1, 1, NAME)
        self.assertEqual([x[0] for x in calls], [16, 17, 17, 17, 18])
        self.assertEqual(calls[0][1], struct.pack(">II", 17, 96))
        self.assertEqual(b"".join(x[1] for x in calls[1:4]), SET)
        self.assertTrue(result["accepted"])
        self.assertFalse(result["completion_verified"])
        self.assertEqual(result["operation_nonce"], NONCE)

    def test_unconfirmed_no_replay(self):
        for command in (16, 17, 18):
            c, calls = self.client(fault=command)
            with self.subTest(command=command), self.assertRaises(w.ControlError):
                c.selection_request("set", EPOCH, NONCE, 1, 1, NAME)
            self.assertTrue(c.closed)
            self.assertEqual(calls[-1][0], command)
            self.assertEqual(sum(k == 16 for k, _ in calls), 1)

    def test_snapshot_identity(self):
        c, calls = self.client()
        result = c.selection_status(
            expected_epoch=EPOCH, expected_nonce=NONCE, expected_id=2
        )
        self.assertEqual(result["revision"], 2)
        self.assertEqual(len({p[4:] for _, p in calls}), 1)
        self.assertTrue(any(calls[0][1][4:]))
        for kwargs in (
            {"expected_epoch": "08" + "00" * 15},
            {"expected_epoch": EPOCH, "expected_nonce": "02" + "00" * 15},
            {"expected_epoch": EPOCH, "expected_id": 3},
        ):
            c, calls = self.client()
            with self.assertRaises(w.ControlError):
                c.selection_status(**kwargs)
            self.assertTrue(c.closed)
            self.assertTrue(all(k == 15 for k, _ in calls))
        c, _ = self.client(torn=True)
        with self.assertRaises(w.ControlError):
            c.selection_status()

    def test_cli_validation(self):
        parser = argparse.ArgumentParser()
        w.add_arguments(parser)
        args = parser.parse_args(["default-set", "--pack-filename", NAME])
        with patch.object(
            w, "_credentials", side_effect=AssertionError("credential access")
        ) as credentials:
            with self.assertRaises(ValueError):
                w.run(args)
            credentials.assert_not_called()
        args = parser.parse_args(["default-status", "--pack-filename", NAME])
        with self.assertRaises(ValueError):
            self.codec().prepare(args)
        args = parser.parse_args(
            [
                "default-set",
                "--pack-filename",
                NAME,
                "--selection-epoch",
                EPOCH,
                "--selection-nonce",
                NONCE,
                "--expected-selection-id",
                "1",
                "--expected-default-revision",
                "1",
            ]
        )
        self.codec().prepare(args)


# The bridge replaces only TLS/USB transport. The client _exchange, native
# SDC1/selection/worker/store/render paths are production code.
from test_workbench_trial import WirePeerBase


class NativeTest(WirePeerBase):
    def peer_command(self):
        import json
        import os

        return json.loads(os.environ["SHANIU_PACK_TEST_PEER"])

    def test_lifecycle(self):
        c = self.c
        self.assertEqual(c.selection_status()["state"], "idle")
        before = self.command("stats")
        self.assertFalse(
            c.selection_request("refresh", EPOCH, NONCE, 0)["completion_verified"]
        )
        self.assertFalse(c.selection_status()["refresh_complete"])
        self.assertEqual(self.command("stats"), before)
        self.assertEqual(self.command("step"), "SELECT 0 2")
        refreshed = c.selection_status(expected_epoch=EPOCH, expected_nonce=NONCE)
        self.assertTrue(refreshed["refresh_complete"])
        self.assertEqual(refreshed["revision"], 1)
        nonce = "02" + "00" * 15
        self.assertTrue(
            c.selection_request("set", EPOCH, nonce, 1, 1, NAME)["accepted"]
        )
        self.assertFalse(c.selection_status()["device_reports_saved"])
        self.command("step")
        result = c.selection_status(
            expected_epoch=EPOCH, expected_nonce=nonce, expected_id=2
        )
        self.assertTrue(result["selection_complete"])
        self.assertEqual((result["revision"], result["filename"]), (2, NAME))
        stable = self.command("stats")
        c.selection_request("set", EPOCH, nonce, 1, 1, NAME)
        self.command("step")
        self.assertEqual(self.command("stats"), stable)
        with self.assertRaises(w.ControlError):
            c.selection_status(expected_epoch=EPOCH, expected_nonce=NONCE)
        self.assertEqual(self.command("stats"), stable)

    def test_cancel(self):
        c = self.c
        c.selection_request("set", EPOCH, NONCE, 0, 1, NAME)
        c.selection_request("cancel", EPOCH, "02" + "00" * 15, 1)
        result = c.selection_status()
        self.assertTrue(result["cancel_confirmed"])
        self.assertFalse(result["device_reports_saved"])
        self.assertEqual(self.command("step"), "SELECT 0 2")
        with self.assertRaises(w.ControlError):
            c.selection_request("set", EPOCH, NONCE, 0, 1, NAME)
        self.assertEqual(self.command("step"), "SELECT 0 2")

    def test_stale(self):
        c = self.c
        self.assertTrue(
            c.selection_request("set", EPOCH, NONCE, 0, 0, NAME)["accepted"]
        )
        self.assertEqual(self.command("step"), "SELECT 0 2")
        result = c.selection_status()
        self.assertEqual(result["state"], "failed")
        self.assertFalse(result["device_reports_saved"] or result["selection_complete"])

    def test_recover(self):
        c = self.c
        c.selection_request("set", EPOCH, NONCE, 0, 1, NAME)
        self.assertEqual(self.command("fail-unmount"), "FAULT_READY")
        self.command("step")
        result = c.selection_status()
        self.assertEqual(result["state"], "unknown")
        self.assertTrue(result["device_reports_saved"])
        self.assertFalse(result["device_reports_rendered"])
        c.selection_request("recover", EPOCH, "02" + "00" * 15, 1)
        self.assertTrue(c.selection_status()["recovery_pending"])
        self.command("step")
        result = c.selection_status()
        self.assertEqual(result["state"], "unknown")
        self.assertEqual(result["release_error"], 0)
        self.assertFalse(result["selection_complete"])
        c.selection_request("refresh", EPOCH, "03" + "00" * 15, 1)
        self.command("step")
        result = c.selection_status()
        self.assertTrue(result["refresh_complete"])
        self.assertEqual((result["revision"], result["filename"]), (2, NAME))
        self.assertFalse(
            result["device_reports_saved"] or result["device_reports_rendered"]
        )


if __name__ == "__main__":
    try:
        importlib.import_module("_lib.workbench_selection")
    except ImportError as error:
        print("BLOCKED_INTERFACE:", error)
        raise SystemExit(2)
    unittest.main()
