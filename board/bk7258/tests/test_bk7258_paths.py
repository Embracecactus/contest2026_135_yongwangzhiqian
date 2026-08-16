#!/usr/bin/env python3
"""bk7258_paths 正负测试（P1 路径解析层）。

注意：本机若存在 OpenVela workspace 映射（vendor/openvela/boards/...），
Bk7258Layout() 会自动判定为 manifest-mapped 形态——这是预期行为。
测试因此刻意与环境解耦：source-work 用 /tmp 隔离树，manifest 用临时 ws。
"""

from __future__ import annotations

import os
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS_BK7258 = Path(__file__).resolve().parents[3] / "tools" / "bk7258"
if str(TOOLS_BK7258) not in sys.path:
    sys.path.insert(0, str(TOOLS_BK7258))

import bk7258_paths as P  # noqa: E402
from bk7258_paths import PathResolutionError  # noqa: E402


def _make_source_work_tree(tmp: str) -> Path:
    """构造一个不在任何 workspace 内的 contest 源仓树。"""
    root = Path(tmp) / "contest_src"
    (root / "board/bk7258").mkdir(parents=True)
    (root / "tools/bk7258").mkdir(parents=True)
    return root


class TestSourceWorkForm(unittest.TestCase):
    def test_source_work_detected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = _make_source_work_tree(tmp)
            lay = P.Bk7258Layout(contest_root=str(root))
            self.assertEqual(lay.form, "source-work")
            self.assertEqual(lay.contest_root, root)
            self.assertEqual(lay.board_dir, root / "board" / "bk7258")
            self.assertEqual(lay.tools_dir, root / "tools" / "bk7258")

    def test_derived_paths(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = _make_source_work_tree(tmp)
            lay = P.Bk7258Layout(contest_root=str(root))
            self.assertEqual(lay.scripts_dir, lay.board_dir / "scripts")
            self.assertEqual(lay.partition_dir, lay.board_dir / "partitions")
            self.assertEqual(lay.sdk_dir, lay.board_dir / "bk_idk")
            self.assertEqual(
                lay.sdk_versions_dir,
                lay.board_dir / "bk_idk" / "armino_as_lib" / "versions",
            )
            self.assertEqual(
                lay.sdk_manifests_dir, lay.board_dir / "bk_idk" / "manifests"
            )
            self.assertTrue(str(lay.build_root).endswith(".build/bk7258"))
            self.assertEqual(lay.output_dir, lay.build_root / "output")


class TestManifestMappedForm(unittest.TestCase):
    def _make_ws(self, tmp):
        ws = Path(tmp) / "ws"
        (ws / "vendor/openvela/boards/contest2026_135_bk7258").mkdir(parents=True)
        (ws / "vendor/openvela/tools/contest2026_135_bk7258").mkdir(parents=True)
        return ws

    def test_manifest_form_detected(self):
        with tempfile.TemporaryDirectory() as tmp:
            ws = self._make_ws(tmp)
            lay = P.Bk7258Layout(workspace_root=str(ws))
            self.assertEqual(lay.form, "manifest-mapped")
            self.assertEqual(
                lay.board_dir,
                ws / "vendor/openvela/boards/contest2026_135_bk7258",
            )
            self.assertEqual(
                lay.tools_dir,
                ws / "vendor/openvela/tools/contest2026_135_bk7258",
            )

    def test_manifest_form_via_env(self):
        with tempfile.TemporaryDirectory() as tmp:
            ws = self._make_ws(tmp)
            os.environ["OPENVELA_WORKSPACE_ROOT"] = str(ws)
            try:
                lay = P.Bk7258Layout()
                self.assertEqual(lay.form, "manifest-mapped")
            finally:
                del os.environ["OPENVELA_WORKSPACE_ROOT"]


class TestIsolatedSnapshot(unittest.TestCase):
    def test_explicit_contest_root_is_snapshot_like(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = _make_source_work_tree(tmp)
            lay = P.Bk7258Layout(contest_root=str(root))
            self.assertEqual(lay.form, "source-work")
            self.assertEqual(lay.tools_dir, root / "tools" / "bk7258")


class TestAmbientLayout(unittest.TestCase):
    def test_ambient_form_valid_and_resolvable(self):
        lay = P.Bk7258Layout()
        self.assertIn(lay.form, ("source-work", "manifest-mapped"))
        # board 映射在 workspace 中已由 repo sync 物化；tools 映射依赖后续 sync，
        # 此处只校验路径形态正确、board 已物化。
        self.assertTrue(lay.board_dir.exists())
        if lay.form == "manifest-mapped":
            self.assertEqual(
                lay.tools_dir,
                lay.workspace_root / P.MANIFEST_TOOLS_REL,
            )
        else:
            self.assertEqual(lay.tools_dir, lay.contest_root / P.CONTEST_TOOLS_REL)


class TestSafeJoinNegative(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name) / "root"
        self.root.mkdir()

    def tearDown(self):
        self.tmp.cleanup()

    def test_absolute_part_rejected(self):
        with self.assertRaises(PathResolutionError):
            P.safe_join(self.root, "/etc/passwd")

    def test_double_dot_rejected(self):
        with self.assertRaises(PathResolutionError):
            P.safe_join(self.root, "..", "etc")

    def test_escape_rejected(self):
        with self.assertRaises(PathResolutionError):
            P.safe_join(self.root, "a", "..", "..", "etc")

    def test_normal_join_ok(self):
        got = P.safe_join(self.root, "a", "b")
        self.assertEqual(got, self.root / "a" / "b")

    def test_symlink_escape_rejected(self):
        outside = Path(self.tmp.name) / "outside"
        outside.mkdir()
        (self.root / "link").symlink_to(outside)
        with self.assertRaises(PathResolutionError):
            P.safe_join(self.root, "link", "secret")

    def test_host_home_root_rejected(self):
        with self.assertRaises(PathResolutionError):
            P._as_safe_root("/home/lijian/foo")


class TestDiscoverNegative(unittest.TestCase):
    def test_bad_contest_root_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(PathResolutionError):
                P.discover_contest_root(tmp)


if __name__ == "__main__":
    unittest.main(verbosity=2)
