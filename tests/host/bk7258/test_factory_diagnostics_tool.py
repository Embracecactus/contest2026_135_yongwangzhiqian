#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import importlib
import json
from pathlib import Path
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

    def test_existing_profile_is_rejected_before_console_secret(self):
        self.profile.write_bytes(b"existing")
        with patch.object(self.module, "console_enable") as enable:
            with self.assertRaises(self.module.FactoryDiagnosticsError):
                self.module.enroll("COM9", "COM16", self.profile)
        enable.assert_not_called()
        self.assertEqual(self.profile.read_bytes(), b"existing")

    def test_power_status_is_read_from_coordinator_after_native_usb_closes(self):
        with patch.object(
            self.module, "_run_console", return_value="POWER 258 -115 0"
        ) as bridge:
            result = self.module.console_power("COM9")

        bridge.assert_called_once_with("COM9", "power")
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
        ticks = iter((0.0, 0.1, 0.2, 0.3))
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


if __name__ == "__main__":
    unittest.main()
