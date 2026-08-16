#!/usr/bin/env python3
"""bk7258_paths 正负测试（P1 路径解析层）。

测试刻意与本机布局解耦：source-work 与 manifest-mapped 都使用
临时隔离树；只有 ambient 用例检查当前 checkout 的自动探测。
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
    (root / "board/bk7258/scripts").mkdir(parents=True)
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

    def test_explicit_invalid_workspace_fail_closed(self):
        # 显式传入无效 workspace 根必须报错，不得静默回落到源码仓。
        with tempfile.TemporaryDirectory() as tmp:
            empty = Path(tmp) / "not-a-workspace"
            empty.mkdir()
            with self.assertRaises(P.PathResolutionError):
                P.Bk7258Layout(workspace_root=str(empty))

    def test_explicit_invalid_workspace_env_fail_closed(self):
        with tempfile.TemporaryDirectory() as tmp:
            empty = Path(tmp) / "not-a-workspace"
            empty.mkdir()
            os.environ["OPENVELA_WORKSPACE_ROOT"] = str(empty)
            try:
                with self.assertRaises(P.PathResolutionError):
                    P.Bk7258Layout()
            finally:
                del os.environ["OPENVELA_WORKSPACE_ROOT"]

    def test_workspace_missing_tools_mapping_rejected(self):
        # 只有 board 映射、缺少 tools linkfile 的 workspace 必须被拒绝。
        with tempfile.TemporaryDirectory() as tmp:
            ws = Path(tmp) / "ws-partial"
            (ws / "vendor/openvela/boards/contest2026_135_bk7258").mkdir(
                parents=True
            )
            with self.assertRaises(P.PathResolutionError):
                P.Bk7258Layout(workspace_root=str(ws))

    def test_workspace_missing_board_mapping_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            ws = Path(tmp) / "ws-partial"
            (ws / "vendor/openvela/tools/contest2026_135_bk7258").mkdir(
                parents=True
            )
            with self.assertRaises(P.PathResolutionError):
                P.Bk7258Layout(workspace_root=str(ws))

    def test_explicit_workspace_does_not_borrow_parent_mapping(self):
        # workspace_root 语义是“该路径就是根”，不允许向上借用映射。
        with tempfile.TemporaryDirectory() as tmp:
            ws = self._make_ws(tmp)
            child = ws / "unrelated-child"
            child.mkdir()
            with self.assertRaises(P.PathResolutionError):
                P.Bk7258Layout(workspace_root=str(child))

    def test_discovery_requires_both_manifest_mappings(self):
        with tempfile.TemporaryDirectory() as tmp:
            ws = Path(tmp) / "ws-partial"
            start = ws / "some/deep/path"
            start.mkdir(parents=True)
            (ws / "vendor/openvela/boards/contest2026_135_bk7258").mkdir(
                parents=True
            )
            self.assertIsNone(P.discover_workspace_root(str(start)))


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

    def test_host_absolute_root_allowed(self):
        # 运行时传入的真实绝对仓库路径应当被接受，
        # 禁止的是仓库里硬编码的开发者路径，而非用户运行时的绝对根。
        with tempfile.TemporaryDirectory() as tmp:
            got = P._as_safe_root(tmp)
            self.assertEqual(got, Path(tmp).resolve())


class TestDiscoverNegative(unittest.TestCase):
    def test_bad_contest_root_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(PathResolutionError):
                P.discover_contest_root(tmp)


class TestLoadBoardScript(unittest.TestCase):
    """load_board_script 白名单 / traversal / symlink / module-state 负测。"""

    def test_non_whitelist_name_rejected(self):
        for name in ("os", "sys", "verify_bk7258_partitions", "anything"):
            with self.assertRaises(P.PathResolutionError, msg=name):
                P.load_board_script(name)

    def test_traversal_name_rejected(self):
        for name in (
            "../evil",
            "a/b",
            "..",
            "/tmp/evil",
            "gen_bk7258_partitions/../../x",
        ):
            with self.assertRaises(P.PathResolutionError, msg=name):
                P.load_board_script(name)

    def test_cached_module_returned_without_reload(self):
        # sys.modules 已有同名条目时必须原样返回，不得覆盖、不得重新执行。
        sentinel = object()
        saved = sys.modules.get("gen_bk7258_partitions")
        sys.modules["gen_bk7258_partitions"] = sentinel
        path_before = list(sys.path)
        try:
            got = P.load_board_script("gen_bk7258_partitions")
            self.assertIs(got, sentinel)
            self.assertIs(sys.modules["gen_bk7258_partitions"], sentinel)
            self.assertEqual(sys.path, path_before)
        finally:
            if saved is None:
                sys.modules.pop("gen_bk7258_partitions", None)
            else:
                sys.modules["gen_bk7258_partitions"] = saved

    def test_symlinked_hook_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = _make_source_work_tree(tmp)
            scripts = root / "board/bk7258/scripts"
            outside = Path(tmp) / "outside.py"
            outside.write_text("VALUE = 42\n", encoding="utf-8")
            (scripts / "gen_bk7258_partitions.py").symlink_to(outside)
            os.environ["BK7258_CONTEST_ROOT"] = str(root)
            try:
                with self.assertRaises(P.PathResolutionError):
                    P.load_board_script("gen_bk7258_partitions")
            finally:
                del os.environ["BK7258_CONTEST_ROOT"]
                sys.modules.pop("gen_bk7258_partitions", None)

    def test_internal_symlinked_hook_rejected(self):
        # 即使 symlink 目标仍在 scripts 内，也不是可加载普通文件。
        with tempfile.TemporaryDirectory() as tmp:
            root = _make_source_work_tree(tmp)
            scripts = root / "board/bk7258/scripts"
            target = scripts / "real.py"
            target.write_text("VALUE = 42\n", encoding="utf-8")
            (scripts / "gen_bk7258_partitions.py").symlink_to(target)
            os.environ["BK7258_CONTEST_ROOT"] = str(root)
            try:
                with self.assertRaises(P.PathResolutionError):
                    P.load_board_script("gen_bk7258_partitions")
            finally:
                del os.environ["BK7258_CONTEST_ROOT"]
                sys.modules.pop("gen_bk7258_partitions", None)

    def test_non_regular_hook_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = _make_source_work_tree(tmp)
            hook = root / "board/bk7258/scripts/gen_bk7258_partitions.py"
            hook.mkdir()
            os.environ["BK7258_CONTEST_ROOT"] = str(root)
            try:
                with self.assertRaises(P.PathResolutionError):
                    P.load_board_script("gen_bk7258_partitions")
            finally:
                del os.environ["BK7258_CONTEST_ROOT"]
                sys.modules.pop("gen_bk7258_partitions", None)

    def test_failed_exec_restores_sys_modules(self):
        # 模块执行失败必须删除/恢复 sys.modules 条目，不得留下半初始化模块。
        with tempfile.TemporaryDirectory() as tmp:
            root = _make_source_work_tree(tmp)
            scripts = root / "board/bk7258/scripts"
            (scripts / "gen_bk7258_partitions.py").write_text(
                "raise RuntimeError('boom')\n", encoding="utf-8"
            )
            os.environ["BK7258_CONTEST_ROOT"] = str(root)
            path_before = list(sys.path)
            marker = object()
            saved = sys.modules.pop("gen_bk7258_partitions", marker)
            try:
                with self.assertRaises(RuntimeError):
                    P.load_board_script("gen_bk7258_partitions")
                self.assertNotIn("gen_bk7258_partitions", sys.modules)
                self.assertEqual(sys.path, path_before)
            finally:
                del os.environ["BK7258_CONTEST_ROOT"]
                sys.modules.pop("gen_bk7258_partitions", None)
                if saved is not marker:
                    sys.modules["gen_bk7258_partitions"] = saved

    def test_real_hook_loads_and_caches(self):
        # 正向：真实仓库的 gen_bk7258_partitions 可加载，且二次调用返回同一实例。
        name = "gen_bk7258_partitions"
        marker = object()
        saved = sys.modules.pop(name, marker)
        path_before = list(sys.path)
        try:
            mod1 = P.load_board_script(name)
            mod2 = P.load_board_script(name)
            self.assertIs(mod1, mod2)
            self.assertTrue(hasattr(mod1, "load_layout"))
            self.assertEqual(sys.path, path_before)
        finally:
            sys.modules.pop(name, None)
            if saved is not marker:
                sys.modules[name] = saved


if __name__ == "__main__":
    unittest.main(verbosity=2)
