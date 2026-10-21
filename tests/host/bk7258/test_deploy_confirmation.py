#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check host OTA identity and status confirmation without serial hardware."""

import argparse
import importlib
import io
import itertools
from pathlib import Path
import sys
import types
import unittest
from unittest import mock


sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools/bk7258"))
console = importlib.import_module("_lib.deploy_console")
deploy = importlib.import_module("_lib.deploy")


REQUEST = "0123456789abcdef"


def snapshot(generation, version, counter, *, healthy=True, request=REQUEST):
    state = 2 if healthy else 3
    return "\n".join(
        (
            f"bkota-status-begin request={request} generation={generation}",
            f"pair=confirmed version={version} counter={counter}",
            f"ap magic=1234abcd version=1 state={state} error=0 generation={generation} heartbeat=1",
            f"cpu2 magic=1234abcd state=8 error=0 generation={generation} heartbeat=1 ready=1 online=00000003",
            f"rptun magic=1234abcd version=1 state=4 error=0 generation={generation} flags=0 heartbeat=1/1",
            "manager state=0 phase=0 image=0 operation=0 progress=0/0 error=0",
            f"supervisor state=2 generation={generation} flags=0",
            f"bkota-status-end request={request} generation={generation}\n",
        )
    )


class FakeSerial:
    chunks = []

    def __init__(self, *unused_args, **unused_kwargs):
        self.values = list(self.chunks)

    def __enter__(self):
        return self

    def __exit__(self, *unused_args):
        return False

    def read(self, unused_size):
        return self.values.pop(0) if self.values else b""

    def write(self, unused_data):
        return 0

    def flush(self):
        pass


class DeployConfirmationTest(unittest.TestCase):
    def _confirm(self, chunks):
        FakeSerial.chunks = chunks
        return mock.patch.object(
            console, "serial", types.SimpleNamespace(Serial=FakeSerial)
        )

    def run_confirmation(self, chunks):
        clock = itertools.count(0, 0.01)
        with self._confirm(chunks), mock.patch.object(
            console.time, "monotonic", side_effect=lambda: next(clock)
        ), mock.patch.object(
            console.secrets, "token_hex", return_value=REQUEST
        ), mock.patch.object(
            console.sys, "stdout", io.StringIO()
        ):
            console.reboot_and_confirm("fake", 1, "new", 9, 1, reboot=False)

    def test_confirmation_requires_one_healthy_current_snapshot(self):
        stale = snapshot(6, "old", 8)
        current_unhealthy = snapshot(7, "new", 9, healthy=False)
        with self.assertRaises(TimeoutError):
            self.run_confirmation([stale.encode(), current_unhealthy.encode()])

    def test_confirmation_accepts_fragmented_complete_stable_snapshot(self):
        value = snapshot(7, "new", 9).encode()
        self.run_confirmation([value[:67], value[67:]])

    def test_confirmation_rejects_incomplete_or_different_query(self):
        for value in (
            snapshot(7, "new", 9).rsplit("bkota-status-end", 1)[0],
            snapshot(7, "new", 9, request="1111111111111111"),
            snapshot(7, "new", 9).replace(
                f"end request={REQUEST} generation=7",
                f"end request={REQUEST} generation=8",
            ),
            snapshot(7, "new", 90),
            snapshot(7, "new", 9).replace(
                "supervisor state=2 generation=7", "supervisor state=2 generation=6"
            ),
        ):
            with self.subTest(value=value), self.assertRaises(TimeoutError):
                self.run_confirmation([value.encode()])

    def test_confirmation_rejects_oversized_snapshot_then_recovers(self):
        bad = snapshot(7, "new", 9).replace(
            "pair=confirmed", "x" * (console.MAX_STATUS_BYTES + 1) + "\npair=confirmed"
        )
        with self.assertRaises(TimeoutError):
            self.run_confirmation([bad.encode()])
        self.run_confirmation([bad.encode(), snapshot(7, "new", 9).encode()])

    def test_normal_deploy_rejects_mismatched_expected_identity_before_stream(self):
        objects = types.SimpleNamespace(version="package", counter=7, target="aidk")
        args = argparse.Namespace(
            status_only=False,
            reboot_only=False,
            package=Path("package.bin"),
            inspect_only=False,
            expected_board=None,
            expected_version="required",
            expected_counter=9,
            ota_port="fake",
            control_port="none",
            connect_timeout=1,
            frame_timeout=1,
            baud=1,
            confirm_timeout=1,
        )
        with mock.patch.object(
            deploy, "unique_port", return_value="fake"
        ), mock.patch.object(
            deploy, "open_package", return_value=objects
        ), mock.patch.object(
            deploy, "stream_package"
        ) as stream:
            with self.assertRaisesRegex(ValueError, "package version"):
                deploy.run(args)
        stream.assert_not_called()

    def test_normal_deploy_streams_when_expected_identity_matches(self):
        objects = types.SimpleNamespace(version="package", counter=7, target="aidk")
        args = argparse.Namespace(
            status_only=False,
            reboot_only=False,
            package=Path("package.bin"),
            inspect_only=False,
            expected_board=None,
            expected_version="package",
            expected_counter=7,
            ota_port="fake",
            control_port="none",
            connect_timeout=1,
            frame_timeout=1,
            baud=1,
            confirm_timeout=1,
        )
        with mock.patch.object(
            deploy, "unique_port", return_value="fake"
        ), mock.patch.object(
            deploy, "open_package", return_value=objects
        ), mock.patch.object(
            deploy, "stream_package"
        ) as stream:
            deploy.run(args)
        stream.assert_called_once()

    def test_normal_deploy_rejects_counter_mismatch(self):
        objects = types.SimpleNamespace(version="package", counter=7, target="aidk")
        args = argparse.Namespace(
            status_only=False,
            reboot_only=False,
            package=Path("package.bin"),
            inspect_only=False,
            expected_board=None,
            expected_version="package",
            expected_counter=9,
            ota_port="fake",
            control_port="none",
            connect_timeout=1,
            frame_timeout=1,
            baud=1,
            confirm_timeout=1,
        )
        with mock.patch.object(
            deploy, "unique_port", return_value="fake"
        ), mock.patch.object(
            deploy, "open_package", return_value=objects
        ), mock.patch.object(
            deploy, "stream_package"
        ) as stream:
            with self.assertRaisesRegex(ValueError, "counter"):
                deploy.run(args)
        stream.assert_not_called()


if __name__ == "__main__":
    unittest.main()
