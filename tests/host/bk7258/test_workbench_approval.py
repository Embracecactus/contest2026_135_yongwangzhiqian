# SPDX-License-Identifier: Apache-2.0
"""PC side of Agent Approval Totem against the real C receipt and a fake device."""

from contextlib import contextmanager
import argparse
import hashlib
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tools/bk7258"))
from _lib import workbench  # noqa: E402
from _lib import workbench_approval as approval  # noqa: E402

KEY = b"\x42" * 32


class FakeDevice:
    """SDC1 config semantics of kind 29 plus a scripted phone owner."""

    def __init__(self, decide_after=None, decision=2, tamper=False,
                 decide_at_offset=None):
        self._timeout = 10
        self.record = None
        self.state = 0
        self.staged = bytearray()
        self.reads = 0
        self.decide_after = decide_after
        self.decision = decision
        self.tamper = tamper
        self.decide_at_offset = decide_at_offset
        self.canceled = False

    def _exchange(self, command, payload, deadline):
        if command == 16:
            kind, size = struct.unpack(">II", payload)
            assert kind == 29 and size > 0
            self.staged = bytearray()
            return (0, 0, 0, 0, 0)
        if command == 17:
            self.staged += payload
            return (0, 0, 0, 0, 0)
        if command == 18:
            record = bytes(self.staged)
            if record[:4] == b"APC1":
                assert self.record and record[4:20] == self.record[4:20]
                if self.state == 1:
                    self.state = 5
                self.canceled = True
            else:
                assert record[:4] == b"APR1"
                self.record, self.state = record, 1
            return (0, 0, 0, 0, 0)
        assert command == 15
        selector, = struct.unpack(">I", payload)
        assert selector >> 16 == 29
        offset = selector & 0xFFFF
        if offset == self.decide_at_offset and self.state == 1:
            self.state = self.decision
        if offset == 0:
            self.reads += 1
            if self.decide_after is not None and self.reads > self.decide_after \
                    and self.state == 1:
                self.state = self.decision
        receipt = self.receipt()
        return (96, *struct.unpack(">4I", receipt[offset : offset + 16]))

    def receipt(self):
        out = bytearray(96)
        out[:4] = b"APS1"
        out[4:8] = struct.pack(">I", self.state)
        if self.record:
            digest = hashlib.sha256(self.record).digest()
            out[8:24] = self.record[4:20]
            out[24:56] = digest
            if self.state == 1:
                out[56:60] = struct.pack(">I", 1000)
            if self.state in (2, 3):
                tag = approval.expected_tag(KEY, self.record[4:20], digest, self.state)
                out[64:96] = bytes(32) if self.tamper else tag
        return bytes(out)


class Clock:
    def __init__(self):
        self.now = 0.0

    def __call__(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


class ApprovalTest(unittest.TestCase):
    def test_c_device_receipt_verifies(self):
        binary = HERE / "build/test_pc_approval"
        vector = bytes.fromhex(
            subprocess.run([binary, "vector"], check=True, capture_output=True,
                           text=True).stdout.strip()
        )
        record = approval.encode_request(
            b"\x01" * 16, 60000, "codex", "/repo", "git push --force origin main"
        )
        digest = hashlib.sha256(record).digest()
        self.assertEqual(vector[24:56], digest)
        self.assertEqual(
            approval.decode_receipt(vector, b"\x01" * 16, digest, KEY),
            ("approved", 0, True),
        )
        forged = vector[:64] + bytes([vector[64] ^ 1]) + vector[65:]
        with self.assertRaises(approval.ApprovalError):
            approval.decode_receipt(forged, b"\x01" * 16, digest, KEY)
        with self.assertRaises(approval.ApprovalError):
            approval.decode_receipt(vector, b"\x01" * 16, digest, b"\x43" * 32)
        self.assertEqual(
            approval.decode_receipt(vector, b"\x02" * 16, digest, KEY)[0],
            "superseded",
        )

    def test_exact_text_rules(self):
        rid = b"\x01" * 16
        for bad in ("rm ‮fdp.exe", "a\x1b[2K", "x" * 769, ""):
            with self.assertRaises(approval.ApprovalError):
                approval.encode_request(rid, 60000, "codex", "/r", bad)
        with self.assertRaises(approval.ApprovalError):
            approval.encode_request(rid, 4999, "codex", "/r", "ls")
        record = approval.encode_request(rid, 60000, "codex", "", "ls\n# 中文")
        self.assertEqual(record[:4], b"APR1")
        self.assertEqual(len(record), 32 + 5 + len("ls\n# 中文".encode()))

    def flow(self, device, timeout=30):
        clock = Clock()
        return approval.request(device, KEY, "codex", "/repo", "npm publish",
                                timeout, clock=clock, sleep=clock.sleep)

    def test_approved_rejected_timeout_and_forgery(self):
        result = self.flow(FakeDevice(decide_after=3, decision=2))
        self.assertTrue(approval.approved(result))
        result = self.flow(FakeDevice(decide_after=1, decision=3))
        self.assertEqual(result["decision"], "rejected")
        self.assertFalse(approval.approved(result))
        device = FakeDevice()
        result = self.flow(device, timeout=5)
        self.assertEqual(result["decision"], "canceled")
        self.assertTrue(device.canceled)
        self.assertFalse(approval.approved(result))
        with self.assertRaises(approval.ApprovalError):
            self.flow(FakeDevice(decide_after=0, decision=2, tamper=True))

    def test_decision_during_chunked_receipt_read_is_retried(self):
        result = self.flow(FakeDevice(decision=3, decide_at_offset=64))
        self.assertEqual(result["decision"], "rejected")
        self.assertTrue(result["authenticated"])

    def test_hook_and_run_commands(self):
        payload = {"tool_name": "Bash", "tool_input": {"command": "git push -f"},
                   "cwd": "/repo"}
        self.assertEqual(approval.hook_operation(payload), "Bash: git push -f")
        edit = {"tool_name": "Write", "tool_input": {"file_path": "/a", "content": "x"}}
        self.assertEqual(approval.hook_operation(edit),
                         'Write {"content":"x","file_path":"/a"}')
        deny = approval.hook_output(False, "no")["hookSpecificOutput"]
        self.assertEqual(deny["permissionDecision"], "deny")
        self.assertEqual(approval.command_text(["rm", "-rf", "a b"]), "rm -rf 'a b'")

    def test_cli_paths_use_authorized_client(self):
        devices = []

        @contextmanager
        def fake_client(args, *, retain_key=False):
            self.assertTrue(retain_key)
            device = FakeDevice(decide_after=0, decision=2)
            device.pc_key_copy = bytearray(KEY)
            devices.append(device)
            yield device

        parser = argparse.ArgumentParser()
        workbench.add_arguments(parser)
        original = workbench.authorized_client
        workbench.authorized_client = fake_client
        try:
            args = parser.parse_args(["approval-hook", "--port", "x",
                                      "--approval-timeout", "5"])
            stdin = sys.stdin
            sys.stdin = io.TextIOWrapper(io.BytesIO(json.dumps(
                {"tool_name": "Bash", "tool_input": {"command": "ls"},
                 "cwd": "/repo"}).encode()))
            try:
                out = workbench.run(args)
            finally:
                sys.stdin = stdin
            self.assertEqual(out["hookSpecificOutput"]["permissionDecision"], "allow")
            self.assertIn(b"Bash: ls", devices[-1].record)
            self.assertIn(b"/repo", devices[-1].record)
            args = parser.parse_args(["approval-run", "--port", "x", "--exec",
                                      sys.executable, "-c", "raise SystemExit(7)"])
            out = workbench.run(args)
            self.assertEqual(out["process_returncode"], 7)
        finally:
            workbench.authorized_client = original


if __name__ == "__main__":
    unittest.main()
