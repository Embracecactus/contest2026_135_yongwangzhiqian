#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Independent binary vectors and real OAEP; only OS protection is external."""
import hashlib
import importlib
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding
import test_workbench_client as peer

pairing = importlib.import_module("_lib.workbench_pairing")
profile = importlib.import_module("_lib.workbench_profile")


class PairingTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        peer.WorkbenchClientTest.setUpClass()

    @classmethod
    def tearDownClass(cls):
        peer.WorkbenchClientTest.tearDownClass()

    def setUp(self):
        from cryptography.hazmat.primitives.ciphers.aead import AESGCM

        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.req = self.root / "request.spq"
        self.pending = self.root / "pending.spp"
        self.dest = self.root / "device.spc"
        self.cipher = AESGCM(bytes(range(32)))

        def protect(data, *, decrypt=False):
            if decrypt:
                return bytearray(
                    self.cipher.decrypt(bytes(data[:12]), bytes(data[12:]), b"fixture")
                )
            nonce = os.urandom(12)
            return bytearray(
                nonce + self.cipher.encrypt(nonce, bytes(data), b"fixture")
            )

        self.protect = protect
        hook = patch.object(profile, "_protect", protect)
        hook.start()
        self.addCleanup(hook.stop)
        self.now = 1800000000000

    def start(self):
        return pairing.start(self.req, self.pending, 3, now_ms=self.now)

    def response(self, *, key=peer.PC_KEY, digest=None, caps=3):
        request = self.req.read_bytes()
        request_hash = hashlib.sha256(request).digest()
        public = serialization.load_der_public_key(request[60:])
        der = __import__("ssl").PEM_cert_to_DER_cert(peer.WorkbenchClientTest.pem)
        pin = hashlib.sha256(der).digest()
        message = (
            b"SPK1" + (digest or request_hash) + pin + key + struct.pack(">I", caps)
        )
        encrypted = public.encrypt(
            message,
            padding.OAEP(
                padding.MGF1(hashes.SHA256()),
                hashes.SHA256(),
                b"shaniu-pc-pair-v1" + request_hash,
            ),
        )
        return b"SPR1" + request_hash + struct.pack(">I", len(der)) + der + encrypted

    def test_request_layout_pending_protection_and_roundtrip(self):
        result = self.start()
        wire = self.req.read_bytes()
        self.assertEqual(wire[:4], b"SPQ1")
        self.assertEqual(
            struct.unpack(">IQQ", wire[4:24]), (3, self.now, self.now + 600000)
        )
        self.assertTrue(any(wire[24:40]))
        self.assertTrue(any(wire[40:56]))
        self.assertEqual(struct.unpack(">I", wire[56:60])[0], len(wire) - 60)
        public = serialization.load_der_public_key(wire[60:])
        self.assertEqual(public.key_size, 3072)
        self.assertEqual(public.public_numbers().e, 65537)
        self.assertEqual(result["request_sha256"], hashlib.sha256(wire).hexdigest())
        self.assertNotIn(wire, self.pending.read_bytes())
        pairing.finish(
            self.pending,
            self.response(),
            self.dest,
            peer.WorkbenchClientTest.pin,
            now_ms=self.now + 1,
        )
        with profile.use(self.dest) as (_, pin, key):
            self.assertEqual(
                (pin, bytes(key)), (peer.WorkbenchClientTest.pin, peer.PC_KEY)
            )

    def test_expiry_rollback_and_invalid_permissions_fail_without_profile(self):
        self.start()
        response = self.response()
        for now in [self.now - 1, self.now + 600000]:
            with self.assertRaises(pairing.PairingError):
                pairing.finish(
                    self.pending,
                    response,
                    self.dest,
                    peer.WorkbenchClientTest.pin,
                    now_ms=now,
                )
        self.assertFalse(self.dest.exists())
        for caps in [0, 32, -1]:
            with self.assertRaises(pairing.PairingError):
                pairing.start(
                    self.root / "new.req",
                    self.root / "new.pending",
                    caps,
                    now_ms=self.now,
                )
        self.assertFalse((self.root / "new.pending").exists())

    def test_tamper_wrong_request_caps_key_and_trusted_pin_rejected(self):
        self.start()
        valid = self.response()
        for response in [
            valid[:-1],
            valid + b"x",
            valid[:-1] + bytes([valid[-1] ^ 1]),
            self.response(digest=bytes(32)),
            self.response(caps=15),
            self.response(key=bytes(32)),
        ]:
            with self.assertRaises(pairing.PairingError):
                pairing.finish(
                    self.pending,
                    response,
                    self.dest,
                    peer.WorkbenchClientTest.pin,
                    now_ms=self.now,
                )
            self.assertFalse(self.dest.exists())
        with self.assertRaises(pairing.PairingError):
            pairing.finish(self.pending, valid, self.dest, "00" * 32, now_ms=self.now)
        self.assertFalse(self.dest.exists())

    def test_existing_outputs_are_not_replaced_and_import_does_not_authorize_device(
        self,
    ):
        self.start()
        before = self.pending.read_bytes()
        with self.assertRaises(pairing.PairingError):
            self.start()
        self.assertEqual(before, self.pending.read_bytes())
        result = pairing.finish(
            self.pending,
            self.response(),
            self.dest,
            peer.WorkbenchClientTest.pin,
            now_ms=self.now,
        )
        self.assertFalse(result["device_authorization_verified"])
        saved = self.dest.read_bytes()
        with self.assertRaises(pairing.PairingError):
            pairing.finish(
                self.pending,
                self.response(),
                self.dest,
                peer.WorkbenchClientTest.pin,
                now_ms=self.now,
            )
        self.assertEqual(saved, self.dest.read_bytes())

    def test_invalid_pending_or_unavailable_protection_never_exposes_private_material(
        self,
    ):
        self.pending.write_bytes(b"invalid")
        with self.assertRaises(pairing.PairingError):
            pairing.finish(
                self.pending, b"invalid", self.dest, "00" * 32, now_ms=self.now
            )
        self.assertFalse(self.dest.exists())
        self.pending.unlink()
        with patch.object(profile, "_protect", side_effect=OSError("unavailable")):
            with self.assertRaises(pairing.PairingError):
                self.start()
        self.assertFalse(self.pending.exists())
        self.assertFalse(self.req.exists())

    def test_cli_pairing_is_offline_and_rejects_mixed_credentials(self):
        import contextlib
        import io
        import json

        main = importlib.import_module("bk7258")
        begin = [
            "workbench",
            "pair-start",
            "--request",
            str(self.req),
            "--pending",
            str(self.pending),
            "--allow",
            "resources",
            "scenes",
        ]
        with patch.object(peer.workbench, "SerialChannel") as channel, patch.object(
            pairing.time, "time", return_value=self.now / 1000
        ):
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertNotEqual(main.main(begin + ["--port", "NATIVE"]), 0)
            self.assertFalse(self.pending.exists())
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                self.assertEqual(main.main(begin), 0)
            self.assertEqual(
                json.loads(output.getvalue())["request_sha256"],
                hashlib.sha256(self.req.read_bytes()).hexdigest(),
            )
            response = self.root / "response.spr"
            response.write_bytes(self.response())
            finish = [
                "workbench",
                "pair-finish",
                "--pending",
                str(self.pending),
                "--response",
                str(response),
                "--profile",
                str(self.dest),
                "--confirm-device-sha256",
                peer.WorkbenchClientTest.pin,
            ]
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertNotEqual(
                    main.main(finish + ["--pc-key-file", "must-not-read"]), 0
                )
            self.assertFalse(self.dest.exists())
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                self.assertEqual(main.main(finish), 0)
            self.assertFalse(
                json.loads(output.getvalue())["device_authorization_verified"]
            )
            self.assertNotIn(peer.PC_KEY.hex(), output.getvalue())
            channel.assert_not_called()

    def test_pending_readback_failure_never_publishes_request(self):
        def fail_readback(data, *, decrypt=False):
            return bytearray(b"wrong") if decrypt else self.protect(data)

        with patch.object(profile, "_protect", fail_readback):
            with self.assertRaises(pairing.PairingError):
                self.start()
        self.assertFalse(self.req.exists())
        self.assertTrue(self.pending.exists())
        self.assertNotIn(b"PRIVATE KEY", self.pending.read_bytes())


if __name__ == "__main__":
    if len(sys.argv) == 1:
        sys.argv.append("PairingTest")
    unittest.main()
