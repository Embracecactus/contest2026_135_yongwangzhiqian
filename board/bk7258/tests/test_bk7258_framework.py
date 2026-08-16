#!/usr/bin/env python3
"""Fast schema and resolver checks for the canonical BK7258 script path."""

from __future__ import annotations

import copy
import hashlib
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT_ROOT = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPT_ROOT))

from bk7258_framework import (  # noqa: E402
    FrameworkError,
    _defconfig_text,
    canonical_json,
    classic_report,
    cmake_view,
    build_plan,
    execution_context,
    config_document,
    load_catalog,
    load_json,
    merge_symbols,
    pack_prepare,
    pack_verify,
    relative_path,
    resolve,
    role_view_manifest,
    validate_sdk_lock,
    validate_sdk_import_receipt,
    validate_sdk_registry,
    validate_sdk_set,
    validate_board,
    validate_classic_report,
    validate_ir,
    validate_product,
    validate_role_view,
    validate_build_plan,
    validate_execution_context,
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
        self.assertIn("partition_layout",
                      schema["documents"]["product"]["required"])
        self.assertEqual(schema["strict"]["standard_artifact_contract"], {
            "cp": "vela_nuttx_cp.bin",
            "ap": "vela_nuttx_ap.bin",
            "manifest": "vela_nuttx_manifest.json",
        })
        build_schema = load_json(SCRIPT_ROOT / "bk7258_build_plan_schema.json")
        self.assertIn("partition_layout",
                      build_schema["documents"]["resolved-role-config"]["required"])
        self.assertIn("partition_layout",
                      build_schema["documents"]["isolated-build-plan"]["required"])
        self.assertEqual(build_schema["strict"]["standard_artifact_contract"]["cp"],
                         "vela_nuttx_cp.bin")

    def test_catalog_and_roles_resolve_deterministically(self) -> None:
        catalog = load_catalog(REPOSITORY)
        self.assertEqual(set(catalog["boards"]), {"t5ai_core", "t5_board", "aidk_ai_toy"})
        cp = resolve(REPOSITORY, "t5ai_core_bringup", "cp")
        ap = resolve(REPOSITORY, "t5ai_core_bringup", "ap")
        self.assertIsNone(cp["symbols"]["CONFIG_BK7258_AP_CORE"])
        self.assertEqual(ap["symbols"]["CONFIG_BK7258_AP_CORE"], "y")
        board_selectors = {
            "CONFIG_BK7258_BOARD_AIDK_AI_TOY",
            "CONFIG_BK7258_BOARD_T5_BOARD",
            "CONFIG_BK7258_BOARD_T5AI_CORE",
        }
        self.assertEqual({key for key in board_selectors
                          if cp["symbols"].get(key) == "y"},
                         {"CONFIG_BK7258_BOARD_T5AI_CORE"})
        self.assertEqual(cp, resolve(REPOSITORY, "t5ai_core_bringup", "cp"))
        self.assertIs(validate_ir(cp), cp)

    def test_partition_layout_is_product_pinned_and_identity_bound(self) -> None:
        expected = {
            "layout_id": "bk7258-v3119-ab-124ebfab37ca1fcd",
            "layout_sha256": "124ebfab37ca1fcd9971c5aba7b9f214f0500df74cdc394c88ec602020732d8a",
            "source": "board/bk7258/partitions/bk7258/auto_partitions.csv",
        }
        catalog = load_catalog(REPOSITORY)
        for product in catalog["products"].values():
            self.assertEqual(product["partition_layout"], expected)

        ir = resolve(REPOSITORY, "aidk_ai_toy_bringup", "cp")
        self.assertEqual(ir["inputs"]["partition_layout"], expected)
        config = config_document(ir)
        self.assertEqual(config["partition_layout"], expected)
        self.assertIn(
            f"BK7258_PARTITION_LAYOUT_SHA256={expected['layout_sha256']}",
            config["defconfig"])

        plan = build_plan(REPOSITORY, "aidk_ai_toy_bringup")
        self.assertEqual(plan["partition_layout"], expected)
        self.assertEqual(plan["identity_inputs"]["partition_layout_id"],
                         expected["layout_id"])
        self.assertEqual(plan["identity_inputs"]["partition_layout_sha256"],
                         expected["layout_sha256"])

        package = pack_prepare(REPOSITORY, "aidk_ai_toy_bringup")
        self.assertEqual(package["partition"]["source"], expected["source"])
        self.assertEqual(package["partition"]["layout_id"], expected["layout_id"])
        self.assertEqual(package["partition"]["layout_sha256"],
                         expected["layout_sha256"])
        self.assertFalse(pack_verify(REPOSITORY, package)["hardware_verified"])
        archive_templates = {
            row["name"]: row["path_template"]
            for row in package["artifacts"]
            if row["name"] in {"libarch.a", "libboard.a"}
        }
        self.assertTrue(all("/{cp,ap}/" in value
                            for value in archive_templates.values()))
        repeated = pack_prepare(
            REPOSITORY, "aidk_ai_toy_bringup",
            partition_path=Path(expected["source"]))
        self.assertEqual(repeated, package)
        with self.assertRaises(FrameworkError):
            pack_prepare(
                REPOSITORY, "aidk_ai_toy_bringup",
                partition_path=Path(
                    "board/bk7258/partitions/bk7258/secureboot_xip_cp_ap.csv"))
        with self.assertRaises(FrameworkError):
            # A traversal alias that normalizes to the selected CSV must
            # still be rejected; only the exact catalog path is accepted.
            pack_prepare(
                REPOSITORY, "aidk_ai_toy_bringup",
                partition_path=Path(
                    "board/bk7258/partitions/../partitions/bk7258/"
                    "auto_partitions.csv"))
        with self.assertRaises(FrameworkError):
            pack_prepare(
                REPOSITORY, "aidk_ai_toy_bringup",
                partition_path=(REPOSITORY / expected["source"]))

        config_mismatch = copy.deepcopy(config)
        config_mismatch["partition_layout"]["source"] = \
            "board/bk7258/partitions/bk7258/secureboot_xip_cp_ap.csv"
        with self.assertRaises(FrameworkError):
            validate_config_document(config_mismatch)

        plan_mismatch = copy.deepcopy(plan)
        plan_mismatch["identity_inputs"]["partition_layout_sha256"] = "0" * 64
        with self.assertRaises(FrameworkError):
            validate_build_plan(plan_mismatch)

        incomplete_product = copy.deepcopy(
            catalog["products"]["aidk_ai_toy_bringup"])
        del incomplete_product["partition_layout"]
        with self.assertRaises(FrameworkError):
            validate_product(incomplete_product)

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
        with self.assertRaises(FrameworkError):
            relative_path("board/bk7258_t5ai/chip", "retired source")

        def resign(ir: dict[str, object]) -> dict[str, object]:
            body = copy.deepcopy(ir)
            body.pop("identity_sha256")
            ir["identity_sha256"] = hashlib.sha256(
                canonical_json(body)).hexdigest()
            return ir

        aidk = resolve(REPOSITORY, "aidk_ai_toy_bringup", "cp")
        doubled = copy.deepcopy(aidk)
        doubled["symbols"]["CONFIG_BK7258_BOARD_T5AI_CORE"] = "y"
        with self.assertRaises(FrameworkError):
            validate_ir(resign(doubled))

        missing = copy.deepcopy(aidk)
        del missing["symbols"]["CONFIG_BK7258_BOARD_AIDK_AI_TOY"]
        with self.assertRaises(FrameworkError):
            validate_ir(resign(missing))

        mismatched = copy.deepcopy(aidk)
        del mismatched["symbols"]["CONFIG_BK7258_BOARD_AIDK_AI_TOY"]
        mismatched["symbols"]["CONFIG_BK7258_BOARD_T5AI_CORE"] = "y"
        with self.assertRaises(FrameworkError):
            validate_ir(resign(mismatched))

        def resign_config(document: dict[str, object]) -> dict[str, object]:
            document["defconfig"] = _defconfig_text(
                document["inputs"], document["symbols"],
                document["ir_identity_sha256"])
            document["defconfig_sha256"] = hashlib.sha256(
                document["defconfig"].encode()).hexdigest()
            body = copy.deepcopy(document)
            body.pop("identity_sha256")
            document["identity_sha256"] = hashlib.sha256(
                canonical_json(body)).hexdigest()
            return document

        config = config_document(aidk)
        config_doubled = copy.deepcopy(config)
        config_doubled["symbols"]["CONFIG_BK7258_BOARD_T5AI_CORE"] = "y"
        with self.assertRaises(FrameworkError):
            validate_config_document(resign_config(config_doubled))

        config_missing = copy.deepcopy(config)
        del config_missing["symbols"]["CONFIG_BK7258_BOARD_AIDK_AI_TOY"]
        with self.assertRaises(FrameworkError):
            validate_config_document(resign_config(config_missing))

        config_mismatched = copy.deepcopy(config)
        del config_mismatched["symbols"]["CONFIG_BK7258_BOARD_AIDK_AI_TOY"]
        config_mismatched["symbols"]["CONFIG_BK7258_BOARD_T5AI_CORE"] = "y"
        with self.assertRaises(FrameworkError):
            validate_config_document(resign_config(config_mismatched))

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
            (bundle / "libs/link").unlink()
            os.symlink(bundle, root / "bundle-link")
            with self.assertRaises(FrameworkError):
                verify_sdk_bundle(root, entry, root / "bundle-link")

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
        self.assertIsNone(plan["bl2_image_logical_size"])
        self.assertFalse(plan["legacy_adapter"]["invoked"])
        build_paths = {item["build_root_template"] for item in plan["roles"].values()}
        artifact_paths = {item["artifact_root_template"] for item in plan["roles"].values()}
        config_paths = {item["config_path_template"] for item in plan["roles"].values()}
        self.assertEqual(len(build_paths), 4)
        self.assertEqual(len(artifact_paths), 4)
        self.assertEqual(len(config_paths), 4)

    def test_build_plan_active_roles_and_applicability_are_identity_bound(self) -> None:
        raw = build_plan(REPOSITORY, "t5ai_core_bringup")
        self.assertEqual(raw["active_roles"], ["bl1", "cp", "ap"])
        self.assertEqual(raw["identity_inputs"]["active_roles"], raw["active_roles"])
        self.assertEqual(raw["roles"]["bl2"]["activation"], "inactive")
        self.assertEqual(raw["roles"]["bl2"]["applicability"], "not-applicable")
        mcuboot = build_plan(REPOSITORY, "t5_board_bringup")
        self.assertEqual(mcuboot["active_roles"], ["bl1", "bl2", "cp", "ap"])
        self.assertEqual(mcuboot["bl2_image_logical_size"], 0x3000)
        self.assertEqual(mcuboot["identity_inputs"]["bl2_image_logical_size"], 0x3000)
        tampered = copy.deepcopy(raw)
        tampered["roles"]["bl2"]["activation"] = "active"
        with self.assertRaises(FrameworkError):
            validate_build_plan(tampered)
        backend_tampered = copy.deepcopy(mcuboot)
        backend_tampered["roles"]["cp"]["backend"] = "minimal-make"
        with self.assertRaises(FrameworkError):
            validate_build_plan(backend_tampered)

    def test_execute_defaults_to_host_only_context_for_all_products(self) -> None:
        for product in ("t5ai_core_bringup", "aidk_ai_toy_bringup",
                        "t5_board_bringup"):
            context = execution_context(REPOSITORY, product)
            self.assertIs(validate_execution_context(context), context)
            self.assertEqual(context["execution_mode"], "dry-run")
            self.assertFalse(context["side_effects"]["compile_invoked"])
            self.assertFalse(context["side_effects"]["key_read"])
            self.assertFalse(context["side_effects"]["bytes_written"])
            self.assertEqual(context["adapter_semantic_parity"], "unproven")
            self.assertEqual(context["adapter_execution"], {
                "kind": "shared-legacy-adapter",
                "consumes_role_build_roots": False,
                "role_paths_executed": False,
            })
            self.assertEqual(context["profiles"]["root"],
                             "adapter-owned-temporary")
            self.assertEqual(set(context["environment"]),
                             {"BK7258_PRODUCT", "BK7258_OUTPUT_ROOT"})
            self.assertEqual(
                context["environment"]["BK7258_OUTPUT_ROOT"], "${OUTPUT}")
            self.assertEqual(
                context["sdk"]["versions"]["cp"], "v3.1.1.9")
            self.assertEqual(
                len(context["profiles"]["seed_profiles"]["cp"]
                    ["materialized_defconfig_sha256"]), 64)

    def test_execute_rejects_build_flag_and_profile_checks_all_products(self) -> None:
        with tempfile.TemporaryDirectory(prefix="bk7258-execute-cli-") as directory:
            output = Path(directory) / "context.json"
            result = subprocess.run(
                [sys.executable, str(SCRIPT_ROOT / "bk7258_framework.py"),
                 "--root", str(REPOSITORY), "execute",
                 "--product", "t5_board_bringup", "--build",
                 "--out", str(output)],
                cwd=REPOSITORY, capture_output=True, text=True, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("unrecognized arguments: --build", result.stderr)
            self.assertFalse(output.exists())

        build_dual = SCRIPT_ROOT / "build_dual_image.sh"
        for product in ("t5ai_core_bringup", "t5_board_bringup",
                        "aidk_ai_toy_bringup"):
            environment = os.environ.copy()
            environment.update({
                "BK7258_PRODUCT": product,
                "BK7258_PROFILE_CHECK_ONLY": "YES",
            })
            result = subprocess.run(
                [str(build_dual)], cwd=REPOSITORY, env=environment,
                capture_output=True, text=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_canonical_validation_suite_checks_bind_compat_without_retired_dirs(self) -> None:
        script = (SCRIPT_ROOT / "build_dual_image.sh").read_text(encoding="utf-8")
        for suite, compat in (
            ("audio_dac", "t5_board_audio_dac_validation_mcuboot_v2"),
            ("jpeg_m2m", "t5_board_jpeg_m2m_validation_mcuboot_v1"),
        ):
            with self.subTest(suite=suite):
                suite_pos = script.index(f"BK7258_{suite.upper()}_VALIDATION_SUITE")
                compat_pos = script.index(
                    f"BK7258_{suite.upper()}_VALIDATION_COMPAT={compat}"
                )
                self.assertLess(compat_pos, suite_pos + 160)
                self.assertIn(
                    f'if [[ "${{BK7258_VALIDATION_SUITE}}" == '
                    f'"${{BK7258_{suite.upper()}_VALIDATION_SUITE}}" ]]; then',
                    script,
                )
        self.assertNotIn("BK7258_AUDIO_DAC_VALIDATION_CP=", script)
        self.assertNotIn("BK7258_AUDIO_DAC_VALIDATION_AP=", script)
        self.assertNotIn("BK7258_JPEG_M2M_VALIDATION_CP=", script)
        self.assertNotIn("BK7258_JPEG_M2M_VALIDATION_AP=", script)

    def test_validation_suite_profiles_materialize_for_profile_check_only(self) -> None:
        build_dual = SCRIPT_ROOT / "build_dual_image.sh"
        expected = {
            "audio_dac": "t5_board_audio_dac_validation_mcuboot_v2",
            "jpeg_m2m": "t5_board_jpeg_m2m_validation_mcuboot_v1",
        }
        for suite, compat in expected.items():
            environment = os.environ.copy()
            environment.update({
                "BK7258_PRODUCT": "t5_board_bringup",
                "BK7258_VALIDATION_SUITE": suite,
                "BK7258_PROFILE_CHECK_ONLY": "YES",
            })
            result = subprocess.run(
                [str(build_dual)], cwd=REPOSITORY, env=environment,
                capture_output=True, text=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(f"compat={compat}", result.stdout)
            self.assertIn("profile PASS", result.stdout)

        environment = os.environ.copy()
        environment.update({
            "BK7258_PRODUCT": "t5ai_core_bringup",
            "BK7258_VALIDATION_SUITE": "audio_dac",
            "BK7258_PROFILE_CHECK_ONLY": "YES",
        })
        result = subprocess.run(
            [str(build_dual)], cwd=REPOSITORY, env=environment,
            capture_output=True, text=True, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("bound to t5_board_bringup", result.stderr)

    def test_p9a_shadow_covers_all_profiles_without_fake_green(self) -> None:
        ledger = load_json(SCRIPT_ROOT / "bk7258_shadow_ledger.json")
        self.assertIs(validate_shadow_ledger(REPOSITORY, ledger), ledger)
        report = shadow_parity(REPOSITORY)
        self.assertIs(validate_shadow_report(report), report)
        self.assertEqual(report["profile_count"], 27)
        statuses = {row["status"] for row in report["rows"]}
        self.assertEqual(statuses, {"shadow-equivalent", "retire-blocked-hardware"})
        self.assertNotIn("PASS", statuses)
        self.assertNotIn("EXACT", statuses)
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
