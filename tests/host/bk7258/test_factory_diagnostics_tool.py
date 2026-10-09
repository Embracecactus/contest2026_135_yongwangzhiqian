#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import importlib
import json
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools/bk7258"))


class FactoryDiagnosticsToolTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.module = importlib.import_module("_lib.factory_diagnostics")

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="factory-diagnostics-")
        self.profile = Path(self.temp.name) / "factory.spc"

    def tearDown(self):
        self.temp.cleanup()

    def test_enroll_orders_physical_secret_pin_probe_and_dpapi_profile(self):
        events = []
        random = iter((bytes(range(1, 17)), bytes(range(32, 64))))

        def enable(port, record):
            events.append(("enable", port, bytes(record[:4]), len(record)))
            return {"flags": 3, "ttl_ms": 600000, "certificate_sha256": "a" * 64}

        def probe(port, pin, timeout):
            events.append(("probe", port, pin, timeout))
            return "-----BEGIN CERTIFICATE-----\nZmFrZQ==\n-----END CERTIFICATE-----\n"

        def create(path, pem, pin, key):
            events.append(("create", path, pin, bytes(key)))
            path.write_bytes(b"sealed")

        with patch.object(self.module, "console_enable", side_effect=enable), patch.object(
            self.module.workbench, "probe_certificate", side_effect=probe
        ), patch.object(self.module.workbench_profile, "create", side_effect=create):
            result = self.module.enroll(
                "COM9", "COM16", self.profile, timeout=7, random=lambda n: next(random)
            )

        self.assertEqual([event[0] for event in events], ["enable", "probe", "create"])
        self.assertEqual(events[0][2:], (b"BKD1", 52))
        self.assertEqual(events[2][3], bytes(range(32, 64)))
        self.assertEqual(result["certificate_sha256"], "a" * 64)
        encoded = json.dumps(result, sort_keys=True)
        self.assertNotIn(bytes(range(32, 64)).hex(), encoded)
        self.assertNotIn(bytes(range(1, 17)).hex(), encoded)

    def test_enroll_waits_for_bounded_native_owner_reopen(self):
        events = []
        with patch.object(
            self.module,
            "console_enable",
            return_value={"flags": 3, "ttl_ms": 600000, "certificate_sha256": "a" * 64},
        ), patch.object(
            self.module.workbench,
            "probe_certificate",
            return_value="-----BEGIN CERTIFICATE-----\nZmFrZQ==\n-----END CERTIFICATE-----\n",
        ), patch.object(
            self.module.workbench_profile,
            "create",
            side_effect=lambda *unused: events.append("profile"),
        ):
            self.module.enroll(
                "COM9",
                "COM16",
                self.profile,
                random=lambda n: bytes([n]) * n,
                sleep=lambda seconds: events.append(("settle", seconds)),
            )

        self.assertEqual(
            events,
            [("settle", self.module.NATIVE_REOPEN_SETTLE_SECONDS), "profile"],
        )
        self.assertGreaterEqual(self.module.NATIVE_REOPEN_SETTLE_SECONDS, 1.0)
        self.assertLessEqual(self.module.NATIVE_REOPEN_SETTLE_SECONDS, 2.0)

    def test_pin_probe_failure_revokes_and_never_publishes_profile(self):
        revoked = []
        with patch.object(
            self.module,
            "console_enable",
            return_value={"flags": 3, "ttl_ms": 600000, "certificate_sha256": "b" * 64},
        ), patch.object(
            self.module.workbench,
            "probe_certificate",
            side_effect=ValueError("mismatch"),
        ), patch.object(
            self.module, "console_revoke", side_effect=lambda port: revoked.append(port)
        ), patch.object(self.module.workbench_profile, "create") as create:
            with self.assertRaises(self.module.FactoryDiagnosticsError):
                self.module.enroll(
                    "COM9", "COM16", self.profile, random=lambda n: bytes([n]) * n
                )
        self.assertEqual(revoked, ["COM9"])
        create.assert_not_called()
        self.assertFalse(self.profile.exists())

    def test_pin_probe_stage_is_preserved_after_confirmed_revoke(self):
        revoked = []
        failure = self.module.workbench.CertificateProbeError(
            "hello_written", "timeout"
        )
        with patch.object(
            self.module,
            "console_enable",
            return_value={"flags": 3, "ttl_ms": 600000, "certificate_sha256": "b" * 64},
        ), patch.object(
            self.module.workbench, "probe_certificate", side_effect=failure
        ), patch.object(
            self.module, "console_revoke", side_effect=lambda port: revoked.append(port)
        ):
            with self.assertRaises(self.module.FactoryDiagnosticsError) as rejected:
                self.module.enroll(
                    "COM9", "COM16", self.profile, random=lambda n: bytes([n]) * n
                )
        self.assertEqual(revoked, ["COM9"])
        self.assertEqual(
            str(rejected.exception),
            "Factory enrollment failed stage=hello_written reason=timeout; diagnostics revoked",
        )
        self.assertFalse(self.profile.exists())

    def test_existing_profile_is_rejected_before_console_secret(self):
        self.profile.write_bytes(b"existing")
        with patch.object(self.module, "console_enable") as enable:
            with self.assertRaises(self.module.FactoryDiagnosticsError):
                self.module.enroll("COM9", "COM16", self.profile)
        enable.assert_not_called()
        self.assertEqual(self.profile.read_bytes(), b"existing")

    def test_console_bridge_binds_inputs_without_putting_secret_in_argv(self):
        completed = SimpleNamespace(
            returncode=0, stdout=b"STATUS 3 1000 0\n", stderr=b""
        )
        with patch.object(self.module, "_powershell", return_value="powershell.exe"), \
             patch.object(self.module.subprocess, "run", return_value=completed) as run:
            output = self.module._run_console("COM9", "status")

        argv = run.call_args.args[0]
        options = run.call_args.kwargs
        self.assertEqual(output, "STATUS 3 1000 0")
        self.assertNotIn("COM9", argv)
        self.assertEqual(options["input"], b"BKF1 COM9 status\n")

    def test_power_status_is_read_from_coordinator_after_native_usb_closes(self):
        with patch.object(
            self.module, "_run_console", return_value="POWER 258 -115 0"
        ) as bridge:
            result = self.module.console_power("COM9")

        bridge.assert_called_once_with("COM9", "power", timeout=35)
        self.assertEqual(result["raw_state"], 258)
        self.assertEqual(result["phase"], "pending")
        # The public health wire exposes the CP-pending phase plus one
        # unresolved bit.  It cannot distinguish an accepted wait from an
        # unknown delivery outcome, so the observer must preserve that limit.
        self.assertEqual(result["contract_state"], "WAIT_CP_OR_UNKNOWN")
        self.assertTrue(result["unresolved"])
        self.assertEqual(result["error"], -115)

    def test_power_wait_is_bounded_and_requires_a_terminal_state(self):
        observations = iter(
            (
                {"contract_state": "WAIT_CP_OR_UNKNOWN", "raw_state": 258},
                {"contract_state": "WAIT_CP_OR_UNKNOWN", "raw_state": 258},
                {"contract_state": "FAILED", "raw_state": 3, "error": -110},
            )
        )
        ticks = iter((0.0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6))
        result = self.module.wait_power(
            "COM9",
            timeout=1.0,
            interval=0.01,
            query=lambda port: next(observations),
            clock=lambda: next(ticks),
            sleep=lambda unused: None,
        )
        self.assertEqual(
            [item["contract_state"] for item in result["timeline"]],
            ["WAIT_CP_OR_UNKNOWN", "WAIT_CP_OR_UNKNOWN", "FAILED"],
        )
        self.assertEqual(result["final"]["error"], -110)

    def test_power_wait_retries_read_only_observer_noise_within_same_deadline(self):
        observations = iter(
            (
                self.module.FactoryDiagnosticsError("interleaved target log"),
                {"contract_state": "WAIT_CP_OR_UNKNOWN", "raw_state": 258},
                {"contract_state": "FAILED", "raw_state": 259, "error": -110},
            )
        )
        ticks = iter((0.0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6))

        def query(unused):
            value = next(observations)
            if isinstance(value, Exception):
                raise value
            return value

        result = self.module.wait_power(
            "COM9",
            timeout=1.0,
            interval=0.01,
            query=query,
            clock=lambda: next(ticks),
            sleep=lambda unused: None,
        )
        self.assertEqual(result["observer_failures"], 1)
        self.assertEqual(result["final"]["contract_state"], "FAILED")
        self.assertEqual(len(result["timeline"]), 2)

    def test_power_wait_passes_remaining_deadline_to_real_observer(self):
        timeouts = []

        def run_console(unused_port, unused_action, unused_secret="", *, timeout=None):
            timeouts.append(timeout)
            return "POWER 259 -110 0"

        with patch.object(self.module, "_run_console", side_effect=run_console):
            result = self.module.wait_power(
                "COM9",
                timeout=0.75,
                clock=lambda: 10.0,
                sleep=lambda unused: None,
            )

        self.assertEqual(result["final"]["contract_state"], "FAILED")
        self.assertEqual(len(timeouts), 1)
        self.assertGreater(timeouts[0], 0)
        self.assertLessEqual(timeouts[0], 0.75)


if __name__ == "__main__":
    unittest.main()
