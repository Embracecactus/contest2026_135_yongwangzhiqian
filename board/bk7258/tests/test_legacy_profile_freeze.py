#!/usr/bin/env python3
"""Focused host tests for the BK7258 27-to-3 profile cutover."""

from __future__ import annotations

import copy
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
SCRIPT_DIR = REPO_ROOT / "tools" / "bk7258"
sys.path.insert(0, str(SCRIPT_DIR))

from verify_legacy_profile_freeze import (  # noqa: E402
    APPROVED_BASE_COMMIT,
    CONFIGS_REL,
    CUTOVER_README_REL,
    CUTOVER_SEED_FILE_NAMES,
    FreezeError,
    assert_current_matches_cutover,
    base_snapshot,
    canonical_json,
    check_manifest,
    load_json,
    manifest_digest,
    parse_profile,
    _validate_cutover_manifest_shape,
    validate_cutover_ledger,
    walk_current_entries,
)


MANIFEST_REL = Path("tools/bk7258/legacy_profile_freeze_manifest.json")
LEDGER_REL = Path("tools/bk7258/legacy_profile_migration_ledger.json")
MANIFEST = load_json(SCRIPT_DIR / MANIFEST_REL.name)
SNAPSHOT = base_snapshot(REPO_ROOT)
RETAINED = tuple(MANIFEST["retained_profiles"])
RETIRED = tuple(MANIFEST["retired_profiles"])


def fixture() -> tuple[tempfile.TemporaryDirectory[str], Path]:
    temporary = tempfile.TemporaryDirectory(prefix="bk7258-profile-cutover-")
    root = Path(temporary.name)
    shutil.copytree(REPO_ROOT / CONFIGS_REL, root / CONFIGS_REL)
    ledger = root / LEDGER_REL
    ledger.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(REPO_ROOT / LEDGER_REL, ledger)
    return temporary, root


def write_json(root: Path, relative: Path, value: object) -> None:
    (root / relative).write_bytes(canonical_json(value))


class LegacyProfileFreezeTest(unittest.TestCase):
    def assert_cutover_rejected(self, mutate) -> None:
        temporary, root = fixture()
        try:
            mutate(root)
            with self.assertRaises(FreezeError):
                assert_current_matches_cutover(root, SNAPSHOT, list(RETAINED))
        finally:
            temporary.cleanup()

    def test_positive_exact_cutover_manifest_and_counts(self) -> None:
        result = check_manifest(REPO_ROOT, MANIFEST, require_git=True)
        self.assertEqual(result["profiles"], 3)
        self.assertEqual(result["files"], 7)
        # walk_current_entries excludes the configs root; the root is checked
        # separately by assert_configs_root().
        self.assertEqual(result["entries"], 10)
        self.assertEqual(set(RETAINED), {
            "bl2_mcuboot", "t5ai_core_cp_base", "t5ai_core_ap_base",
        })
        self.assertEqual(len(RETIRED), 24)

    def test_cutover_expected_surface_has_root_readme_three_dirs_six_seeds(self) -> None:
        actual = walk_current_entries(REPO_ROOT)
        expected = {
            CUTOVER_README_REL,
            *(f"{CONFIGS_REL}/{profile}" for profile in RETAINED),
            *(f"{CONFIGS_REL}/{profile}/{name}"
              for profile in RETAINED for name in CUTOVER_SEED_FILE_NAMES),
        }
        self.assertEqual({entry["path"] for entry in actual}, expected)
        self.assertEqual(len(expected), 10)

    def test_retired_directories_and_files_are_absent(self) -> None:
        for profile in RETIRED:
            self.assertFalse((REPO_ROOT / CONFIGS_REL / profile).exists())

        self.assert_cutover_rejected(
            lambda root: (root / CONFIGS_REL / RETIRED[0]).mkdir()
        )

        def add_retired_file(root: Path) -> None:
            directory = root / CONFIGS_REL / RETIRED[1]
            directory.mkdir()
            (directory / "defconfig").write_text("retired\n", encoding="utf-8")

        self.assert_cutover_rejected(add_retired_file)

    def test_readme_is_type_and_mode_only_but_seed_bytes_are_pinned(self) -> None:
        temporary, root = fixture()
        try:
            readme = root / CONFIGS_REL / "README.md"
            readme.write_bytes(readme.read_bytes() + b"\ncutover documentation\n")
            assert_current_matches_cutover(root, SNAPSHOT, list(RETAINED))
        finally:
            temporary.cleanup()

        for profile in RETAINED:
            for name in CUTOVER_SEED_FILE_NAMES:
                def mutate(root: Path, profile=profile, name=name) -> None:
                    path = root / CONFIGS_REL / profile / name
                    path.write_bytes(path.read_bytes() + b"\nCONFIG_CUTOVER_MUTATION=y\n")

                with self.subTest(profile=profile, seed=name):
                    self.assert_cutover_rejected(mutate)

    def test_new_nested_case_and_missing_entries_are_rejected(self) -> None:
        self.assert_cutover_rejected(
            lambda root: (root / CONFIGS_REL / "new_profile").mkdir()
        )

        def nested(root: Path) -> None:
            path = root / CONFIGS_REL / RETAINED[0] / "nested"
            path.mkdir()
            (path / "marker").write_text("x\n", encoding="utf-8")

        self.assert_cutover_rejected(nested)
        self.assert_cutover_rejected(
            lambda root: (root / CONFIGS_REL / "BL2_MCUBOOT").mkdir()
        )
        self.assert_cutover_rejected(
            lambda root: (root / CONFIGS_REL / RETAINED[0] / "defconfig").unlink()
        )

    def test_root_and_entry_types_are_strict(self) -> None:
        temporary, root = fixture()
        try:
            os.chmod(root / CONFIGS_REL, 0o755 ^ 0o100)
            with self.assertRaises(FreezeError):
                assert_current_matches_cutover(root, SNAPSHOT, list(RETAINED))
        finally:
            temporary.cleanup()

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
                assert_current_matches_cutover(root, SNAPSHOT, list(RETAINED))
        finally:
            temporary.cleanup()

    def test_manifest_shape_rules_counts_and_self_digest_are_bound(self) -> None:
        _validate_cutover_manifest_shape(MANIFEST)
        self.assertEqual(MANIFEST["historical_profile_count"], 27)
        self.assertEqual(MANIFEST["profile_count"], 3)
        self.assertEqual(MANIFEST["file_count"], 7)
        self.assertEqual(MANIFEST["manifest_sha256"], manifest_digest(MANIFEST))
        self.assertEqual(MANIFEST["rules"], {
            "approved_base_is_historical_only": True,
            "retained_seed_count_is_exact": True,
            "retired_profile_paths_are_forbidden": True,
            "new_config_directories_forbidden": True,
            "retained_seed_file_bytes_are_sha256_pinned": True,
        })

        for field, value in (("profile_count", 27), ("file_count", 6),
                             ("status", "mutable")):
            candidate = copy.deepcopy(MANIFEST)
            candidate[field] = value
            candidate["manifest_sha256"] = manifest_digest(candidate)
            with self.subTest(field=field):
                with self.assertRaises(FreezeError):
                    _validate_cutover_manifest_shape(candidate)

    def test_manifest_retired_mapping_is_exactly_historical_minus_retained(self) -> None:
        historical = {record["name"] for record in SNAPSHOT["profiles"]}
        self.assertEqual(set(RETIRED), historical - set(RETAINED))
        self.assertEqual(len(historical), 27)

    def test_cutover_ledger_covers_all_historical_profiles(self) -> None:
        ledger, digest = validate_cutover_ledger(
            REPO_ROOT, SNAPSHOT["profiles"]
        )
        self.assertEqual(ledger["status"], "proposal-only")
        self.assertEqual(len(ledger["rows"]), 27)
        self.assertEqual(len(digest), 64)

        temporary, root = fixture()
        try:
            value = load_json(root / LEDGER_REL)
            value["rows"] = value["rows"][:-1]
            write_json(root, LEDGER_REL, value)
            with self.assertRaises(FreezeError):
                validate_cutover_ledger(root, SNAPSHOT["profiles"])
        finally:
            temporary.cleanup()

    def test_metadata_and_duplicate_json_remain_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory(prefix="bk7258-profile-metadata-") as directory:
            path = Path(directory) / "profile.conf"
            path.write_text("BK7258_PROFILE_SCHEMA=1\nBROKEN\n", encoding="utf-8")
            with self.assertRaises(FreezeError):
                parse_profile(path)

        with tempfile.TemporaryDirectory(prefix="bk7258-duplicate-json-") as directory:
            path = Path(directory) / "duplicate.json"
            path.write_text('{"schema":1,"schema":1}\n', encoding="utf-8")
            with self.assertRaises(FreezeError):
                load_json(path)

    def test_cli_verifier_passes_against_cutover_tree(self) -> None:
        result = subprocess.run(
            [sys.executable, str(SCRIPT_DIR / "verify_legacy_profile_freeze.py")],
            cwd=REPO_ROOT, capture_output=True, text=True, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("profiles=3 files=7", result.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
