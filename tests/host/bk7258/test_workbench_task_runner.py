#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Real child processes; only the external device link is replaced here."""
import argparse
import importlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools/bk7258"))
from _lib import workbench as w


class Link:
    def __init__(self, fail=None):
        self.events = []
        self.fail = fail
        self.snapshot = dict(
            state="none", admitted=True, expired=False, task_id=None, event_sequence=40
        )

    def task_status(self):
        return self.snapshot.copy()

    def task_event(self, task, sequence, state, ttl, progress=None):
        self.events.append((task, sequence, state, ttl, progress))
        if state == self.fail:
            raise w.ControlError("external link lost")
        self.snapshot.update(state=state, task_id=task, event_sequence=sequence)
        return dict(accepted=True, feedback_verified=False)


class RunnerTest(unittest.TestCase):
    def module(self):
        return importlib.import_module("_lib.workbench_task_runner")

    def args(self, code, *extra):
        parser = argparse.ArgumentParser()
        w.add_arguments(parser)
        return parser.parse_args(
            ["task-run", *extra, "--exec", sys.executable, "-c", code]
        )

    def run_child(self, code, *extra, link=None, **kwargs):
        m = self.module()
        link = link or Link()
        result = m.perform(link, m.prepare(self.args(code, *extra)), **kwargs)
        return result, link

    def test_real_success_and_failure(self):
        for code in (0, 7):
            with self.subTest(code=code):
                result, link = self.run_child(f"raise SystemExit({code})")
                self.assertEqual(result["process_returncode"], code)
                self.assertEqual(
                    result["process_state"], "failure" if code else "success"
                )
                self.assertEqual(result["device_result"], "confirmed")
                self.assertFalse(result["feedback_verified"])
                self.assertEqual([e[1] for e in link.events], [41, 42])
                self.assertEqual(link.events[-1][2], result["process_state"])

    def test_disconnect_does_not_replay_or_lose_local_result(self):
        result, link = self.run_child("raise SystemExit(9)", link=Link("failure"))
        self.assertEqual(result["process_returncode"], 9)
        self.assertEqual(result["device_result"], "unknown")
        self.assertEqual(len(link.events), 2)

    def test_start_failure_never_launches_child(self):
        m = self.module()
        with patch.object(m.subprocess, "Popen") as spawn:
            with self.assertRaises(w.ControlError):
                m.perform(Link("start"), m.prepare(self.args("raise SystemExit(0)")))
            spawn.assert_not_called()

    def test_timeout_cancels_real_process(self):
        result, link = self.run_child(
            "import time;time.sleep(20)", "--run-timeout", "0.2"
        )
        self.assertEqual(result["process_state"], "canceled")
        self.assertEqual(result["reason"], "timeout")
        self.assertIsNotNone(result["process_returncode"])
        self.assertEqual(link.events[-1][2], "canceled")

    def test_explicit_cancel_and_spawn_error(self):
        result, link = self.run_child(
            "import time;time.sleep(20)", cancel_requested=lambda: True
        )
        self.assertEqual(result["process_state"], "canceled")
        m = self.module()
        args = self.args("pass")
        args.task_command = ["/definitely-missing-shaniu-task-executable"]
        result = m.perform(Link(), m.prepare(args))
        self.assertEqual(result["process_state"], "failure")
        self.assertEqual(result["reason"], "spawn_failed")

    def test_progress_is_opt_in_bounded_and_task_scoped(self):
        m = self.module()
        task = "12" * 16
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "progress.json"
            for data in (
                b"x" * 257,
                b"{}",
                b"bad",
                json.dumps(dict(task_id="34" * 16, progress=80)).encode(),
                json.dumps(dict(task_id=task, progress=True)).encode(),
            ):
                path.write_bytes(data)
                self.assertIsNone(m.read_progress(path, task))
            path.write_text(json.dumps(dict(task_id=task, progress=30)))
            self.assertEqual(m.read_progress(path, task), 30)
            other = path.with_suffix(".link")
            other.symlink_to(path)
            self.assertIsNone(m.read_progress(other, task))

    def test_busy_or_invalid_never_borrows_credentials(self):
        m = self.module()
        link = Link()
        link.snapshot.update(state="progress", expired=False)
        with patch.object(m.subprocess, "Popen") as spawn:
            with self.assertRaises(w.ControlError):
                m.perform(link, m.prepare(self.args("pass")))
            spawn.assert_not_called()
        args = self.args("pass", "--run-timeout", "nan")
        with patch.object(w, "authorized_client") as connect:
            with self.assertRaises(ValueError):
                w.run(args)
            connect.assert_not_called()

    def test_real_progress_and_disconnect(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "progress.json"
            code = "import os,json,time;from pathlib import Path;Path(os.environ['SHANIU_TASK_PROGRESS']).write_text(json.dumps(dict(task_id=os.environ['SHANIU_TASK_ID'],progress=37)));time.sleep(5.3)"
            result, link = self.run_child(
                code, "--progress-file", str(path), link=Link("progress")
            )
            self.assertEqual(result["process_returncode"], 0)
            self.assertEqual(result["device_result"], "unknown")
            self.assertEqual([e[2] for e in link.events], ["start", "progress"])
            self.assertEqual(link.events[-1][4], 37)

    def test_readback_conflict_is_unknown(self):
        link = Link()
        original = link.task_status

        def status():
            result = original()
            if result["state"] == "success":
                result["task_id"] = "ef" * 16
            return result

        link.task_status = status
        result, _ = self.run_child("pass", link=link)
        self.assertEqual(result["device_result"], "unknown")


if __name__ == "__main__":
    unittest.main()
