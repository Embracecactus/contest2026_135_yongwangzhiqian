#!/usr/bin/env python3
"""BK7258 路径解析层（职责收敛重构 P1）。

本模块是 ``board/bk7258/scripts`` 收敛到 ``tools/bk7258`` 后唯一允许使用的
"根目录/布局" 解析入口。它替代历史上散落在各脚本里、通过 ``SCRIPT_DIR.parent``
向上猜 ``board/bk7258`` 根的脆弱写法。

设计约束（来自重构任务书）：
  * 支持三种形态：
      - source-work ：contest 源仓直接 checkout（root/board/bk7258, root/tools/bk7258）
      - manifest-mapped ：OpenVela workspace 中通过 repo manifest 映射
        （ws/vendor/openvela/boards/contest2026_135_bk7258,
         ws/vendor/openvela/tools/contest2026_135_bk7258）
      - isolated-snapshot ：materialized 快照根（结构与 source-work 同构）
  * 禁止绝对主机路径（如 /home/...）进入解析结果或输入。
  * 拒绝 ``..`` 越界与 symlink escape。
  * 不从 ``SCRIPT_DIR.parent`` 猜 board 根；仅以本模块自身位置锚定 contest 根，
    再由 contest 根派生 board/tools/partition/sdk 等所有子路径。

所有公共函数对非法输入 fail-closed（抛 ``ValueError``）。
"""

from __future__ import annotations

import os
from pathlib import Path
from typing import Optional

# ---------------------------------------------------------------------------
# 形态常量
# ---------------------------------------------------------------------------
CONTEST_BOARD_REL = Path("board/bk7258")
CONTEST_TOOLS_REL = Path("tools/bk7258")
MANIFEST_BOARD_REL = Path("vendor/openvela/boards/contest2026_135_bk7258")
MANIFEST_TOOLS_REL = Path("vendor/openvela/tools/contest2026_135_bk7258")

CONTEST_MARKER_FILES = ("board/bk7258", "tools/bk7258")
MANIFEST_MARKER = "vendor/openvela/boards/contest2026_135_bk7258"


class PathResolutionError(ValueError):
    """路径解析失败（越界 / 逃逸 / 非法输入）。"""


# ---------------------------------------------------------------------------
# 根目录发现
# ---------------------------------------------------------------------------
def _module_contest_root() -> Path:
    """以本模块位置锚定 contest 源仓根。

    模块位于 ``<contest_root>/tools/bk7258/bk7258_paths.py``，
    故 ``parents[2]`` 即 contest 根。这里锚定的是 *contest 根*，
    而非通过 ``SCRIPT_DIR.parent`` 去猜 *board 根*，符合任务书约束。
    """
    return Path(__file__).resolve().parents[2]


def discover_contest_root(override: Optional[str] = None) -> Path:
    """解析 contest 源仓根。

    优先级：显式参数 > 环境变量 ``BK7258_CONTEST_ROOT`` > 模块位置自动探测。
    返回前校验其下确实存在 ``board/bk7258`` 与 ``tools/bk7258``。
    """
    if override is not None:
        root = _as_safe_root(override)
    elif os.environ.get("BK7258_CONTEST_ROOT"):
        root = _as_safe_root(os.environ["BK7258_CONTEST_ROOT"])
    else:
        root = _module_contest_root()
    if not root.is_dir():
        raise PathResolutionError(f"contest root 不是目录: {root}")
    for marker in CONTEST_MARKER_FILES:
        if not (root / marker).is_dir():
            raise PathResolutionError(
                f"contest root 缺少标记 {marker}: {root}"
            )
    return root


def discover_workspace_root(override: Optional[str] = None) -> Optional[Path]:
    """解析 OpenVela workspace 根（manifest 映射形态）。

    从给定起点（override > env > 模块位置）向上查找
    ``vendor/openvela/boards/contest2026_135_bk7258`` 标记；找不到返回 ``None``
    （表示非 manifest 形态），不抛异常。
    """
    if override is not None:
        start = _as_safe_root(override)
    elif os.environ.get("OPENVELA_WORKSPACE_ROOT"):
        start = _as_safe_root(os.environ["OPENVELA_WORKSPACE_ROOT"])
    else:
        start = _module_contest_root()
    return _walk_up_to_marker(start, MANIFEST_MARKER)


def _walk_up_to_marker(start: Path, marker: str) -> Optional[Path]:
    cur = start.resolve()
    for _ in range(8):
        if (cur / marker).is_dir():
            return cur
        parent = cur.parent
        if parent == cur:
            break
        cur = parent
    return None


def _as_safe_root(text: str) -> Path:
    p = Path(text)
    if p.is_absolute():
        # 允许绝对路径作为根输入，但拒绝明显的主机 home 路径出现在逻辑里
        if str(p).startswith("/home/") or str(p).startswith("/Users/"):
            raise PathResolutionError(f"拒绝主机绝对路径作为根: {p}")
        return p.resolve()
    return p.resolve()


# ---------------------------------------------------------------------------
# 安全 join
# ---------------------------------------------------------------------------
def safe_join(root: str | Path, *parts: str | Path) -> Path:
    """在 ``root`` 内安全拼接路径。

    - 拒绝任意 part 为绝对路径。
    - 拒绝 ``..``（fail-closed，布局只用固定相对段）。
    - 解析后必须仍位于 ``root`` 之内（覆盖 symlink escape）。
    """
    root_path = Path(root).resolve()
    if not root_path.is_dir():
        raise PathResolutionError(f"root 不是目录: {root_path}")
    cleaned = []
    for part in parts:
        s = str(part)
        if s == "":
            continue
        if Path(s).is_absolute():
            raise PathResolutionError(f"拒绝绝对路径段: {s}")
        if ".." in Path(s).parts:
            raise PathResolutionError(f"拒绝越界段 '..': {s}")
        cleaned.append(s)
    candidate = (root_path / Path(*cleaned)).resolve()
    if candidate != root_path and root_path not in candidate.parents:
        raise PathResolutionError(
            f"路径逃逸 root: {candidate} 不在 {root_path} 内"
        )
    return candidate


# ---------------------------------------------------------------------------
# 布局
# ---------------------------------------------------------------------------
class Bk7258Layout:
    """BK7258 目录布局解析。

    构造时传入 ``contest_root`` 或 ``workspace_root`` 之一；两者皆空时自动探测。
    """

    def __init__(
        self,
        contest_root: Optional[str | Path] = None,
        workspace_root: Optional[str | Path] = None,
    ) -> None:
        ws = None
        if workspace_root is not None:
            ws = discover_workspace_root(str(workspace_root))
        else:
            # 从传入的 contest_root（或模块位置）向上探测 manifest 形态
            ws = discover_workspace_root(
                str(contest_root) if contest_root is not None else None
            )
        if ws is not None and (ws / MANIFEST_BOARD_REL).is_dir():
            self.form = "manifest-mapped"
            self.workspace_root = ws
            self.contest_root = ws  # manifest 形态下 board/tools 即 workspace 内映射
            self.board_dir = ws / MANIFEST_BOARD_REL
            self.tools_dir = ws / MANIFEST_TOOLS_REL
        else:
            cr = discover_contest_root(
                str(contest_root) if contest_root is not None else None
            )
            self.form = "source-work"
            self.contest_root = cr
            self.workspace_root = None
            self.board_dir = cr / CONTEST_BOARD_REL
            self.tools_dir = cr / CONTEST_TOOLS_REL

    # -- 派生路径 ---------------------------------------------------------
    @property
    def scripts_dir(self) -> Path:
        """直接构建钩子所在目录（收敛后仅含 6 个 allowlist 文件）。"""
        return self.board_dir / "scripts"

    @property
    def partition_dir(self) -> Path:
        return self.board_dir / "partitions"

    @property
    def sdk_dir(self) -> Path:
        return self.board_dir / "bk_idk"

    @property
    def sdk_versions_dir(self) -> Path:
        return self.sdk_dir / "armino_as_lib" / "versions"

    @property
    def sdk_manifests_dir(self) -> Path:
        """SDK 清单：从 scripts/sdk-manifests 迁至 bk_idk/manifests。"""
        return self.sdk_dir / "manifests"

    @property
    def build_root(self) -> Path:
        env = os.environ.get("BK7258_BUILD_ROOT")
        if env:
            p = Path(env)
            if p.is_absolute():
                if str(p).startswith("/home/") or str(p).startswith("/Users/"):
                    raise PathResolutionError(f"拒绝主机绝对 build_root: {p}")
                return p.resolve()
            return (self.contest_root / p).resolve()
        return (self.contest_root / ".build" / "bk7258").resolve()

    @property
    def output_dir(self) -> Path:
        return self.build_root / "output"


# ---------------------------------------------------------------------------
# 便捷模块级函数
# ---------------------------------------------------------------------------
def contest_root(override: Optional[str] = None) -> Path:
    return discover_contest_root(override)


def workspace_root(override: Optional[str] = None) -> Optional[Path]:
    return discover_workspace_root(override)


def board_dir(override: Optional[str] = None) -> Path:
    return Bk7258Layout(contest_root=override).board_dir


def tools_dir(override: Optional[str] = None) -> Path:
    return Bk7258Layout(contest_root=override).tools_dir


def layout(
    contest_root: Optional[str | Path] = None,
    workspace_root: Optional[str | Path] = None,
) -> Bk7258Layout:
    return Bk7258Layout(contest_root=contest_root, workspace_root=workspace_root)


if __name__ == "__main__":
    lay = Bk7258Layout()
    print(f"form            : {lay.form}")
    print(f"contest_root    : {lay.contest_root}")
    print(f"workspace_root  : {lay.workspace_root}")
    print(f"board_dir       : {lay.board_dir}")
    print(f"tools_dir       : {lay.tools_dir}")
    print(f"partition_dir   : {lay.partition_dir}")
    print(f"sdk_dir         : {lay.sdk_dir}")
    print(f"sdk_manifests   : {lay.sdk_manifests_dir}")
    print(f"build_root      : {lay.build_root}")
    print(f"output_dir      : {lay.output_dir}")
