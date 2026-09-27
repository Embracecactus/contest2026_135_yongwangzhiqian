#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Production PC client against an external TLS peer; no physical serial I/O."""
import contextlib
import io
import json
from types import SimpleNamespace
from unittest.mock import patch
import hashlib
import importlib
import os
from pathlib import Path
import ssl
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools/bk7258"))
workbench = importlib.import_module("_lib.workbench")
PC_KEY = bytes([84]) + bytes(31)


class TlsPeer:
    def __init__(self, certificate, key, fault=None):
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(certificate, key)
        self.incoming = ssl.MemoryBIO()
        self.outgoing = ssl.MemoryBIO()
        self.tls = context.wrap_bio(self.incoming, self.outgoing, server_side=True)
        self.handshaken = False
        self.requests = []
        self.plain = bytearray()
        self.fault = fault
        self.closed = False
        self.partial = 7
        self.tick = 100.0
        self.sent = 0

    def clock(self):
        return self.tick

    def sleep(self, seconds):
        self.tick += seconds

    def write(self, data):
        if self.fault == "blocked_write":
            return 0
        count = min(self.partial, len(data))
        self.sent += count
        self.incoming.write(bytes(data[:count]))
        self.step()
        return count

    def read(self, size):
        self.step()
        if self.fault == "eof" and self.handshaken:
            return b""
        if self.fault == "stall" and self.requests:
            return None
        return self.outgoing.read(min(size, self.partial)) or None

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
            magic, command, sequence, size = struct.unpack(">4I", self.plain[:16])
            if len(self.plain) < 16 + size:
                return
            payload = bytes(self.plain[16 : 16 + size])
            del self.plain[: 16 + size]
            assert magic == 0x53444331
            self.requests.append((command, sequence, payload))
            error = -13 if command == 1 and payload != PC_KEY else 0
            flags = 0 if command == 1 else 9
            values = (0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0)
            if command == 2:
                values = (37, 0xFFFFFFFF, 0xFFFFFFFF, 0)
            if command == 9:
                flags, values = 0, (7, 14, 123, 661)
            if self.fault == "wrong_sequence" and command == 2:
                sequence += 1
            if self.fault == "bad_flags" and command == 2:
                flags |= 0x8000
            if self.fault == "bad_volume" and command == 2:
                values = (101, 0xFFFFFFFF, 0xFFFFFFFF, 0)
            if self.fault == "positive_error" and command == 2:
                error = 1
            if self.fault == "denied" and command == 2:
                error, flags, values = -16, 0, (0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0)
            frame = struct.pack(
                ">4Ii5I",
                magic,
                command | 0x80000000,
                sequence,
                24,
                error,
                flags,
                *values
            )
            self.tls.write(frame)

    def close(self):
        self.closed = True


class WorkbenchClientTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="shaniu-pc-client-")
        root = Path(cls.temp.name)
        cls.cert, cls.key = root / "cert.pem", root / "key.pem"
        subprocess.run(
            [
                "openssl",
                "req",
                "-x509",
                "-newkey",
                "ec",
                "-pkeyopt",
                "ec_paramgen_curve:P-256",
                "-nodes",
                "-subj",
                "/CN=synthetic-pc-peer",
                "-days",
                "2",
                "-addext",
                "basicConstraints=critical,CA:TRUE",
                "-keyout",
                cls.key,
                "-out",
                cls.cert,
            ],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        cls.alt_cert, cls.alt_key = root / "alt.pem", root / "alt-key.pem"
        csr = root / "alt.csr"
        subprocess.run(
            [
                "openssl",
                "req",
                "-new",
                "-newkey",
                "ec",
                "-pkeyopt",
                "ec_paramgen_curve:P-256",
                "-nodes",
                "-subj",
                "/CN=other-leaf",
                "-keyout",
                cls.alt_key,
                "-out",
                csr,
            ],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        subprocess.run(
            [
                "openssl",
                "x509",
                "-req",
                "-in",
                csr,
                "-CA",
                cls.cert,
                "-CAkey",
                cls.key,
                "-set_serial",
                "2",
                "-days",
                "2",
                "-out",
                cls.alt_cert,
            ],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        cls.pem = cls.cert.read_text()
        cls.pin = hashlib.sha256(ssl.PEM_cert_to_DER_cert(cls.pem)).hexdigest()
        print(
            json.dumps(
                {
                    "fixture": "synthetic-workbench-tls",
                    "certificate_sha256": cls.pin,
                    "alternate_certificate_sha256": hashlib.sha256(
                        ssl.PEM_cert_to_DER_cert(cls.alt_cert.read_text())
                    ).hexdigest(),
                    "openssl": ssl.OPENSSL_VERSION,
                }
            ),
            flush=True,
        )

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def client(self, fault=None, pin=None):
        peer = TlsPeer(self.cert, self.key, fault)
        client = workbench.ControlClient(
            peer,
            self.pem,
            pin or self.pin,
            timeout=1,
            clock=peer.clock,
            sleep=peer.sleep,
        )
        return client, peer

    def test_fragmented_tls_auth_status_and_info(self):
        client, peer = self.client()
        key = bytearray(PC_KEY)
        client.start(key)
        self.assertEqual(key, PC_KEY)  # Borrowed caller storage is not destroyed.
        self.assertEqual(client.status()["volume"], 37)
        self.assertEqual(client.info()["security_counter"], 661)
        self.assertEqual(
            [(r[0], r[1]) for r in peer.requests], [(1, 0), (2, 1), (9, 2)]
        )
        client.close()
        self.assertTrue(peer.closed)
        self.assertFalse(client.authenticated)

    def test_certificate_pin_mismatch_sends_no_secret(self):
        peer = TlsPeer(self.cert, self.key)
        with self.assertRaises(workbench.ControlError):
            client = workbench.ControlClient(peer, self.pem, "00" * 32)
            client.start(PC_KEY)
        self.assertEqual(peer.requests, [])
        self.assertEqual(peer.sent, 0)

    def test_valid_chain_with_wrong_leaf_never_receives_pc_key(self):
        peer = TlsPeer(self.alt_cert, self.alt_key)
        client = workbench.ControlClient(peer, self.pem, self.pin)
        with self.assertRaises(workbench.ControlError):
            client.start(PC_KEY)
        self.assertTrue(
            peer.handshaken
        )  # Chain valid; the leaf pin is the rejection boundary.
        self.assertEqual(peer.requests, [])
        self.assertTrue(peer.closed)

    def test_wrong_principal_closes_without_status(self):
        client, peer = self.client()
        with self.assertRaises(workbench.ControlError):
            client.start(bytes([42]) + bytes(31))
        self.assertTrue(peer.closed)
        self.assertEqual([r[0] for r in peer.requests], [1])
        with self.assertRaises(workbench.ControlError):
            client.status()

    def test_malformed_authenticated_responses_close(self):
        for fault in ("wrong_sequence", "bad_flags", "bad_volume", "positive_error"):
            with self.subTest(fault=fault):
                client, peer = self.client(fault)
                client.start(PC_KEY)
                with self.assertRaises(workbench.ControlError):
                    client.status()
                self.assertTrue(peer.closed)
                self.assertEqual(len(peer.requests), 2)

    def test_stall_and_backpressure_have_absolute_deadlines(self):
        for fault in ("blocked_write", "stall"):
            with self.subTest(fault=fault):
                client, peer = self.client(fault)
                with self.assertRaises(workbench.ControlError):
                    client.start(PC_KEY)
                self.assertLessEqual(peer.tick, 101.01)
                self.assertTrue(peer.closed)
                self.assertLessEqual(len(peer.requests), 1)

    def test_error_response_is_not_success_or_replayed(self):
        client, peer = self.client("denied")
        client.start(PC_KEY)
        with self.assertRaises(workbench.ControlError):
            client.status()
        self.assertEqual([r[0] for r in peer.requests], [1, 2])

    def test_no_request_before_auth_or_after_close(self):
        client, peer = self.client()
        with self.assertRaises(workbench.ControlError):
            client.status()
        self.assertEqual(peer.sent, 0)
        client.close()
        with self.assertRaises(workbench.ControlError):
            client.start(PC_KEY)

    def test_cli_uses_real_client_without_printing_credentials(self):
        cli = importlib.import_module("bk7258")
        peer = TlsPeer(self.cert, self.key)
        key_path = Path(self.temp.name) / "pc.bin"
        key_path.write_bytes(PC_KEY)
        key_path.chmod(0o600)
        output = io.StringIO()
        with patch.object(
            workbench, "SerialChannel", return_value=peer
        ), contextlib.redirect_stdout(output):
            result = cli.main(
                [
                    "workbench",
                    "status",
                    "--port",
                    "fixture-native",
                    "--certificate",
                    str(self.cert),
                    "--certificate-sha256",
                    self.pin,
                    "--pc-key-file",
                    str(key_path),
                ]
            )
        self.assertEqual(result, 0)
        self.assertEqual(json.loads(output.getvalue())["volume"], 37)
        self.assertNotIn(PC_KEY.hex(), output.getvalue())
        self.assertTrue(peer.closed)

    def test_native_adapter_preserves_partial_io_without_global_output_redirect(self):
        from unittest.mock import Mock

        port = SimpleNamespace(device="fixture-native", vid=0x1209, pid=0x0001)
        raw = Mock()
        raw.read.return_value = b""
        raw.write.return_value = 2
        original = sys.stdout
        with patch.object(
            workbench.deploy_usb.list_ports, "comports", return_value=[port]
        ), patch.object(
            workbench.deploy_usb, "open_native_port", autospec=True, return_value=raw
        ) as opened:
            channel = workbench.SerialChannel("fixture-native", 1)
            opened.assert_called_once_with(
                "fixture-native", 1, label="USB control", output=sys.stderr
            )
            self.assertIs(sys.stdout, original)
            self.assertIsNone(channel.read(4096))
            self.assertEqual(channel.write(b"abcd"), 2)
            channel.close()
            raw.close.assert_called_once()

    def test_native_port_filter_rejects_uart_without_opening(self):
        port = SimpleNamespace(device="fixture-UART", vid=0x1A86, pid=0x7523)
        with patch.object(
            workbench.deploy_usb.list_ports, "comports", return_value=[port]
        ), patch.object(workbench.deploy_usb, "open_native_port") as opened:
            with self.assertRaises(workbench.ControlError):
                workbench.SerialChannel("fixture-UART", 1)
            opened.assert_not_called()

    def test_cli_invalid_certificate_never_opens_port(self):
        key_path = Path(self.temp.name) / "pc.bin"
        key_path.write_bytes(PC_KEY)
        args = SimpleNamespace(
            pc_key_file=key_path,
            certificate=self.cert,
            certificate_sha256="00" * 32,
            port="fixture",
            timeout=1,
            operation="status",
        )
        with patch.object(workbench, "SerialChannel") as opened:
            with self.assertRaises(workbench.ControlError):
                workbench.run(args)
            opened.assert_not_called()

    def test_clock_rollback_terminates_without_replay(self):
        client, peer = self.client()
        client.start(PC_KEY)
        peer.tick = 99
        with self.assertRaises(workbench.ControlError):
            client.status()
        self.assertTrue(peer.closed)
        self.assertEqual([r[0] for r in peer.requests], [1])


def pc_interop(executable, certificate, private_key):
    class PcPeerTest(unittest.TestCase):
        def exercise(self, key, accepted):
            with tempfile.TemporaryDirectory(prefix="pc-grant-peer-") as directory:
                process = subprocess.Popen(
                    [executable, "--pc-peer", certificate, private_key, directory],
                    stdin=subprocess.PIPE,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                )
                os.set_blocking(process.stdin.fileno(), False)
                os.set_blocking(process.stdout.fileno(), False)

                class PipeChannel:
                    def write(self, data):
                        try:
                            return os.write(process.stdin.fileno(), bytes(data[:7]))
                        except BlockingIOError:
                            return 0

                    def read(self, size):
                        try:
                            return os.read(process.stdout.fileno(), min(size, 11))
                        except BlockingIOError:
                            return None

                    def close(self):
                        process.stdin.close()

                pem = Path(certificate).read_text()
                pin = hashlib.sha256(ssl.PEM_cert_to_DER_cert(pem)).hexdigest()
                client = workbench.ControlClient(PipeChannel(), pem, pin)
                try:
                    if accepted:
                        client.start(key)
                        self.assertTrue(client.status()["ready"])
                        self.assertIsNone(client.status()["volume"])
                        self.assertEqual(client.info()["security_counter"], 661)
                    else:
                        with self.assertRaises(workbench.ControlError):
                            client.start(key)
                        self.assertFalse(client.authenticated)
                finally:
                    client.close()
                    try:
                        result = process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                        raise
                    details = process.stderr.read().decode(errors="replace")
                    process.stdout.close()
                    process.stderr.close()
                self.assertEqual(result, 0 if accepted else 4, details)

        def test_status_real_pc_principal(self):
            self.exercise(PC_KEY, True)

        def test_phone_owner_key_rejected(self):
            self.exercise(bytes([42]) + bytes(31), False)

    result = unittest.TextTestRunner(verbosity=2).run(
        unittest.defaultTestLoader.loadTestsFromTestCase(PcPeerTest)
    )
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    if len(sys.argv) == 5 and sys.argv[1] == "--pc-peer":
        sys.exit(pc_interop(*sys.argv[2:]))
    unittest.main()
