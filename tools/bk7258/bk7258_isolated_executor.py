#!/usr/bin/env python3
"""Prepare and execute a role-isolated BK7258 build/delivery manifest.

This module has three host-only phases.  ``prepare`` validates the framework
build plan, creates a fresh private workspace, materializes the hash-bound
legacy CP/AP seed profiles, and records a single pending entity source view.
``materialize-sources`` copies and audits that view once, then stages the
bootloader inputs per role.  Neither phase invokes CMake, Make, a signer, a
packer, or hardware tooling.  ``compile-runtime`` is the explicit,
authorization-gated active-role compile phase: BL1/BL2 use Make's raw
ELF/BIN ``compile-only`` target and CP/AP use isolated CMake targets.  It
never runs CRC/manifest/postbuild, signing, packaging, network, or hardware.

The existing ``build_dual_image.sh`` remains a compatibility adapter.  The
delivery phase calls the existing board-owned post-build, manifest, CRC,
MCUboot-pair, dual-image and ``firmware.bkpack`` tools directly from one
private build-root delivery workspace; it does not duplicate their packing
algorithms or write the source checkout.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import os
import shutil
import stat
import subprocess
import sys
import zipfile
from pathlib import Path
from typing import Any, Callable


SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import bk7258_framework as framework  # noqa: E402

try:
    import bk7258_boot_policy as boot_policy  # noqa: E402
except ImportError as error:  # pragma: no cover - repository invariant
    raise RuntimeError("the BK7258 boot-policy boundary is unavailable") from error

try:
    from bk7258_source_snapshot import (  # noqa: E402
        SnapshotError,
        _excluded_reason as _snapshot_excluded_reason,
        audit_snapshot,
        snapshot_workspace,
    )
except ImportError as error:  # pragma: no cover - repository invariant
    raise RuntimeError("the source snapshot helper is unavailable") from error

SCHEMA = "bk7258.role-isolated-execution/1"
KIND = "role-isolated-prepare-manifest"
VERSION = 1
DELIVERY_VERSION = 1
ROLES = ("bl1", "bl2", "cp", "ap")
RUNTIME_ROLES = ("cp", "ap")
BOOT_ROLES = ("bl1", "bl2")
SOURCE_SNAPSHOT_DIRNAME = "source-snapshot"
SNAPSHOT_MANIFEST_FILENAME = "bk7258-source-snapshot.json"
SNAPSHOT_REQUIREMENTS_FILENAME = "SNAPSHOT-REQUIRED.json"
BOOTLOADER_STAGING_DIRNAME = "bootloader-staging"
BOARD_SOURCE_PREFIX = "contest2026_135_yongwangzhiqian/board/bk7258/"
GENERATED_SOURCE_NAMES = frozenset({
    ".config", "defconfig", "Make.defs", "bootloader.tmp", "bl2_crc.bin.json",
    "bl_crc.bin", "bl2.bin", "bl2_crc.bin", "bl.elf", "bl.bin", "bl.map",
    "bl2.elf", "bl2.map",
})
KEY_ENV_NAMES = frozenset({
    "MCUBOOT_SIGNING_KEY", "BL1_MANIFEST_KEY", "BL1_MANIFEST_PRIMARY",
    "BL1_MANIFEST_SECONDARY", "BL2_KEY_SOURCE",
})
SAFE_ENV_NAMES = frozenset({
    "PATH", "LANG", "LC_ALL", "TZ", "PYTHONUNBUFFERED", "PYTHONDONTWRITEBYTECODE",
    "HOME", "TMPDIR", "XDG_CACHE_HOME", "PYTHONPATH",
})
RUNTIME_COMMAND_STAGES = (
    "partition-contract", "cmake-configure", "cmake-build", "cmake-build-bin",
)
BOOT_COMMAND_STAGES = ("partition-contract", "make-compile-only")
RUNTIME_ARTIFACT_NAMES = (
    ".config", "nuttx", "nuttx.map", "nuttx.bin", "arch/libarch.a",
    "boards/libboard.a",
)
OPTIONAL_RUNTIME_ARTIFACT_NAMES = ("System.map",)
BOOT_ARTIFACT_NAMES = {
    "bl1": ("bl.elf", "bl.bin", "bl.map"),
    "bl2": ("bl2.elf", "bl2.bin", "bl2.map"),
}
DELIVERY_COMMAND_STAGES = (
    "bl1-manifest-primary", "bl1-manifest-secondary", "bl1-pack", "bl2-pack",
    "postbuild-cp", "postbuild-ap",
    "mcuboot-pair-sign", "trust-chain-emit", "dual-package",
    "bkpack-create", "bkpack-verify",
)
DELIVERY_PREPARED_COMMAND_STAGES = (
    "bl1-pack", "bl2-pack", "postbuild-cp", "postbuild-ap",
)
PRIVATE_KEY_TOKEN = "<authorized-private-key>"
TOOL_NAMES = ("python", "cmake", "ninja", "arm-none-eabi-gcc", "make")


class IsolatedExecutorError(framework.FrameworkError):
    """Malformed input or an unsafe prepare target."""


def _canonical(value: Any) -> bytes:
    return framework.canonical_json(value)


def _digest_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def _digest_file(path: Path) -> str:
    try:
        return _digest_bytes(path.read_bytes())
    except OSError as error:
        raise IsolatedExecutorError(f"cannot hash file: {path}") from error


def _safe_path(path: Path, field: str) -> Path:
    if not isinstance(path, Path):
        raise IsolatedExecutorError(f"{field} must be a filesystem path")
    if "\x00" in str(path):
        raise IsolatedExecutorError(f"unsafe path in {field}")
    return path.expanduser().absolute()


def _reject_traversal(path: Path, field: str) -> None:
    """Reject lexical traversal before a caller-controlled path is used."""
    if ".." in path.parts:
        raise IsolatedExecutorError(f"{field} must not contain '..' traversal")


def _reject_existing_symlink_components(path: Path, field: str,
                                        boundary: Path | None = None) -> None:
    """Reject a symlink at any existing component of a manifest path."""
    current = path
    while True:
        if current.is_symlink():
            raise IsolatedExecutorError(f"{field} contains a symlink: {current}")
        if boundary is not None and current == boundary:
            return
        parent = current.parent
        if parent == current:
            return
        current = parent
        if boundary is not None and not _inside(current, boundary):
            raise IsolatedExecutorError(f"{field} escaped its path boundary")


def _inside(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
        return True
    except ValueError:
        return False


def _fresh_build_root(path: Path, repository: Path) -> Path:
    """Create a new, non-symlink, empty build root without deleting anything."""
    path = _safe_path(path, "build_root")
    _reject_traversal(path, "build_root")
    repository = repository.resolve()
    # A canonical-parent check alone still permits ``alias/new`` when
    # ``alias`` is a symlink to another external directory.  Walk every
    # existing component with lstat semantics and reject symlink parents
    # before mkdir can follow one.
    parent = path.parent
    while True:
        if parent.is_symlink():
            raise IsolatedExecutorError(
                f"build_root parent must not be a symlink: {parent}")
        next_parent = parent.parent
        if next_parent == parent:
            break
        parent = next_parent
    # Resolve the parent before creating the leaf.  A symlinked parent can
    # otherwise make a lexical /tmp path write into the repository checkout.
    canonical_parent = path.parent.resolve(strict=False)
    canonical = canonical_parent / path.name
    if canonical == repository or _inside(canonical, repository):
        raise IsolatedExecutorError(
            "build_root must be outside the repository checkout")
    if path.exists() or path.is_symlink():
        if path.is_symlink():
            raise IsolatedExecutorError("build_root must not be a symlink")
        if not path.is_dir():
            raise IsolatedExecutorError("build_root must be a directory")
        try:
            nonempty = next(path.iterdir(), None)
        except OSError as error:
            raise IsolatedExecutorError(f"cannot inspect build_root: {path}") from error
        if nonempty is not None:
            raise IsolatedExecutorError(
                f"build_root must be fresh and empty: {path}")
    else:
        try:
            path.mkdir(parents=True)
        except OSError as error:
            raise IsolatedExecutorError(f"cannot create build_root: {path}") from error
    if path.is_symlink():  # race-resistant postcondition for ordinary hosts
        raise IsolatedExecutorError("build_root became a symlink")
    canonical_after = path.resolve(strict=True)
    if canonical_after == repository or _inside(canonical_after, repository):
        raise IsolatedExecutorError(
            "resolved build_root points into the repository checkout")
    return path


def _prepare_output_path(path: Path, build_root: Path) -> Path:
    """Validate an output path before any parent directories are created."""
    path = _safe_path(path, "output")
    _reject_traversal(path, "output")
    if path == build_root or not _inside(path, build_root):
        raise IsolatedExecutorError(
            "output must be a new path inside the fresh build_root")
    # Existing symlink components would make a lexical in-root path write
    # somewhere else.  The build root itself was checked by
    # ``_fresh_build_root``; reject every existing component below it too.
    current = path.parent
    while current != build_root:
        if current.is_symlink():
            raise IsolatedExecutorError(
                f"output parent must not contain a symlink: {current}")
        current = current.parent
        if not _inside(current, build_root):
            raise IsolatedExecutorError("output parent escaped build_root")
    if path.is_symlink() or path.exists():
        raise IsolatedExecutorError(f"refusing to replace output manifest: {path}")
    canonical_root = build_root.resolve(strict=True)
    canonical_output = path.resolve(strict=False)
    if canonical_output == canonical_root or not _inside(canonical_output, canonical_root):
        raise IsolatedExecutorError(
            "resolved output path escaped the fresh build_root")
    return path


def _new_directory(path: Path, field: str) -> Path:
    if path.exists() or path.is_symlink():
        if path.is_symlink() or not path.is_dir():
            raise IsolatedExecutorError(f"{field} is not a private directory: {path}")
        if next(path.iterdir(), None) is not None:
            raise IsolatedExecutorError(f"{field} is not empty: {path}")
    else:
        path.mkdir(parents=True)
    return path


def _git_head(root: Path) -> str | None:
    try:
        result = subprocess.run(
            ["git", "-C", str(root), "rev-parse", "HEAD"],
            capture_output=True, text=True, check=False)
    except OSError:
        return None
    value = result.stdout.strip()
    return value if result.returncode == 0 and value else None


def _tree_digest(root: Path) -> str:
    """Digest board source bytes without following generated output links."""
    if not root.is_dir() or root.is_symlink():
        raise IsolatedExecutorError(f"board source is not a real directory: {root}")
    digest = hashlib.sha256()
    for current, dirs, files in os.walk(root, followlinks=False):
        kept_dirs: list[str] = []
        for name in sorted(dirs):
            if name == "__pycache__":
                continue
            path = Path(current) / name
            relative = path.relative_to(root).as_posix()
            try:
                is_directory = path.is_dir() and not path.is_symlink()
                excluded = _snapshot_excluded_reason(
                    BOARD_SOURCE_PREFIX + relative, is_directory, path)
            except OSError as error:
                raise IsolatedExecutorError(
                    f"cannot inspect board source directory: {path}") from error
            if excluded is None:
                kept_dirs.append(name)
        dirs[:] = kept_dirs
        for name in sorted(files):
            path = Path(current) / name
            relative = path.relative_to(root).as_posix()
            # Seed profiles are intentionally outside the board-source digest;
            # board/chip/scripts Make.defs, however, are real snapshot inputs
            # and must remain bound to the prepare identity.
            if name in {".config", "defconfig"} or relative.startswith("configs/"):
                continue
            try:
                excluded = _snapshot_excluded_reason(
                    BOARD_SOURCE_PREFIX + relative, False, path)
            except OSError as error:
                raise IsolatedExecutorError(f"cannot inspect board source: {path}") from error
            if excluded is not None:
                continue
            try:
                if path.is_symlink():
                    data = os.readlink(path).encode()
                else:
                    data = path.read_bytes()
            except OSError as error:
                raise IsolatedExecutorError(f"cannot digest board source: {path}") from error
            digest.update(relative.encode())
            digest.update(b"\0")
            digest.update(_digest_bytes(data).encode())
            digest.update(b"\n")
    return digest.hexdigest()


def _workspace_root(repository: Path, requested: Path | None) -> Path:
    candidates = []
    if requested is not None:
        candidate = _safe_path(requested, "workspace_root")
        _reject_traversal(candidate, "workspace_root")
        if candidate.is_symlink() or candidate.resolve(strict=False) != candidate.absolute():
            raise IsolatedExecutorError(
                f"workspace_root path contains a symlink: {candidate}")
        candidates.append(candidate)
    candidates.extend((repository.parent.resolve(), repository.resolve()))
    for candidate in candidates:
        if (candidate / "nuttx" / "CMakeLists.txt").is_file():
            return candidate
    raise IsolatedExecutorError(
        "workspace_root must contain nuttx/CMakeLists.txt; no shared source root found")


def _declare_source_view(repository: Path, workspace: Path,
                         source_view: Path) -> dict[str, Any]:
    """Declare one entity-level source snapshot for every execution role.

    ``prepare`` creates only a validated requirement marker.  The actual
    filesystem copy is performed by :func:`materialize_sources`, once for the
    entity, and every role then points at this same read-only tree.
    """
    if source_view.exists() or source_view.is_symlink():
        raise IsolatedExecutorError(
            f"source snapshot root must be fresh and absent: {source_view}")
    source_view.mkdir(parents=True)
    if source_view.is_symlink():
        raise IsolatedExecutorError("source snapshot root must not be a symlink")
    shared_nuttx = workspace / "nuttx"
    shared_config = shared_nuttx / ".config"
    shared_make_defs = shared_nuttx / "Make.defs"
    board = source_view / "contest2026_135_yongwangzhiqian/board/bk7258"
    nuttx = source_view / "nuttx"
    board_digest = _tree_digest(repository / "board/bk7258")
    requirements = {
        "schema": SCHEMA,
        "kind": "entity-source-snapshot-requirement",
        "version": 1,
        "materialized": False,
        "scope": [
            "nuttx", "apps", "external", "frameworks", "packages",
            "prebuilts", "vendor", "contest2026_135_yongwangzhiqian",
        ],
        "excluded_shared_state": [
            "nuttx/.config", "nuttx/defconfig", "nuttx/Make.defs",
            "nuttx/arch/arm/include/board", "nuttx/arch/arm/include/chip",
            "nuttx/arch/arm/src/board", "nuttx/arch/arm/src/chip",
            "board/bk7258/bootloader.tmp", "board/bk7258/bootloader/bl2_crc.bin.json",
            "board/bk7258/bootloader/bl_crc.bin",
        ],
        "source_head": _git_head(repository),
        "workspace_head": _git_head(workspace),
        "untracked_source_whitelist": ["app", "board/bk7258", "nuttx/openamp"],
        "ignored_source_paths": ["nuttx/openamp"],
        "board_source_digest": board_digest,
    }
    requirements_path = source_view / "SNAPSHOT-REQUIRED.json"
    requirements_path.write_bytes(_canonical(requirements))
    return {
        "root": str(source_view),
        "nuttx": str(nuttx),
        "board": str(board),
        "phase": "prepare",
        "materialized": False,
        "required": True,
        "kind": "entity-snapshot-required",
        "shared_source_root_used": False,
        "shared_nuttx": str(shared_nuttx),
        "shared_config": str(shared_config),
        "shared_config_detected": shared_config.is_file(),
        "shared_make_defs_detected": shared_make_defs.is_file(),
        "excluded_shared_state": [
            "nuttx/.config", "nuttx/defconfig", "nuttx/Make.defs",
            "nuttx/arch/arm/include/board", "nuttx/arch/arm/include/chip",
            "nuttx/arch/arm/src/board", "nuttx/arch/arm/src/chip",
            "board/bk7258/bootloader.tmp", "board/bk7258/bootloader/bl2_crc.bin.json",
            "board/bk7258/bootloader/bl_crc.bin",
        ],
        "snapshot_scope": [
            "nuttx", "apps", "external", "frameworks", "packages",
            "prebuilts", "vendor", "contest2026_135_yongwangzhiqian",
        ],
        "snapshot_source_head": _git_head(repository),
        "workspace_source_head": _git_head(workspace),
        "snapshot_board_source_digest": board_digest,
        "untracked_source_whitelist": ["app", "board/bk7258", "nuttx/openamp"],
        "ignored_source_paths": ["nuttx/openamp"],
        "snapshot_requirements": str(requirements_path),
        "snapshot_requirements_sha256": _digest_file(requirements_path),
        "snapshot_manifest": None,
        "snapshot_manifest_sha256": None,
        "snapshot_identity_sha256": None,
    }


def _copy_seed(source: Path, target: Path) -> None:
    if target.exists() or target.is_symlink():
        raise IsolatedExecutorError(f"refusing to replace role seed: {target}")
    shutil.copytree(source, target, symlinks=False)
    if target.is_symlink():
        raise IsolatedExecutorError(f"materialized role seed is a symlink: {target}")


def _materialize_seeds(plan: dict[str, Any], repository: Path,
                       source_views: dict[str, dict[str, Any]],
                       role_roots: dict[str, Path]) -> dict[str, dict[str, Any]]:
    """Generate private role configs from canonical fragments.

    The compatibility-shaped return record is retained for the manifest
    schema, but no legacy ``configs/<profile>`` input is read or copied.  A
    role directory contains only the small generated profile header and the
    resolved canonical defconfig.
    """
    result: dict[str, dict[str, Any]] = {}
    for role in RUNTIME_ROLES:
        row = plan["legacy_adapter"]["seed_profiles"][role]
        (role_roots[role] / "config").mkdir(parents=True, exist_ok=True)
        role_config = role_roots[role] / "config" / row["target_profile"]
        if role_config.exists() or role_config.is_symlink():
            raise IsolatedExecutorError(f"refusing to replace generated role config: {role_config}")
        ir = framework.resolve(
            repository, plan["identity_inputs"]["product"], role,
            plan["identity_inputs"]["board"], plan["identity_inputs"]["mode"])
        config = framework.config_document(ir)
        if config["identity_sha256"] != plan["roles"][role]["config_identity_sha256"]:
            raise IsolatedExecutorError(f"canonical role config identity mismatch: {role}")
        role_config.mkdir()
        profile_text = framework._canonical_profile_text(ir)
        (role_config / "profile.conf").write_text(profile_text, encoding="utf-8")
        (role_config / "defconfig").write_text(config["defconfig"], encoding="utf-8")
        profile_sha = _digest_file(role_config / "profile.conf")
        defconfig_sha = _digest_file(role_config / "defconfig")
        if profile_sha != row["materialized_profile_sha256"]:
            raise IsolatedExecutorError(f"canonical profile identity mismatch: {role}")
        if defconfig_sha != row["materialized_defconfig_sha256"]:
            raise IsolatedExecutorError(f"canonical defconfig identity mismatch: {role}")
        result[role] = {
            "seed_profile": row["seed_profile"],
            "target_profile": row["target_profile"],
            "overlay": row["overlay"],
            "overlay_sha256": row["overlay_sha256"],
            "profile_sha256": row["profile_sha256"],
            "defconfig_sha256": row["defconfig_sha256"],
            "materialized_profile_sha256": profile_sha,
            "materialized_defconfig_sha256": defconfig_sha,
            # ``role_config_root`` is the private directory containing the
            # materialized profile.  BOARD_CONFIG points at the actual
            # materialized profile, never at the pending source snapshot.
            "role_config_root": str(role_config.parent),
            "role_config_path": str(role_config),
            "cmake_board_config": str(role_config),
        }
    return result


def _copy_writable_tree(source: Path, destination: Path) -> None:
    """Copy bootloader inputs into a new role-local writable tree.

    The source snapshot is immutable and may contain audited internal links.
    ``copytree(..., symlinks=False)`` dereferences those links into fresh
    inodes; the resulting staging tree is then checked to ensure no link or
    hardlink can route a bootloader write back to the snapshot or workspace.
    """
    if source.is_symlink() or not source.is_dir():
        raise IsolatedExecutorError(
            f"bootloader source is not a real directory: {source}")
    if destination.exists() or destination.is_symlink():
        raise IsolatedExecutorError(
            f"bootloader staging must be fresh and absent: {destination}")
    try:
        shutil.copytree(source, destination, symlinks=False)
    except OSError as error:
        raise IsolatedExecutorError(
            f"cannot copy bootloader source to staging: {destination}") from error
    try:
        entries = list(os.walk(destination, topdown=True, followlinks=False))
        for current, directories, files in entries:
            current_path = Path(current)
            if current_path.is_symlink():
                raise IsolatedExecutorError(
                    f"bootloader staging contains a symlink: {current_path}")
            os.chmod(current_path, os.stat(current_path, follow_symlinks=False).st_mode | 0o700)
            for name in directories + files:
                path = current_path / name
                if path.is_symlink():
                    raise IsolatedExecutorError(
                        f"bootloader staging contains a symlink: {path}")
                item_stat = path.stat(follow_symlinks=False)
                if not (item_stat.st_mode & 0o170000) in (0o040000, 0o100000):
                    raise IsolatedExecutorError(
                        f"bootloader staging contains a special entry: {path}")
                if path.is_dir():
                    os.chmod(path, item_stat.st_mode | 0o700)
                else:
                    if item_stat.st_nlink != 1:
                        raise IsolatedExecutorError(
                            f"bootloader staging contains a hardlink: {path}")
                    os.chmod(path, item_stat.st_mode | 0o600)
    except OSError as error:
        raise IsolatedExecutorError(
            f"cannot make bootloader staging writable: {destination}") from error


def _copy_bootloader_sources(source_board: Path, destination: Path) -> None:
    """Mirror the board-local paths used by BL1/BL2 Makefiles.

    The Makefiles intentionally use paths such as ``../include`` and
    ``../partitions`` from ``bootloader``.  A staging directory containing
    only ``bootloader`` would therefore silently fall back to a shared
    checkout.  Copy the small board-owned dependency roots beside it, while
    leaving the large SDK bundle out because it is not a bootloader source
    input (SDK identity is pinned separately in the build plan).
    """
    if source_board.is_symlink() or not source_board.is_dir():
        raise IsolatedExecutorError(f"snapshot board source is not a directory: {source_board}")
    if destination.exists() or destination.is_symlink():
        raise IsolatedExecutorError(f"bootloader staging must be fresh and absent: {destination}")
    destination.mkdir(parents=True)
    # These are the relative board roots consumed by the two Makefiles.  Keep
    # ``bk_idk`` as a real empty namespace only when a source checkout has it;
    # no link is ever introduced back to the read-only snapshot.
    required_roots = ("bootloader", "chip", "include", "partitions", "scripts")
    try:
        for name in required_roots:
            _copy_writable_tree(source_board / name, destination / name)
        bk_idk = source_board / "bk_idk"
        if bk_idk.is_dir() and not bk_idk.is_symlink():
            # Makefile execution does not consume the SDK payload here, but a
            # real namespace keeps relative metadata references unambiguous.
            (destination / "bk_idk").mkdir()
            os.chmod(destination / "bk_idk", 0o700)
    except OSError as error:
        raise IsolatedExecutorError(
            f"cannot create bootloader staging layout: {destination}") from error


def _audit_writable_tree(root: Path) -> None:
    """Verify a role-local staging tree cannot write through to the snapshot."""
    if root.is_symlink() or not root.is_dir():
        raise IsolatedExecutorError(f"bootloader staging is not a real directory: {root}")
    try:
        root_stat = root.stat(follow_symlinks=False)
    except OSError as error:
        raise IsolatedExecutorError(
            f"cannot stat bootloader staging: {root}") from error
    if not (root_stat.st_mode & 0o222):
        raise IsolatedExecutorError(f"bootloader staging is not writable: {root}")
    for current, directories, files in os.walk(root, topdown=True, followlinks=False):
        current_path = Path(current)
        if current_path.is_symlink():
            raise IsolatedExecutorError(f"bootloader staging contains a symlink: {current_path}")
        for name in directories + files:
            path = current_path / name
            if path.is_symlink():
                raise IsolatedExecutorError(f"bootloader staging contains a symlink: {path}")
            item_stat = path.stat(follow_symlinks=False)
            if path.is_file() and item_stat.st_nlink != 1:
                raise IsolatedExecutorError(f"bootloader staging contains a hardlink: {path}")
            if not (item_stat.st_mode & 0o222):
                raise IsolatedExecutorError(f"bootloader staging is not writable: {path}")


def _manifest_mapping(value: Path | dict[str, Any]) -> tuple[dict[str, Any], Path | None]:
    """Load a manifest mapping and retain its on-disk path when supplied."""
    if isinstance(value, Path):
        path = _safe_path(value, "manifest")
        _reject_traversal(path, "manifest")
        _reject_existing_symlink_components(path, "manifest")
        try:
            loaded = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, json.JSONDecodeError) as error:
            raise IsolatedExecutorError(f"cannot load isolated manifest: {path}") from error
        if not isinstance(loaded, dict):
            raise IsolatedExecutorError("isolated manifest must be a JSON object")
        return loaded, path
    if isinstance(value, dict):
        return dict(value), None
    raise IsolatedExecutorError("isolated manifest must be a path or object")


def _normalize_materialized_manifest(value: dict[str, Any]) -> dict[str, Any]:
    """Migrate a pre-runtime artifact-record manifest in memory.

    ``materialize-sources`` predates the runtime phase, so manifests produced
    by that stage do not contain the runtime-only ``artifacts`` and command
    execution-record fields.  They are still valid materialized inputs: the
    source snapshot and its identity are the state-machine boundary.  Verify
    the old manifest identity before adding the empty/default fields, then
    bind a new identity to the normalized in-memory value.  The original file
    is not rewritten unless runtime compilation reaches its successful
    terminal phase.
    """
    if value.get("phase") != "materialized":
        return value
    roles = value.get("roles")
    if not isinstance(roles, dict) or set(roles) != set(ROLES):
        return value
    rows = [roles[role] for role in ROLES]
    if any(not isinstance(row, dict) for row in rows):
        return value
    legacy_artifacts = ["artifacts" not in row for row in rows]
    if not any(legacy_artifacts):
        return value
    if not all(legacy_artifacts):
        raise RuntimeCompileError("materialized manifest mixes runtime record schemas")

    supplied = value.get("identity_sha256")
    body = dict(value)
    body.pop("identity_sha256", None)
    if (not isinstance(supplied, str) or
            _digest_bytes(_canonical(body)) != supplied):
        raise RuntimeCompileError("materialized manifest identity mismatch")

    normalized = dict(value)
    normalized_roles: dict[str, dict[str, Any]] = {}
    for role in ROLES:
        row = dict(roles[role])
        row["artifacts"] = {}
        commands = row.get("commands")
        if isinstance(commands, list):
            normalized_commands = []
            for command in commands:
                if not isinstance(command, dict):
                    normalized_commands.append(command)
                    continue
                normalized_command = dict(command)
                normalized_command.setdefault("log", None)
                normalized_command.setdefault("returncode", None)
                normalized_commands.append(normalized_command)
            row["commands"] = normalized_commands
        normalized_roles[role] = row

    # The first materialize-only implementation described the final runtime
    # operation as ``nuttx_post_build``.  That operation is deliberately not
    # part of compile-runtime.  Convert only that exact legacy CP/AP command
    # shape to the current configure -> nuttx -> nuttx-bin contract before the
    # normal allowlist validator sees it.
    apps_root = str(Path(value["source_view"]["root"]) / "apps")
    for role in RUNTIME_ROLES:
        row = normalized_roles[role]
        commands = row.get("commands")
        if not isinstance(commands, list):
            continue
        stages = [command.get("stage") for command in commands
                  if isinstance(command, dict)]
        if stages != ["partition-contract", "cmake-configure", "cmake-build"]:
            continue
        configure = commands[1]
        build = commands[2]
        if (not isinstance(configure, dict) or not isinstance(build, dict) or
                configure.get("tool") != "cmake" or build.get("tool") != "cmake"):
            raise RuntimeCompileError(f"legacy runtime commands are malformed: {role}")
        configure_argv = list(configure.get("argv", []))
        if ("-B" not in configure_argv or
                configure_argv.index("-B") + 1 >= len(configure_argv)):
            raise RuntimeCompileError(f"legacy runtime configure is malformed: {role}")
        if "-G" not in configure_argv:
            binary_index = configure_argv.index("-B") + 2
            configure_argv[binary_index:binary_index] = [
                "-G", "Ninja", f"-DNUTTX_APPS_DIR={apps_root}",
                "-DPython3_EXECUTABLE=python3"]
        configure["argv"] = configure_argv
        build_argv = list(build.get("argv", []))
        if ("--target" not in build_argv or
                build_argv[build_argv.index("--target") + 1] != "nuttx_post_build"):
            raise RuntimeCompileError(f"legacy runtime target is not postbuild: {role}")
        target_index = build_argv.index("--target") + 1
        build_argv[target_index] = "nuttx"
        build["argv"] = build_argv
        build_bin = dict(build)
        build_bin["stage"] = "cmake-build-bin"
        build_bin_argv = list(build_argv)
        build_bin_argv[target_index] = "nuttx-bin"
        build_bin["argv"] = build_bin_argv
        row["commands"] = [commands[0], configure, build, build_bin]
    normalized["roles"] = normalized_roles
    normalized.pop("identity_sha256", None)
    normalized["identity_sha256"] = _digest_bytes(_canonical(normalized))
    return normalized


def _manifest_path_inside_build_root(path: Path, build_root: Path,
                                     field: str, *, allow_existing: bool) -> Path:
    """Validate a manifest path without following a symlink component."""
    path = _safe_path(path, field)
    _reject_traversal(path, field)
    build_root = _safe_path(build_root, "build_root")
    if path == build_root or not _inside(path, build_root):
        raise IsolatedExecutorError(f"{field} must be inside the external build_root")
    current = path.parent
    while current != build_root:
        if current.is_symlink():
            raise IsolatedExecutorError(f"{field} parent contains a symlink: {current}")
        current = current.parent
        if not _inside(current, build_root):
            raise IsolatedExecutorError(f"{field} escaped build_root")
    if path.is_symlink() or (path.exists() and not allow_existing):
        raise IsolatedExecutorError(f"{field} is not a safe regular path: {path}")
    if allow_existing and path.exists() and not (path.is_file() or path.is_dir()):
        raise IsolatedExecutorError(f"{field} must be a regular filesystem entry: {path}")
    canonical_root = build_root.resolve(strict=True)
    canonical = path.resolve(strict=False)
    if canonical == canonical_root or not _inside(canonical, canonical_root):
        raise IsolatedExecutorError(f"{field} escaped resolved build_root")
    return path


def _audit_materialized_snapshot(source_root: Path) -> dict[str, Any]:
    """Audit helper output before any role-local staging is writable."""
    try:
        manifest = audit_snapshot(source_root)
    except (SnapshotError, OSError, ValueError, KeyError, TypeError) as error:
        raise IsolatedExecutorError(
            f"source snapshot manifest audit failed: {source_root}") from error
    if manifest.get("manifest_path") != SNAPSHOT_MANIFEST_FILENAME:
        raise IsolatedExecutorError("source snapshot manifest path is not canonical")
    if manifest.get("version") != 1:
        raise IsolatedExecutorError("source snapshot manifest version is unsupported")
    if (manifest.get("overall_identity_sha256") is not None and
            manifest.get("identity_sha256") != manifest.get("overall_identity_sha256")):
        raise IsolatedExecutorError("source snapshot manifest identity aliases differ")
    return manifest


def _verify_snapshot_inputs(value: dict[str, Any], source_root: Path) -> None:
    """Reject source/catalog drift between prepare and materialization."""
    board_source = source_root / "contest2026_135_yongwangzhiqian/board/bk7258"
    board_digest = _tree_digest(board_source)
    expected_board_digest = value["source_view"]["board_source_digest"]
    if board_digest != expected_board_digest:
        raise IsolatedExecutorError(
            "materialized board source differs from prepare digest")

    # The source helper's entity root is a filesystem snapshot with the
    # contest repository nested under its canonical root name.  Re-resolve
    # the complete plan from that immutable nested repository so catalog,
    # partition, SDK, and legacy seed identities cannot drift between phases.
    snapshot_repository = source_root / "contest2026_135_yongwangzhiqian"
    if snapshot_repository.is_symlink() or not snapshot_repository.is_dir():
        raise IsolatedExecutorError(
            "materialized snapshot lacks a real contest repository root")
    try:
        snapshot_plan = framework.build_plan(
            snapshot_repository, value["product"], value["board"], value["mode"])
    except (framework.FrameworkError, OSError, ValueError, KeyError, TypeError,
            AttributeError) as error:
        raise IsolatedExecutorError(
            "cannot resolve build plan from materialized snapshot") from error
    if (not isinstance(snapshot_plan, dict) or
            snapshot_plan.get("identity_sha256") != value["build_plan_identity_sha256"]):
        raise IsolatedExecutorError(
            "materialized snapshot build plan identity differs from prepare")


def _role_root(plan: dict[str, Any], role: str, build_root: Path) -> Path:
    template = plan["roles"][role]["build_root_template"]
    if not template.startswith("${BUILD_ROOT}/"):
        raise IsolatedExecutorError(f"unsupported role build root template: {role}")
    relative = template.removeprefix("${BUILD_ROOT}/")
    path = build_root / relative
    if path == build_root or path.is_symlink():
        raise IsolatedExecutorError(f"role root is not private: {role}")
    return path


def _role_paths(plan: dict[str, Any], build_root: Path) -> dict[str, dict[str, Path]]:
    result: dict[str, dict[str, Path]] = {}
    seen: set[Path] = set()
    for role in ROLES:
        root = _role_root(plan, role, build_root)
        artifact_template = plan["roles"][role]["artifact_root_template"]
        config_template = plan["roles"][role]["config_path_template"]
        if not artifact_template.startswith("${BUILD_ROOT}/") or not config_template.startswith("${BUILD_ROOT}/"):
            raise IsolatedExecutorError(f"unsupported role path template: {role}")
        artifact = build_root / artifact_template.removeprefix("${BUILD_ROOT}/")
        config = build_root / config_template.removeprefix("${BUILD_ROOT}/")
        partition_contract = root / "partition-contract"
        cmake_binary = root / "cmake"
        bootloader_staging = root / BOOTLOADER_STAGING_DIRNAME
        if any(path in seen for path in (
                root, artifact, config, partition_contract, cmake_binary,
                bootloader_staging)):
            raise IsolatedExecutorError("role build/config/artifact paths are not unique")
        seen.update((root, artifact, config, partition_contract, cmake_binary,
                     bootloader_staging))
        result[role] = {
            "root": root,
            "artifact": artifact,
            "config": config,
            "partition_contract": partition_contract,
            "cmake_binary": cmake_binary,
            "bootloader_staging": bootloader_staging,
        }
    return result


def _source_board_path(source_view: dict[str, Any], relative: str) -> Path:
    prefix = "board/bk7258/"
    if not isinstance(relative, str) or not relative.startswith(prefix):
        raise IsolatedExecutorError(
            f"source path is not board/bk7258-relative: {relative}")
    return Path(source_view["board"]) / relative.removeprefix(prefix)


def _env_common(plan: dict[str, Any], role: str, paths: dict[str, Path],
                repository: Path, source_view: dict[str, Any]) -> dict[str, str]:
    inputs = plan["identity_inputs"]
    environment = {
        "BK7258_PRODUCT": inputs["product"],
        "BK7258_BOARD": inputs["board"],
        "BK7258_MODE": inputs["mode"],
        "BK7258_ROLE": role,
        "BK7258_PARTITION_CONTRACT_ROOT": str(paths["partition_contract"]),
        "BK7258_PARTITION_LAYOUT_SOURCE": str(
            _source_board_path(source_view, plan["partition_layout"]["source"])),
        "BK7258_PARTITION_LAYOUT_ID": plan["partition_layout"]["layout_id"],
        "BK7258_PARTITION_LAYOUT_SHA256": plan["partition_layout"]["layout_sha256"],
        "BK7258_SOURCE_VIEW": source_view["root"],
    }
    if role in BOOT_ROLES:
        environment["BK7258_BOOTLOADER_STAGING_ROOT"] = str(paths["bootloader_staging"])
        environment["BK7258_BOOTLOADER_ROOT"] = str(
            paths["bootloader_staging"] / "bootloader")
        # Compile-only is reconciled with the checked-in policy, but never
        # represents a bootable/signed image.
        environment["BK7258_BOOT_POLICY_STATUS"] = "RECONCILED"
        environment["BK7258_BOOT_EXECUTION"] = "COMPILE_ONLY"
    if role in RUNTIME_ROLES:
        environment["BK7258_SDK_BUNDLE_VERSION"] = plan["sdk"]["versions"][role]
        environment[f"BK7258_{role.upper()}_SDK_BUNDLE_VERSION"] = plan["sdk"]["versions"][role]
    # Never inherit or serialize credentials into a future execution command.
    # The command runner adds no ambient environment.  Keep the tiny stable
    # locale/PATH subset in the manifest so the recorded environment is the
    # environment that was actually passed to the child process.
    environment.update({
        "PATH": os.defpath,
        "LANG": "C",
        "LC_ALL": "C",
        "PYTHONUNBUFFERED": "1",
        "PYTHONDONTWRITEBYTECODE": "1",
        "HOME": str(paths["root"] / ".home"),
        "TMPDIR": str(paths["root"] / "tmp"),
        "XDG_CACHE_HOME": str(paths["root"] / ".cache"),
    })
    return environment


def _boot_make_variables(plan: dict[str, Any], resolved_policy: dict[str, Any],
                         role: str) -> list[str]:
    """Render the non-secret boot policy into strict Make variables."""
    policy = resolved_policy["policy"]
    debug = policy["debug_io"]
    swd = debug["swd"]
    console = debug["console"]
    pin_group = {"p20_p21": "0", "p0_p1": "1", "none": "1"}[swd["pin_group"]]
    target = {"none": "0", "cp": "0", "ap0": "1", "ap1": "2"}[swd["target"]]
    holds = swd["stage_holds"]
    console_uart = {"uart0": "0", "uart1": "1", "uart2": "2"}.get(console["uart"], "3")
    console_baud = str(console["baud"] if console["baud"] is not None else 115200)
    console_data_bits = str(console["data_bits"] if console["data_bits"] is not None else 8)
    console_parity = {None: "0", "none": "0", "even": "1", "odd": "2"}[console["parity"]]
    console_stop_bits = str(console["stop_bits"] if console["stop_bits"] is not None else 1)
    common = [
        f"{role.upper()}_SWD_ENABLE={'1' if swd['enabled'] else '0'}",
        f"{role.upper()}_SWD_PIN_GROUP={pin_group}",
        f"{role.upper()}_SWD_TARGET={target}",
        f"{role.upper()}_SWD_BOOT_HOLD={'1' if holds.get(role, False) else '0'}",
        f"{role.upper()}_CONSOLE_UART={console_uart}",
        f"{role.upper()}_CONSOLE_BAUD={console_baud}",
        f"{role.upper()}_CONSOLE_DATA_BITS={console_data_bits}",
        f"{role.upper()}_CONSOLE_PARITY={console_parity}",
        f"{role.upper()}_CONSOLE_STOP_BITS={console_stop_bits}",
        f"{role.upper()}_UART2_PIN_GROUP=0",
    ]
    if role == "bl1":
        boot = policy["boot"]
        common.extend([
            "BL1_COMPILE_ONLY=1",
            f"BL1_COMPILE_POLICY={boot}",
            f"BL1_USE_BL2={'1' if boot == 'mcuboot' else '0'}",
            f"BL1_MANIFEST_ENFORCE={'1' if boot == 'mcuboot' else '0'}",
            "BL1_MANIFEST_KEY_SOURCE=boot_bl1_manifest_key.c",
        ])
        if boot == "mcuboot":
            image_size = resolved_policy.get("bl2_image_logical_size")
            if image_size != plan.get("bl2_image_logical_size") or not isinstance(image_size, int):
                raise IsolatedExecutorError(
                    "BL1/BL2 compile-time image size is not plan-bound")
            common.append(f"BL2_LOGICAL_SIZE=0x{image_size:x}")
        if boot == "raw":
            common.append("BL1_MANIFEST_RAW_PAGE=0")
        return common
    if role != "bl2":
        raise IsolatedExecutorError(f"boot policy variables requested for non-boot role: {role}")
    geometry = resolved_policy["bl2_geometry"]
    # ``resolve_policy`` represents a resolved MCUboot geometry by its
    # primary/secondary records; unlike the raw non-applicable sentinel it
    # does not carry a separate ``required`` flag.
    image_size = resolved_policy.get("bl2_image_logical_size")
    if (policy["boot"] != "mcuboot" or
            not isinstance(geometry.get("primary"), dict) or
            not isinstance(image_size, int) or
            image_size != plan.get("bl2_image_logical_size")):
        raise IsolatedExecutorError("BL2 compile-only variables require MCUboot geometry")
    primary = geometry["primary"]
    # The SRAM execution base is a board-owned linker contract, not a secret
    # or a caller-controlled path.  All flash geometry values come from the
    # resolved policy/layout above.
    execution_base = 0x28020000
    common.extend([
        "BL2_COMPILE_ONLY=1",
        "BL2_COMPILE_POLICY=mcuboot",
        "BL2_KEY_SOURCE=bk7258_bl2_keys.c",
        f"BL2_LOGICAL_SIZE=0x{image_size:x}",
        f"BL2_XIP_BASE=0x{primary['xip_start']:08x}",
        f"BL2_LOGICAL_CAPACITY=0x{primary['logical_size']:x}",
        f"BL2_LOGICAL_CAPACITY_BYTES={primary['logical_size']}",
        f"BL2_PHYSICAL_SIZE=0x{primary['physical_size']:x}",
        f"BL2_PHYSICAL_SIZE_BYTES={primary['physical_size']}",
        f"BL2_EXECUTION_BASE=0x{execution_base:08x}",
        f"BL2_SECURITY_COUNTER_FLOOR={policy['version_policy']['bl2_security_counter_floor']['default']}",
        f"BL2_LAYOUT_ID={plan['partition_layout']['layout_id']}",
        f"BL2_LAYOUT_SHA256={plan['partition_layout']['layout_sha256']}",
    ])
    return common


def _commands(plan: dict[str, Any], role: str, paths: dict[str, Path],
              repository: Path, source_view: dict[str, Any],
              seed: dict[str, Any] | None,
              bootloader_staging: Path | None = None,
              resolved_policy: dict[str, Any] | None = None) -> list[dict[str, Any]]:
    env = _env_common(plan, role, paths, repository, source_view)
    partition_generator = Path(source_view["board"]) / "scripts/gen_bk7258_partitions.py"
    partition_csv = _source_board_path(source_view, plan["partition_layout"]["source"])
    partition_args = [
        "python3", str(partition_generator), "--input",
        str(partition_csv), "--header",
        str(paths["partition_contract"] / "include/arch/board/bk7258_partition_layout.h"),
        "--output-dir", str(paths["partition_contract"] / "generated"),
        "--expect-layout-id", plan["partition_layout"]["layout_id"],
        "--expect-layout-sha256", plan["partition_layout"]["layout_sha256"],
    ]
    commands = [{
        "stage": "partition-contract",
        "tool": "python3",
        "argv": partition_args,
        "environment": dict(env),
        "cwd": str(paths["root"]),
        "status": "NOT_RUN",
        "log": None,
        "returncode": None,
        "precondition": "entity-source-snapshot-materialized",
    }]
    if role == "bl2":
        if bootloader_staging is None:
            raise IsolatedExecutorError("BL2 bootloader staging path is missing")
        partition_source = plan["partition_layout"]["source"]
        source_prefix = "board/bk7258/"
        if not isinstance(partition_source, str) or not partition_source.startswith(source_prefix):
            raise IsolatedExecutorError("resolved partition source is not board-local")
        make_variables = [
            f"PARTITION_CONTRACT_ROOT={paths['partition_contract']}",
            f"PARTITION_CSV={partition_csv}",
            f"PARTITION_GENERATOR={partition_generator}",
            f"PARTITION_LAYOUT_ID={plan['partition_layout']['layout_id']}",
            f"PARTITION_LAYOUT_SHA256={plan['partition_layout']['layout_sha256']}",
            f"NUTTX_ROOT={source_view['nuttx']}",
            f"TOP={source_view['root']}/",
        ]
        if resolved_policy is None:
            raise IsolatedExecutorError("BL2 command requires resolved boot policy")
        make_variables.extend(_boot_make_variables(plan, resolved_policy, role))
        commands.append({
            "stage": "make-compile-only",
            "tool": "make",
            "argv": ["make", *make_variables, "-C",
                     str(bootloader_staging / "bootloader/bl2"), "compile-only"],
            "environment": dict(env),
            "cwd": str(paths["root"]),
            "status": "NOT_RUN",
            "log": None,
            "returncode": None,
            "precondition": "entity-source-snapshot-materialized",
        })
    elif role == "bl1":
        if bootloader_staging is None:
            raise IsolatedExecutorError("BL1 bootloader staging path is missing")
        partition_source = plan["partition_layout"]["source"]
        source_prefix = "board/bk7258/"
        if not isinstance(partition_source, str) or not partition_source.startswith(source_prefix):
            raise IsolatedExecutorError("resolved partition source is not board-local")
        make_variables = [
            f"PARTITION_CONTRACT_ROOT={paths['partition_contract']}",
            f"PARTITION_CSV={partition_csv}",
            f"PARTITION_GENERATOR={partition_generator}",
            f"PARTITION_LAYOUT_ID={plan['partition_layout']['layout_id']}",
            f"PARTITION_LAYOUT_SHA256={plan['partition_layout']['layout_sha256']}",
            f"NUTTX_ROOT={source_view['nuttx']}",
        ]
        if resolved_policy is None:
            raise IsolatedExecutorError("BL1 command requires resolved boot policy")
        make_variables.extend(_boot_make_variables(plan, resolved_policy, role))
        commands.append({
            "stage": "make-compile-only",
            "tool": "make",
            "argv": ["make", *make_variables, "-C",
                     str(bootloader_staging / "bootloader"), "compile-only"],
            "environment": dict(env),
            "cwd": str(paths["root"]),
            "status": "NOT_RUN",
            "log": None,
            "returncode": None,
            "precondition": "entity-source-snapshot-materialized",
        })
    else:
        assert seed is not None
        commands.extend(({
            "stage": "cmake-configure",
            "tool": "cmake",
            "argv": [
                "cmake", "-S", source_view["nuttx"], "-B", str(paths["cmake_binary"]),
                "-G", "Ninja",
                f"-DNUTTX_APPS_DIR={Path(source_view['root']) / 'apps'}",
                "-DPython3_EXECUTABLE=python3",
                f"-DBOARD_CONFIG={seed['cmake_board_config']}",
            ],
            "environment": dict(env),
            "cwd": str(paths["root"]),
            "status": "NOT_RUN",
            "log": None,
            "returncode": None,
            "precondition": "entity-source-snapshot-materialized",
        }, {
            "stage": "cmake-build",
            "tool": "cmake",
            "argv": ["cmake", "--build", str(paths["cmake_binary"]), "--target", "nuttx"],
            "environment": dict(env),
            "cwd": str(paths["root"]),
            "status": "NOT_RUN",
            "log": None,
            "returncode": None,
            "precondition": "entity-source-snapshot-materialized",
        }, {
            "stage": "cmake-build-bin",
            "tool": "cmake",
            "argv": ["cmake", "--build", str(paths["cmake_binary"]), "--target", "nuttx-bin"],
            "environment": dict(env),
            "cwd": str(paths["root"]),
            "status": "NOT_RUN",
            "log": None,
            "returncode": None,
            "precondition": "entity-source-snapshot-materialized",
        }))
    return commands


def _path_is_private_regular(path: Path, role_root: Path) -> bool:
    """Return whether *path* is a non-link regular file below ``role_root``."""
    try:
        path = _safe_path(path, "private output")
        role_root = _safe_path(role_root, "role root")
        _reject_traversal(path, "private output")
        if not _inside(path, role_root) or path == role_root:
            return False
        current = path
        while current != role_root:
            if current.is_symlink():
                return False
            current = current.parent
            if not _inside(current, role_root):
                return False
        mode = path.lstat().st_mode
        return stat.S_ISREG(mode) and not stat.S_ISLNK(mode)
    except (OSError, IsolatedExecutorError):
        return False


def _validate_command_environment(environment: Any, role: str) -> None:
    """Validate the explicit child environment and reject credential leakage."""
    if not isinstance(environment, dict):
        raise IsolatedExecutorError(f"command environment is malformed: {role}")
    for name, raw_value in environment.items():
        if (not isinstance(name, str) or not name or not isinstance(raw_value, str) or
                any(token in name.upper() for token in ("KEY", "TOKEN", "PASSWORD",
                                                        "SECRET", "CREDENTIAL")) or
                name in KEY_ENV_NAMES or name not in SAFE_ENV_NAMES and
                not name.startswith("BK7258_")):
            raise IsolatedExecutorError(f"command environment is not allowlisted: {role}/{name}")
        if "\x00" in raw_value:
            raise IsolatedExecutorError(f"command environment contains NUL: {role}/{name}")
        lowered = raw_value.lower()
        if any(marker in lowered for marker in (".pem", ".key", ".der", ".p8", ".p12",
                                                "private_key", "signing_key", "imgtool",
                                                "openssl")):
            raise IsolatedExecutorError(f"command environment contains credential/tool material: {role}/{name}")


def _validate_runtime_artifacts(artifacts: Any, role_root: Path, role: str) -> None:
    """Validate the immutable artifact index recorded after runtime build."""
    required = set(RUNTIME_ARTIFACT_NAMES)
    allowed = required | set(OPTIONAL_RUNTIME_ARTIFACT_NAMES)
    if (not isinstance(artifacts, dict) or
            not required.issubset(artifacts) or set(artifacts) - allowed):
        raise IsolatedExecutorError(f"runtime artifact set is incomplete: {role}")
    paths: set[str] = set()
    for name in artifacts:
        item = artifacts[name]
        if not isinstance(item, dict) or set(item) != {"path", "sha256", "size"}:
            raise IsolatedExecutorError(f"runtime artifact record is malformed: {role}/{name}")
        path_value = item["path"]
        if not isinstance(path_value, str) or not path_value:
            raise IsolatedExecutorError(f"runtime artifact path is malformed: {role}/{name}")
        path = Path(path_value)
        if path_value in paths or not _path_is_private_regular(path, role_root):
            raise IsolatedExecutorError(f"runtime artifact escaped role root: {role}/{name}")
        paths.add(path_value)
        if _digest_file(path) != item["sha256"]:
            raise IsolatedExecutorError(f"runtime artifact hash changed: {role}/{name}")
        try:
            size = path.stat(follow_symlinks=False).st_size
        except OSError as error:
            raise IsolatedExecutorError(f"runtime artifact stat failed: {role}/{name}") from error
        if (not isinstance(item["size"], int) or isinstance(item["size"], bool) or
                item["size"] <= 0 or item["size"] != size):
            raise IsolatedExecutorError(f"runtime artifact size is invalid: {role}/{name}")
        framework.digest(item["sha256"], f"runtime artifact {role}/{name}")


def _validate_boot_artifacts(artifacts: Any, role_root: Path, role: str) -> None:
    """Validate compile-only BL1/BL2 ELF/BIN records, never CRC outputs."""
    required = set(BOOT_ARTIFACT_NAMES[role])
    if not isinstance(artifacts, dict) or set(artifacts) != required:
        raise IsolatedExecutorError(f"boot compile-only artifact set is incomplete: {role}")
    for name, item in artifacts.items():
        if not isinstance(item, dict) or set(item) != {
                "path", "sha256", "size", "runnable", "trusted"}:
            raise IsolatedExecutorError(f"boot artifact record is malformed: {role}/{name}")
        if item["runnable"] is not False or item["trusted"] is not False:
            raise IsolatedExecutorError(f"boot compile-only artifact claims runtime trust: {role}/{name}")
        path = Path(item["path"])
        if not _path_is_private_regular(path, role_root):
            raise IsolatedExecutorError(f"boot artifact escaped role root: {role}/{name}")
        if _digest_file(path) != item["sha256"]:
            raise IsolatedExecutorError(f"boot artifact hash changed: {role}/{name}")
        size = path.stat(follow_symlinks=False).st_size
        if (not isinstance(item["size"], int) or isinstance(item["size"], bool) or
                item["size"] <= 0 or item["size"] != size):
            raise IsolatedExecutorError(f"boot artifact size is invalid: {role}/{name}")
        framework.digest(item["sha256"], f"boot artifact {role}/{name}")


def _collect_boot_artifacts(role_root: Path, staging_root: Path,
                            artifact_root: Path, role: str) -> dict[str, Any]:
    records: dict[str, Any] = {}
    for logical in BOOT_ARTIFACT_NAMES[role]:
        candidates = _runtime_candidates(staging_root, role_root, logical)
        if not candidates:
            raise RuntimeCompileError(f"compile-only Make did not produce {role}/{logical}")
        source = _select_runtime_output(staging_root, role_root, logical)
        destination = artifact_root / logical
        if destination.exists() or destination.is_symlink():
            raise RuntimeCompileError(f"boot artifact destination is not fresh: {destination}")
        destination.parent.mkdir(parents=True, exist_ok=True)
        try:
            destination.write_bytes(source.read_bytes())
            os.chmod(destination, stat.S_IMODE(source.stat(follow_symlinks=False).st_mode) & 0o555 or 0o400)
        except OSError as error:
            raise RuntimeCompileError(f"cannot collect boot artifact: {role}/{logical}") from error
        records[logical] = {
            "path": str(destination), "sha256": _digest_file(destination),
            "size": destination.stat(follow_symlinks=False).st_size,
            "runnable": False,
            "trusted": False,
        }
    _validate_boot_artifacts(records, role_root, role)
    return records


def _assert_snapshot_read_only(source_root: Path) -> None:
    """Recheck every snapshot entry before invoking an external build tool."""
    if source_root.is_symlink() or not source_root.is_dir():
        raise IsolatedExecutorError("source snapshot is not a real directory")
    for current, directories, files in os.walk(source_root, topdown=True, followlinks=False):
        current_path = Path(current)
        try:
            if current_path.is_symlink() or current_path.stat(follow_symlinks=False).st_mode & 0o222:
                raise IsolatedExecutorError(f"source snapshot directory is writable: {current_path}")
            for name in directories + files:
                path = current_path / name
                if path.is_symlink():
                    # Symlinks are audited separately by audit_snapshot; do not
                    # inspect their target's mode as a source write boundary.
                    continue
                mode = path.stat(follow_symlinks=False).st_mode
                if mode & 0o222:
                    raise IsolatedExecutorError(f"source snapshot entry is writable: {path}")
        except OSError as error:
            raise IsolatedExecutorError(f"cannot audit source snapshot permissions: {current_path}") from error


def _assert_delivery_snapshot_unchanged(
        source_root: Path, expected_identity: str, phase: str) -> None:
    """Keep a delivery phase red until its child tools leave source intact."""
    try:
        audited = _audit_materialized_snapshot(source_root)
    except IsolatedExecutorError as error:
        raise RuntimeDeliveryError(
            f"source snapshot audit failed during {phase}") from error
    if audited.get("identity_sha256") != expected_identity:
        raise RuntimeDeliveryError(f"source snapshot changed during {phase}")
    try:
        _assert_snapshot_read_only(source_root)
    except IsolatedExecutorError as error:
        raise RuntimeDeliveryError(
            f"source snapshot became writable during {phase}") from error


def _audit_plan_copy(value: dict[str, Any]) -> dict[str, Any]:
    """Load and hash-check the plan copy before any runtime subprocess."""
    path = Path(value["plan_copy"])
    if path.is_symlink() or not path.is_file():
        raise IsolatedExecutorError("isolated plan copy is not a regular file")
    if _digest_file(path) != value["plan_copy_sha256"]:
        raise IsolatedExecutorError("isolated plan copy hash changed")
    try:
        plan = framework.load_json(path)
        framework.validate_build_plan(plan)
    except (framework.FrameworkError, OSError, UnicodeError, json.JSONDecodeError) as error:
        raise IsolatedExecutorError("isolated plan copy is invalid") from error
    if plan.get("identity_sha256") != value["build_plan_identity_sha256"]:
        raise IsolatedExecutorError("isolated plan copy identity changed")
    inputs = value["identity_inputs"]
    plan_inputs = plan.get("identity_inputs", {})
    if any(plan_inputs.get(field) != inputs.get(field)
           for field in ("product", "family", "mode", "board", "boot", "active_roles")):
        raise IsolatedExecutorError("isolated plan copy inputs changed")
    if plan.get("active_roles") != value["active_roles"]:
        raise IsolatedExecutorError("isolated plan copy active_roles changed")
    if plan.get("partition_layout") != inputs["partition_layout"]:
        raise IsolatedExecutorError("isolated plan copy partition identity changed")
    return plan


def _resolve_policy_for_plan(repository: Path, plan: dict[str, Any]) -> dict[str, Any]:
    """Resolve checked-in boot metadata and bind it to one exact plan."""
    try:
        resolved = boot_policy.resolve_policy(
            repository, plan["identity_inputs"]["product"], build_plan=plan)
    except (boot_policy.BootPolicyError, OSError, ValueError, KeyError, TypeError) as error:
        raise IsolatedExecutorError("boot policy/plan reconciliation failed") from error
    expected = list(plan["active_roles"])
    if (resolved.get("active_roles") != expected or
            resolved.get("build_plan_identity_sha256") != plan["identity_sha256"] or
            resolved.get("bl2_image_logical_size") !=
            plan.get("bl2_image_logical_size")):
        raise IsolatedExecutorError("boot policy active-role or plan identity binding changed")
    policy = resolved.get("policy")
    if not isinstance(policy, dict) or policy.get("identity_sha256") is None:
        raise IsolatedExecutorError("resolved boot policy lacks policy identity")
    framework.digest(policy["identity_sha256"], "resolved boot policy identity")
    framework.digest(resolved.get("identity_sha256"), "resolved boot policy result identity")
    return resolved


def _manifest_boot_policy(resolved: dict[str, Any], plan: dict[str, Any]) -> dict[str, Any]:
    """Keep policy metadata precise while retaining compile-only boundaries."""
    policy = resolved["policy"]
    return {
        "status": "RECONCILED",
        "execution": "COMPILE_ONLY",
        "policy_id": policy["policy_id"],
        "policy_identity_sha256": policy["identity_sha256"],
        "resolved_policy_identity_sha256": resolved["identity_sha256"],
        "plan_identity_sha256": plan["identity_sha256"],
        "active_roles": list(plan["active_roles"]),
        "private_key_read": "NOT_RUN",
        "signing": "NOT_RUN",
    }


def _tool_search_path(*requested: str) -> str:
    """Build a deterministic tool PATH without inheriting the host PATH."""
    entries = [item for item in os.defpath.split(os.pathsep) if item]
    for value in requested:
        if os.sep not in value:
            continue
        parent = Path(value).expanduser().absolute().parent
        _reject_traversal(parent, "tool directory")
        if parent.is_symlink() or not parent.is_dir():
            raise RuntimeCompileError(f"tool directory is not a real directory: {parent}")
        if str(parent) not in entries:
            entries.insert(0, str(parent))
    return os.pathsep.join(entries)


def _resolve_tool(value: str | Path, name: str, search_path: str) -> Path:
    """Resolve one executable from the explicit PATH and reject unsafe files."""
    value_text = str(value)
    if not value_text or "\x00" in value_text:
        raise RuntimeCompileError(f"{name} executable is malformed")
    candidate = shutil.which(value_text, path=search_path)
    if candidate is None:
        raise RuntimeCompileError(f"{name} executable is not available on the safe PATH")
    path = Path(candidate).absolute()
    _reject_traversal(path, f"{name} executable")
    try:
        mode = path.lstat().st_mode
    except OSError as error:
        raise RuntimeCompileError(f"cannot inspect {name} executable: {path}") from error
    if stat.S_ISLNK(mode) or not stat.S_ISREG(mode) or not (mode & 0o111):
        raise RuntimeCompileError(f"{name} executable is not a normal executable file: {path}")
    return path


def _tool_version(path: Path, name: str, search_path: str) -> str:
    """Read a short tool version using only the explicit safe environment."""
    try:
        result = subprocess.run(
            [str(path), "--version"],
            cwd=str(Path.cwd()),
            env={"PATH": search_path, "LANG": "C", "LC_ALL": "C"},
            capture_output=True, text=True, check=False, timeout=15)
    except (OSError, subprocess.SubprocessError) as error:
        raise RuntimeCompileError(f"cannot query {name} version") from error
    if result.returncode != 0:
        raise RuntimeCompileError(f"{name} version query failed with {result.returncode}")
    output = (result.stdout or "").strip() or (result.stderr or "").strip()
    version = output.splitlines()[0].strip() if output else ""
    if not version:
        raise RuntimeCompileError(f"{name} did not report a version")
    return version


def _resolve_kconfiglib_root(root: str | Path | None) -> Path:
    """Resolve and audit the explicit directory containing Kconfig helpers."""
    if root is None:
        candidates = sorted({
            Path(entry) for entry in sys.path if entry and
            (Path(entry) / "kconfiglib.py").is_file() and
            (Path(entry) / "olddefconfig.py").is_file()
        }, key=str)
        if not candidates:
            raise RuntimeCompileError(
                "kconfiglib root is required (kconfiglib.py/olddefconfig.py not found)")
        root_path = candidates[0]
    else:
        root_path = Path(root).expanduser().absolute()
    root_path = root_path.absolute()
    _reject_traversal(root_path, "kconfiglib root")
    if root_path.is_symlink() or not root_path.is_dir():
        raise RuntimeCompileError(f"kconfiglib root is not a real directory: {root_path}")
    files = {
        name: root_path / name for name in ("kconfiglib.py", "olddefconfig.py")
    }
    for name, path in files.items():
        if path.is_symlink() or not path.is_file():
            raise RuntimeCompileError(f"kconfiglib helper is not a regular file: {path}")
    return root_path


def _resolve_runtime_tools(cmake_executable: str | Path,
                           python_executable: str | Path,
                           olddefconfig_executable: str | Path,
                           kconfiglib_root: str | Path | None,
                           make_executable: str | Path = "make") -> tuple[dict[str, Path], str, dict[str, Any]]:
    search_path = _tool_search_path(
        str(cmake_executable), str(python_executable), str(olddefconfig_executable),
        str(make_executable))
    paths = {
        "cmake": _resolve_tool(cmake_executable, "cmake", search_path),
        "python": _resolve_tool(python_executable, "python", search_path),
        "ninja": _resolve_tool("ninja", "ninja", search_path),
        "arm-none-eabi-gcc": _resolve_tool(
            "arm-none-eabi-gcc", "arm-none-eabi-gcc", search_path),
        "olddefconfig": _resolve_tool(
            olddefconfig_executable, "olddefconfig", search_path),
        "make": _resolve_tool(make_executable, "make", search_path),
    }
    kconfig_root = _resolve_kconfiglib_root(kconfiglib_root)
    versions = {
        name: {"path": str(path), "version": _tool_version(path, name, search_path)}
        for name, path in paths.items() if name != "olddefconfig"
    }
    versions["olddefconfig"] = {
        "path": str(paths["olddefconfig"]),
        "sha256": _digest_file(paths["olddefconfig"]),
    }
    versions["kconfiglib"] = {
        "root": str(kconfig_root),
        "kconfiglib_sha256": _digest_file(kconfig_root / "kconfiglib.py"),
        "olddefconfig_sha256": _digest_file(kconfig_root / "olddefconfig.py"),
    }
    versions["make"] = {
        "path": str(paths["make"]),
        "version": _tool_version(paths["make"], "make", search_path),
    }
    return paths, search_path, versions


def _validate_runtime_tools(tools: Any) -> None:
    if not isinstance(tools, dict) or set(tools) != set(TOOL_NAMES) | {
            "olddefconfig", "kconfiglib", "make"}:
        raise IsolatedExecutorError("runtime tool records are incomplete")
    for name in TOOL_NAMES:
        record = tools[name]
        if not isinstance(record, dict) or set(record) != {"path", "version"}:
            raise IsolatedExecutorError(f"runtime tool record is malformed: {name}")
        path = Path(record["path"])
        if not path.is_absolute() or path.is_symlink():
            raise IsolatedExecutorError(f"runtime tool path is unsafe: {name}")
        try:
            mode = path.lstat().st_mode
        except OSError as error:
            raise IsolatedExecutorError(f"runtime tool path is missing: {name}") from error
        if not stat.S_ISREG(mode) or not (mode & 0o111) or not isinstance(record["version"], str) or not record["version"]:
            raise IsolatedExecutorError(f"runtime tool record is invalid: {name}")
    olddefconfig = tools["olddefconfig"]
    if not isinstance(olddefconfig, dict) or set(olddefconfig) != {"path", "sha256"}:
        raise IsolatedExecutorError("olddefconfig tool record is malformed")
    olddefconfig_path = Path(olddefconfig["path"])
    if (not olddefconfig_path.is_absolute() or olddefconfig_path.is_symlink() or
            not olddefconfig_path.is_file() or
            not (olddefconfig_path.stat().st_mode & 0o111) or
            not isinstance(olddefconfig["sha256"], str) or
            _digest_file(olddefconfig_path) != olddefconfig["sha256"]):
        raise IsolatedExecutorError("olddefconfig tool record is invalid")
    kconfig = tools["kconfiglib"]
    if not isinstance(kconfig, dict) or set(kconfig) != {
            "root", "kconfiglib_sha256", "olddefconfig_sha256"}:
        raise IsolatedExecutorError("kconfiglib tool record is malformed")
    root = Path(kconfig["root"])
    if not root.is_absolute() or root.is_symlink() or not root.is_dir():
        raise IsolatedExecutorError("kconfiglib root record is invalid")
    for filename, digest in (("kconfiglib.py", kconfig["kconfiglib_sha256"]),
                             ("olddefconfig.py", kconfig["olddefconfig_sha256"])):
        path = root / filename
        if path.is_symlink() or not path.is_file() or _digest_file(path) != digest:
            raise IsolatedExecutorError(f"kconfiglib helper record is invalid: {filename}")
    make = tools["make"]
    if not isinstance(make, dict) or set(make) != {"path", "version"}:
        raise IsolatedExecutorError("make tool record is malformed")
    make_path = Path(make["path"])
    if (not make_path.is_absolute() or make_path.is_symlink() or
            not make_path.is_file() or not (make_path.stat().st_mode & 0o111) or
            not isinstance(make["version"], str) or not make["version"]):
        raise IsolatedExecutorError("make tool record is invalid")


class RuntimeCompileError(IsolatedExecutorError):
    """A runtime command or required output failed; the phase stays pending."""


class RuntimeDeliveryError(IsolatedExecutorError):
    """A signed/package delivery operation failed; no green delivery state."""


def _private_directory(path: Path, role_root: Path, field: str) -> Path:
    """Validate a real directory below a role root without following links."""
    path = _safe_path(path, field)
    role_root = _safe_path(role_root, "role root")
    _reject_traversal(path, field)
    if path == role_root or not _inside(path, role_root):
        raise RuntimeCompileError(f"{field} escaped role root")
    current = path
    while current != role_root:
        if current.is_symlink():
            raise RuntimeCompileError(f"{field} contains a symlink: {current}")
        current = current.parent
        if not _inside(current, role_root):
            raise RuntimeCompileError(f"{field} escaped role root")
    if path.is_symlink() or not path.is_dir():
        raise RuntimeCompileError(f"{field} is not a real directory: {path}")
    return path


def _runtime_command_copy(command: dict[str, Any], *, cmake_executable: str,
                          python_executable: str, make_executable: str) -> dict[str, Any]:
    copied = dict(command)
    copied["argv"] = list(command["argv"])
    copied["environment"] = dict(command["environment"])
    if copied["tool"] == "cmake":
        copied["argv"][0] = cmake_executable
        copied["argv"] = [
            (f"-DPython3_EXECUTABLE={python_executable}"
             if item.startswith("-DPython3_EXECUTABLE=") else item)
            for item in copied["argv"]
        ]
    elif copied["tool"] == "python3":
        copied["argv"][0] = python_executable
    elif copied["tool"] == "make":
        copied["argv"][0] = make_executable
    return copied


def _canonical_runtime_commands(
        plan: dict[str, Any], role: str, paths: dict[str, Path],
        repository: Path, source_view: dict[str, Any],
        resolved_policy: dict[str, Any], *, cmake_executable: str,
        python_executable: str, make_executable: str) -> list[dict[str, Any]]:
    """Rebuild the command contract from audited inputs, never from manifest argv."""
    seed = None
    if role in RUNTIME_ROLES:
        profile = plan["legacy_adapter"]["seed_profiles"][role]["target_profile"]
        seed = {"cmake_board_config": str(paths["root"] / "config" / profile)}
    commands = _commands(
        plan, role, paths, repository, source_view, seed,
        paths["bootloader_staging"] if role in BOOT_ROLES else None,
        resolved_policy=resolved_policy)
    return [
        _runtime_command_copy(
            command, cmake_executable=cmake_executable,
            python_executable=python_executable, make_executable=make_executable)
        for command in commands
    ]


def _assert_runtime_role_bindings(
        plan: dict[str, Any], value: dict[str, Any], role: str,
        row: dict[str, Any], canonical_paths: dict[str, Path]) -> None:
    """Bind every role-private path/seed field to the verified plan template."""
    paths = canonical_paths
    expected = {
        "build_root": paths["root"],
        "artifact_root": paths["artifact"],
        "config_seed_root": paths["root"] / "config",
        "partition_contract_root": paths["partition_contract"],
        "cmake_binary_root": paths["cmake_binary"],
    }
    for field, path in expected.items():
        if row[field] != str(path):
            raise RuntimeCompileError(f"{role} {field} is not plan-canonical")
    plan_role = plan["roles"][role]
    if row["backend"] != plan_role["backend"] or \
            row["config_identity_sha256"] != plan_role["config_identity_sha256"]:
        raise RuntimeCompileError(f"{role} role identity is not plan-canonical")
    if role in RUNTIME_ROLES:
        profile = plan["legacy_adapter"]["seed_profiles"][role]["target_profile"]
        expected_seed = paths["root"] / "config" / profile
        if (row["config_seed_profile"] != profile or
                row["cmake_board_config"] != str(expected_seed) or
                row["sdk_bundle"] != plan["sdk"]["versions"][role]):
            raise RuntimeCompileError(f"{role} runtime seed is not plan-canonical")
    elif row["config_seed_profile"] is not None or row["cmake_board_config"] is not None or \
            row["sdk_bundle"] is not None:
        raise RuntimeCompileError(f"{role} boot role carries unexpected seed metadata")
    if role in BOOT_ROLES and role in value["active_roles"]:
        if row["bootloader_staging_root"] != str(paths["bootloader_staging"]):
            raise RuntimeCompileError(f"{role} boot staging is not plan-canonical")
    elif row["bootloader_staging_root"] is not None:
        raise RuntimeCompileError(f"{role} inactive role carries boot staging")


def _runtime_command_paths(command: dict[str, Any], role: str,
                           role_root: Path, cmake_root: Path,
                           source_root: Path, python_executable: str,
                           safe_path: str, olddefconfig_path: Path,
                           kconfiglib_root: Path, make_executable: str | None = None,
                           cmake_executable: str | None = None,
                           plan: dict[str, Any] | None = None,
                           resolved_policy: dict[str, Any] | None = None) -> None:
    """Enforce the command allowlist and all source/output path boundaries."""
    stage = command.get("stage")
    argv = command.get("argv")
    if stage not in RUNTIME_COMMAND_STAGES + BOOT_COMMAND_STAGES or not isinstance(argv, list):
        raise RuntimeCompileError(f"runtime command stage is not allowlisted: {role}")
    if any(not isinstance(item, str) or not item for item in argv):
        raise RuntimeCompileError(f"runtime command argv is malformed: {role}")
    joined = " ".join(argv)
    forbidden_fragments = ("build_dual_image.sh", "nuttx_post_build", "postbuild",
                           "jlink", "openocd", "curl", "wget", "git")
    forbidden_commands = {"sign", "package", "pack", "flash", "imgtool", "openssl", "crc"}
    if (any(token in joined.lower() for token in forbidden_fragments) or
            any(item.lower() in forbidden_commands or
                Path(item).name.lower() in forbidden_commands for item in argv)):
        raise RuntimeCompileError(f"runtime command contains a forbidden operation: {role}")
    lowered_joined = joined.lower()
    if any(token in lowered_joined for token in
           ("make_bl1_manifest", "--manifest", "manifest-primary",
            "manifest-secondary", "signed-record", "signing-key")):
        raise RuntimeCompileError(f"runtime command contains a manifest/signing operation: {role}")
    if command.get("tool") == "make" and "all" in argv:
        raise RuntimeCompileError(f"compile-only command invokes make all: {role}")
    if command.get("cwd") != str(role_root):
        raise RuntimeCompileError(f"runtime command cwd escaped role root: {role}")
    environment = command.get("environment", {})
    if (environment.get("PATH") != safe_path or
            environment.get("PYTHONPATH") != str(kconfiglib_root) or
            shutil.which("olddefconfig", path=safe_path) != str(olddefconfig_path) or
            (make_executable is not None and
             shutil.which(Path(make_executable).name, path=safe_path) != str(make_executable))):
        raise RuntimeCompileError(f"runtime command tool environment changed: {role}")
    if stage == "partition-contract":
        if command.get("tool") != "python3" or argv[0] != python_executable or len(argv) < 2:
            raise RuntimeCompileError(f"partition command is not python-only: {role}")
        if not _inside(Path(argv[1]), source_root):
            raise RuntimeCompileError(f"partition generator escaped source snapshot: {role}")
        for item in argv[2:]:
            if item.startswith("/"):
                path = Path(item.split("=", 1)[-1])
                if not (_inside(path, source_root) or _inside(path, role_root)):
                    raise RuntimeCompileError(f"partition command path escaped role/source root: {role}")
    elif stage == "make-compile-only":
        if (command.get("tool") != "make" or argv[0] != make_executable or
                "compile-only" not in argv or "-C" not in argv):
            raise RuntimeCompileError(f"Make compile-only command is malformed: {role}")
        try:
            root = Path(argv[argv.index("-C") + 1])
        except (ValueError, IndexError):
            raise RuntimeCompileError(f"Make compile-only root is malformed: {role}")
        if not _inside(root, role_root) or root.is_symlink():
            raise RuntimeCompileError(f"Make compile-only root escaped role root: {role}")
        expected = role_root / BOOTLOADER_STAGING_DIRNAME / "bootloader" / (
            "bl2" if role == "bl2" else "")
        if root != expected:
            raise RuntimeCompileError(f"Make compile-only root is not role-bound: {role}")
        if plan is None or resolved_policy is None:
            raise RuntimeCompileError(f"Make compile-only policy binding is unavailable: {role}")
        assignment_keys = [item.split("=", 1)[0] for item in argv
                           if "=" in item and not item.startswith("-")]
        if len(assignment_keys) != len(set(assignment_keys)):
            raise RuntimeCompileError(
                f"Make compile-only variable keys are duplicated: {role}")
        expected_policy_args = _boot_make_variables(plan, resolved_policy, role)
        for item in expected_policy_args:
            if item not in argv:
                raise RuntimeCompileError(f"Make compile-only policy argument is missing: {role}/{item}")
        for key in {item.split("=", 1)[0] for item in expected_policy_args}:
            observed = [item for item in argv if item.startswith(key + "=")]
            expected_item = next(item for item in expected_policy_args
                                 if item.startswith(key + "="))
            if observed != [expected_item]:
                raise RuntimeCompileError(f"Make compile-only policy argument changed: {role}/{key}")
    elif stage == "cmake-configure":
        if (command.get("tool") != "cmake" or
                (cmake_executable is not None and argv[0] != cmake_executable) or
                "-S" not in argv or "-B" not in argv):
            raise RuntimeCompileError(f"cmake configure command is malformed: {role}")
        if argv[argv.index("-S") + 1] != str(source_root / "nuttx"):
            raise RuntimeCompileError(f"cmake configure source escaped snapshot: {role}")
        if argv[argv.index("-B") + 1] != str(cmake_root):
            raise RuntimeCompileError(f"cmake configure binary root changed: {role}")
        if "-G" not in argv or argv[argv.index("-G") + 1] != "Ninja":
            raise RuntimeCompileError(f"cmake configure must use Ninja: {role}")
        apps = f"-DNUTTX_APPS_DIR={source_root / 'apps'}"
        if apps not in argv:
            raise RuntimeCompileError(f"cmake configure apps root changed: {role}")
        python_binding = f"-DPython3_EXECUTABLE={python_executable}"
        if python_binding not in argv:
            raise RuntimeCompileError(f"cmake configure Python executable changed: {role}")
    else:
        if (command.get("tool") != "cmake" or argv[:2] != [argv[0], "--build"] or
                len(argv) != 5 or argv[2] != str(cmake_root) or
                argv[3] != "--target" or argv[4] not in {"nuttx", "nuttx-bin"}):
            raise RuntimeCompileError(f"cmake build target is not allowlisted: {role}")


def _invoke_runtime_command(command: dict[str, Any], role_root: Path, role: str,
                            index: int, *, runner: Callable[..., Any] | None) -> None:
    """Run one allowlisted command and persist its private log record."""
    _validate_command_environment(command["environment"], role)
    effective_env = {
        key: value for key, value in command["environment"].items()
        if key in SAFE_ENV_NAMES or key.startswith("BK7258_")
    }
    log_dir = role_root / "logs"
    if log_dir.exists() and (log_dir.is_symlink() or not log_dir.is_dir()):
        raise RuntimeCompileError(f"runtime log root is not private: {role}")
    log_dir.mkdir(exist_ok=True)
    log_path = log_dir / f"{index:02d}-{command['stage']}.log"
    if log_path.exists() or log_path.is_symlink():
        raise RuntimeCompileError(f"runtime log already exists: {log_path}")
    try:
        if runner is None:
            result = subprocess.run(
                command["argv"], cwd=str(role_root), env=effective_env,
                capture_output=True, text=True, check=False)
        else:
            result = runner(
                command["argv"], cwd=str(role_root), env=effective_env,
                capture_output=True, text=True, check=False)
    except Exception as error:
        output = f"executor error: {error}\n"
        returncode = -1
        log_path.write_text(output, encoding="utf-8")
        command["log"] = str(log_path)
        command["returncode"] = returncode
        command["status"] = "FAIL"
        raise RuntimeCompileError(f"runtime command failed to start: {role}/{command['stage']}") from error
    if isinstance(result, int):
        returncode = result
        stdout = ""
        stderr = ""
    else:
        returncode = getattr(result, "returncode", None)
        stdout = getattr(result, "stdout", "") or ""
        stderr = getattr(result, "stderr", "") or ""
    if not isinstance(returncode, int):
        raise RuntimeCompileError(f"runtime command returned no status: {role}/{command['stage']}")
    if not isinstance(stdout, str):
        stdout = stdout.decode(errors="replace")
    if not isinstance(stderr, str):
        stderr = stderr.decode(errors="replace")
    log_text = stdout
    if stderr:
        log_text += ("\n" if log_text and not log_text.endswith("\n") else "") + \
                    "--- stderr ---\n" + stderr
    try:
        log_path.write_text(log_text, encoding="utf-8")
    except OSError as error:
        raise RuntimeCompileError(f"cannot write runtime command log: {log_path}") from error
    command["log"] = str(log_path)
    command["returncode"] = returncode
    command["status"] = "PASS" if returncode == 0 else "FAIL"
    if returncode != 0:
        raise RuntimeCompileError(
            f"runtime command returned {returncode}: {role}/{command['stage']}")


def _runtime_candidates(cmake_root: Path, role_root: Path, name: str) -> list[Path]:
    """Find a CMake output by basename without following source links."""
    matches: list[Path] = []
    if not cmake_root.is_dir() or cmake_root.is_symlink():
        return matches
    for current, directories, files in os.walk(cmake_root, topdown=True, followlinks=False):
        current_path = Path(current)
        directories[:] = [item for item in directories
                          if not (current_path / item).is_symlink()]
        for filename in files:
            path = current_path / filename
            if filename != name or path.is_symlink() or not path.is_file():
                continue
            if _path_is_private_regular(path, role_root):
                matches.append(path)
    return matches


def _select_runtime_output(cmake_root: Path, role_root: Path, name: str) -> Path:
    candidates = _runtime_candidates(cmake_root, role_root, name)
    if not candidates:
        raise RuntimeCompileError(f"CMake did not produce required runtime output: {name}")
    # Prefer the conventional top-level binary/archive paths, then the
    # shortest deterministic path.  Ambiguous duplicate outputs are unsafe.
    candidates.sort(key=lambda path: (
        0 if path == cmake_root / name else
        1 if path.parent.name in {"arch", "boards"} else 2,
        len(path.parts), str(path),
    ))
    best = candidates[0]
    same_priority = [path for path in candidates if (
        (0 if path == cmake_root / name else
         1 if path.parent.name in {"arch", "boards"} else 2), len(path.parts)
    ) == ((0 if best == cmake_root / name else
           1 if best.parent.name in {"arch", "boards"} else 2), len(best.parts))]
    if len(same_priority) > 1:
        raise RuntimeCompileError(f"ambiguous CMake output path: {name}")
    return best


def _collect_runtime_artifacts(role_root: Path, cmake_root: Path,
                               artifact_root: Path, role: str) -> dict[str, Any]:
    source_names = {
        ".config": ".config", "nuttx": "nuttx", "nuttx.map": "nuttx.map",
        "nuttx.bin": "nuttx.bin",
        "arch/libarch.a": "libarch.a", "boards/libboard.a": "libboard.a",
    }
    selected = {
        logical: _select_runtime_output(cmake_root, role_root, basename)
        for logical, basename in source_names.items()
    }
    for logical in OPTIONAL_RUNTIME_ARTIFACT_NAMES:
        candidates = _runtime_candidates(cmake_root, role_root, logical)
        if candidates:
            selected[logical] = _select_runtime_output(cmake_root, role_root, logical)
    records: dict[str, Any] = {}
    for logical, source in selected.items():
        destination = artifact_root / logical
        if destination.exists() or destination.is_symlink():
            raise RuntimeCompileError(f"runtime artifact destination is not fresh: {destination}")
        destination.parent.mkdir(parents=True, exist_ok=True)
        try:
            if source.is_symlink() or not source.is_file():
                raise RuntimeCompileError(f"runtime artifact source is not regular: {source}")
            destination.write_bytes(source.read_bytes())
            os.chmod(destination, stat.S_IMODE(source.stat(follow_symlinks=False).st_mode) & 0o555 or 0o400)
        except OSError as error:
            raise RuntimeCompileError(f"cannot collect runtime artifact: {logical}") from error
        digest = _digest_file(destination)
        records[logical] = {
            "path": str(destination), "sha256": digest,
            "size": destination.stat(follow_symlinks=False).st_size,
        }
    _validate_runtime_artifacts(records, role_root, role)
    return records


def _delivery_record_path(path: Any, build_root: Path, field: str) -> Path:
    """Validate a persisted delivery path without following links."""
    if not isinstance(path, str) or not path or "\x00" in path:
        raise IsolatedExecutorError(f"delivery path is malformed: {field}")
    candidate = _manifest_path_inside_build_root(
        Path(path), build_root, f"delivery {field}", allow_existing=True)
    return candidate


def _redacted_delivery_argv(argv: Any, field: str) -> list[str]:
    """Validate command argv after private-key arguments have been redacted."""
    if not isinstance(argv, list) or any(
            not isinstance(item, str) or not item for item in argv):
        raise IsolatedExecutorError(f"delivery command argv is malformed: {field}")
    key_flags = {
        "--key", "--private-key", "--bl1-manifest-key",
        "--mcuboot-signing-key",
    }
    for index, item in enumerate(argv):
        if item in key_flags:
            if index + 1 >= len(argv) or argv[index + 1] != PRIVATE_KEY_TOKEN:
                raise IsolatedExecutorError(
                    f"delivery command private-key argument is not redacted: {field}")
    # A persisted delivery manifest may name public generated C sources, but it
    # must never contain a private-key filename/path.  Check each non-key
    # argument independently; a token elsewhere in the argv must not make an
    # unrelated ``/tmp/forged.pem`` argument acceptable.
    secret_markers = (".pem", ".der", ".p8", ".p12", "private_key", "signing_key")
    for index, item in enumerate(argv):
        if item == PRIVATE_KEY_TOKEN or (index and argv[index - 1] in key_flags):
            continue
        lowered = item.lower()
        if any(marker in lowered for marker in secret_markers):
            raise IsolatedExecutorError(
                f"delivery command contains private-key material: {field}")
    return list(argv)


def _expected_delivery_commands(value: dict[str, Any], delivery: dict[str, Any],
                                plan: dict[str, Any],
                                resolved_policy: dict[str, Any]) -> list[dict[str, Any]]:
    """Rebuild the terminal command contract from immutable build inputs.

    The persisted command list is evidence, not authority.  This function is
    intentionally independent of that list: a changed partition path,
    output, tool, version, or layout argument is rejected even if an attacker
    recomputes the command's local digest and the outer manifest identity.
    """
    root = Path(delivery["root"])
    source_root = Path(value["source_view"]["root"])
    source_repository = source_root / "contest2026_135_yongwangzhiqian"
    source_board = source_repository / "board/bk7258"
    source_tools = source_repository / "tools/bk7258"
    scripts = source_board / "scripts"
    bootloader = source_board / "bootloader"
    parameters = delivery["parameters"]
    python = parameters["python"]
    partition_relative = plan["partition_layout"]["source"]
    partition_csv = source_board / partition_relative.removeprefix("board/bk7258/")
    staged_partition = root / "inputs/partition-layout.csv"
    payload = root / "payload"
    manifests = root / "manifests"
    work = {role: root / "work" / role for role in RUNTIME_ROLES}
    bootloader_crc = payload / "bootloader_crc.bin"
    bl2_staged = payload / "bl2.bin"
    bl1_staged = payload / "bl1-raw.bin"
    primary_manifest = manifests / "bl1-manifest-primary.bin"
    secondary_manifest = manifests / "bl1-manifest-secondary.bin"
    pair_output = payload / "mcuboot-pair"
    trust = payload / "bk7258-trust-chain.json"
    dual_input = root / "dual-input"
    package_source = root / "package-source"
    package = root / "firmware.bkpack"
    layout = plan["partition_layout"]
    geometry = resolved_policy["bl2_geometry"]
    primary_xip = geometry["primary"]["xip_start"]
    secondary_xip = geometry["secondary"]["xip_start"]
    bl2_logical_size = plan["bl2_image_logical_size"]
    bl2_capacity = geometry["primary"]["logical_size"]
    manifest_format = resolved_policy["policy"]["manifest"]["format"]
    manifest_version = resolved_policy["policy"]["version_policy"][
        "bl1_manifest_version"]["default"]
    base_env = _delivery_environment(
        root, value["source_view"], parameters["safe_path"],
        layout_source=partition_csv, layout_id=layout["layout_id"],
        layout_sha256=layout["layout_sha256"])

    def make(stage: str, tool: str, argv: list[str], cwd: Path,
             updates: dict[str, str] | None = None,
             authorization: str = "none", index: int = 0) -> dict[str, Any]:
        env = dict(base_env)
        env.update(updates or {})
        redacted = _redacted_delivery_argv(argv, stage)
        return {
            "stage": stage, "tool": tool, "argv": redacted,
            "environment": env, "cwd": str(cwd), "status": "PASS",
            "log": str(root / "logs" /
                       f"{parameters['log_prefix']}{index:02d}-{stage}.log"),
            "returncode": 0, "authorization": authorization,
            "argv_sha256": _digest_bytes(_canonical(redacted)),
        }

    common_manifest_args = [
        "--format", manifest_format,
        "--bl2", str(bl2_staged), "--private-key", PRIVATE_KEY_TOKEN,
        "--generated-root-c", str(manifests / "boot_bl1_manifest_key.c"),
        "--partition-csv", str(partition_csv),
        "--expect-layout-id", layout["layout_id"],
        "--expect-layout-sha256", layout["layout_sha256"],
        "--bl2-slot", "primary", "--bl2-xip", hex(primary_xip),
        "--bl2-size", hex(bl2_logical_size), "--bl2-capacity", hex(bl2_capacity),
        "--bl2-load", hex(0x28020000), "--manifest-version", str(manifest_version),
    ]
    secondary_args = list(common_manifest_args)
    secondary_args[secondary_args.index("--bl2-slot") + 1] = "secondary"
    secondary_args[secondary_args.index("--bl2-xip") + 1] = hex(secondary_xip)
    commands = [
        make("bl1-manifest-primary", "python3", [
            python, str(bootloader / "make_bl1_manifest.py"),
            *common_manifest_args, "--out", str(primary_manifest)],
            manifests, {"BK7258_LAYOUT_ID": layout["layout_id"]}, "sign", 0),
        make("bl1-manifest-secondary", "python3", [
            python, str(bootloader / "make_bl1_manifest.py"),
            *secondary_args, "--out", str(secondary_manifest)],
            manifests, {"BK7258_LAYOUT_ID": layout["layout_id"]}, "sign", 1),
        make("bl1-pack", "python3", [
            python, str(bootloader / "bk7236_pack_min_bootloader.py"),
            "--in", str(bl1_staged), "--out", str(bootloader_crc),
            "--manifest-primary", str(primary_manifest),
            "--manifest-secondary", str(secondary_manifest)],
            payload, {
                "BK7258_PARTITION_LAYOUT_ID": layout["layout_id"],
                "BK7258_PARTITION_LAYOUT_SHA256": layout["layout_sha256"],
            }, "none", 2),
        make("bl2-pack", "python3", [
            python, str(scripts / "bk7258_crc_expand.py"), "--in", str(bl2_staged),
            "--out", str(payload / "bl2_crc.bin"), "--xip-base", hex(primary_xip),
            "--execution-base", hex(0x28020000), "--max-size", hex(bl2_capacity),
            "--pad-size", hex(bl2_logical_size)], payload, {
                "BK7258_PARTITION_LAYOUT_ID": layout["layout_id"],
                "BK7258_PARTITION_LAYOUT_SHA256": layout["layout_sha256"],
            }, "none", 3),
    ]
    for role, index in (("cp", 4), ("ap", 5)):
        updates = {
            "BK7258_POSTBUILD_MODE": "isolated",
            "BK7258_POSTBUILD_ARTIFACT_ROOT": str(payload),
            "BK7258_POSTBUILD_DUAL_ROLE": "1",
            "BK7258_PARTITION_CONTRACT_ROOT": str(work[role] / "partition-contract"),
            "BK7258_PARTITION_LAYOUT_SOURCE": str(partition_csv),
            "BK7258_PARTITION_LAYOUT_ID": layout["layout_id"],
            "BK7258_PARTITION_LAYOUT_SHA256": layout["layout_sha256"],
            "BK7258_BL1_CRC_BIN": str(bootloader_crc),
            "BK7258_ROLE": role,
        }
        commands.append(make(f"postbuild-{role}", "bash", [
            parameters["bash"], str(scripts / "postbuild.sh"), str(work[role]),
            str(source_board), role], work[role], updates, "none", index))
    commands.extend([
        make("mcuboot-pair-sign", "python3", [
            python, str(source_tools / "pack_bk7258_mcuboot_pair.py"),
            "--partition", str(partition_csv), "--expect-layout-id", layout["layout_id"],
            "--expect-layout-sha256", layout["layout_sha256"],
            "--cp-raw", str(payload / "app.bin"), "--ap-raw", str(payload / "app1.bin"),
            "--key", PRIVATE_KEY_TOKEN, "--output", str(pair_output),
            "--version", parameters["version"], "--security-counter",
            parameters["security_counter"], "--imgtool",
            str(source_root / "apps/boot/mcuboot/mcuboot/scripts/imgtool.py")],
            payload, {
                "BK7258_PARTITION_LAYOUT_ID": layout["layout_id"],
                "BK7258_PARTITION_LAYOUT_SHA256": layout["layout_sha256"],
            }, "sign", 6),
        make("trust-chain-emit", "python3", [
            python, str(source_tools / "bk7258_trust_chain.py"), "emit",
            "--bl1-manifest-key", PRIVATE_KEY_TOKEN,
            "--mcuboot-signing-key", PRIVATE_KEY_TOKEN,
            "--bootloader-elf", str(value["roles"]["bl1"]["artifacts"]["bl.elf"]["path"]),
            "--bootloader-bin", str(payload / "bootloader.bin"),
            "--bl2-elf", str(value["roles"]["bl2"]["artifacts"]["bl2.elf"]["path"]),
            "--bl2-bin", str(bl2_staged), "--boot-xip-base", hex(0x02000000),
            "--bl2-load-base", hex(0x28020000),
            "--bl2-primary-xip-base", hex(primary_xip),
            "--output", str(trust)], payload,
            {"BK7258_PARTITION_LAYOUT_ID": layout["layout_id"]}, "sign", 7),
        make("dual-package", "python3", [
            python, str(source_tools / "pack_dual_image.py"), "--boot", str(bootloader_crc),
            "--cp-raw", str(dual_input / "app.bin"),
            "--cp-standard", str(dual_input / "cp-raw.bin"),
            "--cp-crc", str(dual_input / "app_crc.bin"),
            "--ap-raw", str(dual_input / "app1.bin"),
            "--ap-standard", str(dual_input / "ap-raw.bin"),
            "--ap-crc", str(dual_input / "app1_crc.bin"),
            "--bl2-primary-crc", str(dual_input / "bl2_crc.bin"),
            "--bl2-secondary-crc", str(dual_input / "bl2_secondary_crc.bin"),
            "--trust-chain", str(dual_input / "bk7258-trust-chain.json"),
            "--partition", str(partition_csv), "--expect-layout-id", layout["layout_id"],
            "--expect-layout-sha256", layout["layout_sha256"], "--output", str(package_source)],
            package_source, {
                "BK7258_PARTITION_LAYOUT_ID": layout["layout_id"],
                "BK7258_PARTITION_LAYOUT_SHA256": layout["layout_sha256"],
            }, "package", 8),
        make("bkpack-create", "python3", [
            python, str(source_tools / "bk7258_bkpack.py"), "create",
            "--source", str(package_source), "--partition", str(partition_csv),
            "--output", str(package)], package_source, {
                "BK7258_PARTITION_LAYOUT_ID": layout["layout_id"],
                "BK7258_PARTITION_LAYOUT_SHA256": layout["layout_sha256"],
            }, "package", 9),
        make("bkpack-verify", "python3", [
            python, str(source_tools / "bk7258_bkpack.py"), "verify", "--package", str(package)],
            root, {
                "BK7258_PARTITION_LAYOUT_ID": layout["layout_id"],
                "BK7258_PARTITION_LAYOUT_SHA256": layout["layout_sha256"],
            }, "package", 10),
    ])
    return commands


def _assert_delivery_command_canonical(observed: dict[str, Any],
                                       expected: dict[str, Any],
                                       build_root: Path) -> None:
    """Reject any persisted command field that differs from its rebuild."""
    stage = expected["stage"]
    for field in ("stage", "tool", "argv", "environment", "cwd", "status",
                  "returncode", "authorization", "argv_sha256"):
        if observed[field] != expected[field]:
            raise IsolatedExecutorError(
                f"delivery command is not canonical: {stage}/{field}")
    expected_log = _delivery_record_path(expected["log"], build_root,
                                         f"expected command log {stage}")
    observed_log = _delivery_record_path(observed["log"], build_root,
                                         f"command log {stage}")
    if observed_log != expected_log:
        raise IsolatedExecutorError(f"delivery command log is not canonical: {stage}")


def _validate_delivery_plan(value: dict[str, Any], build_root: Path) -> None:
    """Validate the keyless postbuild hand-off.

    This phase is deliberately not a package: it only records the exact
    unsigned BL1/BL2 CRC and CP/AP postbuild outputs.  Signing/package
    authorization remains NOT_RUN until ``deliver`` is called explicitly.
    """
    plan = value.get("delivery_plan")
    if not isinstance(plan, dict) or set(plan) != {
            "schema", "version", "root", "plan_identity_sha256",
            "snapshot_identity_sha256", "layout", "authorization",
            "commands", "tools", "artifacts"}:
        raise IsolatedExecutorError("delivery plan is missing or has unknown fields")
    if plan["schema"] != "bk7258.role-isolated-delivery-plan/1" or \
            plan["version"] != DELIVERY_VERSION:
        raise IsolatedExecutorError("unsupported isolated delivery plan schema")
    root = _delivery_record_path(plan["root"], build_root, "delivery plan root")
    if root == build_root or not root.is_dir() or root.is_symlink():
        raise IsolatedExecutorError("delivery plan root is not private")
    framework.digest(plan["plan_identity_sha256"], "delivery plan identity")
    framework.digest(plan["snapshot_identity_sha256"], "delivery plan snapshot identity")
    if (plan["plan_identity_sha256"] != value["build_plan_identity_sha256"] or
            plan["snapshot_identity_sha256"] !=
            value["source_view"]["snapshot_identity_sha256"]):
        raise IsolatedExecutorError("delivery plan identity is not bound")
    if plan["layout"] != value["identity_inputs"]["partition_layout"]:
        raise IsolatedExecutorError("delivery plan layout is not bound")
    if plan["authorization"] != {"sign": "NOT_RUN", "package": "NOT_RUN"}:
        raise IsolatedExecutorError("keyless delivery plan claims authorization")
    commands = plan["commands"]
    if not isinstance(commands, list) or tuple(
            item.get("stage") for item in commands if isinstance(item, dict)
    ) != DELIVERY_PREPARED_COMMAND_STAGES:
        raise IsolatedExecutorError("delivery plan command order is unsafe")
    for index, command in enumerate(commands):
        if not isinstance(command, dict) or set(command) != {
                "stage", "tool", "argv", "environment", "cwd", "status",
                "log", "returncode", "authorization", "argv_sha256"}:
            raise IsolatedExecutorError(f"delivery plan command is malformed: {index}")
        if command["status"] != "PASS" or command["returncode"] != 0 or \
                command["authorization"] != "none":
            raise IsolatedExecutorError(
                f"delivery plan command is not keyless PASS: {command.get('stage')}")
        argv = _redacted_delivery_argv(command["argv"], command["stage"])
        framework.digest(command["argv_sha256"],
                         f"delivery plan argv {command['stage']}")
        if _digest_bytes(_canonical(argv)) != command["argv_sha256"]:
            raise IsolatedExecutorError(
                f"delivery plan argv digest changed: {command['stage']}")
        _validate_command_environment(command["environment"], "delivery-plan")
        cwd = _delivery_record_path(command["cwd"], build_root,
                                    f"delivery plan cwd {command['stage']}")
        log = _delivery_record_path(command["log"], build_root,
                                    f"delivery plan log {command['stage']}")
        if not cwd.is_dir() or not log.is_file():
            raise IsolatedExecutorError(
                f"delivery plan execution path is missing: {command['stage']}")
        if command["tool"] not in {"python3", "bash"}:
            raise IsolatedExecutorError(
                f"delivery plan tool is not allowlisted: {command['stage']}")
    tools = plan["tools"]
    if not isinstance(tools, dict) or not tools:
        raise IsolatedExecutorError("delivery plan tool records are missing")
    for name, record in tools.items():
        if not isinstance(name, str) or not isinstance(record, dict) or \
                set(record) != {"path", "sha256"}:
            raise IsolatedExecutorError(f"delivery plan tool record is malformed: {name}")
        path = _delivery_record_path(record["path"], build_root,
                                     f"delivery plan tool {name}")
        if not path.is_file() or path.is_symlink() or _digest_file(path) != record["sha256"]:
            raise IsolatedExecutorError(f"delivery plan tool changed: {name}")
    artifacts = plan["artifacts"]
    if not isinstance(artifacts, dict):
        raise IsolatedExecutorError("delivery plan artifact index is malformed")
    required_standard = {
        "payload/vela_nuttx_cp.bin", "payload/vela_nuttx_ap.bin",
        "payload/vela_nuttx_manifest.json",
    }
    if not required_standard.issubset(artifacts):
        raise IsolatedExecutorError("delivery plan OpenVela aliases are incomplete")
    for name, record in artifacts.items():
        if not isinstance(name, str) or not isinstance(record, dict) or \
                set(record) != {"path", "sha256", "size"}:
            raise IsolatedExecutorError(f"delivery plan artifact is malformed: {name}")
        path = _delivery_record_path(record["path"], build_root,
                                     f"delivery plan artifact {name}")
        if not path.is_file() or path.is_symlink() or _digest_file(path) != record["sha256"] or \
                path.stat(follow_symlinks=False).st_size != record["size"]:
            raise IsolatedExecutorError(f"delivery plan artifact changed: {name}")


def _validate_delivery_record(value: dict[str, Any], build_root: Path) -> None:
    """Validate the terminal delivery index and its package digest.

    The record intentionally carries only redacted command argv, tool hashes,
    public trust fingerprints embedded in the existing trust-chain contract,
    and output digests.  It does not carry a private-key path or key bytes.
    """
    delivery = value.get("delivery")
    if not isinstance(delivery, dict) or set(delivery) != {
            "schema", "version", "root", "package", "package_sha256",
            "package_size", "plan_identity_sha256", "snapshot_identity_sha256",
            "layout", "authorization", "stale_unsigned_intermediates_removed",
            "parameters", "commands", "tools", "artifacts"}:
        raise IsolatedExecutorError("delivery record is missing or has unknown fields")
    if delivery["schema"] != "bk7258.role-isolated-delivery/1" or \
            delivery["version"] != DELIVERY_VERSION:
        raise IsolatedExecutorError("unsupported isolated delivery schema")
    root = _delivery_record_path(delivery["root"], build_root, "root")
    if root == build_root or not root.is_dir() or root.is_symlink():
        raise IsolatedExecutorError("delivery root is not a private directory")
    package = _delivery_record_path(delivery["package"], build_root, "package")
    if package.parent != root or package.name != "firmware.bkpack" or not package.is_file():
        raise IsolatedExecutorError("delivery package is not the canonical firmware.bkpack")
    framework.digest(delivery["package_sha256"], "delivery package SHA-256")
    if (not isinstance(delivery["package_size"], int) or
            isinstance(delivery["package_size"], bool) or
            delivery["package_size"] <= 0 or
            delivery["package_size"] != package.stat(follow_symlinks=False).st_size or
            _digest_file(package) != delivery["package_sha256"]):
        raise IsolatedExecutorError("delivery package hash/size changed")
    framework.digest(delivery["plan_identity_sha256"], "delivery plan identity")
    framework.digest(delivery["snapshot_identity_sha256"], "delivery snapshot identity")
    if (delivery["plan_identity_sha256"] != value["build_plan_identity_sha256"] or
            delivery["snapshot_identity_sha256"] !=
            value["source_view"]["snapshot_identity_sha256"]):
        raise IsolatedExecutorError("delivery identity is not bound to runtime manifest")
    layout = delivery["layout"]
    if not isinstance(layout, dict) or set(layout) != {
            "layout_id", "layout_sha256", "source"}:
        raise IsolatedExecutorError("delivery layout record is malformed")
    framework.identifier(layout["layout_id"], "delivery layout ID")
    framework.digest(layout["layout_sha256"], "delivery layout SHA-256")
    expected_layout = value["identity_inputs"]["partition_layout"]
    if layout != expected_layout:
        raise IsolatedExecutorError("delivery layout identity differs from runtime plan")
    if delivery["authorization"] != {"sign": "PASS", "package": "PASS"}:
        raise IsolatedExecutorError("delivery authorization record is unsafe")
    if delivery["stale_unsigned_intermediates_removed"] != [
            "all-app.bin", "nuttx_crc.bin"]:
        raise IsolatedExecutorError(
            "delivery retains ambiguous unsigned postbuild intermediates")
    parameters = delivery["parameters"]
    if not isinstance(parameters, dict) or set(parameters) != {
            "version", "security_counter", "python", "bash", "safe_path",
            "log_prefix"}:
        raise IsolatedExecutorError("delivery execution parameters are malformed")
    if (not isinstance(parameters["version"], str) or not parameters["version"] or
            any(char in parameters["version"] for char in "\x00\r\n") or
            not isinstance(parameters["security_counter"], str) or
            not parameters["security_counter"] or
            any(char in parameters["security_counter"] for char in "\x00\r\n")):
        raise IsolatedExecutorError("delivery version/counter parameters are malformed")
    for name in ("python", "bash"):
        path = Path(parameters[name])
        if not path.is_absolute() or path.is_symlink() or not path.is_file() or \
                not (path.stat(follow_symlinks=False).st_mode & 0o111):
            raise IsolatedExecutorError(f"delivery {name} executable is unsafe")
    if not isinstance(parameters["safe_path"], str) or not parameters["safe_path"]:
        raise IsolatedExecutorError("delivery safe PATH is malformed")
    if parameters["log_prefix"] not in {"", "final-"}:
        raise IsolatedExecutorError("delivery log prefix is not canonical")
    try:
        plan = _audit_plan_copy(value)
        snapshot_repository = Path(value["source_view"]["root"])
        snapshot_repository = snapshot_repository / "contest2026_135_yongwangzhiqian"
        resolved_policy = _resolve_policy_for_plan(snapshot_repository, plan)
        expected_commands = _expected_delivery_commands(
            value, delivery, plan, resolved_policy)
    except (IsolatedExecutorError, framework.FrameworkError, OSError, ValueError, KeyError) as error:
        raise IsolatedExecutorError("cannot reconstruct canonical delivery commands") from error
    commands = delivery["commands"]
    if not isinstance(commands, list) or tuple(
            command.get("stage") for command in commands
            if isinstance(command, dict)) != DELIVERY_COMMAND_STAGES:
        raise IsolatedExecutorError("delivery command order is unsafe")
    for index, command in enumerate(commands):
        if not isinstance(command, dict) or set(command) != {
                "stage", "tool", "argv", "environment", "cwd", "status",
                "log", "returncode", "authorization", "argv_sha256"}:
            raise IsolatedExecutorError(f"delivery command record is malformed: {index}")
        stage = command["stage"]
        if stage not in DELIVERY_COMMAND_STAGES or command["status"] != "PASS" or \
                command["returncode"] != 0 or command["authorization"] not in {
                    "none", "sign", "package"}:
            raise IsolatedExecutorError(f"delivery command result is unsafe: {stage}")
        argv = _redacted_delivery_argv(command["argv"], stage)
        framework.digest(command["argv_sha256"], f"delivery argv {stage}")
        if _digest_bytes(_canonical(argv)) != command["argv_sha256"]:
            raise IsolatedExecutorError(f"delivery argv digest changed: {stage}")
        _validate_command_environment(command["environment"], "delivery")
        cwd = _delivery_record_path(command["cwd"], build_root, f"command cwd {stage}")
        if not cwd.is_dir():
            raise IsolatedExecutorError(f"delivery command cwd is not a directory: {stage}")
        log = _delivery_record_path(command["log"], build_root, f"command log {stage}")
        if not log.is_file():
            raise IsolatedExecutorError(f"delivery command log is not a file: {stage}")
        if command["tool"] not in {"python3", "bash"}:
            raise IsolatedExecutorError(f"delivery command tool is not allowlisted: {stage}")
        _assert_delivery_command_canonical(
            command, expected_commands[index], build_root)
    tools = delivery["tools"]
    if not isinstance(tools, dict) or not tools:
        raise IsolatedExecutorError("delivery tool records are missing")
    for name, record in tools.items():
        if not isinstance(name, str) or not isinstance(record, dict) or \
                set(record) != {"path", "sha256"}:
            raise IsolatedExecutorError(f"delivery tool record is malformed: {name}")
        path = _delivery_record_path(record["path"], build_root, f"tool {name}")
        if not path.is_file() or path.is_symlink() or _digest_file(path) != record["sha256"]:
            raise IsolatedExecutorError(f"delivery tool record changed: {name}")
        framework.digest(record["sha256"], f"delivery tool {name}")
    artifacts = delivery["artifacts"]
    required_standard = {
        "payload/vela_nuttx_cp.bin", "payload/vela_nuttx_ap.bin",
        "payload/vela_nuttx_manifest.json",
    }
    if (not isinstance(artifacts, dict) or "firmware.bkpack" not in artifacts or
            not required_standard.issubset(artifacts)):
        raise IsolatedExecutorError("delivery artifact index is incomplete")
    for name, record in artifacts.items():
        if not isinstance(name, str) or not isinstance(record, dict) or \
                set(record) != {"path", "sha256", "size"}:
            raise IsolatedExecutorError(f"delivery artifact record is malformed: {name}")
        path = _delivery_record_path(record["path"], build_root, f"artifact {name}")
        if not path.is_file() or path.is_symlink():
            raise IsolatedExecutorError(f"delivery artifact is not a regular file: {name}")
        framework.digest(record["sha256"], f"delivery artifact {name}")
        if (record["size"] != path.stat(follow_symlinks=False).st_size or
                _digest_file(path) != record["sha256"]):
            raise IsolatedExecutorError(f"delivery artifact hash/size changed: {name}")


def _validate_manifest(value: dict[str, Any]) -> dict[str, Any]:
    required = {
        "schema", "kind", "version", "phase", "execution_mode",
        "product", "board", "mode", "boot",
        "active_roles",
        "build_plan_identity_sha256", "build_root", "plan_copy", "plan_copy_sha256",
        "source_view", "roles",
        "identity_inputs", "boot_policy", "side_effects", "credentials", "tools",
        "output", "identity_sha256",
    }
    allowed = required | {"delivery", "delivery_plan"}
    if set(value) - allowed or required - set(value):
        raise IsolatedExecutorError("isolated prepare manifest keys are not exact")
    if value["schema"] != SCHEMA or value["kind"] != KIND or value["version"] != VERSION:
        raise IsolatedExecutorError("unsupported isolated prepare manifest")
    if value["phase"] not in {
            "prepare", "materialized", "runtime-built", "delivery-prepared",
            "delivery-built"}:
        raise IsolatedExecutorError("isolated manifest phase is unsafe")
    expected_execution_mode = {
        "prepare": {"prepare-only"},
        "materialized": {"materialize-sources-only"},
        "runtime-built": {"compile-runtime", "build-runtime"},
        "delivery-prepared": {"prepare-delivery", "postbuild-runtime"},
        "delivery-built": {"deliver", "delivery"},
    }[value["phase"]]
    if value["execution_mode"] not in expected_execution_mode:
        raise IsolatedExecutorError("isolated manifest execution mode is unsafe")
    for field in ("product", "board", "mode"):
        framework.identifier(value[field], f"prepare manifest {field}")
    if value["boot"] not in framework.BOOTS:
        raise IsolatedExecutorError("prepare manifest boot is invalid")
    if not isinstance(value["active_roles"], list) or \
            value["active_roles"] != list(framework.ACTIVE_ROLES_BY_BOOT[value["boot"]]):
        raise IsolatedExecutorError("isolated active_roles are not bound to boot")
    policy_record = value["boot_policy"]
    if not isinstance(policy_record, dict) or set(policy_record) != {
            "status", "execution", "policy_id", "policy_identity_sha256",
            "resolved_policy_identity_sha256", "plan_identity_sha256", "active_roles",
            "private_key_read", "signing"}:
        raise IsolatedExecutorError("isolated boot policy record is malformed")
    if value["phase"] == "delivery-built":
        expected_policy_state = (
            "RECONCILED", "SIGNED_DELIVERY", "PASS", "PASS")
    else:
        expected_policy_state = (
            "RECONCILED", "COMPILE_ONLY", "NOT_RUN", "NOT_RUN")
    if (policy_record["status"], policy_record["execution"],
            policy_record["private_key_read"], policy_record["signing"]) != expected_policy_state:
        raise IsolatedExecutorError("isolated boot policy terminal state is unsafe")
    for field in ("policy_identity_sha256", "resolved_policy_identity_sha256",
                  "plan_identity_sha256"):
        framework.digest(policy_record[field], f"boot policy {field}")
    if policy_record["plan_identity_sha256"] != value["build_plan_identity_sha256"] or \
            policy_record["active_roles"] != value["active_roles"]:
        raise IsolatedExecutorError("boot policy record is not bound to manifest plan")
    framework.identifier(policy_record["policy_id"], "boot policy policy_id")
    framework.digest(value["build_plan_identity_sha256"], "prepare plan identity")
    for field in ("build_root", "plan_copy", "output"):
        if not isinstance(value[field], str) or not value[field] or "\x00" in value[field]:
            raise IsolatedExecutorError(f"prepare manifest path is malformed: {field}")
        path_value = Path(value[field])
        _reject_traversal(path_value, f"prepare manifest {field}")
        if not path_value.is_absolute():
            raise IsolatedExecutorError(f"prepare manifest path is not absolute: {field}")
    framework.digest(value["plan_copy_sha256"], "prepare plan copy")
    identity = value["identity_inputs"]
    if not isinstance(identity, dict) or set(identity) != {
            "family", "product", "board", "mode", "boot", "bl2_image_logical_size", "active_roles",
            "partition_layout", "sdk"}:
        raise IsolatedExecutorError("prepare identity inputs are malformed")
    if identity["active_roles"] != value["active_roles"]:
        raise IsolatedExecutorError("prepare identity active_roles mismatch")
    if identity["bl2_image_logical_size"] != framework.BL2_IMAGE_LOGICAL_SIZE_BY_BOOT[
            value["boot"]]:
        raise IsolatedExecutorError("prepare identity BL2 image logical size is unsafe")
    partition = identity["partition_layout"]
    if not isinstance(partition, dict) or set(partition) != {"layout_id", "layout_sha256", "source"}:
        raise IsolatedExecutorError("prepare partition identity is malformed")
    framework._partition_layout_reference(partition, "prepare partition")
    if not isinstance(value["roles"], dict) or set(value["roles"]) != set(ROLES):
        raise IsolatedExecutorError("prepare roles are incomplete")
    source = value["source_view"]
    if not isinstance(source, dict) or set(source) != {
            "policy", "phase", "root", "materialized", "snapshot_manifest",
            "snapshot_manifest_sha256", "snapshot_identity_sha256",
            "workspace_root", "workspace_head", "repository_head", "board_source_digest",
            "shared_nuttx_config_detected", "shared_nuttx_config_excluded",
            "shared_source_root_used", "roles"}:
        raise IsolatedExecutorError("prepare source-view declaration is malformed")
    if source["policy"] != "required-entity-snapshot-no-command-execution":
        raise IsolatedExecutorError("prepare source-view policy is unsafe")
    requires_materialized = value["phase"] in {
        "materialized", "runtime-built", "delivery-prepared", "delivery-built"}
    if source["phase"] != value["phase"] or source["materialized"] != requires_materialized:
        raise IsolatedExecutorError("isolated source-view phase is inconsistent")
    for field in ("root", "workspace_root"):
        if not isinstance(source[field], str) or not source[field] or "\x00" in source[field]:
            raise IsolatedExecutorError(f"prepare source-view path is malformed: {field}")
        source_path = Path(source[field])
        _reject_traversal(source_path, f"prepare source-view {field}")
        if not source_path.is_absolute():
            raise IsolatedExecutorError(f"prepare source-view path is not absolute: {field}")
        if source_path.is_symlink():
            raise IsolatedExecutorError(f"prepare source-view path contains a symlink: {field}")
    if source["snapshot_manifest"] is not None and (
            not isinstance(source["snapshot_manifest"], str) or
            not source["snapshot_manifest"]):
        raise IsolatedExecutorError("prepare source-view manifest path is malformed")
    if source["snapshot_manifest"] is not None:
        snapshot_manifest_path = Path(source["snapshot_manifest"])
        _reject_traversal(snapshot_manifest_path, "prepare source-view snapshot_manifest")
        if not snapshot_manifest_path.is_absolute():
            raise IsolatedExecutorError("prepare source-view manifest path is not absolute")
    for field in ("snapshot_manifest_sha256", "snapshot_identity_sha256"):
        if source[field] is not None:
            framework.digest(source[field], f"prepare source {field}")
    if value["phase"] == "prepare" and any(
            source[field] is not None
            for field in ("snapshot_manifest", "snapshot_manifest_sha256",
                          "snapshot_identity_sha256")):
        raise IsolatedExecutorError("prepare source-view claims a materialized manifest")
    if value["phase"] in {
            "materialized", "runtime-built", "delivery-prepared", "delivery-built"} and any(
            source[field] is None
            for field in ("snapshot_manifest", "snapshot_manifest_sha256",
                          "snapshot_identity_sha256")):
        raise IsolatedExecutorError("materialized source-view lacks manifest identity")
    framework.digest(source["board_source_digest"], "prepare board source digest")
    if source["shared_source_root_used"] is not False or source["shared_nuttx_config_excluded"] is not True:
        raise IsolatedExecutorError("prepare source-view claims shared source use")
    if not isinstance(source["shared_nuttx_config_detected"], bool):
        raise IsolatedExecutorError("prepare shared config detection is malformed")
    if not isinstance(source["roles"], dict) or set(source["roles"]) != set(ROLES):
        raise IsolatedExecutorError("prepare source-view roles are incomplete")
    build_root_path = Path(value["build_root"])
    workspace_path = Path(source["workspace_root"])
    _reject_existing_symlink_components(build_root_path, "prepare build_root")
    _reject_existing_symlink_components(workspace_path, "prepare workspace_root")
    if (build_root_path == workspace_path or
            _inside(build_root_path, workspace_path) or
            _inside(workspace_path, build_root_path)):
        raise IsolatedExecutorError(
            "prepare build root and workspace root must be disjoint")
    source_root_path = Path(source["root"])
    if source_root_path != build_root_path / SOURCE_SNAPSHOT_DIRNAME:
        raise IsolatedExecutorError("prepare source root is not the entity build-root sibling")
    _reject_existing_symlink_components(
        source_root_path, "prepare source root", build_root_path)
    canonical_snapshot_nuttx = source_root_path / "nuttx"
    canonical_snapshot_board = (
        source_root_path / "contest2026_135_yongwangzhiqian/board/bk7258")
    canonical_shared_nuttx = workspace_path / "nuttx"
    canonical_shared_config = canonical_shared_nuttx / ".config"
    partition_source = partition["source"]
    partition_prefix = "board/bk7258/"
    if not partition_source.startswith(partition_prefix):
        raise IsolatedExecutorError("prepare partition source is not board-local")
    canonical_partition_csv = (
        canonical_snapshot_board / partition_source.removeprefix(partition_prefix))
    canonical_partition_generator = (
        canonical_snapshot_board / "scripts/gen_bk7258_partitions.py")
    if value["phase"] in {
            "materialized", "runtime-built", "delivery-prepared", "delivery-built"}:
        manifest_path = Path(source["snapshot_manifest"])
        if manifest_path != source_root_path / SNAPSHOT_MANIFEST_FILENAME:
            raise IsolatedExecutorError("materialized source manifest path is not canonical")
        if _digest_file(manifest_path) != source["snapshot_manifest_sha256"]:
            raise IsolatedExecutorError("materialized source manifest hash changed")
    marker_path = source_root_path / SNAPSHOT_REQUIREMENTS_FILENAME
    if marker_path.is_symlink() or not marker_path.is_file():
        raise IsolatedExecutorError("source requirements marker is not a regular file")
    try:
        marker_value = json.loads(marker_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise IsolatedExecutorError("source requirements marker is unreadable") from error
    if not isinstance(marker_value, dict):
        raise IsolatedExecutorError("source requirements marker is malformed")
    if value["phase"] == "prepare" and marker_value.get("materialized") is not False:
        raise IsolatedExecutorError("prepare source requirements marker is not pending")
    if value["phase"] in {
            "materialized", "runtime-built", "delivery-prepared", "delivery-built"} and (
            marker_value.get("materialized") is not True or
            marker_value.get("snapshot_identity_sha256") != source["snapshot_identity_sha256"]):
        raise IsolatedExecutorError("materialized source requirements marker is not bound")
    roots: set[str] = set()
    private_paths: set[str] = set()
    source_roots: set[str] = set()
    for role in ROLES:
        row = value["roles"][role]
        needed = {"backend", "build_root", "artifact_root", "config_path", "config_seed_root",
                  "config_seed_profile", "cmake_board_config", "partition_contract_root",
                  "cmake_binary_root", "bootloader_staging_root", "source_view",
                  "config_identity_sha256", "sdk_bundle", "commands", "artifacts",
                  "activation", "applicability"}
        if not isinstance(row, dict) or set(row) != needed:
            raise IsolatedExecutorError(f"prepare role fields are malformed: {role}")
        role_active = role in value["active_roles"]
        if row["activation"] != ("active" if role_active else "inactive") or \
                row["applicability"] != ("required" if role_active else "not-applicable"):
            raise IsolatedExecutorError(f"prepare role activation/applicability is unsafe: {role}")
        for field in ("build_root", "artifact_root", "config_path", "config_seed_root",
                      "partition_contract_root", "cmake_binary_root"):
            if not isinstance(row[field], str) or not row[field]:
                raise IsolatedExecutorError(f"prepare role path is malformed: {role}/{field}")
            role_path = Path(row[field])
            _reject_traversal(role_path, f"prepare role {role}/{field}")
            if not role_path.is_absolute():
                raise IsolatedExecutorError(
                    f"prepare role path is not absolute: {role}/{field}")
            _reject_existing_symlink_components(
                role_path, f"prepare role {role}/{field}", build_root_path)
        if role in BOOT_ROLES and role_active:
            if (not isinstance(row["bootloader_staging_root"], str) or
                    not row["bootloader_staging_root"]):
                raise IsolatedExecutorError(f"prepare boot staging path is malformed: {role}")
            staging_path = Path(row["bootloader_staging_root"])
            _reject_traversal(staging_path, f"prepare role {role}/bootloader_staging_root")
            if not staging_path.is_absolute():
                raise IsolatedExecutorError(
                    f"prepare boot staging path is not absolute: {role}")
        elif role in BOOT_ROLES:
            if row["bootloader_staging_root"] is not None:
                raise IsolatedExecutorError(f"inactive boot role unexpectedly has boot staging: {role}")
        elif row["bootloader_staging_root"] is not None:
            raise IsolatedExecutorError(f"runtime role unexpectedly has boot staging: {role}")
        if role in RUNTIME_ROLES:
            if (not isinstance(row["config_seed_profile"], str) or
                    not row["config_seed_profile"] or
                    not isinstance(row["cmake_board_config"], str) or
                    not row["cmake_board_config"]):
                raise IsolatedExecutorError(f"prepare runtime seed is malformed: {role}")
        elif row["config_seed_profile"] is not None or row["cmake_board_config"] is not None:
            raise IsolatedExecutorError(f"prepare boot role has a seed: {role}")
        if row["build_root"] in roots:
            raise IsolatedExecutorError("prepare role roots are not unique")
        roots.add(row["build_root"])
        for field in ("build_root", "artifact_root", "config_seed_root",
                      "partition_contract_root", "cmake_binary_root"):
            field_path = Path(row[field])
            if field_path == build_root_path or not _inside(field_path, build_root_path):
                raise IsolatedExecutorError(
                    f"prepare role path escaped build_root: {role}/{field}")
            if row[field] in private_paths:
                raise IsolatedExecutorError("prepare role private paths are not unique")
            private_paths.add(row[field])
        if role in BOOT_ROLES and role_active:
            staging_path = Path(row["bootloader_staging_root"])
            if staging_path == build_root_path or not _inside(staging_path, build_root_path) or \
                    not _inside(staging_path, Path(row["build_root"])):
                raise IsolatedExecutorError(
                    f"prepare boot staging escaped role root: {role}")
            if row["bootloader_staging_root"] in private_paths:
                raise IsolatedExecutorError("prepare boot staging roots are not unique")
            private_paths.add(row["bootloader_staging_root"])
        role_source = row["source_view"]
        if source["roles"].get(role) != role_source:
            raise IsolatedExecutorError(f"prepare source-view role mismatch: {role}")
        role_source_required = {
            "root", "nuttx", "board", "phase", "materialized", "required", "kind",
            "shared_source_root_used", "shared_nuttx", "shared_config",
            "shared_config_detected", "shared_make_defs_detected", "excluded_shared_state",
            "snapshot_scope", "snapshot_source_head", "workspace_source_head",
            "snapshot_board_source_digest", "untracked_source_whitelist",
            "ignored_source_paths", "snapshot_requirements", "snapshot_requirements_sha256",
            "snapshot_manifest", "snapshot_manifest_sha256", "snapshot_identity_sha256",
        }
        if (not isinstance(role_source, dict) or set(role_source) != role_source_required or
                role_source.get("kind") != "entity-snapshot-required"):
            raise IsolatedExecutorError(f"prepare role source snapshot is not explicit: {role}")
        if role_source.get("phase") != value["phase"] or \
                role_source.get("materialized") != requires_materialized or \
                role_source.get("required") is not True:
            raise IsolatedExecutorError(f"role source snapshot phase is inconsistent: {role}")
        if role_source.get("shared_source_root_used") is not False:
            raise IsolatedExecutorError(f"prepare role source uses shared root: {role}")
        source_roots.add(role_source["root"])
        for field in ("root", "nuttx", "board", "shared_nuttx", "shared_config",
                      "snapshot_requirements"):
            if not isinstance(role_source.get(field), str) or not role_source[field]:
                raise IsolatedExecutorError(f"prepare source path is malformed: {role}/{field}")
            source_path = Path(role_source[field])
            _reject_traversal(source_path, f"prepare source {role}/{field}")
            if not source_path.is_absolute():
                raise IsolatedExecutorError(
                    f"prepare source path is not absolute: {role}/{field}")
            _reject_existing_symlink_components(
                source_path, f"prepare source {role}/{field}")
        if role_source["root"] != source["root"]:
            raise IsolatedExecutorError(f"role source root differs from entity source: {role}")
        expected_source_paths = {
            "root": source_root_path,
            "nuttx": canonical_snapshot_nuttx,
            "board": canonical_snapshot_board,
            "shared_nuttx": canonical_shared_nuttx,
            "shared_config": canonical_shared_config,
        }
        for field, expected_path in expected_source_paths.items():
            if Path(role_source[field]) != expected_path:
                raise IsolatedExecutorError(
                    f"prepare source {role}/{field} is not canonical")
        if (not isinstance(role_source["shared_config_detected"], bool) or
                not isinstance(role_source["shared_make_defs_detected"], bool) or
                role_source["shared_config_detected"] !=
                source["shared_nuttx_config_detected"]):
            raise IsolatedExecutorError(
                f"prepare shared source declaration is not canonical: {role}")
        if _inside(Path(role_source["root"]), Path(role_source["shared_nuttx"])):
            raise IsolatedExecutorError(f"role source unexpectedly uses shared workspace: {role}")
        if _digest_file(Path(role_source["snapshot_requirements"])) != \
                role_source["snapshot_requirements_sha256"]:
            raise IsolatedExecutorError(f"source requirements hash changed: {role}")
        if role_source.get("untracked_source_whitelist") != [
                "app", "board/bk7258", "nuttx/openamp"]:
            raise IsolatedExecutorError(f"prepare source whitelist is incomplete: {role}")
        if role_source.get("ignored_source_paths") != ["nuttx/openamp"]:
            raise IsolatedExecutorError(f"prepare ignored source scope is incomplete: {role}")
        framework.digest(role_source.get("snapshot_requirements_sha256"),
                         f"prepare source requirements {role}")
        for field in ("snapshot_manifest_sha256", "snapshot_identity_sha256"):
            if role_source.get(field) is not None:
                framework.digest(role_source[field], f"prepare source {field} {role}")
        if (role_source.get("snapshot_manifest") != source.get("snapshot_manifest") or
                role_source.get("snapshot_manifest_sha256") != source.get("snapshot_manifest_sha256") or
                role_source.get("snapshot_identity_sha256") != source.get("snapshot_identity_sha256")):
            raise IsolatedExecutorError(f"role source manifest mismatch: {role}")
        framework.digest(row["config_identity_sha256"], f"prepare config identity {role}")
        if not isinstance(row["commands"], list) or (role_active and not row["commands"]):
            raise IsolatedExecutorError(f"prepare command list is empty: {role}")
        if not role_active and row["commands"]:
            raise IsolatedExecutorError(f"inactive role has executable commands: {role}")
        if not isinstance(row["artifacts"], dict):
            raise IsolatedExecutorError(f"prepare artifact declaration is malformed: {role}")
        if value["phase"] not in {
                "runtime-built", "delivery-prepared", "delivery-built"} and row["artifacts"]:
            raise IsolatedExecutorError(f"pre-runtime role artifacts are not empty: {role}")
        if not role_active and row["artifacts"]:
            raise IsolatedExecutorError(f"inactive role has artifacts: {role}")
        if value["phase"] in {
                "runtime-built", "delivery-prepared", "delivery-built"} and \
                role in RUNTIME_ROLES and role_active:
            role_root = Path(row["build_root"])
            _validate_runtime_artifacts(row["artifacts"], role_root, role)
            config_path = Path(row["config_path"])
            if not _path_is_private_regular(config_path, role_root):
                raise IsolatedExecutorError(
                    f"runtime config path is not a private regular file: {role}")
            config_record = row["artifacts"][".config"]
            try:
                config_size = config_path.stat(follow_symlinks=False).st_size
            except OSError as error:
                raise IsolatedExecutorError(
                    f"runtime config path stat failed: {role}") from error
            if (_digest_file(config_path) != config_record["sha256"] or
                    config_size != config_record["size"]):
                raise IsolatedExecutorError(
                    f"runtime config path differs from artifact record: {role}")
        if value["phase"] in {
                "runtime-built", "delivery-prepared", "delivery-built"} and \
                role in BOOT_ROLES and role_active:
            _validate_boot_artifacts(row["artifacts"], Path(row["build_root"]), role)
        for command in row["commands"]:
            if not isinstance(command, dict) or set(command) != {
                    "stage", "tool", "argv", "environment", "cwd", "status", "log",
                    "returncode", "precondition"}:
                raise IsolatedExecutorError(f"prepare command is malformed: {role}")
            expected_precondition = "entity-source-snapshot-materialized"
            if (command["precondition"] != expected_precondition or
                    not isinstance(command["argv"], list) or
                    any(not isinstance(item, str) or not item for item in command["argv"])):
                raise IsolatedExecutorError(f"prepare command is malformed: {role}")
            if command["tool"] not in {"python3", "cmake", "make"}:
                raise IsolatedExecutorError(f"prepare command tool is not allowlisted: {role}")
            if command["status"] == "NOT_RUN":
                if command["log"] is not None or command["returncode"] is not None:
                    raise IsolatedExecutorError(f"no-run command has an execution record: {role}")
            elif value["phase"] in {
                    "runtime-built", "delivery-prepared", "delivery-built"} and role_active:
                if command["status"] != "PASS" or command["returncode"] != 0:
                    raise IsolatedExecutorError(f"runtime command did not pass: {role}")
                if (not isinstance(command["log"], str) or not command["log"] or
                        not _path_is_private_regular(Path(command["log"]), Path(row["build_root"]))):
                    raise IsolatedExecutorError(f"runtime command log is not private: {role}")
            else:
                raise IsolatedExecutorError(f"command is not fail-closed: {role}")
            _validate_command_environment(command["environment"], role)
            if command["stage"] == "cmake-build" and "nuttx_post_build" in command["argv"]:
                raise IsolatedExecutorError("runtime manifest invokes forbidden nuttx_post_build")
            joined = " ".join(command["argv"]).lower()
            if any(token in joined for token in (
                    "build_dual_image.sh", "postbuild", "imgtool", "openssl",
                    "nuttx_post_build", "crc", "sign", "pack", "flash")) or \
                    any(token in joined for token in (
                        "make_bl1_manifest", "--manifest", "manifest-primary",
                        "manifest-secondary", "signed-record", "signing-key")):
                raise IsolatedExecutorError("prepare manifest routes through a forbidden compile operation")
            if command["tool"] == "make" and "all" in command["argv"]:
                raise IsolatedExecutorError("prepare manifest routes through make all")
            if role in BOOT_ROLES and role_active and command["stage"] not in BOOT_COMMAND_STAGES:
                raise IsolatedExecutorError(f"boot role command stage is not compile-only: {role}")
        if role in BOOT_ROLES and role_active:
            if tuple(command["stage"] for command in row["commands"]) != BOOT_COMMAND_STAGES:
                raise IsolatedExecutorError(f"boot compile-only command order is unsafe: {role}")
            partition_command, make_command = row["commands"]
            partition_argv = partition_command["argv"]
            if (len(partition_argv) < 4 or
                    Path(partition_argv[1]) != canonical_partition_generator or
                    partition_argv[2] != "--input" or
                    Path(partition_argv[3]) != canonical_partition_csv):
                raise IsolatedExecutorError(
                    f"boot partition command is not snapshot-canonical: {role}")
            make_argv = make_command["argv"]
            expected_make_assignments = {
                f"PARTITION_GENERATOR={canonical_partition_generator}",
                f"PARTITION_CSV={canonical_partition_csv}",
            }
            if not expected_make_assignments.issubset(set(make_argv)):
                raise IsolatedExecutorError(
                    f"boot Make partition inputs are not snapshot-canonical: {role}")
        if value["phase"] in {
                "runtime-built", "delivery-prepared", "delivery-built"}:
            stages = [command["stage"] for command in row["commands"]]
            if not role_active and stages:
                raise IsolatedExecutorError(f"inactive role has runtime commands: {role}")
            if role in RUNTIME_ROLES and role_active and tuple(stages) != RUNTIME_COMMAND_STAGES:
                raise IsolatedExecutorError(f"runtime command order is unsafe: {role}")
            if role in BOOT_ROLES and role_active and tuple(stages) != BOOT_COMMAND_STAGES:
                raise IsolatedExecutorError(f"boot compile-only command order is unsafe: {role}")
    if len(source_roots) != 1:
        raise IsolatedExecutorError("all roles must share one entity source snapshot")
    if value["phase"] in {
            "materialized", "runtime-built", "delivery-prepared", "delivery-built"}:
        audited = _audit_materialized_snapshot(Path(source["root"]))
        if audited["identity_sha256"] != source["snapshot_identity_sha256"]:
            raise IsolatedExecutorError("manifest source snapshot identity is not audited")
        for role in BOOT_ROLES:
            if role in value["active_roles"]:
                _audit_writable_tree(Path(value["roles"][role]["bootloader_staging_root"]))
    expected_side_effects = {
        "compile": "PASS" if value["phase"] in {
            "runtime-built", "delivery-prepared", "delivery-built"} else "NOT_RUN",
        "sign": "PASS" if value["phase"] == "delivery-built" else "NOT_RUN",
        "package": "PASS" if value["phase"] == "delivery-built" else "NOT_RUN",
        "hardware": "NOT_RUN", "network": "NOT_RUN", "prepare_files_written": True,
    }
    if value["side_effects"] != expected_side_effects:
        raise IsolatedExecutorError("prepare side-effect declaration is unsafe")
    if value["phase"] in {
            "runtime-built", "delivery-prepared", "delivery-built"}:
        _validate_runtime_tools(value["tools"])
    credentials = value["credentials"]
    expected_credentials = {
        "private_key_read": "PASS" if value["phase"] == "delivery-built" else "NOT_RUN",
        "signing": "PASS" if value["phase"] == "delivery-built" else "NOT_RUN",
    }
    if credentials != expected_credentials:
        raise IsolatedExecutorError("prepare credential declaration is unsafe")
    if value["phase"] == "delivery-built":
        _validate_delivery_record(value, build_root_path)
    elif value["phase"] == "delivery-prepared":
        _validate_delivery_plan(value, build_root_path)
    elif "delivery" in value or "delivery_plan" in value:
        raise IsolatedExecutorError("non-terminal manifest carries a delivery record")
    framework.digest(value["identity_sha256"], "prepare manifest identity")
    body = dict(value)
    supplied = body.pop("identity_sha256")
    if _digest_bytes(_canonical(body)) != supplied:
        raise IsolatedExecutorError("prepare manifest identity mismatch")
    return value


def materialize_sources(
    repository: Path,
    manifest: Path | dict[str, Any],
    *,
    workspace_root: Path | None = None,
) -> dict[str, Any]:
    """Materialize and audit the one entity source snapshot.

    This is intentionally a source-only phase.  It copies the required
    source roots once, audits the helper's hash-bound manifest, and then
    copies the bootloader inputs into independent BL1/BL2 writable staging
    trees.  No compiler, signer, packer, network, or hardware command is
    invoked.  The returned manifest changes phase from ``prepare`` to
    ``materialized`` while all execution statuses remain ``NOT_RUN``.
    """
    repository = _safe_path(repository, "repository")
    _reject_traversal(repository, "repository")
    if repository.is_symlink() or not repository.is_dir():
        raise IsolatedExecutorError("repository must be a real directory")
    repository = repository.resolve(strict=True)
    value, manifest_path = _manifest_mapping(manifest)
    _validate_manifest(value)
    build_root = _safe_path(Path(value["build_root"]), "build_root")
    _reject_traversal(build_root, "build_root")
    if build_root.is_symlink() or not build_root.is_dir():
        raise IsolatedExecutorError("manifest build_root must be a real directory")
    build_root = build_root.resolve(strict=True)
    if build_root == repository or _inside(build_root, repository):
        raise IsolatedExecutorError("manifest build_root must be outside repository")

    if value["phase"] == "materialized":
        # A second call is read-only and useful to a caller that wants an
        # explicit audit checkpoint.  Do not attempt to invoke the fresh-only
        # snapshot helper against an already materialized destination.
        audited = _audit_materialized_snapshot(Path(value["source_view"]["root"]))
        if audited["identity_sha256"] != value["source_view"]["snapshot_identity_sha256"]:
            raise IsolatedExecutorError("materialized manifest snapshot identity changed")
        return value
    if value["phase"] != "prepare":  # defensive: _validate_manifest is exact
        raise IsolatedExecutorError("materialize-sources requires prepare phase")
    source_root = _manifest_path_inside_build_root(
        Path(value["source_view"]["root"]), build_root,
        "source_view.root", allow_existing=True)
    if source_root != build_root / SOURCE_SNAPSHOT_DIRNAME:
        raise IsolatedExecutorError("source snapshot must be the entity build-root sibling")
    if source_root.is_symlink() or not source_root.is_dir():
        raise IsolatedExecutorError("pending source snapshot root is not a real directory")
    try:
        entries = sorted(entry.name for entry in os.scandir(source_root))
    except OSError as error:
        raise IsolatedExecutorError(f"cannot inspect pending source snapshot: {source_root}") from error
    if entries != [SNAPSHOT_REQUIREMENTS_FILENAME]:
        raise IsolatedExecutorError("pending source snapshot contains unexpected entries")
    marker_path = source_root / SNAPSHOT_REQUIREMENTS_FILENAME
    try:
        marker = json.loads(marker_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise IsolatedExecutorError("pending source snapshot marker is unreadable") from error
    if not isinstance(marker, dict) or marker.get("materialized") is not False:
        raise IsolatedExecutorError("pending source snapshot marker is not pending")

    recorded_workspace = _safe_path(Path(value["source_view"]["workspace_root"]),
                                    "source_view.workspace_root")
    _reject_traversal(recorded_workspace, "source_view.workspace_root")
    recorded_workspace = recorded_workspace.resolve(strict=True)
    workspace = _workspace_root(repository, workspace_root or recorded_workspace)
    if workspace != recorded_workspace:
        raise IsolatedExecutorError("materialize workspace differs from prepared workspace")
    if (_inside(build_root, workspace) or build_root == workspace or
            _inside(workspace, build_root)):
        raise IsolatedExecutorError(
            "manifest build_root and workspace_root must be disjoint")

    # Validate all role destinations before copying any source bytes.  This
    # preserves the fresh-only and no-repository-write boundary on failures.
    for role in BOOT_ROLES:
        if role not in value["active_roles"]:
            continue
        destination = _manifest_path_inside_build_root(
            Path(value["roles"][role]["bootloader_staging_root"]), build_root,
            f"{role}.bootloader_staging_root", allow_existing=False)
        role_root = _manifest_path_inside_build_root(
            Path(value["roles"][role]["build_root"]), build_root,
            f"{role}.build_root", allow_existing=True)
        if not _inside(destination, role_root):
            raise IsolatedExecutorError(
                f"{role} bootloader staging is not role-local")

    try:
        helper_manifest = snapshot_workspace(
            workspace, source_root, allow_requirements_marker=True)
    except (SnapshotError, OSError, ValueError) as error:
        raise IsolatedExecutorError(
            f"source snapshot materialization failed: {source_root}") from error
    audited = _audit_materialized_snapshot(source_root)
    if audited.get("identity_sha256") != helper_manifest.get("identity_sha256"):
        raise IsolatedExecutorError("source snapshot helper/audit identity mismatch")

    # The audit is deliberately before this input-drift check and loop: no
    # role-local writable staging is trusted until the immutable source
    # manifest and the prepare-bound board/plan identities have verified.
    _verify_snapshot_inputs(value, source_root)
    snapshot_repository = source_root / "contest2026_135_yongwangzhiqian"
    bootloader_source = source_root / "contest2026_135_yongwangzhiqian/board/bk7258"
    partition_source = value["identity_inputs"]["partition_layout"]["source"]
    source_prefix = "board/bk7258/"
    if not isinstance(partition_source, str) or not partition_source.startswith(source_prefix):
        raise IsolatedExecutorError("manifest partition source is not board-local")
    for role in BOOT_ROLES:
        if role not in value["active_roles"]:
            continue
        destination = Path(value["roles"][role]["bootloader_staging_root"])
        _copy_bootloader_sources(bootloader_source, destination)
        staged_partition = destination / partition_source.removeprefix(source_prefix)
        if staged_partition.is_symlink() or not staged_partition.is_file():
            raise IsolatedExecutorError(
                f"{role} partition source was not staged: {staged_partition}")

    # The helper has already made every source entry read-only.  Lock the
    # entity root itself as well, so a caller cannot create a generated file
    # beside the audited roots and mistake it for source input.
    try:
        os.chmod(source_root, os.stat(source_root, follow_symlinks=False).st_mode & 0o555)
    except OSError as error:
        raise IsolatedExecutorError(
            f"cannot make source snapshot root read-only: {source_root}") from error

    source_manifest_path = source_root / SNAPSHOT_MANIFEST_FILENAME
    source_manifest_hash = _digest_file(source_manifest_path)
    requirements_hash = _digest_file(marker_path)
    identity = audited["identity_sha256"]
    updated_source = dict(value["source_view"])
    updated_source.update({
        "phase": "materialized",
        "materialized": True,
        "snapshot_manifest": str(source_manifest_path),
        "snapshot_manifest_sha256": source_manifest_hash,
        "snapshot_identity_sha256": identity,
    })
    updated_roles: dict[str, dict[str, Any]] = {}
    for role in ROLES:
        row = dict(value["roles"][role])
        row_source = dict(value["roles"][role]["source_view"])
        row_source.update({
            "phase": "materialized",
            "materialized": True,
            "snapshot_manifest": str(source_manifest_path),
            "snapshot_manifest_sha256": source_manifest_hash,
            "snapshot_identity_sha256": identity,
        })
        row_source["snapshot_requirements_sha256"] = requirements_hash
        row["source_view"] = row_source
        if row["commands"]:
            row["commands"] = [dict(command) for command in row["commands"]]
            for command in row["commands"]:
                command["environment"] = dict(command["environment"])
                command["environment"]["BK7258_SOURCE_SNAPSHOT_IDENTITY_SHA256"] = identity
                command["environment"]["BK7258_SOURCE_SNAPSHOT_MANIFEST"] = str(source_manifest_path)
        updated_roles[role] = row
    role_views = {role: updated_roles[role]["source_view"] for role in ROLES}
    updated_source["roles"] = role_views
    updated = dict(value)
    updated["phase"] = "materialized"
    updated["execution_mode"] = "materialize-sources-only"
    updated["source_view"] = updated_source
    updated["roles"] = updated_roles
    updated.pop("identity_sha256", None)
    updated["identity_sha256"] = _digest_bytes(_canonical(updated))
    updated = _validate_manifest(updated)

    if manifest_path is None:
        # A mapping caller can still consume the completed contract without
        # causing an unrequested filesystem write.
        return updated
    output_path = _manifest_path_inside_build_root(
        Path(updated["output"]), build_root, "output", allow_existing=True)
    if output_path != manifest_path:
        raise IsolatedExecutorError("manifest path differs from manifest output")
    if output_path.is_symlink() or not output_path.is_file():
        raise IsolatedExecutorError("materialized manifest output is not a regular file")
    try:
        output_path.write_bytes(_canonical(updated))
    except OSError as error:
        raise IsolatedExecutorError(
            f"cannot update materialized execution manifest: {output_path}") from error
    return updated


# Operation-oriented aliases make the phase easy to discover for callers that
# use the source helper's naming convention.
materialize = materialize_sources
materialize_source_snapshot = materialize_sources


def compile_runtime(
    repository: Path,
    manifest: Path | dict[str, Any],
    *,
    authorize_compile: bool = False,
    cmake_executable: str | Path = "cmake",
    python_executable: str | Path = Path(sys.executable).resolve(),
    olddefconfig_executable: str | Path = "olddefconfig",
    kconfiglib_root: str | Path | None = None,
    make_executable: str | Path = "make",
    command_runner: Callable[..., Any] | None = None,
) -> dict[str, Any]:
    """Compile isolated CP/AP runtime images from a materialized manifest.

    The function intentionally accepts an explicit authorization bit.  The
    resolved boot policy marks compilation as requiring user authorization;
    preparation and source materialization never implicitly grant it.  Only
    the CP/AP partition contract, CMake configure, ``nuttx`` and ``nuttx-bin``
    targets are run.  BL1/BL2, postbuild, signing, packaging, network, and
    hardware operations remain outside this phase.  Active BL1/BL2 roles use
    only their compile-only Make target; raw plans mark BL2 not-applicable.
    """
    if authorize_compile is not True:
        raise RuntimeCompileError(
            "compile-runtime requires explicit authorize_compile=True")
    repository = _safe_path(repository, "repository")
    _reject_traversal(repository, "repository")
    _reject_existing_symlink_components(repository, "repository")
    if repository.is_symlink() or not repository.is_dir():
        raise RuntimeCompileError("repository must be a real directory")
    repository = repository.resolve(strict=True)
    value, manifest_path = _manifest_mapping(manifest)
    value = _normalize_materialized_manifest(value)
    _validate_manifest(value)
    if value["phase"] != "materialized" or value["execution_mode"] != "materialize-sources-only":
        raise RuntimeCompileError("compile-runtime requires a materialized manifest")
    build_root = _safe_path(Path(value["build_root"]), "build_root")
    _reject_traversal(build_root, "build_root")
    if build_root.is_symlink() or not build_root.is_dir():
        raise RuntimeCompileError("manifest build_root is not a real directory")
    build_root = build_root.resolve(strict=True)
    if build_root == repository or _inside(build_root, repository):
        raise RuntimeCompileError("manifest build_root is inside repository")
    source_root = Path(value["source_view"]["root"])
    source_root = _manifest_path_inside_build_root(
        source_root, build_root, "source_view.root", allow_existing=True)
    if source_root != build_root / SOURCE_SNAPSHOT_DIRNAME:
        raise RuntimeCompileError("materialized source root is not canonical")

    # Complete audit gate: plan copy bytes, source manifest identity, every
    # source tree permission, and a fresh plan resolution from the snapshot
    # all happen before a command can create a writable output.
    plan = _audit_plan_copy(value)
    resolved_policy = _resolve_policy_for_plan(repository, plan)
    if _manifest_boot_policy(resolved_policy, plan) != value["boot_policy"]:
        raise RuntimeCompileError("manifest boot policy reconciliation identity changed")
    audited = _audit_materialized_snapshot(source_root)
    if audited.get("identity_sha256") != value["source_view"]["snapshot_identity_sha256"]:
        raise RuntimeCompileError("source snapshot identity changed")
    _verify_snapshot_inputs(value, source_root)
    _assert_snapshot_read_only(source_root)
    tool_paths, safe_path, runtime_tools = _resolve_runtime_tools(
        cmake_executable, python_executable,
        olddefconfig_executable, kconfiglib_root, make_executable)
    cmake_executable = str(tool_paths["cmake"])
    python_executable = str(tool_paths["python"])
    make_executable = str(tool_paths["make"])
    olddefconfig_path = tool_paths["olddefconfig"]
    kconfiglib_root_path = Path(runtime_tools["kconfiglib"]["root"])
    canonical_paths = _role_paths(plan, build_root)

    runtime_commands: dict[str, list[dict[str, Any]]] = {}
    for role in value["active_roles"]:
        row = value["roles"][role]
        _assert_runtime_role_bindings(plan, value, role, row, canonical_paths[role])
        role_root = _private_directory(Path(row["build_root"]), build_root, f"{role}.build_root")
        cmake_root = _private_directory(Path(row["cmake_binary_root"]), role_root,
                                         f"{role}.cmake_binary_root")
        artifact_root = _private_directory(Path(row["artifact_root"]), role_root,
                                           f"{role}.artifact_root")
        partition_root = _private_directory(Path(row["partition_contract_root"]), role_root,
                                             f"{role}.partition_contract_root")
        # A second runtime invocation against the same materialized tree must
        # fail closed instead of mixing stale CMake outputs into the index.
        if any(artifact_root.iterdir()):
            raise RuntimeCompileError(f"{role} artifact root is not fresh")
        if any((role_root / "logs").iterdir()) if (role_root / "logs").is_dir() else False:
            raise RuntimeCompileError(f"{role} log root is not fresh")
        for private_env_dir in (role_root / ".home", role_root / "tmp", role_root / ".cache"):
            if private_env_dir.exists() or private_env_dir.is_symlink():
                if private_env_dir.is_symlink() or not private_env_dir.is_dir() or \
                        any(private_env_dir.iterdir()):
                    raise RuntimeCompileError(f"{role} private environment root is not fresh")
            else:
                private_env_dir.mkdir()
        commands = [
            _runtime_command_copy(command, cmake_executable=cmake_executable,
                                  python_executable=python_executable,
                                  make_executable=make_executable)
            for command in row["commands"]
        ]
        expected_commands = _canonical_runtime_commands(
            plan, role, canonical_paths[role], repository, row["source_view"],
            resolved_policy, cmake_executable=cmake_executable,
            python_executable=python_executable, make_executable=make_executable)
        expected_stages = (RUNTIME_COMMAND_STAGES if role in RUNTIME_ROLES
                           else BOOT_COMMAND_STAGES)
        if (tuple(command["stage"] for command in commands) != expected_stages or
                len(commands) != len(expected_commands)):
            raise RuntimeCompileError(f"{role} compile command list is incomplete")
        for command, expected in zip(commands, expected_commands):
            if any(command[field] != expected[field]
                   for field in ("stage", "tool", "cwd", "precondition")):
                raise RuntimeCompileError(f"{role} compile command metadata changed")
            command["environment"]["PATH"] = safe_path
            command["environment"]["PYTHONPATH"] = str(kconfiglib_root_path)
            expected_environment = dict(expected["environment"])
            expected_environment["BK7258_SOURCE_SNAPSHOT_IDENTITY_SHA256"] = \
                value["source_view"]["snapshot_identity_sha256"]
            expected_environment["BK7258_SOURCE_SNAPSHOT_MANIFEST"] = \
                value["source_view"]["snapshot_manifest"]
            expected_environment["PATH"] = safe_path
            expected_environment["PYTHONPATH"] = str(kconfiglib_root_path)
            if command["argv"] != expected["argv"] or \
                    command["environment"] != expected_environment:
                raise RuntimeCompileError(
                    f"{role} compile command argv/environment is not canonical")
            _runtime_command_paths(
                command, role, role_root, cmake_root, source_root, python_executable,
                safe_path, olddefconfig_path, kconfiglib_root_path, make_executable,
                cmake_executable, plan, resolved_policy)
            # Runtime environment must be explicitly bound to this audited
            # snapshot.  A hand-edited manifest with a recomputed identity
            # cannot redirect CMake to a shared checkout.
            environment = command["environment"]
            if (environment.get("BK7258_SOURCE_SNAPSHOT_IDENTITY_SHA256") !=
                    value["source_view"]["snapshot_identity_sha256"] or
                    environment.get("BK7258_SOURCE_SNAPSHOT_MANIFEST") !=
                    value["source_view"]["snapshot_manifest"]):
                raise RuntimeCompileError(f"{role} command is not snapshot-bound")
        runtime_commands[role] = commands

    # Execute active roles in policy order.  The first non-zero result short
    # circuits and leaves the on-disk manifest in ``materialized`` state.
    for role in value["active_roles"]:
        row = value["roles"][role]
        role_root = Path(row["build_root"])
        for index, command in enumerate(runtime_commands[role]):
            _invoke_runtime_command(command, role_root, role, index,
                                     runner=command_runner)

    # External build tools must not be able to mutate the audited source view
    # and still publish a green runtime phase.  Re-audit both content identity
    # and read-only permissions after the final command, before collecting
    # outputs or constructing the runtime-built manifest.  No manifest write
    # has happened yet, so every failure remains materialized on disk.
    post_build_audit = _audit_materialized_snapshot(source_root)
    if post_build_audit.get("identity_sha256") != \
            value["source_view"]["snapshot_identity_sha256"]:
        raise RuntimeCompileError("source snapshot changed during runtime compile")
    _assert_snapshot_read_only(source_root)

    updated = dict(value)
    updated["phase"] = "runtime-built"
    updated["execution_mode"] = "compile-runtime"
    updated["tools"] = runtime_tools
    updated["side_effects"] = {
        "compile": "PASS", "sign": "NOT_RUN", "package": "NOT_RUN",
        "hardware": "NOT_RUN", "network": "NOT_RUN", "prepare_files_written": True,
    }
    updated_source = dict(value["source_view"])
    updated_source["phase"] = "runtime-built"
    updated_source["materialized"] = True
    updated_roles: dict[str, dict[str, Any]] = {}
    for role in ROLES:
        row = dict(value["roles"][role])
        row_source = dict(row["source_view"])
        row_source["phase"] = "runtime-built"
        row_source["materialized"] = True
        row["source_view"] = row_source
        if role in RUNTIME_ROLES:
            row["commands"] = runtime_commands[role]
            role_root = Path(row["build_root"])
            cmake_root = Path(row["cmake_binary_root"])
            row["artifacts"] = _collect_runtime_artifacts(
                role_root, cmake_root, Path(row["artifact_root"]), role)
            # ``config_path`` denotes the resolved CMake configuration input,
            # while artifacts[".config"] remains its independent archive.
            row["config_path"] = str(
                _select_runtime_output(cmake_root, role_root, ".config"))
        elif role in BOOT_ROLES and role in value["active_roles"]:
            row["commands"] = runtime_commands[role]
            row["artifacts"] = _collect_boot_artifacts(
                Path(row["build_root"]), Path(row["bootloader_staging_root"]),
                Path(row["artifact_root"]), role)
        updated_roles[role] = row
    updated_source["roles"] = {role: row["source_view"] for role, row in updated_roles.items()}
    updated["source_view"] = updated_source
    updated["roles"] = updated_roles
    updated.pop("identity_sha256", None)
    updated["identity_sha256"] = _digest_bytes(_canonical(updated))
    updated = _validate_manifest(updated)

    if manifest_path is None:
        return updated
    output_path = _manifest_path_inside_build_root(
        Path(updated["output"]), build_root, "output", allow_existing=True)
    if output_path != manifest_path or output_path.is_symlink() or not output_path.is_file():
        raise RuntimeCompileError("runtime manifest output is not the prepared regular file")
    try:
        output_path.write_bytes(_canonical(updated))
    except OSError as error:
        raise RuntimeCompileError(f"cannot write runtime-built manifest: {output_path}") from error
    return updated


# Operation-oriented aliases keep the API discoverable for callers that name
# the stage ``build-runtime`` rather than ``compile-runtime``.
build_runtime = compile_runtime
compile = compile_runtime


def _authorized_private_key(path: Path | str | None, field: str,
                            repository: Path, build_root: Path) -> Path:
    """Validate an explicitly authorized external key without persisting it."""
    if path is None:
        raise RuntimeDeliveryError(f"{field} is required for authorized signing")
    candidate = _safe_path(Path(path), field)
    _reject_traversal(candidate, field)
    _reject_existing_symlink_components(candidate, field)
    if candidate.is_symlink() or not candidate.is_file():
        raise RuntimeDeliveryError(f"{field} must be an external regular file")
    if _inside(candidate, repository):
        raise RuntimeDeliveryError(
            f"{field} must not be read from the repository checkout")
    # The delivery index walks the complete build root.  A key placed below
    # it would therefore be retained as a delivery artifact (and its path
    # would enter the terminal manifest), even though command argv is
    # redacted.  Keep credentials outside the entire private execution tree;
    # this also prevents a failed/retried delivery from accidentally
    # packaging a caller-provided key file.
    if _inside(candidate, build_root):
        raise RuntimeDeliveryError(
            f"{field} must be outside the isolated build_root")
    try:
        mode = candidate.stat(follow_symlinks=False).st_mode
    except OSError as error:
        raise RuntimeDeliveryError(f"cannot inspect {field}") from error
    if not stat.S_ISREG(mode):
        raise RuntimeDeliveryError(f"{field} is not a regular file")
    return candidate


def _delivery_path(path: Path, root: Path, field: str) -> Path:
    """Validate/create a fresh private delivery path below *root*."""
    path = _safe_path(path, field)
    _reject_traversal(path, field)
    root = _safe_path(root, "delivery root")
    if path == root or not _inside(path, root):
        raise RuntimeDeliveryError(f"{field} escaped delivery root")
    current = path.parent
    while current != root:
        if current.is_symlink():
            raise RuntimeDeliveryError(f"{field} parent contains a symlink")
        current = current.parent
        if not _inside(current, root):
            raise RuntimeDeliveryError(f"{field} escaped delivery root")
    if path.is_symlink():
        raise RuntimeDeliveryError(f"{field} must not be a symlink")
    return path


def _copy_verified_runtime_input(source: Path, record: dict[str, Any],
                                 destination: Path, role: str,
                                 logical: str) -> None:
    """Copy one already-indexed runtime artifact into delivery staging."""
    if _digest_file(source) != record.get("sha256") or \
            source.stat(follow_symlinks=False).st_size != record.get("size"):
        raise RuntimeDeliveryError(
            f"runtime input changed before delivery: {role}/{logical}")
    if destination.exists() or destination.is_symlink():
        raise RuntimeDeliveryError(f"delivery input is not fresh: {destination}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    try:
        destination.write_bytes(source.read_bytes())
        os.chmod(destination, stat.S_IMODE(source.stat(follow_symlinks=False).st_mode) & 0o555 or 0o400)
    except OSError as error:
        raise RuntimeDeliveryError(
            f"cannot copy runtime input {role}/{logical}") from error


def _delivery_environment(root: Path, source_view: dict[str, Any],
                          safe_path: str, *, layout_source: Path,
                          layout_id: str, layout_sha256: str) -> dict[str, str]:
    env_root = root / "environment"
    for path in (env_root, env_root / "home", env_root / "tmp",
                 env_root / "cache"):
        if path.exists() or path.is_symlink():
            if path.is_symlink() or not path.is_dir():
                raise RuntimeDeliveryError(f"delivery environment is unsafe: {path}")
        else:
            path.mkdir(parents=True)
    return {
        "PATH": safe_path,
        "LANG": "C",
        "LC_ALL": "C",
        "PYTHONUNBUFFERED": "1",
        "PYTHONDONTWRITEBYTECODE": "1",
        "HOME": str(env_root / "home"),
        "TMPDIR": str(env_root / "tmp"),
        "XDG_CACHE_HOME": str(env_root / "cache"),
        "BK7258_SOURCE_VIEW": source_view["root"],
        "BK7258_SOURCE_SNAPSHOT_IDENTITY_SHA256":
            source_view["snapshot_identity_sha256"],
        "BK7258_SOURCE_SNAPSHOT_MANIFEST": source_view["snapshot_manifest"],
        # Every delivery child receives the resolved layout triple.  Tools
        # that have a historical default CSV must still be pinned to this
        # plan's source/semantic identity.
        "BK7258_PARTITION_LAYOUT_SOURCE": str(layout_source),
        "BK7258_PARTITION_LAYOUT_ID": layout_id,
        "BK7258_PARTITION_LAYOUT_SHA256": layout_sha256,
    }


def _run_delivery_command(
        command: dict[str, Any], actual_argv: list[str], env: dict[str, str],
        cwd: Path, log_path: Path, *, runner: Callable[..., Any] | None,
        role: str = "delivery") -> dict[str, Any]:
    """Run one existing delivery tool and retain only a redacted command record."""
    _validate_command_environment(env, role)
    effective_env = {
        key: value for key, value in env.items()
        if key in SAFE_ENV_NAMES or key.startswith("BK7258_")
    }
    if log_path.exists() or log_path.is_symlink():
        raise RuntimeDeliveryError(f"delivery command log is not fresh: {log_path}")
    log_path.parent.mkdir(parents=True, exist_ok=True)
    try:
        if runner is None:
            result = subprocess.run(
                actual_argv, cwd=str(cwd), env=effective_env,
                capture_output=True, text=True, check=False)
        else:
            result = runner(
                actual_argv, cwd=str(cwd), env=effective_env,
                capture_output=True, text=True, check=False)
    except Exception as error:
        error_text = _redact_delivery_text(str(error), actual_argv)
        try:
            log_path.write_text(f"executor error: {error_text}\n", encoding="utf-8")
        except OSError:
            pass
        raise RuntimeDeliveryError(
            f"delivery command failed to start: {command['stage']}") from error
    returncode = result if isinstance(result, int) else getattr(result, "returncode", None)
    stdout = "" if isinstance(result, int) else getattr(result, "stdout", "") or ""
    stderr = "" if isinstance(result, int) else getattr(result, "stderr", "") or ""
    if not isinstance(returncode, int):
        raise RuntimeDeliveryError(
            f"delivery command returned no status: {command['stage']}")
    if not isinstance(stdout, str):
        stdout = stdout.decode(errors="replace")
    if not isinstance(stderr, str):
        stderr = stderr.decode(errors="replace")
    stdout = _redact_delivery_text(stdout, actual_argv)
    stderr = _redact_delivery_text(stderr, actual_argv)
    text = stdout
    if stderr:
        text += ("\n" if text and not text.endswith("\n") else "") + \
                "--- stderr ---\n" + stderr
    try:
        log_path.write_text(text, encoding="utf-8")
    except OSError as error:
        raise RuntimeDeliveryError(
            f"cannot write delivery command log: {command['stage']}") from error
    command["status"] = "PASS" if returncode == 0 else "FAIL"
    command["returncode"] = returncode
    command["log"] = str(log_path)
    if returncode != 0:
        raise RuntimeDeliveryError(
            f"delivery command returned {returncode}: {command['stage']}")
    return command


def _redact_delivery_text(text: str, actual_argv: list[str]) -> str:
    """Remove only authorized key argument values from child diagnostics.

    Existing signing tools are allowed to print their argv while failing.  A
    delivery log is persistent and therefore must not contain the actual key
    pathname, including in an exception string.  Do not blanket-redact
    arbitrary ``.pem`` text: only values immediately following the explicit
    private-key flags are credentials.
    """
    secret_values: list[str] = []
    key_flags = {
        "--key", "--private-key", "--bl1-manifest-key",
        "--mcuboot-signing-key",
    }
    for index, item in enumerate(actual_argv[:-1]):
        if item in key_flags and actual_argv[index + 1]:
            secret_values.append(actual_argv[index + 1])
    for secret in secret_values:
        text = text.replace(secret, PRIVATE_KEY_TOKEN)
    return text


def _make_delivery_command(stage: str, tool: str, argv: list[str],
                           cwd: Path, environment: dict[str, str],
                           authorization: str) -> dict[str, Any]:
    redacted = list(argv)
    # Actual key paths are replaced only in the persisted contract.  The
    # caller passes the actual argv to _run_delivery_command and this record
    # remains safe to write to the repository's staging/log export.
    key_flags = {
        "--key", "--private-key", "--bl1-manifest-key",
        "--mcuboot-signing-key",
    }
    for index, item in enumerate(redacted):
        if item in key_flags and index + 1 < len(redacted):
            redacted[index + 1] = PRIVATE_KEY_TOKEN
    redacted = _redacted_delivery_argv(redacted, stage)
    return {
        "stage": stage,
        "tool": tool,
        "argv": redacted,
        "environment": dict(environment),
        "cwd": str(cwd),
        "status": "NOT_RUN",
        "log": None,
        "returncode": None,
        "authorization": authorization,
        "argv_sha256": _digest_bytes(_canonical(redacted)),
    }


def _delivery_tool_records(
    source_board: Path,
    source_tools: Path,
) -> dict[str, dict[str, str]]:
    paths = {
        "postbuild": source_board / "scripts/postbuild.sh",
        "crc_expand": source_board / "scripts/bk7258_crc_expand.py",
        "bl1_pack": source_board / "bootloader/bk7236_pack_min_bootloader.py",
        "bl1_manifest": source_board / "bootloader/make_bl1_manifest.py",
        "bl2_crc": source_board / "scripts/bk7258_crc_expand.py",
        "mcuboot_pair": source_tools / "pack_bk7258_mcuboot_pair.py",
        "dual_image": source_tools / "pack_dual_image.py",
        "bkpack": source_tools / "bk7258_bkpack.py",
        "trust_chain": source_tools / "bk7258_trust_chain.py",
        "imgtool": source_board.parent.parent.parent /
            "apps/boot/mcuboot/mcuboot/scripts/imgtool.py",
    }
    result: dict[str, dict[str, str]] = {}
    for name, path in paths.items():
        if path.is_symlink() or not path.is_file():
            raise RuntimeDeliveryError(f"delivery tool is missing: {name}")
        result[name] = {"path": str(path), "sha256": _digest_file(path)}
    return result


def _delivery_artifact_index(root: Path, *, require_package: bool = True) -> dict[str, dict[str, Any]]:
    result: dict[str, dict[str, Any]] = {}
    if not root.is_dir() or root.is_symlink():
        raise RuntimeDeliveryError("delivery root is not a real directory")
    for current, directories, files in os.walk(root, topdown=True, followlinks=False):
        current_path = Path(current)
        directories[:] = [name for name in directories
                          if not (current_path / name).is_symlink()]
        for name in sorted(files):
            path = current_path / name
            if path.is_symlink() or not path.is_file():
                raise RuntimeDeliveryError(f"delivery contains unsafe file: {path}")
            # The terminal execution manifest is written outside this root;
            # command logs and all internal payloads remain traceable here.
            relative = path.relative_to(root).as_posix()
            result[relative] = {
                "path": str(path), "sha256": _digest_file(path),
                "size": path.stat(follow_symlinks=False).st_size,
            }
    if require_package and "firmware.bkpack" not in result:
        raise RuntimeDeliveryError("delivery did not produce firmware.bkpack")
    return result


def _verify_bkpack_standard_members(package: Path) -> None:
    """Verify that the final container carries canonical aliases and metadata."""

    required = {
        "vela_nuttx_cp.bin", "vela_nuttx_ap.bin",
        "vela_nuttx_manifest.json",
    }
    try:
        with zipfile.ZipFile(package) as archive:
            names = {item.filename for item in archive.infolist()}
            if not required.issubset(names):
                raise RuntimeDeliveryError(
                    "firmware.bkpack omits canonical OpenVela aliases or manifest")
            package_manifest = json.loads(
                archive.read("bkpack-manifest.json").decode("utf-8"))
            member_rows = package_manifest.get("members")
            rows = {
                row.get("name"): row for row in member_rows
                if isinstance(row, dict)
            }
            if not required.issubset(rows):
                raise RuntimeDeliveryError(
                    "firmware.bkpack manifest omits canonical OpenVela members")
            for name in required:
                payload = archive.read(name)
                row = rows[name]
                if (row.get("size") != len(payload) or
                        row.get("sha256") != _digest_bytes(payload)):
                    raise RuntimeDeliveryError(
                        f"firmware.bkpack member metadata drift: {name}")
    except (OSError, KeyError, ValueError, zipfile.BadZipFile) as error:
        raise RuntimeDeliveryError(
            f"cannot inspect canonical firmware.bkpack members: {error}") from error


def _write_dual_nuttx_manifest(payload: Path) -> Path:
    """Publish role-qualified OpenVela aliases for one shared delivery root.

    ``postbuild.sh`` keeps the internal app/app1 and CRC names stable.  The
    aliases below are the external logical NuttX images; they deliberately do
    not use the ambiguous generic ``vela_nuttx.bin`` name in a dual-core
    delivery.  The manifest binds each alias back to its internal source and
    records the byte-exact digest used by the delivery artifact index.
    """
    roles = {
        "cp": ("app.bin", "vela_nuttx_cp.bin"),
        "ap": ("app1.bin", "vela_nuttx_ap.bin"),
    }
    role_records: dict[str, dict[str, Any]] = {}
    for role, (source_name, alias_name) in roles.items():
        alias = payload / alias_name
        # A signed delivery retains the pre-sign raw logical image as
        # cp-raw/ap-raw.  Bind the public alias to that stable source instead
        # of the Flash-facing app/app1 file, which is replaced by the signed
        # MCUboot image later in the same phase.
        raw_source_name = "cp-raw.bin" if role == "cp" else "ap-raw.bin"
        if (payload / raw_source_name).is_file():
            source_name = raw_source_name
        source = payload / source_name
        if (source.is_symlink() or alias.is_symlink() or
                not source.is_file() or not alias.is_file()):
            raise RuntimeDeliveryError(
                f"OpenVela {role} NuttX alias is missing or unsafe")
        source_bytes = source.read_bytes()
        alias_bytes = alias.read_bytes()
        if source_bytes != alias_bytes:
            raise RuntimeDeliveryError(
                f"OpenVela {role} NuttX alias is not byte-exact")
        role_records[role] = {
            "source_file": source_name,
            "alias": alias_name,
            "sha256": _digest_bytes(source_bytes),
            "size": len(source_bytes),
            "byte_exact": True,
        }
        (payload / f"vela_nuttx_{role}.json").write_bytes(_canonical({
            "schema": "openvela.nuttx-artifacts/1",
            "version": 1,
            "role": role,
            "dual_core": True,
            "source_file": source_name,
            "role_alias": alias_name,
            "generic_alias": None,
            "sha256": role_records[role]["sha256"],
            "size": role_records[role]["size"],
            "byte_exact": True,
        }))
    manifest = payload / "vela_nuttx_manifest.json"
    if manifest.is_symlink():
        raise RuntimeDeliveryError("OpenVela NuttX manifest must not be a symlink")
    document = {
        "schema": "openvela.nuttx-artifacts/1",
        "version": 1,
        "dual_core": True,
        "generic_alias": None,
        "roles": role_records,
    }
    manifest.write_bytes(_canonical(document))
    return manifest


def _write_delivery_manifest(value: dict[str, Any], manifest_path: Path,
                             build_root: Path) -> dict[str, Any]:
    updated = copy.deepcopy(value)
    updated["phase"] = "delivery-built"
    updated["execution_mode"] = "deliver"
    updated["source_view"]["phase"] = "delivery-built"
    for role in ROLES:
        updated["roles"][role]["source_view"]["phase"] = "delivery-built"
        updated["source_view"]["roles"][role]["phase"] = "delivery-built"
    updated.pop("delivery_plan", None)
    updated["boot_policy"] = dict(updated["boot_policy"])
    updated["boot_policy"].update({
        "status": "RECONCILED",
        "execution": "SIGNED_DELIVERY",
        "private_key_read": "PASS",
        "signing": "PASS",
    })
    updated["side_effects"] = {
        "compile": "PASS", "sign": "PASS", "package": "PASS",
        "hardware": "NOT_RUN", "network": "NOT_RUN",
        "prepare_files_written": True,
    }
    updated["credentials"] = {"private_key_read": "PASS", "signing": "PASS"}
    updated.pop("identity_sha256", None)
    updated["identity_sha256"] = _digest_bytes(_canonical(updated))
    updated = _validate_manifest(updated)
    output_path = _manifest_path_inside_build_root(
        Path(updated["output"]), build_root, "output", allow_existing=True)
    if output_path != manifest_path or output_path.is_symlink() or not output_path.is_file():
        raise RuntimeDeliveryError("delivery manifest output is not the prepared regular file")
    try:
        output_path.write_bytes(_canonical(updated))
    except OSError as error:
        raise RuntimeDeliveryError("cannot write delivery-built manifest") from error
    return updated


def _write_delivery_plan_manifest(value: dict[str, Any], manifest_path: Path,
                                  build_root: Path,
                                  delivery_plan: dict[str, Any]) -> dict[str, Any]:
    """Persist the keyless postbuild checkpoint without changing auth state."""
    updated = copy.deepcopy(value)
    updated["phase"] = "delivery-prepared"
    updated["execution_mode"] = "prepare-delivery"
    updated["source_view"]["phase"] = "delivery-prepared"
    for role in ROLES:
        updated["roles"][role]["source_view"]["phase"] = "delivery-prepared"
        updated["source_view"]["roles"][role]["phase"] = "delivery-prepared"
    updated["side_effects"] = {
        "compile": "PASS", "sign": "NOT_RUN", "package": "NOT_RUN",
        "hardware": "NOT_RUN", "network": "NOT_RUN",
        "prepare_files_written": True,
    }
    updated["credentials"] = {"private_key_read": "NOT_RUN", "signing": "NOT_RUN"}
    updated["delivery_plan"] = delivery_plan
    updated.pop("delivery", None)
    updated.pop("identity_sha256", None)
    updated["identity_sha256"] = _digest_bytes(_canonical(updated))
    updated = _validate_manifest(updated)
    output_path = _manifest_path_inside_build_root(
        Path(updated["output"]), build_root, "output", allow_existing=True)
    if output_path != manifest_path or output_path.is_symlink() or not output_path.is_file():
        raise RuntimeDeliveryError("delivery plan output is not the prepared regular file")
    try:
        output_path.write_bytes(_canonical(updated))
    except OSError as error:
        raise RuntimeDeliveryError("cannot write delivery-prepared manifest") from error
    return updated


def deliver(
    repository: Path,
    manifest: Path | dict[str, Any],
    *,
    authorize_sign: bool = False,
    authorize_package: bool = False,
    mcuboot_signing_key: Path | str | None = None,
    bl1_manifest_key: Path | str | None = None,
    version: str | None = None,
    security_counter: str = "auto",
    python_executable: str | Path = Path(sys.executable).resolve(),
    command_runner: Callable[..., Any] | None = None,
) -> dict[str, Any]:
    """Create and verify one private, signed MCUboot delivery.

    This is intentionally one terminal operation with two independent
    authorization gates.  Both ``authorize_sign`` and ``authorize_package``
    must be true before any output is created.  The function refuses raw
    products, reads no key unless the caller explicitly authorizes signing,
    never serializes a key path, and keeps the only download artifact at
    ``<build_root>/delivery/firmware.bkpack``.  All transformations are
    delegated to the existing board-owned scripts.
    """
    if authorize_sign is not True:
        raise RuntimeDeliveryError(
            "delivery requires explicit authorize_sign=True")
    if authorize_package is not True:
        raise RuntimeDeliveryError(
            "delivery requires explicit authorize_package=True")
    repository = _safe_path(repository, "repository")
    _reject_traversal(repository, "repository")
    _reject_existing_symlink_components(repository, "repository")
    if repository.is_symlink() or not repository.is_dir():
        raise RuntimeDeliveryError("repository must be a real directory")
    repository = repository.resolve(strict=True)
    value, manifest_path = _manifest_mapping(manifest)
    _validate_manifest(value)
    if manifest_path is None:
        raise RuntimeDeliveryError("delivery requires a persisted runtime manifest")
    if value["phase"] not in {"runtime-built", "delivery-prepared"} or \
            value["execution_mode"] not in {
                "compile-runtime", "build-runtime", "prepare-delivery",
                "postbuild-runtime"}:
        raise RuntimeDeliveryError(
            "delivery requires a runtime-built or delivery-prepared manifest")
    if value["boot"] != "mcuboot" or value["active_roles"] != list(ROLES):
        raise RuntimeDeliveryError(
            "raw/non-MCUboot delivery is not applicable and is fail-closed")
    build_root = _safe_path(Path(value["build_root"]), "build_root")
    _reject_traversal(build_root, "build_root")
    if build_root.is_symlink() or not build_root.is_dir():
        raise RuntimeDeliveryError("runtime build_root is not a real directory")
    build_root = build_root.resolve(strict=True)
    if build_root == repository or _inside(build_root, repository):
        raise RuntimeDeliveryError("delivery build_root is inside repository")
    source_root = Path(value["source_view"]["root"])
    source_root = _manifest_path_inside_build_root(
        source_root, build_root, "source_view.root", allow_existing=True)
    if source_root != build_root / SOURCE_SNAPSHOT_DIRNAME:
        raise RuntimeDeliveryError("delivery source root is not canonical")
    plan = _audit_plan_copy(value)
    resolved_policy = _resolve_policy_for_plan(repository, plan)
    if _manifest_boot_policy(resolved_policy, plan) != value["boot_policy"]:
        raise RuntimeDeliveryError("delivery boot policy reconciliation changed")
    audited = _audit_materialized_snapshot(source_root)
    if audited.get("identity_sha256") != value["source_view"]["snapshot_identity_sha256"]:
        raise RuntimeDeliveryError("delivery source snapshot identity changed")
    _verify_snapshot_inputs(value, source_root)
    _assert_snapshot_read_only(source_root)
    for role in ROLES:
        row = value["roles"][role]
        if role in RUNTIME_ROLES:
            _validate_runtime_artifacts(row["artifacts"], Path(row["build_root"]), role)
        else:
            _validate_boot_artifacts(row["artifacts"], Path(row["build_root"]), role)

    mcuboot_key = _authorized_private_key(
        mcuboot_signing_key, "mcuboot_signing_key", repository, build_root)
    bl1_key = _authorized_private_key(
        bl1_manifest_key, "bl1_manifest_key", repository, build_root)
    if not isinstance(version, str) or not version or any(char in version for char in "\x00\r\n"):
        raise RuntimeDeliveryError("version is required and must be one line")
    if (not isinstance(security_counter, str) or not security_counter or
            any(char in security_counter for char in "\x00\r\n")):
        raise RuntimeDeliveryError("security_counter is malformed")
    if security_counter != "auto":
        try:
            counter = int(security_counter, 0)
        except ValueError as error:
            raise RuntimeDeliveryError("security_counter must be auto or uint32") from error
        if counter < 0 or counter > 0xffffffff:
            raise RuntimeDeliveryError("security_counter is outside uint32")

    prepared_delivery = value["phase"] == "delivery-prepared"
    delivery_root = build_root / "delivery"
    if prepared_delivery:
        _validate_delivery_plan(value, build_root)
        if delivery_root.is_symlink() or not delivery_root.is_dir():
            raise RuntimeDeliveryError("delivery-prepared root is not private")
        for name in ("payload", "work", "manifests", "dual-input",
                     "package-source", "inputs", "logs"):
            path = delivery_root / name
            if path.is_symlink() or not path.is_dir():
                raise RuntimeDeliveryError(
                    f"delivery-prepared directory is missing: {name}")
    else:
        if delivery_root.exists() or delivery_root.is_symlink():
            raise RuntimeDeliveryError("delivery root must be fresh and absent")
        delivery_root.mkdir()
        for name in ("payload", "work", "manifests", "dual-input", "package-source",
                     "inputs", "logs"):
            (delivery_root / name).mkdir()
    source_repository = source_root / "contest2026_135_yongwangzhiqian"
    source_board = source_repository / "board/bk7258"
    source_tools = source_repository / "tools/bk7258"
    scripts = source_board / "scripts"
    bootloader = source_board / "bootloader"
    partition_relative = plan["partition_layout"]["source"]
    if not isinstance(partition_relative, str) or not partition_relative.startswith(
            "board/bk7258/"):
        raise RuntimeDeliveryError("delivery partition source is not board-local")
    if partition_relative != "board/bk7258/partitions/bk7258/auto_partitions.csv":
        raise RuntimeDeliveryError(
            "delivery BL1 packer only supports the explicit auto_partitions.csv contract")
    partition_csv = source_board / partition_relative.removeprefix("board/bk7258/")
    if partition_csv.is_symlink() or not partition_csv.is_file():
        raise RuntimeDeliveryError("delivery partition source is missing")
    staged_partition = delivery_root / "inputs/partition-layout.csv"
    if prepared_delivery:
        if staged_partition.is_symlink() or not staged_partition.is_file() or \
                _digest_file(staged_partition) != _digest_file(partition_csv):
            raise RuntimeDeliveryError("delivery-prepared partition input changed")
    else:
        staged_partition.write_bytes(partition_csv.read_bytes())
        os.chmod(staged_partition, 0o400)
    python_path = _safe_path(Path(python_executable), "python_executable")
    _reject_existing_symlink_components(python_path, "python_executable")
    if python_path.is_symlink() or not python_path.is_file() or \
            not (python_path.stat().st_mode & 0o111):
        raise RuntimeDeliveryError("python_executable is not a normal executable")
    safe_path = _tool_search_path(str(python_path), "bash")
    bash_path = _resolve_tool("bash", "bash", safe_path)
    python = str(python_path)
    base_env = _delivery_environment(
        delivery_root, value["source_view"], safe_path,
        layout_source=partition_csv,
        layout_id=plan["partition_layout"]["layout_id"],
        layout_sha256=plan["partition_layout"]["layout_sha256"],
    )
    tool_records = _delivery_tool_records(source_board, source_tools)
    commands: list[dict[str, Any]] = []
    command_index = 0
    log_prefix = "final-" if prepared_delivery else ""

    def run(stage: str, tool: str, actual: list[str], cwd: Path,
            env_updates: dict[str, str] | None = None,
            authorization: str = "none") -> None:
        nonlocal command_index
        env = dict(base_env)
        env.update(env_updates or {})
        record = _make_delivery_command(stage, tool, actual, cwd, env, authorization)
        log = delivery_root / "logs" / f"{log_prefix}{command_index:02d}-{stage}.log"
        command_index += 1
        _run_delivery_command(record, actual, env, cwd, log, runner=command_runner)
        commands.append(record)

    # Copy raw runtime outputs into two role-private postbuild work roots.  A
    # postbuild invocation therefore sees the same shape as the existing
    # script, while every generated file remains below delivery/.
    for role, raw_name in (("cp", "nuttx.bin"), ("ap", "nuttx.bin")):
        row = value["roles"][role]
        work = delivery_root / "work" / role
        if not prepared_delivery:
            work.mkdir()
        elif work.is_symlink() or not work.is_dir():
            raise RuntimeDeliveryError(f"delivery-prepared work root is missing: {role}")
        if prepared_delivery:
            # The keyless checkpoint already hash-checked these inputs.  Keep
            # them immutable for the signed continuation and re-check their
            # bytes before any signer is invoked.
            for logical, destination in (
                    (raw_name, work / raw_name), (".config", work / ".config")):
                record = row["artifacts"][logical]
                if destination.is_symlink() or not destination.is_file() or \
                        _digest_file(destination) != record["sha256"] or \
                        destination.stat(follow_symlinks=False).st_size != record["size"]:
                    raise RuntimeDeliveryError(
                        f"delivery-prepared input changed: {role}/{logical}")
            continue
        _copy_verified_runtime_input(
            Path(row["artifacts"]["nuttx.bin"]["path"]),
            row["artifacts"]["nuttx.bin"], work / "nuttx.bin", role, "nuttx.bin")
        _copy_verified_runtime_input(
            Path(row["config_path"]), row["artifacts"][".config"],
            work / ".config", role, ".config")
        contract = Path(row["partition_contract_root"])
        if contract.is_symlink() or not contract.is_dir():
            raise RuntimeDeliveryError(f"{role} partition contract is missing")
        shutil.copytree(contract, work / "partition-contract", symlinks=False)

    primary_manifest = delivery_root / "manifests/bl1-manifest-primary.bin"
    secondary_manifest = delivery_root / "manifests/bl1-manifest-secondary.bin"
    manifest_format = resolved_policy["policy"]["manifest"]["format"]
    manifest_version = resolved_policy["policy"]["version_policy"][
        "bl1_manifest_version"]["default"]
    geometry = resolved_policy["bl2_geometry"]
    primary_xip = geometry["primary"]["xip_start"]
    secondary_xip = geometry["secondary"]["xip_start"]
    bl2_logical_size = plan["bl2_image_logical_size"]
    bl2_capacity = geometry["primary"]["logical_size"]
    bl2_raw = Path(value["roles"]["bl2"]["artifacts"]["bl2.bin"]["path"])
    bl2_staged = delivery_root / "payload/bl2.bin"
    if prepared_delivery:
        record = value["roles"]["bl2"]["artifacts"]["bl2.bin"]
        if bl2_staged.is_symlink() or not bl2_staged.is_file() or \
                _digest_file(bl2_staged) != record["sha256"] or \
                bl2_staged.stat(follow_symlinks=False).st_size != record["size"]:
            raise RuntimeDeliveryError("delivery-prepared BL2 input changed")
    else:
        _copy_verified_runtime_input(
            bl2_raw, value["roles"]["bl2"]["artifacts"]["bl2.bin"], bl2_staged,
            "bl2", "bl2.bin")
    bl1_raw = Path(value["roles"]["bl1"]["artifacts"]["bl.bin"]["path"])
    bl1_staged = delivery_root / "payload/bl1-raw.bin"
    if prepared_delivery:
        record = value["roles"]["bl1"]["artifacts"]["bl.bin"]
        if bl1_staged.is_symlink() or not bl1_staged.is_file() or \
                _digest_file(bl1_staged) != record["sha256"] or \
                bl1_staged.stat(follow_symlinks=False).st_size != record["size"]:
            raise RuntimeDeliveryError("delivery-prepared BL1 input changed")
    else:
        _copy_verified_runtime_input(
            bl1_raw, value["roles"]["bl1"]["artifacts"]["bl.bin"], bl1_staged,
            "bl1", "bl.bin")
    shutil.copyfile(bl1_staged, delivery_root / "payload/bootloader.bin")
    common_manifest_args = [
        "--format", manifest_format,
        "--bl2", str(bl2_staged), "--private-key", str(bl1_key),
        "--generated-root-c", str(delivery_root / "manifests/boot_bl1_manifest_key.c"),
        "--partition-csv", str(partition_csv),
        "--expect-layout-id", plan["partition_layout"]["layout_id"],
        "--expect-layout-sha256", plan["partition_layout"]["layout_sha256"],
        "--bl2-slot", "primary", "--bl2-xip", hex(primary_xip),
        "--bl2-size", hex(bl2_logical_size), "--bl2-capacity", hex(bl2_capacity),
        "--bl2-load", hex(0x28020000), "--manifest-version", str(manifest_version),
    ]
    run("bl1-manifest-primary", "python3", [
        python, str(bootloader / "make_bl1_manifest.py"), *common_manifest_args,
        "--out", str(primary_manifest),
    ], delivery_root / "manifests", {"BK7258_LAYOUT_ID": plan["partition_layout"]["layout_id"]}, "sign")
    secondary_args = list(common_manifest_args)
    secondary_args[secondary_args.index("--bl2-slot") + 1] = "secondary"
    secondary_args[secondary_args.index("--bl2-xip") + 1] = hex(secondary_xip)
    run("bl1-manifest-secondary", "python3", [
        python, str(bootloader / "make_bl1_manifest.py"), *secondary_args,
        "--out", str(secondary_manifest),
    ], delivery_root / "manifests", {"BK7258_LAYOUT_ID": plan["partition_layout"]["layout_id"]}, "sign")

    bootloader_crc = delivery_root / "payload/bootloader_crc.bin"
    run("bl1-pack", "python3", [
        python, str(bootloader / "bk7236_pack_min_bootloader.py"),
        "--in", str(bl1_staged), "--out", str(bootloader_crc),
        "--manifest-primary", str(primary_manifest),
        "--manifest-secondary", str(secondary_manifest),
    ], delivery_root / "payload", {
        "BK7258_PARTITION_LAYOUT_ID": plan["partition_layout"]["layout_id"],
        "BK7258_PARTITION_LAYOUT_SHA256": plan["partition_layout"]["layout_sha256"],
    })

    bl2_crc = delivery_root / "payload/bl2_crc.bin"
    run("bl2-pack", "python3", [
        python, str(scripts / "bk7258_crc_expand.py"), "--in", str(bl2_staged),
        "--out", str(bl2_crc), "--xip-base", hex(primary_xip),
        "--execution-base", hex(0x28020000), "--max-size", hex(bl2_capacity),
        "--pad-size", hex(bl2_logical_size),
    ], delivery_root / "payload", {
        "BK7258_PARTITION_LAYOUT_ID": plan["partition_layout"]["layout_id"],
        "BK7258_PARTITION_LAYOUT_SHA256": plan["partition_layout"]["layout_sha256"],
    })
    bl2_secondary_crc = delivery_root / "payload/bl2_secondary_crc.bin"
    bl2_secondary_crc.write_bytes(bl2_crc.read_bytes())
    os.chmod(bl2_secondary_crc, 0o400)

    for role in RUNTIME_ROLES:
        work = delivery_root / "work" / role
        updates = {
            "BK7258_POSTBUILD_MODE": "isolated",
            "BK7258_POSTBUILD_ARTIFACT_ROOT": str(delivery_root / "payload"),
            "BK7258_POSTBUILD_DUAL_ROLE": "1",
            "BK7258_PARTITION_CONTRACT_ROOT": str(work / "partition-contract"),
            "BK7258_PARTITION_LAYOUT_SOURCE": str(partition_csv),
            "BK7258_PARTITION_LAYOUT_ID": plan["partition_layout"]["layout_id"],
            "BK7258_PARTITION_LAYOUT_SHA256": plan["partition_layout"]["layout_sha256"],
            "BK7258_BL1_CRC_BIN": str(bootloader_crc),
            "BK7258_ROLE": role,
        }
        run(f"postbuild-{role}", "bash", [
            str(bash_path), str(scripts / "postbuild.sh"), str(work),
            str(source_board), role,
        ], work, updates)

    pair_output = delivery_root / "payload/mcuboot-pair"
    pair_output.mkdir()
    run("mcuboot-pair-sign", "python3", [
        python, str(source_tools / "pack_bk7258_mcuboot_pair.py"),
        "--partition", str(partition_csv),
        "--expect-layout-id", plan["partition_layout"]["layout_id"],
        "--expect-layout-sha256", plan["partition_layout"]["layout_sha256"],
        "--cp-raw", str(delivery_root / "payload/app.bin"),
        "--ap-raw", str(delivery_root / "payload/app1.bin"),
        "--key", str(mcuboot_key), "--output", str(pair_output),
        "--version", version, "--security-counter", security_counter,
        "--imgtool", str(source_root / "apps/boot/mcuboot/mcuboot/scripts/imgtool.py"),
    ], delivery_root / "payload", {
        "BK7258_PARTITION_LAYOUT_ID": plan["partition_layout"]["layout_id"],
        "BK7258_PARTITION_LAYOUT_SHA256": plan["partition_layout"]["layout_sha256"],
    }, "sign")
    # Preserve unsigned logical role artifacts as the standard openvela
    # inputs, then replace only the Flash-facing app/CRC names with the
    # existing MCUboot pair adapter's signed outputs.
    for role, raw_name, signed_name, crc_name, signed_crc in (
            ("cp", "app.bin", "cp-raw.bin", "app_crc.bin", "cp_signed_crc.bin"),
            ("ap", "app1.bin", "ap-raw.bin", "app1_crc.bin", "ap_signed_crc.bin")):
        raw_source = delivery_root / "payload" / raw_name
        raw_destination = delivery_root / "payload" / signed_name
        shutil.copyfile(raw_source, raw_destination)
        signed = pair_output / ("cp_signed.bin" if role == "cp" else "ap_signed.bin")
        signed_crc_path = pair_output / signed_crc
        (delivery_root / "payload" / raw_name).write_bytes(signed.read_bytes())
        (delivery_root / "payload" / crc_name).write_bytes(signed_crc_path.read_bytes())

    _write_dual_nuttx_manifest(delivery_root / "payload")

    # postbuild.sh intentionally emits the legacy unsigned CP intermediates
    # for compatibility.  They were made from the pre-signing CRC and must
    # never coexist with the signed Flash payload in this terminal delivery.
    # Remove them before the dual-image tool indexes its output; the delivery
    # record carries the explicit not-applicable list below.
    for stale_name in ("all-app.bin", "nuttx_crc.bin"):
        stale = delivery_root / "payload" / stale_name
        if stale.exists() or stale.is_symlink():
            if stale.is_symlink() or not stale.is_file():
                raise RuntimeDeliveryError(
                    f"unsigned postbuild intermediate is unsafe: {stale_name}")
            stale.unlink()

    trust = delivery_root / "payload/bk7258-trust-chain.json"
    run("trust-chain-emit", "python3", [
        python, str(source_tools / "bk7258_trust_chain.py"), "emit",
        "--bl1-manifest-key", str(bl1_key),
        "--mcuboot-signing-key", str(mcuboot_key),
        "--bootloader-elf", str(value["roles"]["bl1"]["artifacts"]["bl.elf"]["path"]),
        "--bootloader-bin", str(delivery_root / "payload/bootloader.bin"),
        "--bl2-elf", str(value["roles"]["bl2"]["artifacts"]["bl2.elf"]["path"]),
        "--bl2-bin", str(bl2_staged), "--boot-xip-base", hex(0x02000000),
        "--bl2-load-base", hex(0x28020000),
        "--bl2-primary-xip-base", hex(primary_xip), "--output", str(trust),
    ], delivery_root / "payload", {
        "BK7258_PARTITION_LAYOUT_ID": plan["partition_layout"]["layout_id"],
    }, "sign")

    dual_input = delivery_root / "dual-input"
    package_source = delivery_root / "package-source"
    # The dual-image tool owns all image placement/manifest logic.  Stage its
    # existing input names only; do not reimplement a second packer here.
    for name in (
            "bootloader.bin", "bl2.bin", "bl2_crc.bin", "bl2_secondary_crc.bin",
            "app.bin", "app_crc.bin", "app1.bin", "app1_crc.bin",
            "cp-raw.bin", "ap-raw.bin", "bk7258-trust-chain.json"):
        shutil.copyfile(delivery_root / "payload" / name, dual_input / name)
    # Existing pack_dual_image stages the CRC-expanded BL1 into its canonical
    # bootloader.bin name, then re-stages the raw bootloader required by the
    # trust-chain contract.  Keep both inputs private; the established tool
    # remains authoritative for the final directory layout.
    shutil.copyfile(bootloader_crc, dual_input / "bootloader_crc.bin")
    for role, artifact_name, output_name in (
            ("bl1", "bl.elf", "bootloader.elf"),
            ("bl2", "bl2.elf", "bl2.elf")):
        shutil.copyfile(
            value["roles"][role]["artifacts"][artifact_name]["path"],
            dual_input / output_name)
    for source_name, target_name in (
            ("mcuboot_pair.json", "mcuboot_pair.json"),
            ("bl1-manifest-primary.bin", "bl1-manifest-primary.bin"),
            ("bl1-manifest-secondary.bin", "bl1-manifest-secondary.bin"),
            ("boot_bl1_manifest_key.c", "boot_bl1_manifest_key.c"),
            ("bootloader_crc.json", "bootloader_crc.json")):
        source = (pair_output / source_name
                  if source_name == "mcuboot_pair.json" else
                  delivery_root / "payload" / source_name)
        if not source.exists():
            source = delivery_root / "manifests" / source_name
        if source.is_file():
            shutil.copyfile(source, dual_input / target_name)
    for role, config_name in (("cp", "nuttx-cp.config"), ("ap", "nuttx-ap.config")):
        shutil.copyfile(delivery_root / "work" / role / ".config", package_source / config_name)
    cp_profile_name = plan["legacy_adapter"]["seed_profiles"]["cp"]["target_profile"]
    ap_profile_name = plan["legacy_adapter"]["seed_profiles"]["ap"]["target_profile"]
    profile_lines = [
        f"PRODUCT_ID={plan['identity_inputs']['product']}",
        f"CP_CONFIG_NAME={cp_profile_name}",
        f"AP_CONFIG_NAME={ap_profile_name}",
        "PROFILE_SCHEMA=1",
        f"PHYSICAL_BOARD={plan['board']}",
        "PROFILE_BOOT=mcuboot", "CP_PROFILE_CLASS=bringup", "AP_PROFILE_CLASS=bringup",
        f"PARTITION_LAYOUT_SOURCE={partition_relative}",
        f"PARTITION_LAYOUT_ID={plan['partition_layout']['layout_id']}",
        f"PARTITION_LAYOUT_SHA256={plan['partition_layout']['layout_sha256']}",
        f"BUILD_PLAN_FILE=bk7258-build-plan.json",
        f"BUILD_PLAN_IDENTITY_SHA256={plan['identity_sha256']}",
        f"BK7258_SDK_BUNDLE_VERSION={plan['sdk']['versions']['cp']}",
        f"CP_SDK_BUNDLE_VERSION={plan['sdk']['versions']['cp']}",
        f"AP_SDK_BUNDLE_VERSION={plan['sdk']['versions']['ap']}",
        "MCUBOOT_PROFILE=true", "BL1_USE_BL2=true",
        f"MCUBOOT_VERSION={version}",
        f"MCUBOOT_SECURITY_COUNTER={security_counter}",
        "MCUBOOT_OFFICIAL_PIPELINE=NO", "MCUBOOT_SIGNING_KEY_REQUIRED=true",
        "TRUST_CHAIN_CONTRACT=bk7258-trust-chain.json",
        "TRUST_CHAIN_PREFLIGHT_REQUIRED=true", "BL1_MANIFEST_ENABLED=true",
        f"BL1_MANIFEST_FORMAT={manifest_format}", "BL1_MANIFEST_RAW_PAGE=false",
        f"BL2_LOGICAL_SIZE=0x{bl2_logical_size:x}",
        f"BL2_SECURITY_COUNTER_FLOOR={resolved_policy['policy']['version_policy']['bl2_security_counter_floor']['default']}",
        f"BL2_XIP_ADDRESS=0x{primary_xip:x}",
        f"BL2_SECONDARY_XIP_ADDRESS=0x{secondary_xip:x}",
        "BL2_LOAD_ADDRESS=0x28020000",
    ]
    (package_source / "build-profile.txt").write_text("\n".join(profile_lines) + "\n", encoding="utf-8")
    (package_source / "bk7258-build-plan.json").write_bytes(
        Path(value["plan_copy"]).read_bytes())
    run("dual-package", "python3", [
        python, str(source_tools / "pack_dual_image.py"),
        "--boot", str(bootloader_crc),
        "--cp-raw", str(dual_input / "app.bin"),
        "--cp-standard", str(dual_input / "cp-raw.bin"),
        "--cp-crc", str(dual_input / "app_crc.bin"),
        "--ap-raw", str(dual_input / "app1.bin"),
        "--ap-standard", str(dual_input / "ap-raw.bin"),
        "--ap-crc", str(dual_input / "app1_crc.bin"),
        "--bl2-primary-crc", str(dual_input / "bl2_crc.bin"),
        "--bl2-secondary-crc", str(dual_input / "bl2_secondary_crc.bin"),
        "--trust-chain", str(dual_input / "bk7258-trust-chain.json"),
        "--partition", str(partition_csv),
        "--expect-layout-id", plan["partition_layout"]["layout_id"],
        "--expect-layout-sha256", plan["partition_layout"]["layout_sha256"],
        "--output", str(package_source),
    ], package_source, {
        "BK7258_PARTITION_LAYOUT_ID": plan["partition_layout"]["layout_id"],
        "BK7258_PARTITION_LAYOUT_SHA256": plan["partition_layout"]["layout_sha256"],
    }, "package")
    package = delivery_root / "firmware.bkpack"
    run("bkpack-create", "python3", [
        python, str(source_tools / "bk7258_bkpack.py"), "create",
        "--source", str(package_source), "--partition", str(partition_csv),
        "--output", str(package),
    ], package_source, {
        "BK7258_PARTITION_LAYOUT_ID": plan["partition_layout"]["layout_id"],
        "BK7258_PARTITION_LAYOUT_SHA256": plan["partition_layout"]["layout_sha256"],
    }, "package")
    run("bkpack-verify", "python3", [
        python, str(source_tools / "bk7258_bkpack.py"), "verify", "--package", str(package),
    ], delivery_root, {
        "BK7258_PARTITION_LAYOUT_ID": plan["partition_layout"]["layout_id"],
        "BK7258_PARTITION_LAYOUT_SHA256": plan["partition_layout"]["layout_sha256"],
    }, "package")
    _verify_bkpack_standard_members(package)

    # Delivery tools execute from the immutable entity snapshot, but a child
    # process can still attempt chmod/write through the host account.  Do not
    # publish a terminal manifest until the post-command audit is still clean.
    _assert_delivery_snapshot_unchanged(
        source_root, value["source_view"]["snapshot_identity_sha256"],
        "delivery")

    delivery = {
        "schema": "bk7258.role-isolated-delivery/1",
        "version": DELIVERY_VERSION,
        "root": str(delivery_root),
        "package": str(package),
        "package_sha256": _digest_file(package),
        "package_size": package.stat(follow_symlinks=False).st_size,
        "plan_identity_sha256": plan["identity_sha256"],
        "snapshot_identity_sha256": value["source_view"]["snapshot_identity_sha256"],
        "layout": dict(plan["partition_layout"]),
        "authorization": {"sign": "PASS", "package": "PASS"},
        "stale_unsigned_intermediates_removed": ["all-app.bin", "nuttx_crc.bin"],
        "parameters": {
            "version": version,
            "security_counter": security_counter,
            "python": python,
            "bash": str(bash_path),
            "safe_path": safe_path,
            "log_prefix": log_prefix,
        },
        "commands": commands,
        "tools": tool_records,
        "artifacts": _delivery_artifact_index(delivery_root),
    }
    updated = dict(value)
    updated["delivery"] = delivery
    return _write_delivery_manifest(updated, manifest_path, build_root)


build_delivery = deliver


def prepare_delivery(
    repository: Path,
    manifest: Path | dict[str, Any],
    *,
    python_executable: str | Path = Path(sys.executable).resolve(),
    command_runner: Callable[..., Any] | None = None,
) -> dict[str, Any]:
    """Run the keyless delivery/postbuild checkpoint.

    BL1 is CRC-packed without manifests, BL2 is CRC-expanded, and existing
    ``postbuild.sh`` produces unsigned CP/AP raw+CRC artifacts.  No key path
    is accepted or read and no package is created.  The returned
    ``delivery-prepared`` manifest is the only input accepted by the later
    explicitly authorized signing/package phase besides a fresh runtime-built
    manifest.
    """
    repository = _safe_path(repository, "repository")
    _reject_traversal(repository, "repository")
    _reject_existing_symlink_components(repository, "repository")
    if repository.is_symlink() or not repository.is_dir():
        raise RuntimeDeliveryError("repository must be a real directory")
    repository = repository.resolve(strict=True)
    value, manifest_path = _manifest_mapping(manifest)
    _validate_manifest(value)
    if manifest_path is None:
        raise RuntimeDeliveryError("prepare-delivery requires a persisted runtime manifest")
    if value["phase"] != "runtime-built" or value["execution_mode"] not in {
            "compile-runtime", "build-runtime"}:
        raise RuntimeDeliveryError("prepare-delivery requires a runtime-built manifest")
    if value["boot"] != "mcuboot" or value["active_roles"] != list(ROLES):
        raise RuntimeDeliveryError(
            "raw/non-MCUboot delivery is not applicable and is fail-closed")
    build_root = _safe_path(Path(value["build_root"]), "build_root")
    _reject_traversal(build_root, "build_root")
    if build_root.is_symlink() or not build_root.is_dir():
        raise RuntimeDeliveryError("runtime build_root is not a real directory")
    build_root = build_root.resolve(strict=True)
    if build_root == repository or _inside(build_root, repository):
        raise RuntimeDeliveryError("delivery build_root is inside repository")
    source_root = _manifest_path_inside_build_root(
        Path(value["source_view"]["root"]), build_root,
        "source_view.root", allow_existing=True)
    if source_root != build_root / SOURCE_SNAPSHOT_DIRNAME:
        raise RuntimeDeliveryError("delivery source root is not canonical")
    plan = _audit_plan_copy(value)
    resolved_policy = _resolve_policy_for_plan(repository, plan)
    if _manifest_boot_policy(resolved_policy, plan) != value["boot_policy"]:
        raise RuntimeDeliveryError("delivery boot policy reconciliation changed")
    audited = _audit_materialized_snapshot(source_root)
    if audited.get("identity_sha256") != value["source_view"]["snapshot_identity_sha256"]:
        raise RuntimeDeliveryError("delivery source snapshot identity changed")
    _verify_snapshot_inputs(value, source_root)
    _assert_snapshot_read_only(source_root)
    for role in ROLES:
        row = value["roles"][role]
        if role in RUNTIME_ROLES:
            _validate_runtime_artifacts(row["artifacts"], Path(row["build_root"]), role)
        else:
            _validate_boot_artifacts(row["artifacts"], Path(row["build_root"]), role)

    delivery_root = build_root / "delivery"
    if delivery_root.exists() or delivery_root.is_symlink():
        raise RuntimeDeliveryError("delivery root must be fresh and absent")
    delivery_root.mkdir()
    for name in ("payload", "work", "manifests", "dual-input", "package-source",
                 "inputs", "logs"):
        (delivery_root / name).mkdir()
    source_repository = source_root / "contest2026_135_yongwangzhiqian"
    source_board = source_repository / "board/bk7258"
    source_tools = source_repository / "tools/bk7258"
    scripts = source_board / "scripts"
    bootloader = source_board / "bootloader"
    partition_relative = plan["partition_layout"]["source"]
    if partition_relative != "board/bk7258/partitions/bk7258/auto_partitions.csv":
        raise RuntimeDeliveryError(
            "delivery BL1 packer only supports the explicit auto_partitions.csv contract")
    partition_csv = source_board / partition_relative.removeprefix("board/bk7258/")
    if partition_csv.is_symlink() or not partition_csv.is_file():
        raise RuntimeDeliveryError("delivery partition source is missing")
    staged_partition = delivery_root / "inputs/partition-layout.csv"
    staged_partition.write_bytes(partition_csv.read_bytes())
    os.chmod(staged_partition, 0o400)
    python_path = _safe_path(Path(python_executable), "python_executable")
    _reject_existing_symlink_components(python_path, "python_executable")
    if python_path.is_symlink() or not python_path.is_file() or \
            not (python_path.stat(follow_symlinks=False).st_mode & 0o111):
        raise RuntimeDeliveryError("python_executable is not a normal executable")
    safe_path = _tool_search_path(str(python_path), "bash")
    bash_path = _resolve_tool("bash", "bash", safe_path)
    python = str(python_path)
    base_env = _delivery_environment(
        delivery_root, value["source_view"], safe_path,
        layout_source=partition_csv,
        layout_id=plan["partition_layout"]["layout_id"],
        layout_sha256=plan["partition_layout"]["layout_sha256"])
    tool_records = _delivery_tool_records(source_board, source_tools)
    commands: list[dict[str, Any]] = []
    command_index = 0

    def run(stage: str, tool: str, actual: list[str], cwd: Path,
            env_updates: dict[str, str] | None = None) -> None:
        nonlocal command_index
        env = dict(base_env)
        env.update(env_updates or {})
        record = _make_delivery_command(stage, tool, actual, cwd, env, "none")
        log = delivery_root / "logs" / f"{command_index:02d}-{stage}.log"
        command_index += 1
        _run_delivery_command(record, actual, env, cwd, log, runner=command_runner)
        commands.append(record)

    for role in RUNTIME_ROLES:
        row = value["roles"][role]
        work = delivery_root / "work" / role
        _copy_verified_runtime_input(
            Path(row["artifacts"]["nuttx.bin"]["path"]),
            row["artifacts"]["nuttx.bin"], work / "nuttx.bin", role, "nuttx.bin")
        _copy_verified_runtime_input(
            Path(row["config_path"]), row["artifacts"][".config"],
            work / ".config", role, ".config")
        contract = Path(row["partition_contract_root"])
        if contract.is_symlink() or not contract.is_dir():
            raise RuntimeDeliveryError(f"{role} partition contract is missing")
        shutil.copytree(contract, work / "partition-contract", symlinks=False)

    bl2_geometry = resolved_policy["bl2_geometry"]
    primary_xip = bl2_geometry["primary"]["xip_start"]
    bl2_capacity = bl2_geometry["primary"]["logical_size"]
    bl2_logical_size = plan["bl2_image_logical_size"]
    bl2_raw = Path(value["roles"]["bl2"]["artifacts"]["bl2.bin"]["path"])
    bl2_staged = delivery_root / "payload/bl2.bin"
    _copy_verified_runtime_input(
        bl2_raw, value["roles"]["bl2"]["artifacts"]["bl2.bin"], bl2_staged,
        "bl2", "bl2.bin")
    bl1_raw = Path(value["roles"]["bl1"]["artifacts"]["bl.bin"]["path"])
    bl1_staged = delivery_root / "payload/bl1-raw.bin"
    _copy_verified_runtime_input(
        bl1_raw, value["roles"]["bl1"]["artifacts"]["bl.bin"], bl1_staged,
        "bl1", "bl.bin")
    bootloader_crc = delivery_root / "payload/bootloader_crc.bin"
    run("bl1-pack", "python3", [
        python, str(bootloader / "bk7236_pack_min_bootloader.py"),
        "--in", str(bl1_staged), "--out", str(bootloader_crc),
    ], delivery_root / "payload", {
        "BK7258_PARTITION_LAYOUT_ID": plan["partition_layout"]["layout_id"],
        "BK7258_PARTITION_LAYOUT_SHA256": plan["partition_layout"]["layout_sha256"],
    })
    bl2_crc = delivery_root / "payload/bl2_crc.bin"
    run("bl2-pack", "python3", [
        python, str(scripts / "bk7258_crc_expand.py"), "--in", str(bl2_staged),
        "--out", str(bl2_crc), "--xip-base", hex(primary_xip),
        "--execution-base", hex(0x28020000), "--max-size", hex(bl2_capacity),
        "--pad-size", hex(bl2_logical_size),
    ], delivery_root / "payload", {
        "BK7258_PARTITION_LAYOUT_ID": plan["partition_layout"]["layout_id"],
        "BK7258_PARTITION_LAYOUT_SHA256": plan["partition_layout"]["layout_sha256"],
    })
    bl2_secondary_crc = delivery_root / "payload/bl2_secondary_crc.bin"
    bl2_secondary_crc.write_bytes(bl2_crc.read_bytes())
    os.chmod(bl2_secondary_crc, 0o400)
    for role in RUNTIME_ROLES:
        work = delivery_root / "work" / role
        run(f"postbuild-{role}", "bash", [
            str(bash_path), str(scripts / "postbuild.sh"), str(work),
            str(source_board), role,
        ], work, {
            "BK7258_POSTBUILD_MODE": "isolated",
            "BK7258_POSTBUILD_ARTIFACT_ROOT": str(delivery_root / "payload"),
            "BK7258_POSTBUILD_DUAL_ROLE": "1",
            "BK7258_PARTITION_CONTRACT_ROOT": str(work / "partition-contract"),
            "BK7258_PARTITION_LAYOUT_SOURCE": str(partition_csv),
            "BK7258_PARTITION_LAYOUT_ID": plan["partition_layout"]["layout_id"],
            "BK7258_PARTITION_LAYOUT_SHA256": plan["partition_layout"]["layout_sha256"],
            "BK7258_BL1_CRC_BIN": str(bootloader_crc),
            "BK7258_ROLE": role,
        })
    _write_dual_nuttx_manifest(delivery_root / "payload")
    for required_name in (
            "bootloader_crc.bin", "bl2_crc.bin", "bl2_secondary_crc.bin",
            "app.bin", "app_crc.bin", "app1.bin", "app1_crc.bin",
            "vela_nuttx_cp.bin", "vela_nuttx_ap.bin",
            "vela_nuttx_manifest.json"):
        required_path = delivery_root / "payload" / required_name
        if required_path.is_symlink() or not required_path.is_file() or \
                required_path.stat(follow_symlinks=False).st_size <= 0:
            raise RuntimeDeliveryError(
                f"keyless postbuild output is missing: {required_name}")
    # The unsigned checkpoint is also a command-execution boundary.  Verify
    # the entity snapshot before changing the manifest to delivery-prepared;
    # a successful child return code is not sufficient evidence of immutability.
    _assert_delivery_snapshot_unchanged(
        source_root, value["source_view"]["snapshot_identity_sha256"],
        "prepare-delivery")
    delivery_plan = {
        "schema": "bk7258.role-isolated-delivery-plan/1",
        "version": DELIVERY_VERSION,
        "root": str(delivery_root),
        "plan_identity_sha256": plan["identity_sha256"],
        "snapshot_identity_sha256": value["source_view"]["snapshot_identity_sha256"],
        "layout": dict(plan["partition_layout"]),
        "authorization": {"sign": "NOT_RUN", "package": "NOT_RUN"},
        "commands": commands,
        "tools": tool_records,
        "artifacts": _delivery_artifact_index(delivery_root, require_package=False),
    }
    return _write_delivery_plan_manifest(
        value, manifest_path, build_root, delivery_plan)


def prepare(repository: Path, product_id: str, build_root: Path,
            output: Path, *, plan_path: Path | None = None,
            workspace_root: Path | None = None) -> dict[str, Any]:
    """Prepare a canonical role-isolated execution manifest without building."""
    repository = _safe_path(repository, "repository")
    _reject_traversal(repository, "repository")
    _reject_existing_symlink_components(repository, "repository")
    if not repository.is_dir():
        raise IsolatedExecutorError("repository must be a real directory")
    repository = repository.resolve(strict=True)
    build_root = _fresh_build_root(build_root, repository)
    output = _prepare_output_path(output, build_root)
    workspace = _workspace_root(repository, workspace_root)
    if (_inside(build_root, workspace) or build_root == workspace or
            _inside(workspace, build_root)):
        raise IsolatedExecutorError(
            "build_root and workspace_root must be disjoint")
    board_source = repository / "board/bk7258"
    board_digest = _tree_digest(board_source)

    if plan_path is None:
        plan = framework.build_plan(repository, product_id)
        plan_path_copy = build_root / "bk7258-build-plan.json"
        plan_path_copy.write_bytes(_canonical(plan))
        verified_plan = framework.build_plan_verify(repository, plan_path_copy, product_id)
    else:
        supplied = _safe_path(plan_path, "plan")
        _reject_traversal(supplied, "plan")
        verified_plan = framework.build_plan_verify(repository, supplied, product_id)
        plan_path_copy = build_root / "bk7258-build-plan.json"
        plan_path_copy.write_bytes(_canonical(verified_plan))
        reloaded = framework.build_plan_verify(repository, plan_path_copy, product_id)
        if reloaded["identity_sha256"] != verified_plan["identity_sha256"]:
            raise IsolatedExecutorError("copied build plan identity changed")
    plan_copy_sha256 = _digest_file(plan_path_copy)
    if verified_plan["identity_sha256"] != framework.build_plan(
            repository, product_id, verified_plan["identity_inputs"]["board"],
            verified_plan["identity_inputs"]["mode"])["identity_sha256"]:
        raise IsolatedExecutorError("build plan identity is not current")

    resolved_policy = _resolve_policy_for_plan(repository, verified_plan)
    active_roles = list(verified_plan["active_roles"])

    paths = _role_paths(verified_plan, build_root)
    for role in ROLES:
        _new_directory(paths[role]["root"], f"role root {role}")
        paths[role]["artifact"].mkdir()
        paths[role]["partition_contract"].mkdir()
        (paths[role]["root"] / "config").mkdir()
        paths[role]["cmake_binary"].mkdir()

    # One source snapshot belongs to the entity, not to each role.  The
    # requirement marker is deliberately created at the build-root level so
    # the later materialization step can populate it exactly once.
    shared_source_view = _declare_source_view(
        repository, workspace, build_root / SOURCE_SNAPSHOT_DIRNAME)
    source_views: dict[str, dict[str, Any]] = {
        role: dict(shared_source_view) for role in ROLES
    }
    seeds = _materialize_seeds(verified_plan, repository, source_views, {role: row["root"] for role, row in paths.items()})

    roles: dict[str, dict[str, Any]] = {}
    for role in ROLES:
        item = verified_plan["roles"][role]
        role_active = role in active_roles
        roles[role] = {
            "backend": item["backend"],
            "build_root": str(paths[role]["root"]),
            "artifact_root": str(paths[role]["artifact"]),
            "config_path": str(paths[role]["config"]),
            "config_seed_root": str(paths[role]["root"] / "config"),
            "config_seed_profile": seeds[role]["target_profile"] if role in seeds else None,
            "cmake_board_config": seeds[role]["cmake_board_config"] if role in seeds else None,
            "partition_contract_root": str(paths[role]["partition_contract"]),
            "cmake_binary_root": str(paths[role]["cmake_binary"]),
            "bootloader_staging_root": (
                str(paths[role]["bootloader_staging"])
                if role in BOOT_ROLES and role_active else None),
            "source_view": dict(source_views[role]),
            "config_identity_sha256": item["config_identity_sha256"],
            "sdk_bundle": verified_plan["sdk"]["versions"][role] if role in RUNTIME_ROLES else None,
            "artifacts": {},
            "activation": "active" if role_active else "inactive",
            "applicability": "required" if role_active else "not-applicable",
            "commands": (_commands(
                verified_plan, role, paths[role], repository, source_views[role],
                seeds.get(role), paths[role]["bootloader_staging"]
                if role in BOOT_ROLES and role_active else None,
                resolved_policy=resolved_policy)
                if role_active else []),
        }
        (paths[role]["root"] / "prepare-input.json").write_bytes(_canonical({
            "schema": SCHEMA, "role": role,
            "build_plan_identity_sha256": verified_plan["identity_sha256"],
            "config_identity_sha256": item["config_identity_sha256"],
            "partition_layout": verified_plan["partition_layout"],
            "sdk_bundle": roles[role]["sdk_bundle"],
            "source_view": source_views[role],
            "seed": seeds.get(role),
        }))

    inputs = verified_plan["identity_inputs"]
    body: dict[str, Any] = {
        "schema": SCHEMA,
        "kind": KIND,
        "version": VERSION,
        "phase": "prepare",
        "execution_mode": "prepare-only",
        "product": inputs["product"],
        "board": inputs["board"],
        "mode": inputs["mode"],
        "boot": inputs["boot"],
        "active_roles": active_roles,
        "build_plan_identity_sha256": verified_plan["identity_sha256"],
        "build_root": str(build_root),
        "plan_copy": str(plan_path_copy),
        "plan_copy_sha256": plan_copy_sha256,
        "source_view": {
            "policy": "required-entity-snapshot-no-command-execution",
            "phase": "prepare",
            "root": shared_source_view["root"],
            "materialized": False,
            "snapshot_manifest": None,
            "snapshot_manifest_sha256": None,
            "snapshot_identity_sha256": None,
            "workspace_root": str(workspace),
            "workspace_head": _git_head(workspace),
            "repository_head": _git_head(repository),
            "board_source_digest": board_digest,
            "shared_nuttx_config_detected": any(
                row["source_view"]["shared_config_detected"]
                for row in roles.values()),
            "shared_nuttx_config_excluded": True,
            "shared_source_root_used": False,
            "roles": {role: row["source_view"] for role, row in roles.items()},
        },
        "identity_inputs": {
            "family": inputs["family"], "product": inputs["product"],
            "board": inputs["board"], "mode": inputs["mode"], "boot": inputs["boot"],
            "bl2_image_logical_size": inputs["bl2_image_logical_size"],
            "active_roles": active_roles,
            "partition_layout": dict(verified_plan["partition_layout"]),
            "sdk": {
                "set_id": verified_plan["sdk"]["set_id"],
                "lock_id": verified_plan["sdk"]["lock_id"],
                "lock_identity_sha256": verified_plan["sdk"]["lock_identity_sha256"],
                "versions": dict(verified_plan["sdk"]["versions"]),
            },
        },
        "boot_policy": _manifest_boot_policy(resolved_policy, verified_plan),
        "roles": roles,
        "side_effects": {
            "compile": "NOT_RUN", "sign": "NOT_RUN", "package": "NOT_RUN",
            "hardware": "NOT_RUN", "network": "NOT_RUN", "prepare_files_written": True,
        },
        "credentials": {"private_key_read": "NOT_RUN", "signing": "NOT_RUN"},
        "tools": {
            "python": sys.version.split()[0],
            "cmake": "NOT_RUN",
            "ninja": "NOT_RUN",
            "arm-none-eabi-gcc": "NOT_RUN",
            "olddefconfig": "NOT_RUN",
            "kconfiglib": "NOT_RUN",
            "make": "NOT_RUN",
        },
        "output": str(output),
    }
    manifest = dict(body)
    manifest["identity_sha256"] = _digest_bytes(_canonical(body))
    manifest = _validate_manifest(manifest)
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.is_symlink() or output.exists():
        raise IsolatedExecutorError(f"refusing to replace output manifest: {output}")
    output.write_bytes(_canonical(manifest))
    return manifest


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    commands = parser.add_subparsers(dest="command", required=True)
    prepare_parser = commands.add_parser("prepare")
    prepare_parser.add_argument("--product", required=True)
    prepare_parser.add_argument("--build-root", type=Path, required=True)
    prepare_parser.add_argument("--out", type=Path, required=True)
    prepare_parser.add_argument("--plan", type=Path)
    prepare_parser.add_argument("--workspace-root", type=Path)
    materialize_parser = commands.add_parser(
        "materialize-sources", allow_abbrev=False,
        help="materialize and audit the one entity source snapshot")
    materialize_parser.add_argument("--manifest", type=Path, required=True)
    materialize_parser.add_argument("--workspace-root", type=Path)
    runtime_parser = commands.add_parser(
        "compile-runtime", aliases=("build-runtime",), allow_abbrev=False,
        help="compile isolated CP/AP runtime targets from a materialized manifest")
    runtime_parser.add_argument("--manifest", type=Path, required=True)
    runtime_parser.add_argument(
        "--authorize-compile", action="store_true",
        help="explicitly authorize the policy-gated runtime compile phase")
    runtime_parser.add_argument("--cmake", "--cmake-executable", dest="cmake_executable",
                                default="cmake")
    runtime_parser.add_argument("--python", "--python-executable", dest="python_executable",
                                default=str(Path(sys.executable).resolve()))
    runtime_parser.add_argument("--olddefconfig", "--olddefconfig-executable",
                                dest="olddefconfig_executable", default="olddefconfig")
    runtime_parser.add_argument("--kconfiglib-root", type=Path)
    runtime_parser.add_argument("--make", "--make-executable", dest="make_executable",
                                default="make")
    postbuild_parser = commands.add_parser(
        "prepare-delivery", aliases=("postbuild-runtime",), allow_abbrev=False,
        help="run keyless BL1/BL2 CRC and CP/AP postbuild checkpoint")
    postbuild_parser.add_argument("--manifest", type=Path, required=True)
    postbuild_parser.add_argument("--python", "--python-executable",
                                  dest="postbuild_python_executable",
                                  default=str(Path(sys.executable).resolve()))
    delivery_parser = commands.add_parser(
        "deliver", aliases=("build-delivery",), allow_abbrev=False,
        help="sign, package and verify one MCUboot firmware.bkpack")
    delivery_parser.add_argument("--manifest", type=Path, required=True)
    delivery_parser.add_argument(
        "--authorize-sign", action="store_true",
        help="explicitly authorize private-key reads and signing")
    delivery_parser.add_argument(
        "--authorize-package", action="store_true",
        help="explicitly authorize package creation and verification")
    delivery_parser.add_argument("--mcuboot-signing-key", type=Path, required=False)
    delivery_parser.add_argument("--bl1-manifest-key", type=Path, required=False)
    delivery_parser.add_argument("--version", required=False)
    delivery_parser.add_argument("--security-counter", default="auto")
    delivery_parser.add_argument("--python", "--python-executable",
                                 dest="delivery_python_executable",
                                 default=str(Path(sys.executable).resolve()))
    return parser


def cli(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        if args.command == "prepare":
            manifest = prepare(args.root.resolve(), args.product, args.build_root,
                               args.out, plan_path=args.plan,
                               workspace_root=args.workspace_root)
            print(f"bk7258-isolated-executor: PREPARE PASS identity={manifest['identity_sha256']}")
        elif args.command == "materialize-sources":
            manifest = materialize_sources(
                args.root.resolve(), args.manifest,
                workspace_root=args.workspace_root)
            print(
                "bk7258-isolated-executor: MATERIALIZE-SOURCES PASS "
                f"identity={manifest['identity_sha256']} "
                f"snapshot={manifest['source_view']['snapshot_identity_sha256']}")
        elif args.command in {"compile-runtime", "build-runtime"}:
            manifest = compile_runtime(
                args.root.resolve(), args.manifest,
                authorize_compile=args.authorize_compile,
                cmake_executable=args.cmake_executable,
                python_executable=args.python_executable,
                olddefconfig_executable=args.olddefconfig_executable,
                kconfiglib_root=args.kconfiglib_root,
                make_executable=args.make_executable)
            print(
                "bk7258-isolated-executor: COMPILE-RUNTIME PASS "
                f"identity={manifest['identity_sha256']}"
            )
        elif args.command in {"prepare-delivery", "postbuild-runtime"}:
            manifest = prepare_delivery(
                args.root.resolve(), args.manifest,
                python_executable=args.postbuild_python_executable)
            print(
                "bk7258-isolated-executor: PREPARE-DELIVERY PASS "
                f"identity={manifest['identity_sha256']}"
            )
        elif args.command in {"deliver", "build-delivery"}:
            manifest = deliver(
                args.root.resolve(), args.manifest,
                authorize_sign=args.authorize_sign,
                authorize_package=args.authorize_package,
                mcuboot_signing_key=args.mcuboot_signing_key,
                bl1_manifest_key=args.bl1_manifest_key,
                version=args.version,
                security_counter=args.security_counter,
                python_executable=args.delivery_python_executable)
            print(
                "bk7258-isolated-executor: DELIVERY PASS "
                f"package={manifest['delivery']['package']} "
                f"sha256={manifest['delivery']['package_sha256']}"
            )
        return 0
    except (framework.FrameworkError, SnapshotError, OSError, ValueError,
            RuntimeDeliveryError) as error:
        print(f"bk7258-isolated-executor: FAIL: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(cli())
