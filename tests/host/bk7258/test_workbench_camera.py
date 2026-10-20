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

    def test_capture_uses_blocks_and_preserves_partial_tail(self):
        from _lib.workbench import ControlError, DeviceRejected

        m = self.module()
        frame = b"\xff\xd8" + bytes(range(256)) * 2 + b"x" * 4 + b"\xff\xd9"

        class Client:
            closed = False
            authenticated = True
            _timeout = 1

            def __init__(
                self,
                blocks=True,
                interrupt=False,
                phase_after_request=3,
                lost_request_receipt=False,
            ):
                self.blocks, self.interrupt = blocks, interrupt
                self.phase_after_request = phase_after_request
                self.lost_request_receipt = lost_request_receipt
                self.phase, self.identity = 0, 0
                self.nonce = bytes(16)
                self.block_offsets = []
                self.actions = []
                self.now = 0

            def _now(self):
                self.now += 1
                return self.now

            def _sleep(self, unused):
                pass

            def snapshot(self):
                flags = 3 if self.phase == 3 else 1
                size = len(frame) if self.phase == 3 else 0
                return struct.pack(
                    ">4sIQ16sQIIIIiIIIII",
                    b"CCS1",
                    self.phase,
                    self.identity,
                    self.nonce,
                    1,
                    size,
                    2,
                    2,
                    0x4745504A if size else 0,
                    0,
                    flags,
                    1,
                    1,
                    1,
                    120000 if self.phase == 3 else 0,
                )

            def _exchange(self, command, payload, deadline):
                if command == 15 and payload == struct.pack(">I", 25 << 16):
                    if not self.blocks:
                        raise DeviceRejected(-13)
                    return (
                        16,
                        *struct.unpack(">4I", b"CBR1" + struct.pack(">III", 1, 512, 0)),
                    )
                if command == 15:
                    kind = struct.unpack(">I", payload[:4])[0] >> 16
                    if kind == 23:
                        offset = struct.unpack(">I", payload[:4])[0] & 0xFFFF
                        return (
                            80,
                            *struct.unpack(
                                ">4I", self.snapshot()[offset : offset + 16]
                            ),
                        )
                    if kind == 22:
                        offset = struct.unpack(">IQII", payload)[2]
                        return (
                            len(frame),
                            *struct.unpack(
                                ">4I", (frame[offset : offset + 16]).ljust(16, b"\0")
                            ),
                        )
                if command == 17:
                    action, identity = struct.unpack(">IQ", payload[4:16])
                    self.actions.append(action)
                    if action == 1:
                        self.identity, self.nonce, self.phase = (
                            identity + 1,
                            payload[16:],
                            self.phase_after_request,
                        )
                        if self.lost_request_receipt:
                            raise ControlError("request receipt lost after send")
                    else:
                        self.phase = 5
                return (0, 0, 0, 0, 0)

            def _camera_block_exchange(self, selector, deadline):
                _kind, identity, offset, total = struct.unpack(">IQII", selector)
                self.block_offsets.append(offset)
                if identity != self.identity or total != len(frame):
                    raise AssertionError("invalid block selector")
                if self.interrupt and offset == 512:
                    raise ControlError("fixture interruption")
                return len(frame), frame[offset : offset + 512].ljust(512, b"\0")

        client = Client()
        result = m.capture(client)
        self.assertEqual(client.block_offsets, [0, 512])
        self.assertEqual(result["transfer_chunk_bytes"], 512)
        self.assertEqual(
            result["sha256"], __import__("hashlib").sha256(frame).hexdigest()
        )
        self.assertTrue(result["release_confirmed"])

        legacy = Client(blocks=False)
        result = m.capture(legacy)
        self.assertEqual(legacy.block_offsets, [])
        self.assertEqual(result["transfer_chunk_bytes"], 16)
        self.assertTrue(result["release_confirmed"])

        interrupted = Client(interrupt=True)
        with self.assertRaises(ControlError):
            m.capture(interrupted)
        self.assertEqual(interrupted.block_offsets, [0, 512])
        self.assertEqual(interrupted.actions, [1, 2])

        expired = Client(phase_after_request=6)
        result = m.capture(expired)
        self.assertEqual(result["state"], "expired")
        self.assertNotIn("image_base64", result)
        self.assertTrue(result["release_confirmed"])
        self.assertEqual(expired.actions, [1, 2])

        canceled = Client()
        result = m.capture(
            canceled, cancel_requested=lambda: bool(canceled.block_offsets)
        )
        self.assertEqual(result["state"], "canceled")
        self.assertNotIn("image_base64", result)
        self.assertTrue(result["release_confirmed"])
        self.assertEqual(canceled.actions, [1, 2])

        timeout = Client(phase_after_request=1)
        with self.assertRaises(ControlError):
            m.capture(timeout)
        self.assertEqual(timeout.actions, [1, 2])

        lost_receipt = Client(lost_request_receipt=True)
        with self.assertRaises(ControlError):
            m.capture(lost_receipt)
        self.assertEqual(lost_receipt.actions, [1, 2])


if __name__ == "__main__":
    unittest.main()
