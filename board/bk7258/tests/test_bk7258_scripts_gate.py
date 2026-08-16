#!/usr/bin/env python3
"""P4 结构门禁：scripts/ 职责收敛的机器可验证约束。

门禁内容（对应 2026-08-16 scripts 收敛重构的 P4 验收）：

1. ``board/bk7258/scripts/`` 只允许 6 个构建钩子文件（精确 allowlist）；
2. scripts/ 内不得出现 JSON / PowerShell / framework / pack / verify 类
   host-only 内容；
3. 活动代码不得引用已迁移的 ``board/bk7258/scripts/<旧文件>`` 路径——
   仅以下例外允许出现旧路径：
   - 保留文件本身（Make.defs、gen_bk7258_partitions.py、bk7258_crc_expand.py、
     ld.script、ld_ap.script、postbuild.sh）；
   - 历史证据（progress/、docs/、logs/、memory/decisions/）；
   - ``verify_legacy_profile_freeze.py`` 从已批准 Git 基线提交读取历史
     ``scripts/sdk-manifests`` 的扫描逻辑；
   - 描述历史基线的账本数据（legacy_profile_consumers.json）。
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPOSITORY_ROOT = Path(__file__).resolve().parents[3]
BOARD_DIR = REPOSITORY_ROOT / "board" / "bk7258"
SCRIPTS_DIR = BOARD_DIR / "scripts"
TOOLS_DIR = REPOSITORY_ROOT / "tools" / "bk7258"

SCRIPTS_ALLOWLIST = {
    "Make.defs",
    "ld.script",
    "ld_ap.script",
    "postbuild.sh",
    "gen_bk7258_partitions.py",
    "bk7258_crc_expand.py",
}

# scripts/ 内禁止出现的 host-only 内容类别（按扩展名 + 名称关键字）。
FORBIDDEN_SCRIPTS_SUFFIXES = {".json", ".ps1", ".provenance", ".sha256"}
FORBIDDEN_SCRIPTS_NAME_RE = re.compile(
    r"(framework|isolated_executor|boot_policy|pack_|bkpack|verify_|materialize"
    r"|resource_graph|auto_debug|sdk_registry|sdk_lock|sdk_set|catalog|transport"
    r"|legacy_profile|source_snapshot|trust_chain|validation)",
    re.IGNORECASE,
)

# 活动代码扫描范围（历史证据目录不在其中）。
ACTIVE_CODE_GLOBS = (
    "tools/bk7258/**/*.py",
    "tools/bk7258/**/*.sh",
    "tools/bk7258/**/*.ps1",
    "tools/bk7258/**/*.json",
    "board/bk7258/**/*.c",
    "board/bk7258/**/*.h",
    "board/bk7258/**/*.py",
    "board/bk7258/**/*.sh",
    "board/bk7258/Make.defs",
    "board/bk7258/**/Make.defs",
    "board/bk7258/**/Makefile",
    "board/bk7258/**/CMakeLists.txt",
    "board/bk7258/**/*.cmake",
    "board/bk7258/**/*.Kconfig",
    "app/**/*.c",
    "app/**/*.h",
    "app/**/*.md",
    "*.xml",
)

# 这些是当前构建/导入/验证 SOP，不是历史证据。只显式扫描活动
# 文档，避免把 docs/ 下保留的历史路径误判为回归。
ACTIVE_DOCUMENTS = (
    "board/bk7258/bk_idk/README.md",
    "board/bk7258/bootloader/README.md",
    "board/bk7258/chip/ap/PWM_BLOCKED_ROOT_CAUSE.md",
    "board/bk7258/configs/README.md",
    "docs/bk7258-t5ai/beginner-porting-guide/10-build-flash-debug-and-evidence.md",
    "docs/bk7258-t5ai/beginner-porting-guide/appendix-key-files.md",
    "docs/bk7258-t5ai/nuttx-port/bk7258-build-flash-debug-sop.md",
    "docs/bk7258-t5ai/nuttx-port/n14-psram-source-verification.md",
    "docs/bk7258-t5ai/nuttx-port/sdk-static-library-import.md",
    "memory/OPERATIONS.md",
)

# 允许在活动代码/文档中引用旧路径的例外规则。
# 注：本门禁测试文件自身包含旧路径字面量（正则定义与文档说明），
# 属于门禁的检测规则本身，自豁免。
OLD_PATH_EXCEPTION_RE = re.compile(
    r"board/bk7258/scripts/(Make\.defs|ld\.script|ld_ap\.script|postbuild\.sh"
    r"|gen_bk7258_partitions\.py|bk7258_crc_expand\.py)$"
)
SANCTIONED_OLD_PATH_FILES = {
    # 历史 Git 基线扫描器：唯一被批准读取旧 sdk-manifests 路径的活动代码。
    "tools/bk7258/verify_legacy_profile_freeze.py",
    # 路径层文档字符串中说明旧位置与迁移语义。
    "tools/bk7258/bk7258_paths.py",
    # 历史基线账本（绑定已批准基线提交的审计数据）。
    "tools/bk7258/legacy_profile_consumers.json",
    # 门禁自身的检测规则包含旧路径字面量。
    "board/bk7258/tests/test_bk7258_scripts_gate.py",
}

OLD_PATH_RE = re.compile(r"board/bk7258/scripts/([A-Za-z0-9_./-]+)")
SDK_MANIFESTS_OLD_RE = re.compile(r"board/bk7258/scripts/sdk-manifests")


def _relative(path: Path) -> str:
    return path.relative_to(REPOSITORY_ROOT).as_posix()


class TestScriptsAllowlist(unittest.TestCase):
    def test_scripts_dir_exact_allowlist(self):
        actual = {p.name for p in SCRIPTS_DIR.iterdir() if p.is_file()}
        self.assertEqual(
            actual, SCRIPTS_ALLOWLIST,
            f"board/bk7258/scripts 内容偏离 allowlist："
            f"多出 {sorted(actual - SCRIPTS_ALLOWLIST)}，"
            f"缺失 {sorted(SCRIPTS_ALLOWLIST - actual)}",
        )

    def test_scripts_dir_has_no_subdirectories(self):
        # __pycache__ 是运行 gen_bk7258_partitions.py 留下的未跟踪产物，
        # 属于构建垃圾而非受管内容；门禁只约束 git 受管的目录结构。
        subdirs = [
            p.name for p in SCRIPTS_DIR.iterdir()
            if p.is_dir() and p.name != "__pycache__"
        ]
        self.assertEqual(subdirs, [], f"scripts/ 不允许子目录：{subdirs}")

    def test_scripts_host_only_content_forbidden(self):
        offenders = []
        for path in SCRIPTS_DIR.iterdir():
            if not path.is_file():
                continue
            if path.suffix in FORBIDDEN_SCRIPTS_SUFFIXES:
                offenders.append(f"{path.name}: 禁止的扩展名 {path.suffix}")
            if FORBIDDEN_SCRIPTS_NAME_RE.search(path.stem):
                offenders.append(f"{path.name}: host-only 名称关键字")
        self.assertEqual(offenders, [], f"scripts/ 含 host-only 内容：{offenders}")


class TestNoActiveOldPathReferences(unittest.TestCase):
    def _active_files(self):
        seen = set()
        for pattern in ACTIVE_CODE_GLOBS:
            for path in REPOSITORY_ROOT.glob(pattern):
                if path.is_file():
                    seen.add(path.resolve())
        for relative in ACTIVE_DOCUMENTS:
            path = REPOSITORY_ROOT / relative
            if path.is_file():
                seen.add(path.resolve())
        return sorted(seen)

    def test_active_code_does_not_reference_migrated_paths(self):
        violations = []
        for path in self._active_files():
            rel = _relative(path)
            if rel in SANCTIONED_OLD_PATH_FILES:
                continue
            try:
                text = path.read_text(encoding="utf-8")
            except (UnicodeDecodeError, OSError):
                continue
            for match in OLD_PATH_RE.finditer(text):
                matched = match.group(0)
                if OLD_PATH_EXCEPTION_RE.fullmatch(matched):
                    continue  # 保留在 scripts/ 的构建钩子，引用合法。
                violations.append(f"{rel}: 引用已迁移旧路径 {matched}")
        self.assertEqual(
            violations, [],
            "活动代码不得引用已迁移的旧 scripts/ 路径（历史证据与保留钩子除外）",
        )

    def test_only_freeze_scanner_reads_old_sdk_manifests(self):
        readers = []
        for path in self._active_files():
            rel = _relative(path)
            if rel in SANCTIONED_OLD_PATH_FILES:
                continue
            try:
                text = path.read_text(encoding="utf-8")
            except (UnicodeDecodeError, OSError):
                continue
            if SDK_MANIFESTS_OLD_RE.search(text):
                readers.append(rel)
        self.assertEqual(
            readers, [],
            "仅历史 Git 基线 freeze 扫描允许读取旧 sdk-manifests 路径",
        )


class TestToolsLayerContract(unittest.TestCase):
    def test_migrated_framework_present_in_tools(self):
        for name in (
            "bk7258_framework.py",
            "bk7258_isolated_executor.py",
            "bk7258_boot_policy.py",
            "bk7258_paths.py",
            "bk7258_bkpack.py",
            "build_dual_image.sh",
        ):
            self.assertTrue(
                (TOOLS_DIR / name).is_file(),
                f"tools/bk7258 缺少迁移文件 {name}",
            )

    def test_import_bridge_absent(self):
        bridge = TOOLS_DIR / "_ensure_scripts_path.py"
        self.assertFalse(
            bridge.exists(),
            "隐式 sys.path 桥接 _ensure_scripts_path.py 必须保持删除",
        )
        for path in TOOLS_DIR.glob("*.py"):
            if path.name == "bk7258_paths.py":
                continue
            text = path.read_text(encoding="utf-8")
            self.assertNotIn(
                "_ensure_scripts_path", text,
                f"{path.name} 仍引用已删除的隐式桥接",
            )

    def test_dynamic_consumers_do_not_reconstruct_migrated_script_paths(self):
        executor = (TOOLS_DIR / "bk7258_isolated_executor.py").read_text(
            encoding="utf-8"
        )
        for name in (
            "pack_bk7258_mcuboot_pair.py",
            "pack_dual_image.py",
            "bk7258_bkpack.py",
            "bk7258_trust_chain.py",
        ):
            self.assertNotRegex(
                executor,
                rf"\bscripts\s*/\s*[\"']{re.escape(name)}[\"']",
                f"isolated executor 仍动态拼接旧 scripts/{name}",
            )

        psram = (TOOLS_DIR / "verify_bk7258_psram.py").read_text(
            encoding="utf-8"
        )
        self.assertNotIn('board / "scripts/build_dual_image.sh"', psram)
        self.assertIn('tools / "build_dual_image.sh"', psram)

    def test_sdk_shells_resolve_source_and_manifest_layouts(self):
        scripts = (
            TOOLS_DIR / "setup_bk7258_sdk.sh",
            TOOLS_DIR / "import_bk7258_sdk_role.sh",
        )
        source_env = dict(os.environ)
        source_env.pop("OPENVELA_WORKSPACE_ROOT", None)
        source_env.pop("BK7258_CONTEST_ROOT", None)
        for script in scripts:
            result = subprocess.run(
                [str(script), "--help"], cwd=REPOSITORY_ROOT,
                env=source_env, text=True, capture_output=True, check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(str(BOARD_DIR), result.stdout)

        with tempfile.TemporaryDirectory() as temporary:
            workspace = Path(temporary) / "workspace"
            mapped_board = workspace / (
                "vendor/openvela/boards/contest2026_135_bk7258"
            )
            mapped_tools = workspace / (
                "vendor/openvela/tools/contest2026_135_bk7258"
            )
            mapped_board.mkdir(parents=True)
            mapped_tools.mkdir(parents=True)
            manifest_env = dict(source_env)
            manifest_env["OPENVELA_WORKSPACE_ROOT"] = str(workspace)
            for script in scripts:
                result = subprocess.run(
                    [str(script), "--help"], cwd=workspace,
                    env=manifest_env, text=True, capture_output=True,
                    check=False,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn(str(mapped_board), result.stdout)

    def test_sdk_shells_do_not_guess_board_from_script_depth(self):
        for name in ("setup_bk7258_sdk.sh", "import_bk7258_sdk_role.sh"):
            text = (TOOLS_DIR / name).read_text(encoding="utf-8")
            self.assertIn("Bk7258Layout", text)
            self.assertNotRegex(text, r"SCRIPT_DIR[^\n]*(?:/\.\.|/../)")


if __name__ == "__main__":
    unittest.main()
