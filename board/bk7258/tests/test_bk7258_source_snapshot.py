#!/usr/bin/env python3
"""Small host-only contract tests for the BK7258 entity source snapshot."""

from __future__ import annotations

import json
import os
import stat
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parents[3] / "tools" / "bk7258"
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import bk7258_source_snapshot as snapshot  # noqa: E402


class SourceSnapshotTests(unittest.TestCase):
    def _workspace(self, temporary: tempfile.TemporaryDirectory[str]) -> Path:
        workspace = Path(temporary) / "workspace"
        workspace.mkdir()
        for root in snapshot.ROOTS:
            (workspace / root).mkdir()
        return workspace

    def test_dirty_ignored_and_internal_links_are_copied_without_outputs(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            workspace = self._workspace(temporary)
            dirty = workspace / "contest2026_135_yongwangzhiqian/board/bk7258/dirty.c"
            dirty.parent.mkdir(parents=True)
            dirty.write_text("dirty board source\n", encoding="utf-8")

            ignored_openamp = workspace / "external/openamp/ignored_required.c"
            ignored_openamp.parent.mkdir()
            ignored_openamp.write_text("ignored but build-required\n", encoding="utf-8")
            (workspace / "external/.gitignore").write_text(
                "openamp/ignored_required.c\n", encoding="utf-8")
            third_party_build_source = workspace / "external/lib/build/source.c"
            third_party_build_source.parent.mkdir(parents=True)
            third_party_build_source.write_text(
                "third-party source directory named build\n", encoding="utf-8")

            (workspace / "nuttx/.config").write_text("shared config\n", encoding="utf-8")
            (workspace / "nuttx/defconfig").write_text("generated config\n", encoding="utf-8")
            (workspace / "nuttx/Make.defs").write_text("generated link target\n", encoding="utf-8")
            (workspace / "nuttx/generated.elf").write_bytes(b"generated image")
            (workspace / "nuttx/bk7258-old-build").mkdir()
            (workspace / "nuttx/bk7258-old-build/result.bin").write_bytes(b"generated")
            cmake = workspace / "nuttx/CMakeFiles"
            cmake.mkdir()
            (cmake / "CMakeCache.txt").write_text("generated\n", encoding="utf-8")
            board_test_build = workspace / (
                "contest2026_135_yongwangzhiqian/board/bk7258/tests/build")
            board_test_build.mkdir(parents=True)
            (board_test_build / "test.o").write_bytes(b"generated test output")

            bootloader = workspace / (
                "contest2026_135_yongwangzhiqian/board/bk7258/bootloader")
            bootloader.mkdir(parents=True)
            (bootloader / "bl.bin").write_bytes(b"generated image")
            (bootloader / "bl2_crc.bin.json").write_text("{}", encoding="utf-8")
            (bootloader / "boot_main.c").write_text("source\n", encoding="utf-8")

            linked = workspace / "nuttx/dirty-link"
            try:
                linked.symlink_to("../contest2026_135_yongwangzhiqian/board/bk7258/dirty.c")
            except OSError as error:
                self.skipTest(f"symlink unavailable: {error}")

            destination = Path(temporary) / "snapshot"
            manifest = snapshot.snapshot_workspace(workspace, destination)

            self.assertEqual(tuple(manifest["scope"]), snapshot.ROOTS)
            self.assertEqual(set(manifest["roots"]), set(snapshot.ROOTS))
            self.assertTrue((destination / dirty.relative_to(workspace)).is_file())
            self.assertTrue((destination / ignored_openamp.relative_to(workspace)).is_file())
            self.assertTrue(
                (destination / third_party_build_source.relative_to(workspace)).is_file())
            self.assertFalse((destination / "nuttx/.config").exists())
            self.assertFalse((destination / "nuttx/defconfig").exists())
            self.assertFalse((destination / "nuttx/Make.defs").exists())
            self.assertFalse((destination / "nuttx/generated.elf").exists())
            self.assertFalse((destination / "nuttx/bk7258-old-build").exists())
            self.assertFalse((destination / "nuttx/CMakeFiles").exists())
            self.assertFalse(
                (destination / board_test_build.relative_to(workspace)).exists())
            self.assertFalse((destination / bootloader.relative_to(workspace) / "bl.bin").exists())
            self.assertTrue((destination / bootloader.relative_to(workspace) / "boot_main.c").is_file())
            self.assertTrue((destination / "nuttx/dirty-link").is_symlink())
            self.assertEqual(
                os.readlink(destination / "nuttx/dirty-link"),
                "../contest2026_135_yongwangzhiqian/board/bk7258/dirty.c",
            )
            self.assertEqual(
                snapshot.audit_snapshot(destination)["identity_sha256"],
                manifest["identity_sha256"],
            )

    def test_absolute_external_symlink_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            workspace = self._workspace(temporary)
            outside = Path(temporary) / "outside.txt"
            outside.write_text("must not be followed\n", encoding="utf-8")
            link = workspace / "frameworks/external-link"
            try:
                link.symlink_to(outside)
            except OSError as error:
                self.skipTest(f"symlink unavailable: {error}")

            destination = Path(temporary) / "snapshot"
            with self.assertRaises(snapshot.SnapshotError):
                snapshot.snapshot_workspace(workspace, destination)
            # A failed attempt is never cleaned up by the helper.
            self.assertTrue(destination.is_dir())
            self.assertFalse((destination / "frameworks/external-link").exists())

    def test_known_optional_dangling_links_are_excluded_but_unknown_links_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            workspace = self._workspace(temporary)
            optional_links = [
                workspace / "external/android/system/libbase/.clang-format",
                workspace / "apps/netutils/connectedhomeip/pigweed/.dockerignore",
                workspace / "apps/tests",
                workspace / "external/pcre2/pcre2_chartables.c",
            ]
            for link in optional_links:
                link.parent.mkdir(parents=True, exist_ok=True)
                try:
                    link.symlink_to("missing-source")
                except OSError as error:
                    self.skipTest(f"symlink unavailable: {error}")
            unknown = workspace / "frameworks/unknown-dangling-link"
            unknown.symlink_to("missing-source")

            report = snapshot.audit_source_symlinks(workspace, raise_on_error=False)
            self.assertEqual(report["bad_count"], 1)
            self.assertEqual(report["bad"][0]["path"], "frameworks/unknown-dangling-link")
            with self.assertRaises(snapshot.SnapshotError):
                snapshot.audit_source_symlinks(workspace)

            unknown.unlink()
            report = snapshot.audit_source_symlinks(workspace)
            self.assertEqual(report["bad_count"], 0)
            self.assertEqual(report["excluded_symlink_count"], len(optional_links))
            reasons = {item["path"]: item["reason"] for item in report["excluded_symlinks"]}
            self.assertEqual(reasons["apps/tests"], "optional-missing-source-link")
            self.assertEqual(
                reasons["external/pcre2/pcre2_chartables.c"],
                "optional-missing-source-link",
            )
            self.assertEqual(
                reasons["external/android/system/libbase/.clang-format"],
                "optional-metadata-symlink",
            )
            self.assertEqual(
                reasons["apps/netutils/connectedhomeip/pigweed/.dockerignore"],
                "optional-metadata-symlink",
            )

            destination = Path(temporary) / "snapshot"
            manifest = snapshot.snapshot_workspace(workspace, destination)
            for link in optional_links:
                self.assertFalse(destination.joinpath(link.relative_to(workspace)).exists())
            self.assertGreaterEqual(
                manifest["roots"]["external"]["excluded_by_reason"].get(
                    "optional-missing-source-link", 0),
                1,
            )

    def test_hardlinks_are_materialized_as_independent_read_only_files(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            workspace = self._workspace(temporary)
            first = workspace / "apps/a.c"
            second = workspace / "apps/b.c"
            first.write_text("same bytes\n", encoding="utf-8")
            try:
                os.link(first, second)
            except OSError as error:
                self.skipTest(f"hardlink unavailable: {error}")

            destination = Path(temporary) / "snapshot"
            snapshot.snapshot_workspace(workspace, destination)
            first_copy = destination / "apps/a.c"
            second_copy = destination / "apps/b.c"
            self.assertNotEqual(first_copy.stat().st_ino, second_copy.stat().st_ino)
            self.assertEqual(first_copy.stat().st_nlink, 1)
            self.assertEqual(second_copy.stat().st_nlink, 1)
            self.assertFalse(first_copy.stat().st_mode & stat.S_IWUSR)
            self.assertFalse(second_copy.stat().st_mode & stat.S_IWUSR)

    def test_source_mutation_does_not_change_snapshot(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            workspace = self._workspace(temporary)
            source = workspace / "packages/input.txt"
            source.write_text("before\n", encoding="utf-8")
            destination = Path(temporary) / "snapshot"
            manifest = snapshot.snapshot_workspace(workspace, destination)

            source.write_text("after mutation\n", encoding="utf-8")
            self.assertEqual(
                (destination / "packages/input.txt").read_text(encoding="utf-8"),
                "before\n",
            )
            self.assertEqual(
                snapshot.audit_snapshot(destination)["identity_sha256"],
                manifest["identity_sha256"],
            )

    def test_destination_is_fresh_only_except_validated_prepare_marker(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            workspace = self._workspace(temporary)
            existing = Path(temporary) / "existing"
            existing.mkdir()
            with self.assertRaises(snapshot.SnapshotError):
                snapshot.snapshot_workspace(workspace, existing)

            marker_destination = Path(temporary) / "prepared"
            marker_destination.mkdir()
            marker = {
                "schema": "bk7258.role-isolated-execution/1",
                "kind": "entity-source-snapshot-requirement",
                "version": 1,
                "materialized": False,
                "scope": list(snapshot.ROOTS),
            }
            (marker_destination / snapshot.REQUIREMENTS_MARKER_FILENAME).write_text(
                json.dumps(marker), encoding="utf-8")
            manifest = snapshot.snapshot_workspace(workspace, marker_destination)
            self.assertTrue(
                (marker_destination / snapshot.REQUIREMENTS_MARKER_FILENAME).is_file())
            completed_marker = json.loads(
                (marker_destination / snapshot.REQUIREMENTS_MARKER_FILENAME).read_text(
                    encoding="utf-8"))
            self.assertTrue(completed_marker["materialized"])
            self.assertEqual(
                completed_marker["snapshot_identity_sha256"],
                manifest["identity_sha256"],
            )
            self.assertEqual(
                snapshot.audit_snapshot(marker_destination)["identity_sha256"],
                manifest["identity_sha256"],
            )

            real_parent = Path(temporary) / "real-parent"
            real_parent.mkdir()
            linked_parent = Path(temporary) / "linked-parent"
            try:
                linked_parent.symlink_to(real_parent, target_is_directory=True)
            except OSError as error:
                self.skipTest(f"symlink unavailable: {error}")
            with self.assertRaises(snapshot.SnapshotError):
                snapshot.snapshot_workspace(workspace, linked_parent / "snapshot")

            nested_real = real_parent / "nested"
            nested_real.mkdir()
            nested_alias = Path(temporary) / "nested-alias"
            try:
                nested_alias.symlink_to(nested_real, target_is_directory=True)
            except OSError as error:
                self.skipTest(f"symlink unavailable: {error}")
            with self.assertRaises(snapshot.SnapshotError):
                snapshot.snapshot_workspace(workspace, nested_alias / "deeper" / "snapshot")


if __name__ == "__main__":
    unittest.main()
