#!/usr/bin/env python3
"""Focused dry-run tests for cross-platform BK7258 transport selection."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path


SCRIPT_ROOT = Path(__file__).resolve().parents[3] / "tools" / "bk7258"
sys.path.insert(0, str(SCRIPT_ROOT))

from bk7258_framework import (  # noqa: E402
    FrameworkError,
    load_json,
    port_list,
    port_resolve,
    transport_plan,
    validate_transport_plan,
)


REPOSITORY = Path(__file__).resolve().parents[3]


def candidate(port: str, serial: str) -> dict[str, object]:
    return {
        "port": port,
        "identity": {
            "vid": "1d50",
            "pid": "60c7",
            "serial_prefix": serial,
            "interface": "usb-serial",
            "location": "usb-1",
        },
        "capabilities": {"rts": True, "dtr": True, "reset": False, "rts_reset": False},
        "source": "native",
    }


class TransportTest(unittest.TestCase):
    def test_schema_and_cross_platform_explicit_port_keep_board_separate(self) -> None:
        schema = load_json(SCRIPT_ROOT / "bk7258_transport_schema.json")
        self.assertEqual(schema["schema"], "bk7258.transport/1")
        self.assertEqual(schema["strict"]["darwin_ports"], "/dev/cu.*")
        windows = port_resolve("windows", port="COM9")
        self.assertEqual(windows["port_identity"]["port"], "COM9")
        self.assertIsNone(windows["board_identity"])
        wsl = port_list("wsl", windows_ports=["COM9"], powershell_adapter=True)
        self.assertEqual(wsl["host"]["backend"], "powershell-adapter")
        self.assertEqual(wsl["candidates"][0]["source"], "powershell-adapter")
        mac = port_resolve("darwin", port="/dev/cu.usbserial")
        self.assertEqual(mac["port_identity"]["port"], "/dev/cu.usbserial")

    def test_auto_selection_rejects_ambiguity_and_accepts_usb_identity(self) -> None:
        candidates = [candidate("/dev/ttyUSB0", "A"), candidate("/dev/ttyUSB1", "B")]
        listed = port_list("linux", candidates=candidates)
        self.assertEqual(len(listed["candidates"]), 2)
        with self.assertRaisesRegex(FrameworkError, "ambiguous"):
            port_resolve("linux", candidates=candidates)
        resolved = port_resolve("linux", identity={"serial_prefix": "B"}, candidates=candidates)
        self.assertEqual(resolved["selection"]["mode"], "usb-identity")
        self.assertEqual(resolved["port_identity"]["port"], "/dev/ttyUSB1")

    def test_transport_plan_serializes_loader_release_before_console(self) -> None:
        plan = transport_plan(REPOSITORY, "t5ai_core", host="linux", port="/dev/ttyUSB0")
        self.assertIs(validate_transport_plan(plan), plan)
        self.assertEqual(plan["board_identity"], {"id": "t5ai_core", "variant": "t5ai_core"})
        self.assertEqual([row["action"] for row in plan["sequence"]],
                         ["open", "close-release", "open", "capture"])
        self.assertFalse(plan["policy"]["capture_before_loader"])
        with self.assertRaisesRegex(FrameworkError, "AIDK forbids RTS reset"):
            transport_plan(
                REPOSITORY, "t5ai_core", host="linux", port="/dev/ttyUSB0",
                aidk=True,
                capabilities={"rts": True, "dtr": True, "reset": True, "rts_reset": True},
            )

    def test_unsupported_host_is_actionable(self) -> None:
        with self.assertRaisesRegex(FrameworkError, "unsupported host"):
            port_list("freebsd")

    def test_windows_loader_requires_positive_markers_and_rejects_failures(
            self) -> None:
        script = (SCRIPT_ROOT / "bk7258_auto_debug.sh").read_text(
            encoding="utf-8")
        self.assertIn("'Writing Flash OK'", script)
        self.assertIn("'{All Finished Successfully}'", script)
        self.assertIn("GetBus fail", script)
        self.assertIn("Writing Flash Failed", script)
        self.assertIn(
            "bk_loader did not emit both complete success markers", script)
        self.assertIn("loader_rc=1", script)

    def test_auto_debug_consumes_verified_package_layout_contract(self) -> None:
        script = (SCRIPT_ROOT / "bk7258_auto_debug.sh").read_text(
            encoding="utf-8")
        self.assertIn("flash-contract", script)
        self.assertIn("PARTITION_LAYOUT_SHA256", script)
        self.assertIn("Flash source plan member", (
            SCRIPT_ROOT / "bk7258_bkpack.py").read_text(encoding="utf-8"))
        self.assertIn("legacy or raw directory packages are not download-authorized",
                      script)
        self.assertNotIn("PARTITION_GENERATOR", script)
        self.assertNotIn("--get slot_a_cp.offset", script)


if __name__ == "__main__":
    unittest.main(verbosity=2)
