#!/usr/bin/env python3
"""Lightweight host-only tests for BK7258 product boot-policy metadata."""

from __future__ import annotations

import copy
import json
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT_ROOT = Path(__file__).resolve().parents[3] / "tools" / "bk7258"
REPOSITORY = Path(__file__).resolve().parents[3]
if str(SCRIPT_ROOT) not in sys.path:
    sys.path.insert(0, str(SCRIPT_ROOT))

import bk7258_boot_policy as boot_policy  # noqa: E402


PRODUCTS = (
    "aidk_ai_toy_bringup",
    "t5ai_core_bringup",
    "t5_board_bringup",
)


def _t5_config_root(root: Path) -> Path:
    config_root = root / "configs"
    config_root.mkdir()
    (config_root / "cp.config").write_text(
        "CONFIG_BK7258_BOARD_T5_BOARD=y\n"
        "CONFIG_BK7258_MCUBOOT_IMAGE=y\n"
        "# CONFIG_BK7258_AP_CORE is not set\n",
        encoding="utf-8")
    (config_root / "ap.config").write_text(
        "CONFIG_BK7258_BOARD_T5_BOARD=y\n"
        "CONFIG_BK7258_MCUBOOT_IMAGE=y\n"
        "CONFIG_BK7258_AP_CORE=y\n",
        encoding="utf-8")
    return config_root


class BootPolicyTests(unittest.TestCase):
    def test_schema_is_strict_and_declares_fail_closed_contract(self) -> None:
        schema = json.loads(
            (SCRIPT_ROOT / "bk7258_boot_policy_schema.json").read_text(encoding="utf-8")
        )
        self.assertEqual(schema["$schema"], "https://json-schema.org/draft/2020-12/schema")
        self.assertEqual(schema["properties"]["schema"]["const"], "bk7258.boot-policy/1")
        self.assertEqual(schema["properties"]["kind"]["const"], "product-boot-policy")
        self.assertEqual(schema["properties"]["version"]["const"], 1)
        self.assertEqual(schema["strict"]["unknown_keys"], "reject")
        self.assertEqual(schema["strict"]["identity"],
                         "sha256-canonical-document-without-identity_sha256")
        self.assertEqual(schema["strict"]["geometry"], "resolved-layout-only-no-literals")
        self.assertEqual(schema["strict"]["credentials"],
                         "ids-and-requirements-only-no-key-paths-or-key-reads")
        required = schema["required"]
        for field in ("manifest", "debug_io", "version_policy", "bl2_geometry",
                      "bl2_image_logical_size", "credentials", "permissions",
                      "executor", "identity_sha256"):
            self.assertIn(field, required)
        self.assertIn("$defs", schema)
        self.assertEqual(schema["properties"]["debug_io"]["$ref"], "#/$defs/debug_io")
        self.assertIn("oneOf", schema["properties"]["mechanism"])
        self.assertNotIn("documents", schema)

    def test_all_checked_in_products_validate_and_identity_is_bound(self) -> None:
        policies = boot_policy.validate_all(REPOSITORY)
        self.assertEqual(set(policies), set(PRODUCTS))
        for product, policy in policies.items():
            self.assertEqual(
                policy["identity_sha256"], boot_policy._policy_identity(policy)
            )
            self.assertEqual(policy["product"], product)
            self.assertEqual(policy["bl2_image_logical_size"],
                             0x3000 if policy["boot"] == "mcuboot" else None)
            self.assertFalse(policy["credentials"]["read_private_material"])
            self.assertTrue(policy["metadata_only"])
            self.assertFalse(policy["executor_authoritative"])
            self.assertEqual(policy["integration_status"], "BLOCKED")
            self.assertEqual(policy["executor"]["integration_status"], "BLOCKED")
            self.assertEqual(policy["permissions"]["key_read"], "forbidden")
            self.assertEqual(policy["permissions"]["hardware"], "forbidden")
            self.assertEqual(policy["permissions"]["network"], "forbidden")

    def test_aidk_no_swd_dynamic_com_uart_policy_is_explicit(self) -> None:
        policy = boot_policy.load_policy(REPOSITORY, "aidk_ai_toy_bringup")
        self.assertEqual(policy["boot"], "mcuboot")
        self.assertFalse(policy["debug_io"]["swd"]["enabled"])
        self.assertEqual(policy["debug_io"]["console"]["uart"], "uart0")
        self.assertEqual(policy["debug_io"]["console"]["baud"], 115200)
        self.assertEqual(policy["debug_io"]["console"]["port_strategy"],
                         "dynamic-usb-serial")
        self.assertEqual(policy["debug_io"]["loader"]["transport"], "com-uart")
        self.assertEqual(policy["debug_io"]["loader"]["uart"], "uart0")
        self.assertEqual(policy["debug_io"]["loader"]["tool"], "BKFIL/bk_loader")
        self.assertFalse(policy["debug_io"]["loader"]["rts_reset"])
        self.assertFalse(policy["debug_io"]["loader"]["dtr_reset"])

    def test_t5_policy_id_is_shared_but_board_swd_routes_are_parseable(self) -> None:
        t5ai = boot_policy.load_policy(REPOSITORY, "t5ai_core_bringup")
        t5board = boot_policy.load_policy(REPOSITORY, "t5_board_bringup")
        self.assertEqual(t5ai["policy_id"], t5board["policy_id"])
        self.assertFalse(t5ai["debug_io"]["swd"]["enabled"])
        self.assertEqual(t5ai["debug_io"]["console"]["uart"], "uart1")
        self.assertEqual(t5ai["debug_io"]["console"]["baud"], 460800)
        self.assertEqual(t5ai["active_roles"], ["bl1", "cp", "ap"])
        self.assertEqual(t5ai["mechanism"]["bl2"]["role"], "not-applicable")
        self.assertTrue(t5board["debug_io"]["swd"]["enabled"])
        self.assertEqual(t5board["debug_io"]["swd"]["pin_group"], "p0_p1")
        self.assertEqual(t5board["debug_io"]["swd"]["target"], "cp")
        self.assertEqual(t5board["debug_io"]["swd"]["stage_holds"],
                         {"bl1": False, "bl2": True, "cp": True})
        self.assertEqual(t5board["debug_io"]["console"]["kind"], "rtt")
        self.assertEqual(t5board["debug_io"]["loader"]["transport"], "serial-uart")
        self.assertEqual(t5board["debug_io"]["loader"]["download"], "bkfil")
        self.assertEqual(t5board["debug_io"]["loader"]["uart"], "uart0")
        self.assertFalse(t5board["debug_io"]["loader"]["requires_swd"])

    def test_resolve_derives_bl2_geometry_from_product_layout(self) -> None:
        from gen_bk7258_partitions import load_layout

        layout_source = REPOSITORY / "board/bk7258/partitions/bk7258/auto_partitions.csv"
        layout = load_layout(layout_source)
        primary = layout.by_role("bl2")
        boundary = layout.by_role("littlefs")
        expected_primary_xip = layout.xip_base + layout.logical_offset(primary)
        expected_logical_size = layout.logical_size(primary)
        for product in PRODUCTS:
            resolved = boot_policy.resolve_policy(REPOSITORY, product)
            self.assertEqual(resolved["resolved_layout"]["layout_id"], layout.layout_id)
            self.assertEqual(resolved["resolved_layout"]["layout_sha256"], layout.layout_sha256)
            self.assertEqual(resolved["integration_status"], "BLOCKED")
            self.assertFalse(resolved["executable"])
            geometry = resolved["bl2_geometry"]
            if product == "t5ai_core_bringup":
                self.assertEqual(geometry["source"], "not-applicable")
                self.assertIsNone(geometry["primary"])
                continue
            self.assertEqual(geometry["primary"]["physical_start"], primary.offset)
            self.assertEqual(geometry["primary"]["physical_size"], primary.size)
            self.assertEqual(geometry["primary"]["xip_start"], expected_primary_xip)
            self.assertEqual(geometry["primary"]["logical_size"], expected_logical_size)
            self.assertEqual(
                geometry["secondary"]["physical_start"], primary.offset + primary.size
            )
            self.assertEqual(
                geometry["secondary"]["xip_start"], expected_primary_xip + expected_logical_size
            )
            self.assertEqual(geometry["secondary"]["boundary_offset"], boundary.offset)

    def test_missing_field_fails_closed(self) -> None:
        policy = boot_policy.load_policy(REPOSITORY, "t5_board_bringup")
        missing = copy.deepcopy(policy)
        del missing["manifest"]
        with self.assertRaises(boot_policy.BootPolicyError):
            boot_policy.validate_policy(missing, REPOSITORY)

    def test_credential_key_path_fails_closed(self) -> None:
        policy = boot_policy.load_policy(REPOSITORY, "aidk_ai_toy_bringup")
        with_path = copy.deepcopy(policy)
        with_path["credentials"]["required"][0]["private_key_path"] = "/tmp/key.pem"
        with self.assertRaises(boot_policy.BootPolicyError):
            boot_policy.validate_policy(with_path, REPOSITORY)

    def test_hardcoded_geometry_fails_closed(self) -> None:
        policy = boot_policy.load_policy(REPOSITORY, "t5_board_bringup")
        hardcoded = copy.deepcopy(policy)
        hardcoded["bl2_geometry"]["primary"]["physical_start"] = 0x51D000
        with self.assertRaises(boot_policy.BootPolicyError):
            boot_policy.validate_policy(hardcoded, REPOSITORY)

    def test_invalid_version_state_fails_closed(self) -> None:
        policy = boot_policy.load_policy(REPOSITORY, "t5_board_bringup")
        invalid = copy.deepcopy(policy)
        invalid["version_policy"]["security_counter"]["state"] = "accepted"
        with self.assertRaises(boot_policy.BootPolicyError):
            boot_policy.validate_policy(invalid, REPOSITORY)

    def test_identity_tamper_and_duplicate_json_fail_closed(self) -> None:
        policy = boot_policy.load_policy(REPOSITORY, "t5ai_core_bringup")
        tampered = copy.deepcopy(policy)
        tampered["policy_id"] = "other-policy"
        with self.assertRaises(boot_policy.BootPolicyError):
            boot_policy.validate_policy(tampered, REPOSITORY)

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "duplicate.json"
            path.write_text('{"schema":"a","schema":"b"}\n', encoding="utf-8")
            with self.assertRaises(boot_policy.BootPolicyError):
                boot_policy.load_json(path)

    def test_resolution_identity_is_still_bound_to_build_plan_layout(self) -> None:
        policy = boot_policy.load_policy(REPOSITORY, "t5_board_bringup")
        catalog_layout = json.loads(
            (REPOSITORY / policy["catalog"]).read_text(encoding="utf-8")
        )["partition_layout"]
        mismatched = dict(catalog_layout)
        mismatched["layout_sha256"] = "0" * 64
        with self.assertRaises(boot_policy.BootPolicyError):
            boot_policy.validate_policy(policy, REPOSITORY, resolved_layout=mismatched)

    def test_resolve_accepts_full_plan_but_never_claims_executable(self) -> None:
        from bk7258_framework import build_plan

        with tempfile.TemporaryDirectory(prefix="bk7258-boot-") as directory:
            plan = build_plan(REPOSITORY, "t5_board_bringup",
                              config_root=_t5_config_root(Path(directory)))
        resolved = boot_policy.resolve_policy(
            REPOSITORY, "t5_board_bringup", build_plan=plan
        )
        self.assertEqual(resolved["integration_status"], "BLOCKED")
        self.assertEqual(resolved["integration"]["plan_profile_reconciliation"], "validated")
        self.assertEqual(resolved["build_plan_identity_sha256"], plan["identity_sha256"])
        self.assertFalse(resolved["executable"])

        # A layout-only caller must not accidentally cross the executor boundary.
        layout = plan["partition_layout"]
        layout_only = boot_policy.resolve_policy(
            REPOSITORY, "t5_board_bringup", resolved_layout=layout
        )
        self.assertEqual(layout_only["integration_status"], "BLOCKED")
        self.assertEqual(layout_only["integration"]["plan_profile_reconciliation"], "required")
        self.assertFalse(layout_only["executable"])

        mismatched = copy.deepcopy(plan)
        mismatched["identity_inputs"]["boot"] = "raw"
        with self.assertRaises(boot_policy.BootPolicyError):
            boot_policy.resolve_policy(REPOSITORY, "t5_board_bringup", build_plan=mismatched)

        missing_role = copy.deepcopy(plan)
        del missing_role["roles"]["bl2"]
        with self.assertRaises(boot_policy.BootPolicyError):
            boot_policy.resolve_policy(REPOSITORY, "t5_board_bringup", build_plan=missing_role)

    def test_resolve_requires_canonical_build_plan_metadata_and_identity(self) -> None:
        from bk7258_framework import build_plan

        with tempfile.TemporaryDirectory(prefix="bk7258-boot-") as directory:
            plan = build_plan(REPOSITORY, "t5_board_bringup",
                              config_root=_t5_config_root(Path(directory)))
        for field in ("schema", "kind", "version", "identity_sha256"):
            with self.subTest(missing=field):
                missing = copy.deepcopy(plan)
                del missing[field]
                with self.assertRaises(boot_policy.BootPolicyError):
                    boot_policy.resolve_policy(
                        REPOSITORY, "t5_board_bringup", build_plan=missing)

        wrong_identity = copy.deepcopy(plan)
        wrong_identity["identity_sha256"] = "0" * 64
        with self.assertRaises(boot_policy.BootPolicyError):
            boot_policy.resolve_policy(
                REPOSITORY, "t5_board_bringup", build_plan=wrong_identity)


if __name__ == "__main__":
    unittest.main()
