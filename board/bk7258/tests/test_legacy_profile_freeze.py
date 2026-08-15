#!/usr/bin/env python3
"""Fail-closed host tests for the P0 legacy profile freeze."""

from __future__ import annotations

import copy
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


REPO_ROOT = Path(__file__).resolve().parents[3]
SCRIPT_DIR = REPO_ROOT / "board/bk7258/scripts"
sys.path.insert(0, str(SCRIPT_DIR))

from inventory_legacy_profile_consumers import build_inventory  # noqa: E402
from verify_legacy_profile_freeze import (  # noqa: E402
    APPROVED_BASE_COMMIT,
    CONFIGS_REL,
    FreezeError,
    build_manifest,
    canonical_json,
    check_manifest,
    load_json,
    manifest_digest,
    parse_profile,
    validate_inventory,
    validate_ledger,
    validate_manifest_shape,
)


MANIFEST_REL = Path("board/bk7258/scripts/legacy_profile_freeze_manifest.json")
LEDGER_REL = Path("board/bk7258/scripts/legacy_profile_migration_ledger.json")
INVENTORY_REL = Path("board/bk7258/scripts/legacy_profile_consumers.json")
MANIFEST = load_json(SCRIPT_DIR / MANIFEST_REL.name)


def fixture() -> tuple[tempfile.TemporaryDirectory[str], Path]:
    """Copy every mandatory P0 input; no positive fixture may omit one."""

    temporary = tempfile.TemporaryDirectory(prefix="bk7258-profile-freeze-")
    root = Path(temporary.name)
    shutil.copytree(REPO_ROOT / CONFIGS_REL, root / CONFIGS_REL)
    for relative in (MANIFEST_REL, LEDGER_REL, INVENTORY_REL):
        destination = root / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(REPO_ROOT / relative, destination)
    return temporary, root


def write_fixture_json(root: Path, relative: Path, value: object) -> None:
    (root / relative).write_bytes(canonical_json(value))


class LegacyProfileFreezeTest(unittest.TestCase):
    def assert_rejected(self, mutate) -> None:
        temporary, root = fixture()
        try:
            mutate(root)
            with self.assertRaises(FreezeError):
                check_manifest(root, MANIFEST, require_git=False)
        finally:
            temporary.cleanup()

    def assert_ledger_rejected(self, mutate) -> None:
        temporary, root = fixture()
        try:
            mutate(root)
            with self.assertRaises(FreezeError):
                validate_ledger(root, MANIFEST["profiles"])
        finally:
            temporary.cleanup()

    def test_positive_exact_manifest_and_mandatory_artifacts(self) -> None:
        temporary, root = fixture()
        try:
            result = check_manifest(root, MANIFEST, require_git=False)
            self.assertEqual(result["profiles"], 27)
            self.assertEqual(result["files"], 55)
            self.assertEqual(result["entries"], 82)
            self.assertEqual(result["consumers"], 104)
            self.assertEqual(validate_inventory(root, MANIFEST, require_git=False)["schema"], 1)
        finally:
            temporary.cleanup()

    def test_new_profile_directory_is_rejected(self) -> None:
        self.assert_rejected(lambda root: (root / CONFIGS_REL / "new_profile").mkdir())

    def test_nested_directory_is_rejected(self) -> None:
        def mutate(root: Path) -> None:
            nested = root / CONFIGS_REL / "t5ai_core_cp_base" / "nested"
            nested.mkdir()
            (nested / "marker").write_text("x\n", encoding="utf-8")

        self.assert_rejected(mutate)

    def test_case_variant_is_rejected(self) -> None:
        self.assert_rejected(
            lambda root: (root / CONFIGS_REL / "T5AI_CORE_CP_BASE").mkdir()
        )

    def test_root_symlink_is_rejected(self) -> None:
        temporary, root = fixture()
        try:
            configs = root / CONFIGS_REL
            renamed = root / "board/bk7258/configs-real"
            configs.rename(renamed)
            try:
                configs.symlink_to(renamed, target_is_directory=True)
            except (OSError, NotImplementedError) as error:
                self.skipTest(f"symlink unavailable: {error}")
            with self.assertRaises(FreezeError):
                check_manifest(root, MANIFEST, require_git=False)
        finally:
            temporary.cleanup()

    def test_root_mode_is_rejected(self) -> None:
        temporary, root = fixture()
        try:
            os.chmod(root / CONFIGS_REL, 0o755 ^ 0o100)
            with self.assertRaises(FreezeError):
                check_manifest(root, MANIFEST, require_git=False)
        finally:
            temporary.cleanup()

    def test_symlink_is_rejected_without_following_it(self) -> None:
        def mutate(root: Path) -> None:
            link = root / CONFIGS_REL / "profile_link"
            link.symlink_to("t5ai_core_cp_base", target_is_directory=True)

        temporary, root = fixture()
        try:
            try:
                mutate(root)
            except (OSError, NotImplementedError) as error:
                self.skipTest(f"symlink unavailable: {error}")
            with self.assertRaises(FreezeError):
                check_manifest(root, MANIFEST, require_git=False)
        finally:
            temporary.cleanup()

    def test_special_file_is_rejected(self) -> None:
        temporary, root = fixture()
        try:
            fifo = root / CONFIGS_REL / "special"
            try:
                os.mkfifo(fifo)
            except (AttributeError, OSError) as error:
                self.skipTest(f"FIFO unavailable: {error}")
            with self.assertRaises(FreezeError):
                check_manifest(root, MANIFEST, require_git=False)
        finally:
            temporary.cleanup()

    def test_missing_file_is_rejected(self) -> None:
        self.assert_rejected(
            lambda root: (root / CONFIGS_REL / "t5ai_core_cp_base" / "defconfig").unlink()
        )

    def test_renamed_file_is_rejected(self) -> None:
        def mutate(root: Path) -> None:
            source = root / CONFIGS_REL / "t5ai_core_cp_base" / "defconfig"
            source.rename(source.with_name("Defconfig"))

        self.assert_rejected(mutate)

    def test_in_place_defconfig_mutation_is_rejected(self) -> None:
        def mutate(root: Path) -> None:
            path = root / CONFIGS_REL / "t5ai_core_cp_base" / "defconfig"
            path.write_bytes(path.read_bytes() + b"CONFIG_TEST_FREEZE_MUTATION=y\n")

        self.assert_rejected(mutate)

    def test_missing_metadata_is_rejected(self) -> None:
        self.assert_rejected(
            lambda root: (root / CONFIGS_REL / "t5ai_core_cp_base" / "profile.conf").unlink()
        )

    def test_malformed_metadata_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="bk7258-profile-metadata-") as directory:
            path = Path(directory) / "profile.conf"
            path.write_text("BK7258_PROFILE_SCHEMA=1\nBROKEN\n", encoding="utf-8")
            with self.assertRaises(FreezeError):
                parse_profile(path)

    def test_malformed_pair_metadata_is_rejected(self) -> None:
        def mutate(root: Path) -> None:
            path = root / CONFIGS_REL / "t5ai_core_cp_base" / "profile.conf"
            path.write_text(
                path.read_text(encoding="utf-8").replace(
                    "BK7258_PROFILE_COMPAT=t5ai_core_base_raw_v1",
                    "BK7258_PROFILE_COMPAT=orphan_pair",
                ),
                encoding="utf-8",
            )

        self.assert_rejected(mutate)

    def test_profile_compat_and_sdk_bundle_identifiers_are_safe(self) -> None:
        with tempfile.TemporaryDirectory(prefix="bk7258-profile-identifiers-") as directory:
            path = Path(directory) / "profile.conf"
            base = (
                "BK7258_PROFILE_SCHEMA=1\n"
                "BK7258_PROFILE_BOARD=t5ai_core\n"
                "BK7258_PROFILE_ROLE=cp\n"
                "BK7258_PROFILE_BOOT=raw\n"
                "BK7258_PROFILE_CLASS=runnable\n"
            )
            for field, value in (
                ("BK7258_PROFILE_COMPAT", "../escape"),
                ("BK7258_PROFILE_COMPAT", "contains whitespace"),
                ("BK7258_PROFILE_SDK_BUNDLE", "bundle with whitespace"),
            ):
                text = base + f"{field}={value}\n"
                path.write_text(text, encoding="utf-8")
                with self.assertRaises(FreezeError):
                    parse_profile(path)

    def test_duplicate_json_keys_are_rejected_for_all_p0_documents(self) -> None:
        with tempfile.TemporaryDirectory(prefix="bk7258-duplicate-json-") as directory:
            path = Path(directory) / "duplicate.json"
            for label in ("manifest", "ledger", "inventory"):
                path.write_text('{"schema":1,"schema":1}\n', encoding="utf-8")
                with self.subTest(document=label):
                    with self.assertRaises(FreezeError):
                        load_json(path)

    def test_manifest_schema_paths_modes_hashes_symbols_and_pairs_are_strict(self) -> None:
        cases = []
        malformed = copy.deepcopy(MANIFEST)
        malformed["status"] = "mutable"
        cases.append(("status", malformed))
        malformed = copy.deepcopy(MANIFEST)
        malformed["baseline"]["commit"] = "ancestor-only"
        cases.append(("base commit", malformed))
        malformed = copy.deepcopy(MANIFEST)
        malformed["entries"][0]["path"] = "../escape"
        cases.append(("entry path", malformed))
        malformed = copy.deepcopy(MANIFEST)
        malformed["entries"][0]["mode"] = "0777"
        cases.append(("entry mode", malformed))
        malformed = copy.deepcopy(MANIFEST)
        file_entry = next(entry for entry in malformed["entries"] if entry["type"] == "file")
        file_entry["sha256"] = "not-a-sha"
        cases.append(("entry hash", malformed))
        malformed = copy.deepcopy(MANIFEST)
        malformed["profiles"][0]["defconfig_symbols"] = {"not_config": "y"}
        cases.append(("defconfig symbol", malformed))
        malformed = copy.deepcopy(MANIFEST)
        malformed["profiles"][0]["pair"]["members"].append("unknown_profile")
        cases.append(("pair member", malformed))
        for label, candidate in cases:
            with self.subTest(field=label):
                with self.assertRaises(FreezeError):
                    validate_manifest_shape(candidate)

        malformed = copy.deepcopy(MANIFEST)
        malformed["manifest_sha256"] = "0" * 64
        with self.assertRaises(FreezeError):
            validate_manifest_shape(malformed)

    def test_manifest_digest_is_not_a_free_pass_for_edited_shape(self) -> None:
        candidate = copy.deepcopy(MANIFEST)
        candidate["status"] = "mutable"
        candidate["manifest_sha256"] = manifest_digest(candidate)
        with self.assertRaises(FreezeError):
            validate_manifest_shape(candidate)

    def test_ledger_is_mandatory_regular_and_not_symlink(self) -> None:
        self.assert_rejected(lambda root: (root / LEDGER_REL).unlink())

        temporary, root = fixture()
        try:
            ledger = root / LEDGER_REL
            ledger.unlink()
            try:
                ledger.symlink_to("missing-ledger.json")
            except (OSError, NotImplementedError) as error:
                self.skipTest(f"symlink unavailable: {error}")
            with self.assertRaises(FreezeError):
                check_manifest(root, MANIFEST, require_git=False)
        finally:
            temporary.cleanup()

        temporary, root = fixture()
        try:
            ledger = root / LEDGER_REL
            ledger.unlink()
            try:
                os.mkfifo(ledger)
            except (AttributeError, OSError) as error:
                self.skipTest(f"FIFO unavailable: {error}")
            with self.assertRaises(FreezeError):
                check_manifest(root, MANIFEST, require_git=False)
        finally:
            temporary.cleanup()

    def test_duplicate_ledger_json_is_rejected(self) -> None:
        self.assert_ledger_rejected(
            lambda root: (root / LEDGER_REL).write_text(
                '{"schema":1,"schema":1}\n', encoding="utf-8"
            )
        )

    def test_ledger_schema_kind_status_and_source_are_bound(self) -> None:
        for field, value in (
            ("schema", 99),
            ("kind", "wrong-kind"),
            ("status", "accepted"),
            ("source_manifest", "elsewhere.json"),
        ):
            def mutate(root: Path, field=field, value=value) -> None:
                ledger = load_json(root / LEDGER_REL)
                ledger[field] = value
                write_fixture_json(root, LEDGER_REL, ledger)

            with self.subTest(field=field):
                self.assert_ledger_rejected(mutate)

    def test_ledger_rows_bind_metadata_and_pair(self) -> None:
        for field, value in (
            ("board", "t5ai_core"),
            ("role", "cp"),
            ("boot", "raw"),
            ("class", "runnable"),
            ("pair", "wrong_pair"),
        ):
            def mutate(root: Path, field=field, value=value) -> None:
                ledger = load_json(root / LEDGER_REL)
                ledger["rows"][0][field] = value
                write_fixture_json(root, LEDGER_REL, ledger)

            with self.subTest(field=field):
                self.assert_ledger_rejected(mutate)

    def test_ledger_rejects_tbd_empty_malformed_and_escaping_targets(self) -> None:
        cases = (
            ("migration_state", "TBD"),
            ("review", ""),
            ("target", {"family": "../escape"}),
            ("target", {"family": "bk7258"}),
        )
        for field, value in cases:
            def mutate(root: Path, field=field, value=value) -> None:
                ledger = load_json(root / LEDGER_REL)
                if field == "target" and len(value) == 1:
                    ledger["rows"][0][field] = value
                else:
                    ledger["rows"][0][field] = value
                write_fixture_json(root, LEDGER_REL, ledger)

            with self.subTest(field=field, value=str(value)):
                self.assert_ledger_rejected(mutate)

    def test_ledger_separates_resource_mode_and_validation_suite(self) -> None:
        rows = load_json(SCRIPT_DIR / LEDGER_REL.name)["rows"]
        self.assertTrue(all(row["target"]["resource_mode"] for row in rows))
        self.assertTrue(
            all(
                row["target"]["validation_suite"] is not None
                for row in rows
                if row["class"] in {"validation", "ci"}
            )
        )
        self.assertTrue(
            all(
                row["target"]["validation_suite"] is None
                for row in rows
                if row["class"] not in {"validation", "ci"}
            )
        )

    def test_inventory_is_mandatory_and_duplicate_safe(self) -> None:
        self.assert_rejected(lambda root: (root / INVENTORY_REL).unlink())
        temporary, root = fixture()
        try:
            inventory = root / INVENTORY_REL
            inventory.write_text('{"schema":1,"schema":1}\n', encoding="utf-8")
            with self.assertRaises(FreezeError):
                check_manifest(root, MANIFEST, require_git=False)
        finally:
            temporary.cleanup()

    def test_inventory_schema_base_and_digest_are_strict(self) -> None:
        for field, value in (
            ("status", "mutable"),
            ("source_manifest", "other.json"),
            ("baseline", {"commit": APPROVED_BASE_COMMIT}),
            ("inventory_sha256", "0" * 64),
        ):
            def mutate(root: Path, field=field, value=value) -> None:
                inventory = load_json(root / INVENTORY_REL)
                inventory[field] = value
                write_fixture_json(root, INVENTORY_REL, inventory)

            with self.subTest(field=field):
                self.assert_rejected(mutate)

    def test_invalid_base_generation_fails_without_output(self) -> None:
        with tempfile.TemporaryDirectory(prefix="bk7258-invalid-base-") as directory:
            output = Path(directory) / "manifest.json"
            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT_DIR / "verify_legacy_profile_freeze.py"),
                    "--root",
                    str(REPO_ROOT),
                    "--manifest",
                    str(output),
                    "--generate",
                    "--base-commit",
                    "deadbeef",
                ],
                cwd=REPO_ROOT,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(output.exists())

    def test_dirty_configs_cannot_regenerate_manifest(self) -> None:
        path = REPO_ROOT / CONFIGS_REL / "t5ai_core_cp_base" / "defconfig"
        original = path.read_bytes()
        try:
            path.write_bytes(original + b"CONFIG_P0_DIRTY_GENERATION_TEST=y\n")
            with tempfile.TemporaryDirectory(prefix="bk7258-dirty-generation-") as directory:
                output = Path(directory) / "manifest.json"
                result = subprocess.run(
                    [
                        sys.executable,
                        str(SCRIPT_DIR / "verify_legacy_profile_freeze.py"),
                        "--root",
                        str(REPO_ROOT),
                        "--manifest",
                        str(output),
                        "--generate",
                    ],
                    cwd=REPO_ROOT,
                    capture_output=True,
                    text=True,
                    check=False,
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(output.exists())
        finally:
            path.write_bytes(original)

    def test_inventory_is_exact_base_reproducible_despite_current_files(self) -> None:
        current_only = REPO_ROOT / "board/bk7258/scripts/.p0-current-only-test"
        try:
            current_only.write_text("profile.conf\n", encoding="utf-8")
            actual = build_inventory(REPO_ROOT, MANIFEST)
        finally:
            current_only.unlink(missing_ok=True)
        expected = load_json(SCRIPT_DIR / INVENTORY_REL.name)
        self.assertEqual(actual, expected)
        paths = {consumer["path"] for consumer in actual["consumers"]}
        self.assertNotIn("board/bk7258/scripts/legacy_profile_consumers.json", paths)
        self.assertNotIn("board/bk7258/scripts/verify_legacy_profile_freeze.py", paths)
        self.assertEqual(len(paths), 104)

    def test_inventory_fails_closed_when_git_or_base_is_unavailable(self) -> None:
        with tempfile.TemporaryDirectory(prefix="bk7258-no-git-") as directory:
            with self.assertRaises(FreezeError):
                build_inventory(Path(directory), MANIFEST)
        with patch(
            "inventory_legacy_profile_consumers.verify_approved_base",
            side_effect=FreezeError("Git unavailable"),
        ):
            with self.assertRaises(FreezeError):
                build_inventory(REPO_ROOT, MANIFEST)

    def test_manifest_and_inventory_are_recomputed_from_approved_base(self) -> None:
        self.assertEqual(build_manifest(REPO_ROOT), MANIFEST)
        inventory = build_inventory(REPO_ROOT, MANIFEST)
        self.assertEqual(inventory, load_json(SCRIPT_DIR / INVENTORY_REL.name))


if __name__ == "__main__":
    unittest.main(verbosity=2)
