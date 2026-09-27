#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Real client SD C1 -> production session/job/native worker/store, without TLS.

Only encryption/transport uses stdio. TLS certificate/PC authorization remain
separate existing gates; no real USB or device is opened here.
"""
import argparse
import importlib
import json
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools/bk7258"))
w = importlib.import_module("_lib.workbench")
m = importlib.import_module("_lib.workbench_resources")
PACK = ROOT / "tests/host/bk7258/build/shaniu-default-v1.bkep"


class Pipe:
    def __init__(self):
        self.process = subprocess.Popen(
            [
                str(ROOT / "tests/host/bk7258/build/test_display_job_control"),
                "peer",
                str(PACK),
            ],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

    def write(self, data):
        count = self.process.stdin.write(data)
        self.process.stdin.flush()
        return count

    def read(self, size):
        return self.process.stdout.read(size)

    def close(self):
        self.process.stdin.close()
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
            raise
        error = self.process.stderr.read().decode()
        self.process.stdout.close()
        self.process.stderr.close()
        if self.process.returncode:
            raise AssertionError(error)


def client():
    c = w.ControlClient.__new__(w.ControlClient)
    c.channel = Pipe()
    c._tls = c.channel
    c.closed = False
    c.authenticated = False
    c._sequence = 0
    c._pin = "ab" * 32
    c._timeout = 10
    c._clock = time.monotonic
    c._sleep = time.sleep
    c._last = c._clock()

    def call(fn, deadline):
        c._check(deadline)
        return fn()

    c._call = call
    c._exchange(1, b"\x2a" + bytes(31), c._now() + 10)
    c.authenticated = True
    return c


def args(op, root):
    return argparse.Namespace(
        operation=op,
        file=PACK if op in ("resource-upload", "resource-resume") else None,
        receipt=root / "receipt.json",
        ttl_ms=5000 if op == "resource-upload" else None,
    )


class ResourceFlow(unittest.TestCase):
    def test_cooperative_cancel_after_first_chunk(self):
        with tempfile.TemporaryDirectory() as directory:
            a = args("resource-upload", Path(directory))
            p, c = m.prepare(a), client()
            seen, commands = [], []
            real = c.resource_request

            def observe(snapshot):
                seen.append(dict(snapshot))
                # A UI observer cannot alter the authoritative transfer snapshot.
                snapshot["written"] = 0xFFFFFFFF

            def send(*values, **kwargs):
                commands.append(values[0])
                return real(*values, **kwargs)

            try:
                with patch.object(c, "resource_request", side_effect=send):
                    result = m.perform(
                        c,
                        a,
                        p,
                        observe=observe,
                        cancel_requested=lambda: bool(seen and seen[-1]["written"] > 0),
                    )
                self.assertEqual(result["state"], "canceled")
                self.assertFalse(result["installed"])
                self.assertEqual(commands.count("append"), 1)
                self.assertEqual(commands.count("cancel"), 1)
                self.assertNotIn("finish", commands)
                self.assertEqual(seen[-1]["state"], "canceled")
                self.assertEqual(c.resource_status()["state"], "canceled")
                self.assertTrue(a.receipt.is_file())
            finally:
                p.close()
                c.close()

    def test_cooperative_cancel_before_begin(self):
        with tempfile.TemporaryDirectory() as directory:
            a = args("resource-upload", Path(directory))
            p, c = m.prepare(a), client()
            try:
                with patch.object(
                    c, "resource_request", wraps=c.resource_request
                ) as send:
                    with self.assertRaisesRegex(ValueError, "not started"):
                        m.perform(c, a, p, cancel_requested=lambda: True)
                    send.assert_not_called()
                self.assertFalse(a.receipt.exists())
                self.assertEqual(c.resource_status()["state"], "idle")
            finally:
                p.close()
                c.close()

    def test_cooperative_terminal_progress_is_not_canceled(self):
        with tempfile.TemporaryDirectory() as directory:
            a = args("resource-upload", Path(directory))
            p, c = m.prepare(a), client()
            seen = []
            try:
                with patch.object(
                    c, "resource_request", wraps=c.resource_request
                ) as send:
                    result = m.perform(
                        c,
                        a,
                        p,
                        observe=lambda v: seen.append(v),
                        cancel_requested=lambda: bool(seen and seen[-1]["installed"]),
                    )
                self.assertTrue(result["installed"])
                self.assertEqual(seen[-1]["state"], "done")
                self.assertNotIn(
                    "cancel", [call.args[0] for call in send.call_args_list]
                )
                self.assertEqual(
                    [v["written"] for v in seen], sorted(v["written"] for v in seen)
                )
            finally:
                p.close()
                c.close()

    def test_cooperative_lost_cancel_ack_remains_unknown(self):
        with tempfile.TemporaryDirectory() as directory:
            a = args("resource-upload", Path(directory))
            p, c = m.prepare(a), client()
            seen, commands = [], []
            real = c.resource_request

            def send(*values, **kwargs):
                commands.append(values[0])
                result = real(*values, **kwargs)
                if values[0] == "cancel":
                    raise w.ControlError("Cancel ACK lost after actual acceptance")
                return result

            try:
                with patch.object(c, "resource_request", side_effect=send):
                    with self.assertRaises(w.ControlError):
                        m.perform(
                            c,
                            a,
                            p,
                            observe=lambda v: seen.append(v),
                            cancel_requested=lambda: bool(
                                seen and seen[-1]["written"] > 0
                            ),
                        )
                self.assertEqual(commands.count("cancel"), 1)
                self.assertNotEqual(seen[-1]["state"], "canceled")
                # Explicit independent read resolves the unknown, not local closure.
                self.assertEqual(c.resource_status()["state"], "canceled")
                self.assertTrue(a.receipt.is_file())
            finally:
                p.close()
                c.close()

    def test_upload(self):
        with tempfile.TemporaryDirectory() as directory:
            a = args("resource-upload", Path(directory))
            prepared = m.prepare(a)
            c = client()
            try:
                result = m.perform(c, a, prepared)
                self.assertEqual(result["state"], "done")
                self.assertTrue(result["installed"])
                self.assertEqual(result["written"], PACK.stat().st_size)
                receipt = m.read_receipt(a.receipt)
                self.assertEqual(receipt["sha256"], prepared.sha256)
                self.assertNotIn("key", receipt)
                query = args("resource-status", Path(directory))
                q = m.prepare(query)
                try:
                    self.assertTrue(m.perform(c, query, q)["installed"])
                finally:
                    q.close()
            finally:
                prepared.close()
                c.close()

    def test_cli_tls_upload(self):
        self._cli_tls_transfer(False)

    def test_cli_tls_cooperative_cancel(self):
        self._cli_tls_transfer(True)

    def _cli_tls_transfer(self, cancel):
        import contextlib
        import ssl
        import struct
        from test_workbench_client import TlsPeer, WorkbenchClientTest

        class NativeTlsPeer(TlsPeer):
            def __init__(self, certificate, key):
                super().__init__(certificate, key)
                self.native = Pipe()

            def step(self):
                if not self.handshaken:
                    try:
                        self.tls.do_handshake()
                        self.handshaken = True
                    except ssl.SSLWantReadError:
                        return
                try:
                    self.plain.extend(self.tls.read(4096))
                except ssl.SSLWantReadError:
                    return
                while len(self.plain) >= 16:
                    size = struct.unpack_from(">I", self.plain, 12)[0]
                    if len(self.plain) < 16 + size:
                        return
                    frame = bytes(self.plain[: 16 + size])
                    del self.plain[: 16 + size]
                    self.native.write(frame)
                    response = self.native.read(40)
                    if len(response) != 40:
                        raise ValueError("Native session closed")
                    self.tls.write(response)

            def close(self):
                if not self.closed:
                    self.native.close()
                super().close()

        WorkbenchClientTest.setUpClass()
        try:
            with tempfile.TemporaryDirectory() as directory:
                a = args("resource-upload", Path(directory))
                a.port = "fixture-native"
                a.timeout = 10
                peer = NativeTlsPeer(WorkbenchClientTest.cert, WorkbenchClientTest.key)

                @contextlib.contextmanager
                def material(unused):
                    yield WorkbenchClientTest.pem, WorkbenchClientTest.pin, bytearray(
                        b"\x2a" + bytes(31)
                    )

                try:
                    with patch.object(
                        w, "SerialChannel", return_value=peer
                    ), patch.object(w, "_credentials", material):
                        seen = []
                        if cancel:
                            result = w.run(
                                a,
                                observe=lambda value: seen.append(value),
                                cancel_requested=lambda: bool(
                                    seen and seen[-1]["written"] > 0
                                ),
                            )
                        else:
                            result = w.run(a)
                    self.assertEqual(result["installed"], not cancel)
                    self.assertEqual(result["state"], "canceled" if cancel else "done")
                    self.assertTrue(peer.closed)
                finally:
                    peer.close()
        finally:
            WorkbenchClientTest.tearDownClass()

    def test_lost_ack_resume(self):
        with tempfile.TemporaryDirectory() as directory:
            a = args("resource-upload", Path(directory))
            prepared = m.prepare(a)
            c = client()
            real = c.resource_request

            def lose(*values, **kwargs):
                answer = real(*values, **kwargs)
                if values[0] == "append":
                    raise w.ControlError("ACK lost after real acceptance")
                return answer

            try:
                with patch.object(
                    c, "resource_request", side_effect=lose
                ), self.assertRaises(w.ControlError):
                    m.perform(c, a, prepared)
                self.assertTrue(a.receipt.is_file())
                # This peer retains the real native worker. A new client operation
                # re-queries authoritative written progress, never re-BEGINs.
                resume = args("resource-resume", Path(directory))
                p = m.prepare(resume)
                seen = []

                def observed(*values, **kwargs):
                    seen.append(values[0])
                    return real(*values, **kwargs)

                try:
                    with patch.object(c, "resource_request", side_effect=observed):
                        result = m.perform(c, resume, p)
                    self.assertTrue(result["installed"])
                    self.assertNotIn("begin", seen)
                finally:
                    p.close()
            finally:
                prepared.close()
                c.close()

    def test_receipt_precedes_begin(self):
        with tempfile.TemporaryDirectory() as directory:
            a = args("resource-upload", Path(directory))
            p = m.prepare(a)
            c = client()
            try:
                with patch.object(
                    m, "create_receipt", side_effect=OSError("disk full")
                ), patch.object(c, "resource_request") as send:
                    with self.assertRaises(OSError):
                        m.perform(c, a, p)
                    send.assert_not_called()
                self.assertEqual(c.resource_status()["state"], "idle")
            finally:
                p.close()
                c.close()

    def test_unknown_epoch_no_replay(self):
        with tempfile.TemporaryDirectory() as directory:
            a = args("resource-upload", Path(directory))
            p = m.prepare(a)
            c = client()
            try:
                m.perform(c, a, p)
            finally:
                p.close()
                c.close()
            # Fresh native process cannot claim the old volatile receipt.
            resume = args("resource-resume", Path(directory))
            p = m.prepare(resume)
            c = client()
            try:
                with patch.object(c, "resource_request") as send, self.assertRaises(
                    ValueError
                ):
                    m.perform(c, resume, p)
                send.assert_not_called()
            finally:
                p.close()
                c.close()

    def test_cancel_and_no_default(self):
        with tempfile.TemporaryDirectory() as directory:
            a = args("resource-upload", Path(directory))
            p = m.prepare(a)
            c = client()
            real = c.resource_request

            def stop(*values, **kwargs):
                if values[0] == "append":
                    raise w.ControlError("stop before append")
                return real(*values, **kwargs)

            try:
                with patch.object(
                    c, "resource_request", side_effect=stop
                ), self.assertRaises(w.ControlError):
                    m.perform(c, a, p)
                cancel = args("resource-cancel", Path(directory))
                q = m.prepare(cancel)
                try:
                    result = m.perform(c, cancel, q)
                    self.assertEqual(result["state"], "canceled")
                    self.assertFalse(result["installed"])
                    self.assertFalse(result["resources_held"])
                finally:
                    q.close()
            finally:
                p.close()
                c.close()

    def test_non_pack_rejected_before_connect(self):
        with tempfile.TemporaryDirectory() as directory:
            a = args("resource-upload", Path(directory))
            a.file = Path(directory) / "not-a-pack"
            a.file.write_bytes(b"not-an-eye-pack" * 20)
            with patch.object(w, "SerialChannel") as channel, patch.object(
                w, "_credentials"
            ) as creds:
                with self.assertRaises(ValueError):
                    w.run(a)
                channel.assert_not_called()
                creds.assert_not_called()

    def test_changed_file_and_existing_receipt(self):
        with tempfile.TemporaryDirectory() as directory:
            a = args("resource-upload", Path(directory))
            p = m.prepare(a)
            c = client()
            try:
                m.perform(c, a, p)
            finally:
                p.close()
                c.close()
            with self.assertRaises(ValueError):
                m.prepare(a)
            resume = args("resource-resume", Path(directory))
            bad = Path(directory) / "changed.bkep"
            bad.write_bytes(bytes(128))
            resume.file = bad
            with self.assertRaises(ValueError):
                m.prepare(resume)


if __name__ == "__main__":
    unittest.main()
