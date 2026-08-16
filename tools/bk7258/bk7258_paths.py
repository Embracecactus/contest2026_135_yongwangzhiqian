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
  * 运行时可接受用户显式传入的绝对根路径；仓库不硬编码主机路径。
  * 拒绝 ``..`` 越界与 symlink escape。
  * 不从 ``SCRIPT_DIR.parent`` 猜 board 根；仅以本模块自身位置锚定 contest 根，
    再由 contest 根派生 board/tools/partition/sdk 等所有子路径。

所有公共函数对非法输入 fail-closed（抛 ``ValueError``）。
"""

from __future__ import annotations

import os
from pathlib import Path
from typing import Any, Optional

# ---------------------------------------------------------------------------
# 形态常量
# ---------------------------------------------------------------------------
CONTEST_BOARD_REL = Path("board/bk7258")
CONTEST_TOOLS_REL = Path("tools/bk7258")
MANIFEST_BOARD_REL = Path("vendor/openvela/boards/contest2026_135_bk7258")
MANIFEST_TOOLS_REL = Path("vendor/openvela/tools/contest2026_135_bk7258")

CONTEST_MARKER_FILES = ("board/bk7258", "tools/bk7258")
MANIFEST_MARKER_FILES = (
    "vendor/openvela/boards/contest2026_135_bk7258",
    "vendor/openvela/tools/contest2026_135_bk7258",
)

# load_board_script 唯一允许加载的 board 钩子模块（收敛后 scripts/ 中仅有的
# 两个 Python 构建钩子）。
BOARD_SCRIPT_ALLOWLIST = (
    "gen_bk7258_partitions",
    "bk7258_crc_expand",
    "bk7258_crc16",
)


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
    board 与 tools 两个 manifest 映射标记；任一缺失都不是
    完整的 manifest 形态。找不到返回 ``None``，不抛异常。
    """
    if override is not None:
        start = _as_safe_root(override)
    elif os.environ.get("OPENVELA_WORKSPACE_ROOT"):
        start = _as_safe_root(os.environ["OPENVELA_WORKSPACE_ROOT"])
    else:
        start = _module_contest_root()
    return _walk_up_to_markers(start, MANIFEST_MARKER_FILES)


def _walk_up_to_markers(start: Path, markers: tuple[str, ...]) -> Optional[Path]:
    cur = start.resolve()
    for _ in range(8):
        if all((cur / marker).is_dir() for marker in markers):
            return cur
        parent = cur.parent
        if parent == cur:
            break
        cur = parent
    return None


def _is_real_dir(path: Path) -> bool:
    """判断 ``path`` 是否为真实目录（存在且非符号链接）。

    形态检测的关键区分：source-work / isolated-snapshot 形态下，
    ``board/bk7258`` 与 ``tools/bk7258`` 是真实目录；而 manifest-mapped 形态下
    它们通常是符号链接，指向 ``vendor/openvela/...``。当 contest 源仓嵌套在
    manifest workspace 内（workspace 的 ``vendor/openvela/boards/...`` 通过符号链接
    指回源仓）时，仅靠向上查找标记会把源仓误判为 manifest-mapped，进而把仓库根
    错误锚定到父 workspace，导致 ``tools/bk7258`` 解析失败。优先以「真实目录标记」
    识别 source-work 可避免该误判。
    """
    return path.is_dir() and not path.is_symlink()


def load_board_script(name: str) -> Any:
    """按绝对路径显式加载 ``board/bk7258/scripts`` 里的直接构建钩子模块。

    仅允许 ``BOARD_SCRIPT_ALLOWLIST`` 中的模块名（收敛后 scripts/ 中仅有的
    三个 Python 构建钩子 ``gen_bk7258_partitions`` / ``bk7258_crc_expand`` /
    ``bk7258_crc16``，
    其直接依赖 armino SDK 模块布局，故不能变为 ``tools/bk7258`` 的同级模块）。

    本函数即为唯一的 sanctioned 导入边界：用 ``importlib`` 按绝对路径加载，
    且对 ``sys.modules`` 幂等——已加载过则直接返回缓存，不覆盖既有条目；
    加载失败时恢复调用前的 ``sys.modules`` 状态。加载过程不修改
    ``sys.path``，因而不把 ``scripts`` 当作 module root，也不依赖隐藏桥接文件。

    fail-closed 边界：
      * 名称必须命中白名单且为裸模块名（拒绝路径段 / ``..`` traversal）；
      * 解析后的文件路径必须真实位于 ``scripts`` 目录内；
      * 目标必须是非符号链接的普通文件。
    """
    import importlib.util
    import re
    import sys

    if name not in BOARD_SCRIPT_ALLOWLIST:
        raise PathResolutionError(
            f"拒绝加载非白名单 board 钩子模块: {name!r} "
            f"(allowlist: {list(BOARD_SCRIPT_ALLOWLIST)})"
        )
    if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name) is None:
        raise PathResolutionError(f"拒绝非法模块名: {name!r}")

    env_root = os.environ.get("BK7258_CONTEST_ROOT")
    lay = Bk7258Layout(contest_root=env_root) if env_root else Bk7258Layout()
    scripts = lay.scripts_dir
    try:
        scripts_resolved = scripts.resolve(strict=True)
    except (OSError, RuntimeError) as exc:
        raise PathResolutionError(f"scripts 目录无法解析: {scripts}") from exc
    if not scripts_resolved.is_dir():
        raise PathResolutionError(f"scripts 路径不是目录: {scripts_resolved}")

    candidate = scripts / f"{name}.py"
    try:
        resolved = candidate.resolve(strict=True)
    except (OSError, RuntimeError) as exc:
        raise PathResolutionError(f"找不到 board 钩子模块: {candidate}") from exc

    # 先做 containment：可单独拦截指向 scripts 外部的 symlink。
    if resolved.parent != scripts_resolved:
        raise PathResolutionError(
            f"board 钩子模块越界: {resolved} 不在 {scripts_resolved} 内"
        )
    if candidate.is_symlink():
        raise PathResolutionError(f"拒绝经符号链接加载 board 钩子模块: {candidate}")
    if not candidate.is_file():
        raise PathResolutionError(f"board 钩子模块不是普通文件: {candidate}")

    # 完成路径与文件边界校验后才允许使用缓存，防止已有缓存
    # 绕过当前 contest root 下的 hook 完整性检查。
    if name in sys.modules:
        return sys.modules[name]

    spec = importlib.util.spec_from_file_location(name, str(resolved))
    if spec is None or spec.loader is None:
        raise PathResolutionError(f"无法加载 board 钩子模块: {resolved}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    try:
        spec.loader.exec_module(module)
    except BaseException:
        # 进入执行分支时 name 必然未缓存；失败后删除半初始化条目。
        # pop 也能安全处理模块自身替换/删除该条目的情形。
        sys.modules.pop(name, None)
        raise
    return module


def _as_safe_root(text: str) -> Path:
    """把用户输入的文本解析为安全根目录。

    允许运行时传入任意绝对仓库路径作为根；
    需要禁止的是「仓库里硬编码的开发者路径」，而不是用户运行时的绝对路径。
    绝对路径经 ``resolve()`` 规范化（覆盖同义、多余 ``.``/``..`` 与符号链接），
    具体逃逸防护由 ``safe_join`` 负责。
    """
    p = Path(text)
    if p.is_absolute():
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
        if contest_root is not None:
            # 显式 contest 根 → 直接按 source-work 处理（调用方已声明根）。
            cr = discover_contest_root(str(contest_root))
            self._init_source_work(cr)
            return
        if workspace_root is not None or os.environ.get("OPENVELA_WORKSPACE_ROOT"):
            # 显式 workspace 根（参数或环境变量）→ 强制 manifest-mapped 形态。
            # fail-closed：显式声明的 workspace 无效必须报错，
            # 不得静默回落到源码仓自动探测。
            explicit = str(workspace_root) if workspace_root is not None else str(
                os.environ["OPENVELA_WORKSPACE_ROOT"]
            )
            ws = _as_safe_root(explicit)
            if not ws.is_dir():
                raise PathResolutionError(f"显式 workspace root 不是目录: {ws}")
            if not self._manifest_complete(ws):
                raise PathResolutionError(
                    "manifest workspace 映射不完整（需要 board 与 tools 两个 "
                    f"linkfile）: {ws}"
                )
            self._init_manifest(ws)
            return
        # 自动探测：优先 source-work —— 仅当模块自身 contest 根的 board/tools
        # 标记是真实目录（非符号链接）时成立。这能正确识别「嵌套在 manifest
        # workspace 内的源仓」，避免被误判为 manifest-mapped。
        candidate = _module_contest_root()
        if _is_real_dir(candidate / CONTEST_BOARD_REL) and _is_real_dir(
            candidate / CONTEST_TOOLS_REL
        ):
            self._init_source_work(discover_contest_root())
            return
        # 否则尝试 manifest-mapped 形态（同样要求映射完整）。
        ws = discover_workspace_root(str(candidate))
        if ws is not None and self._manifest_complete(ws):
            self._init_manifest(ws)
            return
        # 兜底：source-work。
        self._init_source_work(discover_contest_root())

    @staticmethod
    def _manifest_complete(ws: Path) -> bool:
        """manifest 形态要求 board 与 tools 两个映射同时存在，缺一拒绝。"""
        return all((ws / marker).is_dir() for marker in MANIFEST_MARKER_FILES)

    def _init_source_work(self, cr: Path) -> None:
        self.form = "source-work"
        self.contest_root = cr
        self.workspace_root = None
        self.board_dir = cr / CONTEST_BOARD_REL
        self.tools_dir = cr / CONTEST_TOOLS_REL

    def _init_manifest(self, ws: Path) -> None:
        self.form = "manifest-mapped"
        self.workspace_root = ws
        self.contest_root = ws
        self.board_dir = ws / MANIFEST_BOARD_REL
        self.tools_dir = ws / MANIFEST_TOOLS_REL

    # -- 派生路径 ---------------------------------------------------------
    @property
    def scripts_dir(self) -> Path:
        """直接构建钩子所在目录（其中仅两个 Python 钩子可加载）。"""
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
