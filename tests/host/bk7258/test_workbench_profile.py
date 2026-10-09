#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Real profile storage and client; only OS protection/serial are external peers."""
import contextlib
import hashlib
import importlib
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

from cryptography.hazmat.primitives.ciphers.aead import AESGCM
import test_workbench_client as peer_test

PC_KEY = peer_test.PC_KEY
workbench = peer_test.workbench

profile = importlib.import_module("_lib.workbench_profile")


class ProfileTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        peer_test.WorkbenchClientTest.setUpClass()
        cls.pem = peer_test.WorkbenchClientTest.pem
        cls.pin = peer_test.WorkbenchClientTest.pin

    @classmethod
    def tearDownClass(cls):
        peer_test.WorkbenchClientTest.tearDownClass()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "device.spc"
        # External OS primitive only; production file format/validation/CLI remain real.
        self.cipher = AESGCM(bytes(range(32)))
        self.mock = patch.object(profile, "_protect", self.protect)
        self.mock.start()
        self.addCleanup(self.mock.stop)

    def protect(self, data, *, decrypt=False):
        if decrypt:
            return bytearray(
                self.cipher.decrypt(data[:12], bytes(data[12:]), b"fixture")
            )
        nonce = os.urandom(12)
        return bytearray(nonce + self.cipher.encrypt(nonce, bytes(data), b"fixture"))

    def test_profile_binds_pin_certificate_key_and_clears_borrowed_plaintext(self):
        key = bytearray(PC_KEY)
        profile.create(self.path, self.pem, self.pin, key)
        self.assertEqual(key, PC_KEY)
        wire = self.path.read_bytes()
        self.assertNotIn(PC_KEY, wire)
        self.assertNotIn(self.pem.encode(), wire)
        with profile.use(self.path) as (pem, pin, borrowed):
            self.assertEqual((pem, pin, bytes(borrowed)), (self.pem, self.pin, PC_KEY))
        self.assertEqual(borrowed, bytes(32))
        with self.assertRaises(RuntimeError):
            with profile.use(self.path) as (_, _, borrowed):
                raise RuntimeError("consumer failed")
        self.assertEqual(borrowed, bytes(32))

    def test_tampered_truncated_and_unknown_profiles_fail_closed(self):
        profile.create(self.path, self.pem, self.pin, PC_KEY)
        wire = self.path.read_bytes()
        for invalid in [
            b"",
            wire[:12],
            b"BAD1" + wire[4:],
            wire[:-1] + bytes([wire[-1] ^ 1]),
            wire + b"x",
        ]:
            self.path.write_bytes(invalid)
            with self.assertRaises(profile.ProfileError):
                with profile.use(self.path):
                    self.fail("invalid profile exposed plaintext")

    def test_existing_profile_and_symlink_are_never_overwritten(self):
        profile.create(self.path, self.pem, self.pin, PC_KEY)
        before = self.path.read_bytes()
        with self.assertRaises(profile.ProfileError):
            profile.create(self.path, self.pem, self.pin, bytes([1]) * 32)
        self.assertEqual(before, self.path.read_bytes())
        link = self.path.with_suffix(".link")
        link.symlink_to(self.path)
        with self.assertRaises(profile.ProfileError):
            with profile.use(link):
                self.fail("symlink was accepted")
        self.assertEqual(before, self.path.read_bytes())

    def test_invalid_material_and_os_failure_never_create_profile(self):
        for pem, pin, key in [
            (self.pem, "00" * 32, PC_KEY),
            (self.pem, self.pin, bytes(32)),
            (self.pem, self.pin, b"short"),
        ]:
            with self.assertRaises(profile.ProfileError):
                profile.create(self.path, pem, pin, key)
            self.assertFalse(self.path.exists())
        with patch.object(profile, "_protect", side_effect=OSError("OS unavailable")):
            with self.assertRaises(profile.ProfileError):
                profile.create(self.path, self.pem, self.pin, PC_KEY)
        self.assertFalse(self.path.exists())

    def test_cli_profile_reaches_real_tls_client_without_plaintext_file(self):
        profile.create(self.path, self.pem, self.pin, PC_KEY)
        peer = peer_test.TlsPeer(
            peer_test.WorkbenchClientTest.cert, peer_test.WorkbenchClientTest.key
        )
        main = importlib.import_module("bk7258")
        output = io.StringIO()
        with patch.object(
            workbench, "SerialChannel", return_value=peer
        ), contextlib.redirect_stdout(output):
            code = main.main(
                ["workbench", "status", "--port", "NATIVE", "--profile", str(self.path)]
            )
        self.assertEqual(code, 0)
        self.assertEqual(json.loads(output.getvalue())["volume"], 37)
        self.assertEqual([r[0] for r in peer.requests], [1, 2])
        self.assertNotIn(PC_KEY.hex(), output.getvalue())
        self.assertEqual(list(Path(self.temp.name).iterdir()), [self.path])

    def test_cli_invalid_profile_never_opens_port_or_falls_back(self):
        self.path.write_bytes(b"invalid")
        main = importlib.import_module("bk7258")
        with patch.object(
            workbench, "SerialChannel"
        ) as channel, contextlib.redirect_stderr(io.StringIO()):
            code = main.main(
                ["workbench", "info", "--port", "NATIVE", "--profile", str(self.path)]
            )
        self.assertNotEqual(code, 0)
        channel.assert_not_called()

    def test_cli_save_profile_is_offline_and_preserves_import_source(self):
        root = Path(self.temp.name)
        key = root / "input.key"
        cert = root / "device.pem"
        key.write_bytes(PC_KEY)
        cert.write_text(self.pem)
        main = importlib.import_module("bk7258")
        output = io.StringIO()
        with patch.object(
            workbench, "SerialChannel"
        ) as channel, contextlib.redirect_stdout(output):
            code = main.main(
                [
                    "workbench",
                    "save-profile",
                    "--profile",
                    str(self.path),
                    "--pc-key-file",
                    str(key),
                    "--certificate",
                    str(cert),
                    "--certificate-sha256",
                    self.pin,
                ]
            )
        self.assertEqual(code, 0)
        channel.assert_not_called()
        self.assertEqual(key.read_bytes(), PC_KEY)
        self.assertFalse(json.loads(output.getvalue())["device_authorization_verified"])
        with profile.use(self.path) as (_, _, saved):
            self.assertEqual(saved, PC_KEY)

    def test_authenticated_profile_with_wrong_trust_binding_is_rejected(self):
        import struct

        pem = self.pem.encode("ascii")
        plain = b"PCI1" + bytes(32) + PC_KEY + struct.pack(">I", len(pem)) + pem
        sealed = self.protect(plain)
        self.path.write_bytes(b"SPC1" + struct.pack(">I", len(sealed)) + sealed)
        with self.assertRaises(profile.ProfileError):
            with profile.use(self.path):
                self.fail(
                    "valid protection cannot replace certificate identity validation"
                )

    def test_io_failures_do_not_publish_success_or_overwrite(self):
        with patch.object(
            profile.os, "fsync", side_effect=OSError("synthetic sync failure")
        ):
            with self.assertRaises(profile.ProfileError):
                profile.create(self.path, self.pem, self.pin, PC_KEY)
        self.assertFalse(self.path.exists())
        self.assertEqual(list(Path(self.temp.name).iterdir()), [])
        with patch.object(
            profile,
            "use",
            side_effect=profile.ProfileError("synthetic readback failure"),
        ):
            with self.assertRaises(profile.ProfileError):
                profile.create(self.path, self.pem, self.pin, PC_KEY)
        self.assertTrue(self.path.exists())
        with profile.use(self.path) as (_, _, key):
            self.assertEqual(key, PC_KEY)
        with self.assertRaises(profile.ProfileError):
            profile.create(self.path, self.pem, self.pin, PC_KEY)

    def test_cli_mixed_profile_credentials_never_downgrade(self):
        profile.create(self.path, self.pem, self.pin, PC_KEY)
        main = importlib.import_module("bk7258")
        with patch.object(
            workbench, "SerialChannel"
        ) as channel, contextlib.redirect_stderr(io.StringIO()):
            result = main.main(
                [
                    "workbench",
                    "status",
                    "--port",
                    "NATIVE",
                    "--profile",
                    str(self.path),
                    "--pc-key-file",
                    "must-not-read",
                ]
            )
        self.assertNotEqual(result, 0)
        channel.assert_not_called()

    def test_os_bridge_uses_stdin_current_user_and_generic_failure(self):
        import base64
        import subprocess

        self.mock.stop()
        with patch.object(
            profile.shutil, "which", return_value="powershell.exe"
        ), patch.object(
            profile.subprocess,
            "run",
            return_value=subprocess.CompletedProcess(
                [], 0, base64.b64encode(b"protected")
            ),
        ) as call:
            self.assertEqual(profile._protect(PC_KEY), b"protected")
        args, kwargs = call.call_args
        self.assertNotIn(base64.b64encode(PC_KEY).decode(), str(args))
        self.assertEqual(kwargs["input"], base64.b64encode(PC_KEY))
        self.assertIn("DataProtectionScope]::CurrentUser", args[0][-1])
        self.assertNotIn("LocalMachine", args[0][-1])
        self.assertIn("[Console]::In.ReadToEnd()", args[0][-1])
        with patch.object(
            profile.shutil, "which", return_value="powershell.exe"
        ), patch.object(
            profile.subprocess,
            "run",
            return_value=subprocess.CompletedProcess([], 1, b"private diagnostic"),
        ):
            with self.assertRaisesRegex(profile.ProfileError, "no plaintext fallback"):
                profile._protect(PC_KEY)


class DpapiIntegrationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        peer_test.WorkbenchClientTest.setUpClass()

    @classmethod
    def tearDownClass(cls):
        peer_test.WorkbenchClientTest.tearDownClass()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "dpapi.spc"
        self.pem = peer_test.WorkbenchClientTest.pem
        self.pin = peer_test.WorkbenchClientTest.pin
        profile.create(self.path, self.pem, self.pin, PC_KEY)

    def test_real_current_user_roundtrip(self):
        self.assertNotIn(PC_KEY, self.path.read_bytes())
        with profile.use(self.path) as (pem, pin, key):
            self.assertEqual((pem, pin, bytes(key)), (self.pem, self.pin, PC_KEY))
        self.assertEqual(key, bytes(32))

    def test_real_entropy_binding_and_tamper_rejected(self):
        with patch.object(profile, "_ENTROPY", "different-application"):
            with self.assertRaises(profile.ProfileError):
                with profile.use(self.path):
                    self.fail("wrong protection context accepted")
        wire = bytearray(self.path.read_bytes())
        wire[-1] ^= 1
        self.path.write_bytes(wire)
        with self.assertRaises(profile.ProfileError):
            with profile.use(self.path):
                self.fail("modified DPAPI data accepted")

    def test_real_profile_cli_to_tls_peer(self):
        peer = peer_test.TlsPeer(
            peer_test.WorkbenchClientTest.cert, peer_test.WorkbenchClientTest.key
        )
        output = io.StringIO()
        main = importlib.import_module("bk7258")
        with patch.object(
            workbench, "SerialChannel", return_value=peer
        ), contextlib.redirect_stdout(output):
            result = main.main(
                ["workbench", "info", "--port", "NATIVE", "--profile", str(self.path)]
            )
        self.assertEqual(result, 0)
        self.assertEqual(json.loads(output.getvalue())["security_counter"], 661)
        self.assertEqual([r[0] for r in peer.requests], [1, 9])


if __name__ == "__main__":
    if "--dpapi" in sys.argv:
        sys.argv = [sys.argv[0], "DpapiIntegrationTest"]
    elif len(sys.argv) == 1:
        sys.argv.append("ProfileTest")
    unittest.main()
