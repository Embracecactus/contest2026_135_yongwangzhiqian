#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Real SDC1 client -> production session/job/native worker/store.

TLS cases use fresh synthetic certificates and bridge plaintext to native stdio.
Physical USB, production PC grants and filesystem mounts remain separate gates.
"""
import argparse
import contextlib
import importlib
import json
import ssl
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch
import test_workbench_client as tls_fixture

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

    def reconnect_session(self):
        # Test harness boundary, not an SDC1 command or a USB unplug model.
        marker = b"PEER-RECONNECT!\n"
        self.write(marker)
        if self.read(len(marker)) != marker:
            raise AssertionError("Native peer did not reopen its control session")

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


class NativeTlsPeer(tls_fixture.TlsPeer):
    def __init__(self, certificate, key, native=None, drop_operation=None):
        super().__init__(certificate, key)
        self.native = native if native is not None else Pipe()
        self.owns_native = native is None
        self.drop_operation = drop_operation
        self.disconnected = False
        self.record = bytearray()
        self.operations = []

    def read(self, size):
        if self.disconnected:
            return b""
        return super().read(size)

    def step(self):
        if self.disconnected:
            return
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
            _, command, sequence, size = struct.unpack_from(">4I", self.plain)
            if len(self.plain) < 16 + size:
                return
            frame = bytes(self.plain[: 16 + size])
            del self.plain[: 16 + size]
            self.requests.append((command, sequence))
            if command == 16:
                self.record.clear()
            elif command == 17:
                self.record.extend(frame[16:])
            self.native.write(frame)
            response = self.native.read(40)
            if len(response) != 40:
                raise ValueError("Native session closed")
            if command == 18 and self.record[:4] == b"RJI1":
                operation = struct.unpack_from(">I", self.record, 4)[0]
                self.operations.append(bytes(self.record))
                if (
                    operation == self.drop_operation
                    and struct.unpack_from(">i", response, 16)[0] == 0
                ):
                    # Native worker accepted the operation; its TLS response is
                    # discarded and the client sees EOF, not a synthetic ACK.
                    self.disconnected = True
                    return
            self.tls.write(response)

    def close(self):
        if not self.closed and self.owns_native:
            self.native.close()
        super().close()


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
        tls_fixture.WorkbenchClientTest.setUpClass()
        try:
            with tempfile.TemporaryDirectory() as directory:
                a = args("resource-upload", Path(directory))
                a.port = "fixture-native"
                a.timeout = 10
                peer = NativeTlsPeer(
                    tls_fixture.WorkbenchClientTest.cert,
                    tls_fixture.WorkbenchClientTest.key,
                )

                @contextlib.contextmanager
                def material(unused):
                    yield tls_fixture.WorkbenchClientTest.pem, tls_fixture.WorkbenchClientTest.pin, bytearray(
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
            tls_fixture.WorkbenchClientTest.tearDownClass()

    def test_lost_ack_resume(self):
        # Keep the device worker/file/epoch alive, but replace both TLS and SDC1
        # sessions. Grant ownership and physical disconnect remain board gates.
        tls_fixture.WorkbenchClientTest.setUpClass()
        try:
            for drop_operation, outcome in (
                (1, "resume"),
                (2, "resume"),
                (3, "done"),
                (2, "canceled"),
                (2, "expired"),
            ):
                with self.subTest(drop=drop_operation, outcome=outcome):
                    self._reconnect_after_lost_ack(drop_operation, outcome)
        finally:
            tls_fixture.WorkbenchClientTest.tearDownClass()

    def _reconnect_after_lost_ack(self, drop_operation, outcome):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            native = Pipe()
            peers = []

            @contextlib.contextmanager
            def material(unused):
                yield tls_fixture.WorkbenchClientTest.pem, tls_fixture.WorkbenchClientTest.pin, bytearray(
                    b"\x2a" + bytes(31)
                )

            def run(operation, *, drop=None, observe=None):
                if peers:
                    native.reconnect_session()
                peer = NativeTlsPeer(
                    tls_fixture.WorkbenchClientTest.cert,
                    tls_fixture.WorkbenchClientTest.key,
                    native=native,
                    drop_operation=drop,
                )
                peers.append(peer)
                a = args(operation, root)
                a.port, a.timeout = "fixture-native", 10
                with patch.object(w, "SerialChannel", return_value=peer), patch.object(
                    w, "_credentials", material
                ):
                    return w.run(a, observe=observe)

            try:
                with self.assertRaises(w.ControlError):
                    run("resource-upload", drop=drop_operation)
                self.assertTrue(peers[0].disconnected)
                self.assertTrue(peers[0].closed)
                receipt_bytes = (root / "receipt.json").read_bytes()
                receipt = m.read_receipt(root / "receipt.json")
                time.sleep(0.05)
                before = run("resource-status")
                self.assertEqual(before["epoch"], receipt["epoch"])
                self.assertEqual(before["nonce"], receipt["nonce"])
                self.assertEqual(before["id"], receipt["previous_id"] + 1)
                self.assertLessEqual(before["remaining_ms"], receipt["ttl_ms"] - 40)
                self.assertEqual(peers[-1].operations, [])
                if outcome == "canceled":
                    terminal = run("resource-cancel")
                    self.assertEqual(terminal["state"], "canceled")
                    self.assertFalse(terminal["resources_held"])
                elif outcome == "expired":
                    # Let the original device TTL elapse. Reconnect/query do not
                    # renew it; no test clock replaces the real native worker.
                    time.sleep(before["remaining_ms"] / 1000 + 0.05)
                    terminal = run("resource-status")
                    self.assertEqual(terminal["state"], "canceled")
                    self.assertEqual(terminal["error"], -110)
                    self.assertFalse(terminal["resources_held"])
                observed = []
                if outcome in ("canceled", "expired"):
                    with self.assertRaises(ValueError):
                        run("resource-resume", observe=observed.append)
                    self.assertEqual(peers[-1].operations, [])
                else:
                    result = run("resource-resume", observe=observed.append)
                    self.assertTrue(result["installed"])
                    self.assertEqual(result["id"], before["id"])
                    self.assertEqual(result["written"], PACK.stat().st_size)
                    mutations = peers[-1].operations
                    self.assertNotIn(
                        1, [struct.unpack_from(">I", r, 4)[0] for r in mutations]
                    )
                    if outcome == "done":
                        self.assertEqual(mutations, [])
                    else:
                        self.assertEqual(observed[0]["state"], "receiving")
                        self.assertLessEqual(
                            observed[0]["remaining_ms"], before["remaining_ms"]
                        )
                        confirmed = observed[0]["written"]
                        self.assertEqual(confirmed, 4096 if drop_operation == 2 else 0)
                        self.assertEqual(
                            struct.unpack_from(">I", mutations[0], 48)[0], confirmed
                        )
                    self.assertFalse(result["resources_held"])
                for peer in peers[1:]:
                    for record in peer.operations:
                        self.assertNotEqual(struct.unpack_from(">I", record, 4)[0], 1)
                        self.assertEqual(struct.unpack_from(">I", record, 52)[0], 0)
                self.assertEqual((root / "receipt.json").read_bytes(), receipt_bytes)
                self.assertTrue(all(p.closed and p.handshaken for p in peers))
                self.assertTrue(all(p.requests[0] == (1, 0) for p in peers))
            finally:
                for peer in peers:
                    peer.close()
                native.close()

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
