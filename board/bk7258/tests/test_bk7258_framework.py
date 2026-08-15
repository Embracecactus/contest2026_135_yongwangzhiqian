#!/usr/bin/env python3
"""Fast schema and resolver checks for the canonical BK7258 script path."""

from __future__ import annotations

import copy
import hashlib
import os
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT_ROOT = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPT_ROOT))

from bk7258_framework import (  # noqa: E402
    FrameworkError,
    canonical_json,
    classic_report,
    cmake_view,
    build_plan,
    config_document,
    load_catalog,
    load_json,
    merge_symbols,
    resolve,
    role_view_manifest,
    validate_sdk_lock,
    validate_sdk_import_receipt,
    validate_sdk_registry,
    validate_sdk_set,
    validate_board,
    validate_classic_report,
    validate_ir,
    validate_role_view,
    validate_build_plan,
    validate_config_document,
    verify_sdk_bundle,
    framework_check,
    shadow_parity,
    validate_framework_check,
    validate_shadow_ledger,
    validate_shadow_report,
)


REPOSITORY = Path(__file__).resolve().parents[3]


class FrameworkTest(unittest.TestCase):
    def test_versioned_schema_declares_strict_role_and_mode_inputs(self) -> None:
        schema = load_json(SCRIPT_ROOT / "bk7258_composition_schema.json")
        self.assertEqual(schema["schema"], "bk7258.composition/1")
        self.assertEqual(schema["kind"], "composition-schema")
        self.assertEqual(schema["version"], 1)
        self.assertEqual(schema["enums"]["roles"], ["cp", "ap", "bl2"])
        self.assertIn("bringup", schema["enums"]["modes"])
        self.assertEqual(schema["strict"]["board_selection"], "exactly-one")
        self.assertEqual(schema["strict"]["legacy_fallback"], "forbidden")

    def test_catalog_and_roles_resolve_deterministically(self) -> None:
        catalog = load_catalog(REPOSITORY)
        self.assertEqual(set(catalog["boards"]), {"t5ai_core", "t5_board", "aidk_ai_toy"})
        cp = resolve(REPOSITORY, "t5ai_core_bringup", "cp")
        ap = resolve(REPOSITORY, "t5ai_core_bringup", "ap")
        self.assertIsNone(cp["symbols"]["CONFIG_BK7258_AP_CORE"])
        self.assertEqual(ap["symbols"]["CONFIG_BK7258_AP_CORE"], "y")
        self.assertEqual(cp, resolve(REPOSITORY, "t5ai_core_bringup", "cp"))
        self.assertIs(validate_ir(cp), cp)

    def test_aidk_ai_toy_product_resolves_without_legacy_profile_or_devices(self) -> None:
        cp = resolve(REPOSITORY, "aidk_ai_toy_bringup", "cp")
        self.assertEqual(cp["inputs"]["board"], "aidk_ai_toy")
        self.assertIsNone(cp["inputs"]["legacy_profile"])
        plan = build_plan(REPOSITORY, "aidk_ai_toy_bringup")
        self.assertEqual(plan["board"]["variant"], "aidk_ai_toy")
        self.assertFalse(plan["legacy_adapter"]["invoked"])

    def test_exact_board_mode_and_symbol_conflicts_fail_closed(self) -> None:
        with self.assertRaises(FrameworkError):
            resolve(REPOSITORY, "t5ai_core_bringup", "cp", board_id="t5_board")
        with self.assertRaises(FrameworkError):
            resolve(REPOSITORY, "t5ai_core_bringup", "cp", mode="application")
        with self.assertRaises(FrameworkError):
            resolve(REPOSITORY, "unknown", "cp")
        with self.assertRaises(FrameworkError):
            merge_symbols([{"symbols": {"CONFIG_X": "y"}}, {"symbols": {"CONFIG_X": None}}])

    def test_strict_duplicate_and_ir_identity_checks(self) -> None:
        with tempfile.TemporaryDirectory(prefix="bk7258-framework-") as directory:
            duplicate = Path(directory) / "duplicate.json"
            duplicate.write_text('{"a":1,"a":2}\n', encoding="utf-8")
            with self.assertRaises(FrameworkError):
                load_json(duplicate)
        board = copy.deepcopy(load_catalog(REPOSITORY)["boards"]["t5ai_core"])
        board["bindings"]["console"]["rts_reset"] = True
        with self.assertRaises(FrameworkError):
            validate_board(board)
        ir = resolve(REPOSITORY, "t5ai_core_bringup", "cp")
        broken = copy.deepcopy(ir)
        broken["identity_sha256"] = "0" * 64
        with self.assertRaises(FrameworkError):
            validate_ir(broken)

    def test_classic_report_is_explicit_adapter_boundary(self) -> None:
        report = classic_report(REPOSITORY)
        self.assertEqual(report["status"], "feasible-with-adapter")
        self.assertFalse(report["proven"])
        self.assertFalse(report["isolation_proven"])
        self.assertTrue(report["repository_relative_source_view"])
        self.assertIs(validate_classic_report(report), report)
        self.assertNotIn(str(REPOSITORY), canonical_json(report).decode())

    def test_role_view_isolated_and_cmake_adapter_is_non_legacy(self) -> None:
        ir = resolve(REPOSITORY, "t5ai_core_bringup", "cp")
        view = role_view_manifest(ir)
        self.assertTrue(view["source_view"]["read_only"])
        self.assertTrue(view["build_view"]["role_local"])
        self.assertFalse(view["build_view"]["shared_config"])
        self.assertFalse(view["legacy_semantics"]["invoked"])
        self.assertIs(validate_role_view(view), view)
        cmake = cmake_view(ir)
        self.assertIn("BK7258_COMPOSITION_ROLE_BUILD_VIEW", cmake)
        self.assertIn("BK7258_COMPOSITION_SHARED_CONFIG_FORBIDDEN TRUE", cmake)
        self.assertIn("BK7258_COMPOSITION_LEGACY_BUILDER_INVOKED FALSE", cmake)

    def test_sdk_registry_set_lock_are_immutable_metadata_and_bl2_is_empty(self) -> None:
        registry = load_json(SCRIPT_ROOT / "bk7258_sdk_registry.json")
        self.assertIs(validate_sdk_registry(REPOSITORY, registry), registry)
        sdk_set = load_json(SCRIPT_ROOT / "bk7258_sdk_set.json")
        self.assertIs(validate_sdk_set(sdk_set, registry), sdk_set)
        lock = load_json(SCRIPT_ROOT / "bk7258_sdk_lock.json")
        self.assertIs(validate_sdk_lock(
            REPOSITORY, SCRIPT_ROOT / "bk7258_sdk_registry.json",
            SCRIPT_ROOT / "bk7258_sdk_set.json", lock, registry, sdk_set), lock)
        self.assertFalse(next(item for item in registry["entries"]
                              if item["role"] == "ap" and item["version"] == "v3.1.1.9")["source_reproducible"])
        self.assertIsNone(sdk_set["roles"]["bl2"])
        self.assertFalse(registry["policy"]["private_mirror"]["redistribution_authorized"])

    def test_sdk_bundle_verifier_rejects_extra_and_symlink_entries(self) -> None:
        with tempfile.TemporaryDirectory(prefix="bk7258-sdk-verify-") as directory:
            root = Path(directory)
            manifest = root / "board/bk7258/scripts/sdk-manifests/test/cp.sha256"
            manifest.parent.mkdir(parents=True)
            payload = b"fixture\n"
            payload_hash = hashlib.sha256(payload).hexdigest()
            manifest.write_text(f"{payload_hash}  include/a.h\n", encoding="utf-8")
            bundle = root / "bundle"
            (bundle / "include").mkdir(parents=True)
            (bundle / "config").mkdir()
            (bundle / "libs").mkdir()
            (bundle / "include/a.h").write_bytes(payload)
            entry = {"id": "sha256:" + "0" * 64,
                     "manifest_path": "board/bk7258/scripts/sdk-manifests/test/cp.sha256",
                     "manifest_sha256": hashlib.sha256(manifest.read_bytes()).hexdigest()}
            result = verify_sdk_bundle(root, entry, bundle)
            self.assertEqual(result["file_count"], 1)
            (bundle / "config/extra").write_bytes(b"extra")
            with self.assertRaises(FrameworkError):
                verify_sdk_bundle(root, entry, bundle)
            (bundle / "config/extra").unlink()
            os.symlink("../include/a.h", bundle / "libs/link")
            with self.assertRaises(FrameworkError):
                verify_sdk_bundle(root, entry, bundle)

    def test_sdk_import_receipt_is_non_mutating(self) -> None:
        from bk7258_framework import sdk_import_receipt

        entry_id = "sha256:" + "1" * 64
        receipt = sdk_import_receipt(
            {"id": entry_id, "content_digest": entry_id,
             "manifest_sha256": "2" * 64,
             "provenance_sha256": "3" * 64, "source_reproducible": False},
            {"file_count": 1})
        self.assertIs(validate_sdk_import_receipt(receipt), receipt)
        self.assertFalse(receipt["bytes_copied"])
        self.assertFalse(receipt["network_used"])

    def test_product_config_and_isolated_boot_runtime_plan(self) -> None:
        cp_ir = resolve(REPOSITORY, "t5ai_core_bringup", "cp")
        cp_config = config_document(cp_ir)
        self.assertIs(validate_config_document(cp_config), cp_config)
        self.assertIn("CONFIG_BK7258_AP_CORE is not set", cp_config["defconfig"])
        plan = build_plan(REPOSITORY, "t5ai_core_bringup")
        self.assertIs(validate_build_plan(plan), plan)
        self.assertEqual(set(plan["roles"]), {"bl1", "bl2", "cp", "ap"})
        self.assertIsNone(plan["roles"]["bl2"]["sdk"])
        self.assertEqual(plan["roles"]["bl2"]["config_kind"], "minimal-make-inputs")
        self.assertFalse(plan["roles"]["bl2"]["fake_nuttx_seed"])
        self.assertFalse(plan["legacy_adapter"]["invoked"])
        build_paths = {item["build_root_template"] for item in plan["roles"].values()}
        artifact_paths = {item["artifact_root_template"] for item in plan["roles"].values()}
        config_paths = {item["config_path_template"] for item in plan["roles"].values()}
        self.assertEqual(len(build_paths), 4)
        self.assertEqual(len(artifact_paths), 4)
        self.assertEqual(len(config_paths), 4)

    def test_p9a_shadow_covers_all_profiles_without_fake_green(self) -> None:
        ledger = load_json(SCRIPT_ROOT / "bk7258_shadow_ledger.json")
        self.assertIs(validate_shadow_ledger(REPOSITORY, ledger), ledger)
        report = shadow_parity(REPOSITORY)
        self.assertIs(validate_shadow_report(report), report)
        self.assertEqual(report["profile_count"], 27)
        self.assertEqual({row["status"] for row in report["rows"]}, {"MIGRATION_PENDING"})
        self.assertTrue(all(row["rationale"] for row in report["rows"]))
        self.assertTrue(all("metadata" in row["old"] and "package_plan" in row["new"]
                            for row in report["rows"]))

    def test_p9a_framework_check_is_bounded_and_dry_run(self) -> None:
        result = framework_check(REPOSITORY)
        self.assertIs(validate_framework_check(result), result)
        self.assertEqual(result["status"], "PASS")
        self.assertEqual(len(result["checks"]), 11)
        self.assertFalse(result["hardware_accessed"])
        self.assertFalse(result["network_used"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
