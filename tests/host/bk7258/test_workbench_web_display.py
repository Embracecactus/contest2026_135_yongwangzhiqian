#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Real HTTP/workbench/TLS to native display state/store/render, controlled I/O.

The peer binds a synthetic authority; PC grant and product routing remain
separate tests. Each HTTP device operation uses a fresh real TLS session.
"""
import contextlib
import hashlib
import json
import os
from pathlib import Path
import ssl
import subprocess
import threading
import unittest
from unittest.mock import patch

from test_workbench_web import WebTest

NAME = "shaniu-upload-v1.bkep"


class Peer:
    def __init__(self):
        self.process = subprocess.Popen(
            json.loads(os.environ["SHANIU_WEB_DISPLAY_PEER"]),
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        self.timer = threading.Timer(30, self.process.kill)
        self.timer.start()
        self.incoming = bytearray()
        self.connected = True
        self.opens = 0
        self.closes = 0
        if self.process.stdout.readline().strip() != "READY":
            raise RuntimeError("native TLS peer not ready")

    def external(self, command):
        self.process.stdin.write(command + "\n")
        self.process.stdin.flush()
        results = []
        while True:
            line = self.process.stdout.readline()
            if not line:
                raise RuntimeError("native TLS peer ended")
            line = line.strip()
            if line == "READY":
                return results
            if line.startswith("DATA "):
                self.incoming.extend(bytes.fromhex(line[5:]))
            else:
                results.append(line)

    def channel(self, unused_port, unused_timeout):
        peer = self
        if not self.connected:
            self.external("connect")
            self.connected = True
        self.opens += 1

        class Channel:
            closed = False

            def write(self, data):
                value = bytes(data[:20])
                peer.external("wire " + value.hex())
                return len(value)

            def read(self, size):
                if not peer.incoming:
                    peer.external("poll")
                if not peer.incoming:
                    return None
                value = bytes(peer.incoming[:size])
                del peer.incoming[:size]
                return value

            def close(self):
                if not self.closed:
                    self.closed = True
                    peer.external("disconnect")
                    peer.connected = False
                    peer.incoming.clear()
                    peer.closes += 1

        return Channel()

    def stats(self):
        values = self.external("stats")[0].split()
        return tuple(map(int, values[1:6])) + (values[6],)

    def close(self):
        self.process.stdin.close()
        try:
            self.process.wait(timeout=5)
            error = self.process.stderr.read()
            if self.process.returncode:
                raise AssertionError(error)
        finally:
            self.timer.cancel()
            self.process.kill() if self.process.poll() is None else None
            self.process.stdout.close()
            self.process.stderr.close()


class DisplayHttpTest(WebTest):
    def setUp(self):
        super().setUp()
        self.peer = Peer()
        self.next_id = 100
        pem = Path(os.environ["SHANIU_TEST_CERT"]).read_text()
        pin = hashlib.sha256(ssl.PEM_cert_to_DER_cert(pem)).hexdigest()

        @contextlib.contextmanager
        def credentials(args):
            yield pem, pin, bytearray(b"\x2a" + bytes(31))

        self.patches = [
            patch.object(self.m.workbench, "SerialChannel", self.peer.channel),
            patch.object(self.m.workbench, "_credentials", credentials),
        ]
        for item in self.patches:
            item.start()

    def tearDown(self):
        try:
            super().tearDown()
            self.assertEqual(self.peer.opens, self.peer.closes)
        finally:
            for item in reversed(self.patches):
                item.stop()
            self.peer.close()

    def operation(self, name, **params):
        self.next_id += 1
        code, data = self.request(
            "POST",
            "/api/start",
            {"id": f"{self.next_id:032x}", "operation": name, "params": params},
        )
        self.assertEqual(code, 202, data)
        value = self.wait_result()
        self.assertEqual(value["phase"], "returned", value)
        return value["result"]

    def start_trial(self, name=NAME):
        value = self.operation("trial-status")
        before = self.peer.stats()
        accepted = self.operation(
            "trial-start",
            expected_trial_id=value["id"],
            expression="happy",
            ttl_ms=30000,
            pack_filename=name,
        )
        self.assertTrue(accepted["accepted"])
        self.assertFalse(accepted["completion_verified"])
        pending = self.operation("trial-status")
        self.assertEqual(pending["state"], "pending")
        self.assertEqual(before, self.peer.stats())
        self.peer.external("trial-step")
        return before, self.operation("trial-status")

    def test_trial_expiry(self):
        before, active = self.start_trial()
        self.assertEqual(active["state"], "active")
        self.assertTrue(active["device_reports_rendered"])
        rendered = self.peer.stats()
        self.assertEqual(rendered[1], before[1] + 2)
        self.assertEqual(rendered[0], before[0])
        self.assertEqual(rendered[4:], before[4:])
        self.peer.external("time 30100")
        self.peer.external("trial-step")
        self.assertEqual(self.operation("trial-status")["state"], "expired")
        after = self.peer.stats()
        self.assertEqual(after[1], before[1] + 4)
        self.assertEqual(after[0], before[0])
        self.assertEqual(after[4:], before[4:])
        self.peer.external("trial-step")
        self.assertEqual(self.peer.stats(), after)

    def test_trial_cancel(self):
        before, active = self.start_trial()
        rendered = self.peer.stats()
        self.operation("trial-cancel", expected_trial_id=active["id"])
        pending = self.operation("trial-status")
        self.assertEqual(pending["state"], "cancel_pending")
        self.assertFalse(pending["cancel_confirmed"])
        self.assertEqual(rendered, self.peer.stats())
        self.peer.external("trial-step")
        self.assertTrue(self.operation("trial-status")["cancel_confirmed"])
        after = self.peer.stats()
        self.assertEqual(after[1], before[1] + 4)
        self.assertEqual(after[0], before[0])
        self.assertEqual(after[4:], before[4:])
        self.peer.external("time 30100")
        self.peer.external("trial-step")
        self.assertEqual(after, self.peer.stats())

    def test_missing_pack(self):
        before, result = self.start_trial("missing.bkep")
        self.assertEqual(result["state"], "failed")
        self.assertFalse(result["device_reports_rendered"])
        after = self.peer.stats()
        self.assertEqual(before[:2], after[:2])
        self.assertEqual(before[4:], after[4:])

    def prepare_default(self):
        value = self.operation("default-status")
        self.operation(
            "default-refresh",
            selection_epoch=value["epoch"],
            expected_selection_id=value["id"],
        )
        self.peer.external("step")
        value = self.operation("default-status")
        self.assertTrue(value["refresh_complete"])
        self.assertEqual(value["revision"], 1)
        return value

    def test_default_supersedes_trial(self):
        self.start_trial()
        value = self.prepare_default()
        before = self.peer.stats()
        self.operation(
            "default-set",
            selection_epoch=value["epoch"],
            expected_selection_id=value["id"],
            expected_default_revision=str(value["revision"]),
            pack_filename=NAME,
        )
        pending = self.operation("default-status")
        self.assertFalse(pending["device_reports_saved"])
        self.assertFalse(pending["device_reports_rendered"])
        self.assertEqual(before, self.peer.stats())
        self.peer.external("step")
        done = self.operation("default-status")
        self.assertTrue(done["selection_complete"])
        self.assertEqual(done["revision"], 2)
        after = self.peer.stats()
        self.assertEqual(after[4:], (2, NAME))
        self.assertEqual(after[1], before[1] + 2)
        self.peer.external("time 30100")
        self.peer.external("trial-step")
        self.assertEqual(self.operation("trial-status")["state"], "superseded")
        self.assertEqual(after, self.peer.stats())

    def test_release_recovery_stays_unknown(self):
        value = self.prepare_default()
        before = self.peer.stats()
        self.operation(
            "default-set",
            selection_epoch=value["epoch"],
            expected_selection_id=value["id"],
            expected_default_revision=str(value["revision"]),
            pack_filename=NAME,
        )
        self.peer.external("fail-unmount")
        self.peer.external("step")
        unknown = self.operation("default-status")
        self.assertEqual(unknown["state"], "unknown")
        self.assertTrue(unknown["device_reports_saved"])
        self.assertFalse(unknown["device_reports_rendered"])
        self.assertEqual(unknown["release_error"], -5)
        self.operation(
            "default-recover",
            selection_epoch=unknown["epoch"],
            expected_selection_id=unknown["id"],
        )
        self.peer.external("step")
        recovered = self.operation("default-status")
        self.assertEqual(recovered["state"], "unknown")
        self.assertEqual(recovered["release_error"], 0)
        self.assertFalse(recovered["selection_complete"])
        self.assertFalse(recovered["device_reports_rendered"])
        self.assertEqual(self.peer.stats()[1], before[1])


if __name__ == "__main__":
    unittest.main()
