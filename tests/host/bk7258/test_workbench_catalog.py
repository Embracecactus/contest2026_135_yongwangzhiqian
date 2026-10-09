#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Independent ECC1/ECL1 vectors and real ControlClient message sequencing."""
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
EPOCH = "07" + "00" * 15
NONCE = "01" + "00" * 15


def snapshot():
    out = bytearray(608)
    out[:4] = b"ECL1"
    struct.pack_into(">I", out, 4, 6)
    out[8:24] = bytes.fromhex(EPOCH)
    struct.pack_into(">IiiI", out, 24, 2, 0, 0, 5)
    out[40:56] = bytes.fromhex(NONCE)
    struct.pack_into(">QI", out, 56, 9, 1)
    out[96:102] = b"a.bkep"
    out[136:142] = b"pack-a"
    struct.pack_into(">IHHHHH", out, 168, 1, 1, 160, 160, 2, 2)
    struct.pack_into(">I", out, 184, 10494)
    out[188:220] = bytes(range(32))
    return out


class CatalogTest(unittest.TestCase):
    def codec(self):
        return importlib.import_module("_lib.workbench_catalog")

    def client(self, data=None, fault=None, torn=False):
        c = w.ControlClient.__new__(w.ControlClient)
        c.closed = False
        c.authenticated = True
        c._now = lambda: 1.0
        c._timeout = 10
        c.channel = type("Channel", (), {"close": lambda s: None})()
        calls = []
        data = snapshot() if data is None else data

        def exchange(cmd, payload, deadline):
            calls.append((cmd, payload))
            if cmd == fault:
                raise w.ControlError("lost response")
            if cmd == 15:
                self.assertEqual(len(payload), 20)
                word = struct.unpack_from(">I", payload)[0]
                self.assertEqual(word >> 16, 18)
                part = data[word & 65535 : (word & 65535) + 16]
                if torn and len(calls) > 38:
                    part = bytes(16)
                return (608, *struct.unpack(">4I", part))
            return (0, 0, 0, 0, 0)

        c._exchange = exchange
        return c, calls

    def test_golden(self):
        expected = (
            b"ECC1"
            + struct.pack(">I", 1)
            + bytes.fromhex(EPOCH + NONCE)
            + struct.pack(">I", 2)
            + bytes(12)
            + b"a.bkep".ljust(40, b"\0")
        )
        self.assertEqual(
            self.codec().encode("page", EPOCH, NONCE, 2, "a.bkep"), expected
        )
        for action, number in (("cancel", 2), ("recover", 3)):
            expected = (
                b"ECC1"
                + struct.pack(">I", number)
                + bytes.fromhex(EPOCH + NONCE)
                + struct.pack(">I", 2)
                + bytes(52)
            )
            self.assertEqual(self.codec().encode(action, EPOCH, NONCE, 2), expected)
        for args in [
            ("page", EPOCH, NONCE, True),
            ("page", EPOCH, NONCE, 0, "../a.bkep"),
            ("cancel", EPOCH, NONCE, 0),
            ("recover", EPOCH, NONCE, 1, "a.bkep"),
        ]:
            with self.assertRaises(ValueError):
                self.codec().encode(*args)

    def test_decode(self):
        v = self.codec().decode(snapshot())
        self.assertTrue(v["page_available"])
        self.assertFalse(v["more"])
        self.assertEqual(v["entries"][0]["filename"], "a.bkep")
        self.assertEqual(v["entries"][0]["source_sha256"], bytes(range(32)).hex())
        self.assertNotIn("file_sha256", v["entries"][0])
        self.assertEqual(v["entries"][0]["total_bytes"], 10494)
        self.assertEqual(v["next_cursor"], None)
        empty = snapshot()
        struct.pack_into(">I", empty, 64, 0)
        empty[96:] = bytes(512)
        self.assertEqual(self.codec().decode(empty)["entries"], [])
        pending = bytearray(empty)
        struct.pack_into(">I", pending, 4, 1)
        struct.pack_into(">I", pending, 36, 1)
        self.assertFalse(self.codec().decode(pending)["page_available"])

    def test_malformed(self):
        for offset, value in (
            (0, 0),
            (7, 255),
            (39, 4),
            (39, 13),
            (67, 5),
            (68, 1),
            (135, 1),
            (182, 1),
            (224, 1),
            (32, 1),
        ):
            data = snapshot()
            data[offset] = value
            with self.subTest(offset=offset, value=value), self.assertRaises(
                ValueError
            ):
                self.codec().decode(data)
        for state in (1, 7, 8, 9):
            data = snapshot()
            struct.pack_into(">I", data, 4, state)
            with self.assertRaises(ValueError):
                self.codec().decode(data)
        with self.assertRaises(ValueError):
            self.codec().decode(snapshot()[:-1])

    def test_page_order_and_cursor(self):
        data = snapshot()
        struct.pack_into(">I", data, 36, 13)
        struct.pack_into(">I", data, 64, 4)
        entry = bytes(data[96:224])
        for index, letter in enumerate("abcd"):
            start = 96 + index * 128
            data[start : start + 128] = entry
            data[start] = ord(letter)
        value = self.codec().decode(data)
        self.assertEqual(value["next_cursor"], "d.bkep")
        self.assertTrue(value["more"])
        for letter in "az":
            bad = bytearray(data)
            bad[224] = ord(letter)
            with self.assertRaises(ValueError):
                self.codec().decode(bad)

    def test_staging(self):
        c, calls = self.client()
        result = c.catalog_request("page", EPOCH, NONCE, 2)
        self.assertEqual([x[0] for x in calls], [16, 17, 17, 17, 18])
        self.assertEqual(calls[0][1], struct.pack(">II", 18, 96))
        self.assertEqual(b"".join(x[1] for x in calls[1:4])[:4], b"ECC1")
        self.assertTrue(result["accepted"])
        self.assertFalse(result["completion_verified"])
        for cmd in (16, 17, 18):
            c, calls = self.client(fault=cmd)
            with self.assertRaises(w.ControlError):
                c.catalog_request("page", EPOCH, NONCE, 2)
            self.assertTrue(c.closed)
            self.assertEqual(sum(x[0] == 16 for x in calls), 1)

    def test_identity_and_snapshot(self):
        c, calls = self.client()
        value = c.catalog_status(
            expected_epoch=EPOCH, expected_nonce=NONCE, expected_id=2
        )
        self.assertTrue(value["page_available"])
        self.assertEqual(len(calls), 39)
        self.assertEqual(len({p[4:] for _, p in calls}), 1)
        for options in (
            {"expected_epoch": "08" + "00" * 15},
            {"expected_epoch": EPOCH, "expected_nonce": "02" + "00" * 15},
            {"expected_epoch": EPOCH, "expected_id": 3},
        ):
            c, calls = self.client()
            with self.assertRaises(w.ControlError):
                c.catalog_status(**options)
            self.assertTrue(c.closed)
            self.assertTrue(all(cmd == 15 for cmd, _ in calls))
        c, _ = self.client(torn=True)
        with self.assertRaises(w.ControlError):
            c.catalog_status()

    def test_validation_before_credentials(self):
        parser = argparse.ArgumentParser()
        w.add_arguments(parser)
        for argv in (
            ["catalog-page"],
            ["catalog-status", "--file", "private.bkep"],
            ["catalog-cancel", "--catalog-after", "a.bkep"],
        ):
            args = parser.parse_args(argv)
            with patch.object(
                w, "_credentials", side_effect=AssertionError("credential access")
            ) as credentials:
                with self.assertRaises(ValueError):
                    w.run(args)
                credentials.assert_not_called()


if __name__ == "__main__":
    try:
        importlib.import_module("_lib.workbench_catalog")
    except ImportError as error:
        print("BLOCKED_INTERFACE:", error)
        raise SystemExit(2)
    unittest.main()
