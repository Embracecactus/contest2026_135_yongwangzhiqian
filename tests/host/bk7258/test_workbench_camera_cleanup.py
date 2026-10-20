#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""16-byte camera transport: every capture outcome releases the device slot."""
import base64
import hashlib
import importlib
import os
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(
    os.environ.get("WORKBENCH_CAMERA_ROOT", Path(__file__).resolve().parents[3])
)
sys.path.insert(0, str(ROOT / "tools/bk7258"))
from _lib.workbench import ControlError

camera = importlib.import_module("_lib.workbench_camera")

JPEG = b"\xff\xd8" + b"0123456789ABCDEFGHIJKLMNOPQR" + b"\xff\xd9"


class Client:
    """Peer-level fixture; the frame reader remains the original 16-byte SDC1 path."""

    def __init__(self, mode="success"):
        self.mode, self.closed, self.authenticated = mode, False, True
        self._timeout, self.now, self.started, self.released = 1, 0, False, False
        self.identity, self.nonce, self.actions, self.frame_reads = 9, bytes(16), [], []

    def _now(self):
        return self.now

    def _sleep(self, seconds):
        self.now += seconds

    def snapshot(self):
        if not self.started:
            state, ident, nonce, size, valid = 0, 9, bytes(16), 0, False
        elif self.released:
            state, ident, nonce, size, valid = 5, 10, self.nonce, 0, False
        elif self.mode == "expired":
            state, ident, nonce, size, valid = 6, 10, self.nonce, 0, False
        elif self.mode == "timeout":
            state, ident, nonce, size, valid = 1, 10, self.nonce, 0, False
        else:
            state, ident, nonce, size, valid = 3, 10, self.nonce, len(JPEG), True
        flags = 1 | (2 if valid else 0)
        return struct.pack(
            ">4sIQ16sQIIIIiIIIII",
            b"CCS1",
            state,
            ident,
            nonce,
            0,
            size,
            2 if size else 0,
            2 if size else 0,
            0x4745504A if size else 0,
            0,
            flags,
            0,
            1,
            0,
            1000,
        )

    def assert_frame_request(self, identity, offset, size):
        if identity != 10 or offset not in (0, 16) or size != len(JPEG):
            raise AssertionError("invalid 16-byte camera frame request")

    def _exchange(self, kind, payload, deadline):
        if kind == 15:
            selector = struct.unpack(">I", payload[:4])[0]
            channel, offset = selector >> 16, selector & 0xFFFF
            if channel == 23:
                chunk = self.snapshot()[offset : offset + 16]
                return (80, *struct.unpack(">4I", chunk))
            if channel == 25:
                # Newer clients must retain this legacy-peer fallback; c38 never probes it.
                raise camera.DeviceRejected(-95)
            if channel == 22:
                _, identity, offset, size = struct.unpack(">IQII", payload)
                self.frame_reads.append(offset)
                if self.mode == "transport-error":
                    raise OSError("synthetic read fault")
                self.assert_frame_request(identity, offset, size)
                chunk = JPEG[offset : offset + 16]
                return (len(JPEG), *struct.unpack(">4I", chunk))
            raise AssertionError(f"unexpected read selector {channel}")
        if kind == 17:
            _, action, ident, nonce = struct.unpack(">4sIQ16s", payload)
            self.actions.append(action)
            if action == 1:
                self.started, self.identity, self.nonce = True, ident + 1, nonce
            elif action == 2:
                self.released = True
            return (0,)
        if kind == 18:
            if self.mode == "ack-lost" and self.actions[-1] == 1:
                raise OSError("synthetic accepted ACK lost")
            return (0,)
        if kind == 16:
            return (0,)
        raise AssertionError(f"unexpected kind {kind}")


class CleanupTest(unittest.TestCase):
    def capture(self, mode, **kwargs):
        client = Client(mode)
        return client, camera.capture(client, preview=True, **kwargs)

    def assert_released(self, client):
        self.assertEqual(client.actions.count(1), 1)
        self.assertEqual(client.actions.count(2), 1)
        self.assertTrue(client.released)

    def test_success_keeps_exact_jpeg_only_after_release(self):
        client, result = self.capture("success")
        self.assert_released(client)
        self.assertEqual(client.frame_reads, [0, 16])
        self.assertEqual(result["image_base64"], base64.b64encode(JPEG).decode("ascii"))
        self.assertTrue(result["release_confirmed"])
        self.assertEqual(result["sha256"], hashlib.sha256(JPEG).hexdigest())

    def test_expired_and_capture_timeout_release(self):
        client, result = self.capture("expired")
        self.assert_released(client)
        self.assertEqual(
            (result["state"], result["release_confirmed"]), ("expired", True)
        )
        client = Client("timeout")
        with self.assertRaises(ControlError):
            camera.capture(client)
        self.assert_released(client)

    def test_transfer_cancel_and_transport_error_release(self):
        client = Client("transfer-cancel")
        result = camera.capture(
            client, preview=True, cancel_requested=lambda: len(client.frame_reads) >= 1
        )
        self.assert_released(client)
        self.assertEqual(client.frame_reads, [0])
        self.assertEqual(
            (result["state"], result["release_confirmed"]), ("canceled", True)
        )
        client = Client("transport-error")
        with self.assertRaises(OSError):
            camera.capture(client)
        self.assert_released(client)

    def test_accepted_start_ack_loss_still_releases(self):
        client = Client("ack-lost")
        with self.assertRaises(OSError):
            camera.capture(client)
        self.assert_released(client)


if __name__ == "__main__":
    unittest.main()
