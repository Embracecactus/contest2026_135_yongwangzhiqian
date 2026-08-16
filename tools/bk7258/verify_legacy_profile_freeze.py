#!/usr/bin/env python3
"""Fail-closed BK7258 legacy profile freeze and migration audit.

The approved Git object is the only baseline.  Generation first reconstructs
the complete configuration tree from that object and then verifies that the
current checkout is byte-for-byte identical.  A dirty checkout therefore
cannot be used to regenerate a new baseline manifest.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import stat
import subprocess
import sys
from pathlib import Path
from typing import Any, Iterable

from bk7258_paths import Bk7258Layout


APPROVED_BASE_COMMIT = "2eb0353ee6989e6654629aa0b67cac8c7c1ee810"
CONFIGS_REL = Path("board/bk7258/configs")
CUTOVER_README_REL = (CONFIGS_REL / "README.md").as_posix()
CUTOVER_SEED_FILE_NAMES = ("defconfig", "profile.conf")
MANIFEST_REL = Path("tools/bk7258/legacy_profile_freeze_manifest.json")
LEDGER_REL = Path("tools/bk7258/legacy_profile_migration_ledger.json")
INVENTORY_REL = Path("tools/bk7258/legacy_profile_consumers.json")
MANIFEST_SCHEMA = 1
CUTOVER_MANIFEST_SCHEMA = 2
CUTOVER_STATUS = "cutover-approved"
ROOT_MODE = "0755"
DIR_MODE = "0755"
FILE_MODE = {"100644": "0644", "100755": "0755"}
IDENT_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]*$")
HASH_RE = re.compile(r"^[0-9a-f]{64}$")
GIT_OBJECT_RE = re.compile(r"^[0-9a-f]{40}$")

PROFILE_REQUIRED = {
    "BK7258_PROFILE_SCHEMA",
    "BK7258_PROFILE_BOARD",
    "BK7258_PROFILE_ROLE",
    "BK7258_PROFILE_BOOT",
    "BK7258_PROFILE_CLASS",
    "BK7258_PROFILE_COMPAT",
}
PROFILE_OPTIONAL = {"BK7258_PROFILE_SDK_BUNDLE"}
PROFILE_ALLOWED = PROFILE_REQUIRED | PROFILE_OPTIONAL
PROFILE_VALUES = {
    "BK7258_PROFILE_BOARD": {"t5ai_core", "t5_board", "common"},
    "BK7258_PROFILE_ROLE": {"cp", "ap", "bl2"},
    "BK7258_PROFILE_BOOT": {"raw", "mcuboot"},
    "BK7258_PROFILE_CLASS": {"runnable", "validation", "ci", "infrastructure"},
}

MANIFEST_KEYS = {
    "schema", "kind", "status", "root", "root_entry", "rules", "baseline",
    "profile_count", "file_count", "entries", "profiles", "migration_ledger",
    "migration_ledger_sha256", "consumer_inventory", "manifest_sha256",
}
CUTOVER_MANIFEST_KEYS = {
    "schema", "kind", "status", "root", "root_entry", "rules", "baseline",
    "historical_profile_count", "retained_profiles", "retired_profiles",
    "profile_count", "file_count", "migration_ledger",
    "migration_ledger_sha256", "manifest_sha256",
}
BASELINE_KEYS = {
    "commit", "commit_tree", "configs_git_tree", "entries_sha256",
    "remote_ref", "remote_fetch_verified",
}
RULE_KEYS = {
    "profile_directory_count_may_only_decrease",
    "new_profile_directory_requires_architecture_adr",
    "new_driver_or_validator_profile_forbidden",
    "exact_case_and_type_are_part_of_identity",
    "all_file_bytes_are_sha256_pinned",
    "baseline_is_git_object_not_worktree",
}
ENTRY_KEYS = {"path", "type", "mode", "git_mode", "git_object", "sha256"}
PROFILE_KEYS = {"name", "metadata", "layout", "sdk", "pair", "defconfig_symbols"}
LAYOUT_KEYS = {"id", "partition_source"}
PAIR_KEYS = {"id", "members", "peers"}
SDK_KEYS = {
    "bundle", "upstream_version", "role", "bundle_profile", "sha256_manifest",
    "provenance", "provenance_sha256", "profile_inputs", "source_archive_state",
    "source_archive_sha256", "source_reproducibility", "object_replacements",
}
LEDGER_KEYS = {"schema", "kind", "status", "source_manifest", "rules", "rows"}
LEDGER_RULE_KEYS = {
    "row_count_is_exact", "every_legacy_profile_has_a_non_tbd_state",
    "targets_do_not_create_products_or_config_directories",
    "proposal_requires_p5_product_resource_review",
}
LEDGER_ROW_KEYS = {
    "legacy_profile", "board", "role", "boot", "class", "pair",
    "migration_state", "target", "review",
}
TARGET_KEYS = {
    "family", "resource_mode", "validation_suite", "role", "pair",
    "decision", "owner", "capabilities",
}


class FreezeError(RuntimeError):
    """Any mismatch that must stop the migration."""


def canonical_json(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def repo_root() -> Path:
    # The freeze reads approved blobs out of Git history, so this must be the
    # contest repository root regardless of how the tools are checked out.
    return Bk7258Layout().contest_root


def relative_path(path: Path, root: Path) -> str:
    return path.relative_to(root).as_posix()


def reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise FreezeError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(
            path.read_text(encoding="utf-8"),
            object_pairs_hook=reject_duplicate_keys,
        )
    except FreezeError:
        raise
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise FreezeError(f"cannot load JSON: {path}") from error
    if not isinstance(value, dict):
        raise FreezeError(f"JSON root must be an object: {path}")
    return value


def git_bytes(root: Path, *args: str) -> bytes:
    try:
        return subprocess.check_output(
            ["git", "-C", str(root), *args],
            stderr=subprocess.PIPE,
        )
    except (OSError, subprocess.CalledProcessError) as error:
        raise FreezeError(f"Git object query failed: {' '.join(args)}") from error


def git_text(root: Path, *args: str) -> str:
    return git_bytes(root, *args).decode("utf-8").strip()


def verify_approved_base(root: Path) -> dict[str, str]:
    resolved = git_text(root, "rev-parse", f"{APPROVED_BASE_COMMIT}^{{commit}}")
    if resolved != APPROVED_BASE_COMMIT:
        raise FreezeError("approved base commit does not resolve to the owner-approved SHA")
    return {
        "commit": resolved,
        "commit_tree": git_text(root, "rev-parse", f"{resolved}^{{tree}}"),
        "configs_git_tree": git_text(root, "rev-parse", f"{resolved}:{CONFIGS_REL.as_posix()}"),
    }


def ensure_safe_identifier(value: Any, field: str) -> str:
    if not isinstance(value, str) or not IDENT_RE.fullmatch(value):
        raise FreezeError(f"unsafe identifier in {field}")
    return value


def ensure_hash(value: Any, field: str) -> str:
    if not isinstance(value, str) or not HASH_RE.fullmatch(value):
        raise FreezeError(f"invalid SHA-256 in {field}")
    return value


def ensure_git_object(value: Any, field: str) -> str:
    if not isinstance(value, str) or not GIT_OBJECT_RE.fullmatch(value):
        raise FreezeError(f"invalid Git object in {field}")
    return value


def ensure_relative_repo_path(value: Any, field: str) -> str:
    if (
        not isinstance(value, str)
        or not value
        or "\\" in value
        or "//" in value
        or any(char.isspace() for char in value)
    ):
        raise FreezeError(f"unsafe repository path in {field}")
    path = Path(value)
    if path.is_absolute() or ".." in path.parts or "." in path.parts:
        raise FreezeError(f"path traversal in {field}")
    return value


def ensure_object(value: Any, field: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise FreezeError(f"{field} must be an object")
    return value


def ensure_list(value: Any, field: str) -> list[Any]:
    if not isinstance(value, list):
        raise FreezeError(f"{field} must be an array")
    return value


def kind_for_mode(mode: int) -> str:
    if stat.S_ISREG(mode):
        return "file"
    if stat.S_ISDIR(mode):
        return "directory"
    if stat.S_ISLNK(mode):
        return "symlink"
    if stat.S_ISFIFO(mode):
        return "fifo"
    if stat.S_ISSOCK(mode):
        return "socket"
    if stat.S_ISBLK(mode):
        return "block"
    if stat.S_ISCHR(mode):
        return "character"
    return "special"


def assert_configs_root(root: Path) -> None:
    path = root / CONFIGS_REL
    try:
        info = path.lstat()
    except OSError as error:
        raise FreezeError(f"missing configuration root: {CONFIGS_REL}") from error
    if stat.S_ISLNK(info.st_mode) or not stat.S_ISDIR(info.st_mode):
        raise FreezeError("configuration root must be a real directory")
    if stat.S_IMODE(info.st_mode) != int(ROOT_MODE, 8):
        raise FreezeError("configuration root mode must be 0755")


def walk_current_entries(root: Path) -> list[dict[str, Any]]:
    assert_configs_root(root)
    directory = root / CONFIGS_REL
    entries: list[dict[str, Any]] = []

    def visit(current: Path) -> None:
        try:
            children = sorted(os.scandir(current), key=lambda item: item.name)
        except OSError as error:
            raise FreezeError(f"cannot scan {relative_path(current, root)}") from error
        for child in children:
            path = Path(child.path)
            try:
                info = path.lstat()
            except OSError as error:
                raise FreezeError(f"cannot stat {relative_path(path, root)}") from error
            kind = kind_for_mode(info.st_mode)
            entry: dict[str, Any] = {
                "path": relative_path(path, root),
                "type": kind,
                "mode": format(stat.S_IMODE(info.st_mode), "04o"),
            }
            if kind == "file":
                entry["sha256"] = sha256_file(path)
            elif kind == "symlink":
                entry["target"] = os.readlink(path)
            entries.append(entry)
            if kind == "directory":
                visit(path)

    visit(directory)
    return entries


def parse_git_tree(root: Path, base: dict[str, str]) -> tuple[list[dict[str, Any]], dict[str, bytes]]:
    raw = git_bytes(
        root, "ls-tree", "-r", "-z", "--full-tree", base["commit"], "--",
        CONFIGS_REL.as_posix(),
    )
    blobs: dict[str, bytes] = {}
    file_rows: dict[str, dict[str, str]] = {}
    for record in raw.split(b"\0"):
        if not record:
            continue
        try:
            metadata, path_bytes = record.split(b"\t", 1)
            mode, object_type, object_id = metadata.decode("ascii").split(" ")
            path = path_bytes.decode("utf-8")
        except (UnicodeError, ValueError) as error:
            raise FreezeError("malformed approved Git tree entry") from error
        if object_type != "blob" or mode not in FILE_MODE:
            raise FreezeError(f"approved tree contains unsupported entry: {path}")
        if not path.startswith(CONFIGS_REL.as_posix() + "/"):
            raise FreezeError(f"approved tree escaped configuration root: {path}")
        ensure_relative_repo_path(path, "Git tree path")
        data = git_bytes(root, "show", f"{base['commit']}:{path}")
        blobs[path] = data
        file_rows[path] = {"git_mode": mode, "git_object": object_id}
    if not file_rows:
        raise FreezeError("approved Git tree contains no configuration files")

    directories: set[str] = set()
    for path in file_rows:
        parent = Path(path).parent
        while parent.as_posix() != CONFIGS_REL.as_posix():
            directories.add(parent.as_posix())
            parent = parent.parent
    entries: list[dict[str, Any]] = []
    for path in sorted(directories):
        entries.append({
            "path": path,
            "type": "directory",
            "mode": DIR_MODE,
            "git_mode": "040000",
        })
    for path in sorted(file_rows):
        row = file_rows[path]
        entries.append({
            "path": path,
            "type": "file",
            "mode": FILE_MODE[row["git_mode"]],
            "git_mode": row["git_mode"],
            "git_object": row["git_object"],
            "sha256": sha256_bytes(blobs[path]),
        })
    return entries, blobs


def entry_digest(root_entry: dict[str, Any], entries: Iterable[dict[str, Any]]) -> str:
    return sha256_bytes(canonical_json({"root_entry": root_entry, "entries": list(entries)}))


def parse_profile_bytes(data: bytes, label: str) -> dict[str, str]:
    values: dict[str, str] = {}
    try:
        lines = data.decode("utf-8").splitlines()
    except UnicodeDecodeError as error:
        raise FreezeError(f"metadata is not UTF-8: {label}") from error
    for line_number, line in enumerate(lines, 1):
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            raise FreezeError(f"malformed metadata {label}:{line_number}")
        key, value = line.split("=", 1)
        if key not in PROFILE_ALLOWED or not value or key in values:
            raise FreezeError(f"invalid or duplicate metadata {label}:{line_number}")
        if any(char.isspace() for char in value):
            raise FreezeError(f"metadata contains whitespace {label}:{line_number}")
        if key != "BK7258_PROFILE_SCHEMA":
            ensure_safe_identifier(value, f"{label}:{key}")
        values[key] = value
    missing = sorted(PROFILE_REQUIRED - values.keys())
    if missing or set(values) - PROFILE_ALLOWED:
        raise FreezeError(f"metadata fields are incomplete: {label}")
    if values["BK7258_PROFILE_SCHEMA"] != "1":
        raise FreezeError(f"unsupported metadata schema: {label}")
    for key, allowed in PROFILE_VALUES.items():
        if values[key] not in allowed:
            raise FreezeError(f"invalid {key} in {label}")
    if values["BK7258_PROFILE_ROLE"] == "bl2" and values["BK7258_PROFILE_BOARD"] != "common":
        raise FreezeError(f"BL2 must use common board: {label}")
    if values["BK7258_PROFILE_ROLE"] != "bl2" and values["BK7258_PROFILE_BOARD"] == "common":
        raise FreezeError(f"CP/AP cannot use common board: {label}")
    return values


def parse_profile(path: Path) -> dict[str, str]:
    return parse_profile_bytes(path.read_bytes(), str(path))


def defconfig_symbols_bytes(data: bytes, label: str) -> dict[str, str | None]:
    try:
        lines = data.decode("utf-8").splitlines()
    except UnicodeDecodeError as error:
        raise FreezeError(f"defconfig is not UTF-8: {label}") from error
    symbols: dict[str, str | None] = {}
    for line in lines:
        if line.startswith("# ") and line.endswith(" is not set"):
            symbols[line[2:-8]] = None
        elif line.startswith("CONFIG_") and "=" in line:
            key, value = line.split("=", 1)
            symbols[key] = value
    return symbols


def parse_key_values(data: bytes, label: str) -> dict[str, str]:
    result: dict[str, str] = {}
    try:
        lines = data.decode("utf-8").splitlines()
    except UnicodeDecodeError as error:
        raise FreezeError(f"provenance is not UTF-8: {label}") from error
    for line_number, line in enumerate(lines, 1):
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            raise FreezeError(f"malformed provenance {label}:{line_number}")
        key, value = line.split("=", 1)
        if not key or key in result:
            raise FreezeError(f"duplicate provenance key {label}:{line_number}")
        result[key] = value
    return result


def blob_for(path: str, blobs: dict[str, bytes], root: Path | None = None, base: str | None = None) -> bytes:
    if path in blobs:
        return blobs[path]
    if root is None or base is None:
        raise FreezeError(f"missing approved SDK object: {path}")
    data = git_bytes(root, "show", f"{base}:{path}")
    blobs[path] = data
    return data


def sdk_for(metadata: dict[str, str], blobs: dict[str, bytes], root: Path, base: str) -> dict[str, Any]:
    role = metadata["BK7258_PROFILE_ROLE"]
    if role == "bl2":
        return {
            "bundle": None,
            "upstream_version": None,
            "role": "bl2",
            "bundle_profile": None,
            "sha256_manifest": None,
            "provenance": None,
            "provenance_sha256": None,
            "profile_inputs": [],
            "source_archive_state": "not-applicable",
            "source_archive_sha256": None,
            "source_reproducibility": "not-applicable",
            "object_replacements": {},
        }
    bundle = metadata.get("BK7258_PROFILE_SDK_BUNDLE", "v3.1.1.9")
    if role == "cp" and bundle != "v3.1.1.9":
        raise FreezeError(f"CP profile uses unsupported SDK bundle: {bundle}")
    if role == "ap" and bundle not in {"v3.1.1.9", "v3.1.1.9-sdio4"}:
        raise FreezeError(f"AP profile uses unsupported SDK bundle: {bundle}")
    sdk_dir = f"board/bk7258/scripts/sdk-manifests/{bundle}"
    manifest_path = f"{sdk_dir}/{role}.sha256"
    provenance_path = f"{sdk_dir}/{role}.provenance"
    manifest_data = blob_for(manifest_path, blobs, root, base)
    provenance_data = blob_for(provenance_path, blobs, root, base)
    provenance = parse_key_values(provenance_data, provenance_path)
    if provenance.get("bundle_version") != bundle or provenance.get("role") != role:
        raise FreezeError(f"SDK provenance disagrees with profile: {bundle}/{role}")
    profile_inputs: list[dict[str, str]] = []
    if role == "ap":
        profile_inputs.append({
            "path": "board/bk7258/bk_idk/sdk-profiles/v3.1.1.9/ap-peripherals-r2.config",
            "sha256": sha256_bytes(blob_for(
                "board/bk7258/bk_idk/sdk-profiles/v3.1.1.9/ap-peripherals-r2.config",
                blobs, root, base,
            )),
        })
        if bundle == "v3.1.1.9-sdio4":
            profile_inputs.append({
                "path": "board/bk7258/bk_idk/sdk-profiles/v3.1.1.9/ap-sdio4.config",
                "sha256": sha256_bytes(blob_for(
                    "board/bk7258/bk_idk/sdk-profiles/v3.1.1.9/ap-sdio4.config",
                    blobs, root, base,
                )),
            })
    archive_state = (
        "recorded-with-sha256"
        if role == "cp" and provenance.get("source_archive_sha256") not in {None, "not-provided", "not-recorded"}
        else "not-provided"
    )
    archive_sha256 = provenance.get("source_archive_sha256", "not-provided")
    if role == "cp":
        ensure_hash(archive_sha256, f"SDK provenance source_archive_sha256 for {role}")
    elif archive_sha256 != "not-provided":
        raise FreezeError(f"AP SDK provenance must not claim a source archive: {bundle}")
    replacements = {
        key: provenance.get(key, "not-recorded")
        for key in (
            "uart_patch_define", "uart_patched_object_sha256",
            "libdriver_original_sha256", "libdriver_final_sha256",
            "final_manifest_sha256",
        )
    }
    for key, value in replacements.items():
        if key.endswith("sha256") and value not in {"not-recorded", "not-provided"}:
            ensure_hash(value, f"SDK provenance {key}")
    return {
        "bundle": bundle,
        "upstream_version": "v3.1.1.9",
        "role": role,
        "bundle_profile": provenance.get("bundle_profile", role),
        "sha256_manifest": sha256_bytes(manifest_data),
        "provenance": provenance_path,
        "provenance_sha256": sha256_bytes(provenance_data),
        "profile_inputs": profile_inputs,
        "source_archive_state": archive_state,
        "source_archive_sha256": archive_sha256,
        "source_reproducibility": "not-claimed",
        "object_replacements": replacements,
    }


def layout_for(metadata: dict[str, str]) -> dict[str, str]:
    role = metadata["BK7258_PROFILE_ROLE"]
    boot = metadata["BK7258_PROFILE_BOOT"]
    if role == "bl2":
        return {"id": "bk7258-bl2-minimal-v1", "partition_source": "none-bl2-makefile-owned"}
    if boot == "mcuboot":
        return {
            "id": "bk7258-secureboot-xip-cp-ap-v1",
            "partition_source": "board/bk7258/partitions/bk7258/secureboot_xip_cp_ap.csv",
        }
    return {
        "id": "bk7258-raw-cp-ap-v1",
        "partition_source": "board/bk7258/partitions/bk7258/auto_partitions.csv",
    }


def profile_records_from_base(
    root: Path, base: dict[str, str], entries: list[dict[str, Any]], blobs: dict[str, bytes],
) -> list[dict[str, Any]]:
    profile_dirs = sorted({
        str(Path(entry["path"]).parent)
        for entry in entries
        if entry["type"] == "file" and Path(entry["path"]).name == "profile.conf"
    })
    if len(profile_dirs) != 27:
        raise FreezeError(f"approved Git tree must contain exactly 27 profiles, found {len(profile_dirs)}")
    records: list[dict[str, Any]] = []
    for directory in profile_dirs:
        name = Path(directory).name
        if not re.fullmatch(r"[a-z0-9][a-z0-9_]*", name):
            raise FreezeError(f"unsafe profile directory name: {name}")
        profile_path = f"{directory}/profile.conf"
        defconfig_path = f"{directory}/defconfig"
        if profile_path not in blobs or defconfig_path not in blobs:
            raise FreezeError(f"profile must contain defconfig and profile.conf: {name}")
        metadata = parse_profile_bytes(blobs[profile_path], profile_path)
        symbols = defconfig_symbols_bytes(blobs[defconfig_path], defconfig_path)
        board = metadata["BK7258_PROFILE_BOARD"]
        role = metadata["BK7258_PROFILE_ROLE"]
        if board == "t5_board" and symbols.get("CONFIG_BK7258_BOARD_T5_BOARD") != "y":
            raise FreezeError(f"board metadata disagrees with defconfig: {name}")
        if board == "t5ai_core" and symbols.get("CONFIG_BK7258_BOARD_T5_BOARD") == "y":
            raise FreezeError(f"board metadata disagrees with defconfig: {name}")
        if role == "bl2" and symbols.get("CONFIG_BK7258_BL2_IMAGE") != "y":
            raise FreezeError(f"BL2 metadata disagrees with defconfig: {name}")
        if role == "ap" and symbols.get("CONFIG_BK7258_AP_CORE") != "y":
            raise FreezeError(f"AP metadata disagrees with defconfig: {name}")
        if role == "cp" and symbols.get("CONFIG_BK7258_AP_CORE") == "y":
            raise FreezeError(f"CP metadata disagrees with defconfig: {name}")
        if role != "bl2":
            mcuboot = symbols.get("CONFIG_BK7258_MCUBOOT_IMAGE") == "y"
            if (metadata["BK7258_PROFILE_BOOT"] == "mcuboot") != mcuboot:
                raise FreezeError(f"boot metadata disagrees with defconfig: {name}")
        records.append({
            "name": name,
            "metadata": metadata,
            "layout": layout_for(metadata),
            "sdk": sdk_for(metadata, blobs, root, base["commit"]),
            "pair": {},
            "defconfig_symbols": {
                key: symbols[key] for key in sorted(symbols)
                if key in {
                    "CONFIG_BK7258_BOARD_T5_BOARD", "CONFIG_BK7258_AP_CORE",
                    "CONFIG_BK7258_BL2_IMAGE", "CONFIG_BK7258_MCUBOOT_IMAGE",
                }
            },
        })
    by_compat: dict[str, list[dict[str, Any]]] = {}
    for record in records:
        by_compat.setdefault(record["metadata"]["BK7258_PROFILE_COMPAT"], []).append(record)
    for compat, members in by_compat.items():
        roles = [item["metadata"]["BK7258_PROFILE_ROLE"] for item in members]
        if roles == ["bl2"] and len(members) == 1:
            pass
        elif roles.count("cp") != 1 or roles.count("ap") < 1:
            raise FreezeError(f"compatibility group has no one-to-many CP/AP shape: {compat}")
        else:
            boards = {item["metadata"]["BK7258_PROFILE_BOARD"] for item in members}
            boots = {item["metadata"]["BK7258_PROFILE_BOOT"] for item in members}
            if len(boards) != 1 or len(boots) != 1:
                raise FreezeError(f"compatibility group crosses board or boot mode: {compat}")
        names = sorted(item["name"] for item in members)
        for item in members:
            item["pair"] = {
                "id": compat,
                "members": names,
                "peers": [name for name in names if name != item["name"]],
            }
    return records


def base_snapshot(root: Path) -> dict[str, Any]:
    base = verify_approved_base(root)
    entries, blobs = parse_git_tree(root, base)
    root_entry = {
        "path": CONFIGS_REL.as_posix(),
        "type": "directory",
        "mode": ROOT_MODE,
        "git_mode": "040000",
        "git_object": base["configs_git_tree"],
    }
    records = profile_records_from_base(root, base, entries, blobs)
    files = [entry for entry in entries if entry["type"] == "file"]
    return {
        "base": base,
        "root_entry": root_entry,
        "entries": entries,
        "files": files,
        "blobs": blobs,
        "profiles": records,
        "profile_count": len(records),
        "file_count": len(files),
        "entries_sha256": entry_digest(root_entry, entries),
    }


def assert_current_matches_snapshot(root: Path, snapshot: dict[str, Any]) -> list[dict[str, Any]]:
    actual = walk_current_entries(root)
    # Empty directories are not Git entries and can be left behind by a
    # filesystem-only checkout after their tracked files are removed.  They
    # are excluded from the commit identity; any non-empty retired directory
    # remains a hard failure below.
    actual_paths = {entry["path"] for entry in actual}
    actual = [
        entry for entry in actual
        if not (
            entry["type"] == "directory" and
            not any(path.startswith(entry["path"] + "/") for path in actual_paths)
        )
    ]
    expected = snapshot["entries"]
    expected_by_path = {entry["path"]: entry for entry in expected}
    actual_by_path = {entry["path"]: entry for entry in actual}
    if len(actual) != len(expected):
        raise FreezeError("current entry count differs from approved Git tree")
    missing = sorted(set(expected_by_path) - set(actual_by_path))
    extra = sorted(set(actual_by_path) - set(expected_by_path))
    if missing:
        raise FreezeError("current legacy tree is missing: " + ", ".join(missing))
    if extra:
        raise FreezeError("current legacy tree grew or changed case: " + ", ".join(extra))
    for path, expected_entry in expected_by_path.items():
        actual_entry = actual_by_path[path]
        if actual_entry.get("type") != expected_entry["type"]:
            raise FreezeError(f"legacy entry type changed: {path}")
        if actual_entry.get("mode") != expected_entry["mode"]:
            raise FreezeError(f"legacy entry mode changed: {path}")
        if expected_entry["type"] == "file" and actual_entry.get("sha256") != expected_entry["sha256"]:
            raise FreezeError(f"legacy file bytes changed: {path}")
    direct = [entry for entry in actual if "/" not in entry["path"][len(CONFIGS_REL.as_posix()) + 1:]]
    profile_count = sum(entry["type"] == "directory" for entry in direct)
    file_count = sum(entry["type"] == "file" for entry in actual)
    if profile_count != snapshot["profile_count"] or file_count != snapshot["file_count"]:
        raise FreezeError("current profile/file counts differ from approved Git tree")
    return actual


def manifest_without_digest(manifest: dict[str, Any]) -> dict[str, Any]:
    value = dict(manifest)
    value.pop("manifest_sha256", None)
    return value


def manifest_digest(manifest: dict[str, Any]) -> str:
    return sha256_bytes(canonical_json(manifest_without_digest(manifest)))


def _current_profile_names(entries: list[dict[str, Any]]) -> set[str]:
    prefix = CONFIGS_REL.as_posix() + "/"
    return {
        path[len(prefix):].split("/", 1)[0]
        for path in (entry["path"] for entry in entries)
        if path.startswith(prefix) and "/" in path[len(prefix):]
    }


def assert_current_matches_cutover(root: Path, snapshot: dict[str, Any],
                                   retained: list[str]) -> list[dict[str, Any]]:
    """Verify that only the approved three seed directories remain.

    Retained bytes are compared with the approved Git object.  Retired
    directories are intentionally absent and cannot be replaced by a new
    profile with a similar name.
    """
    actual = walk_current_entries(root)
    retained_set = set(retained)
    if len(retained_set) != 3 or retained_set != {
        "bl2_mcuboot", "t5ai_core_cp_base", "t5ai_core_ap_base"
    }:
        raise FreezeError("cutover must retain exactly the three canonical seed profiles")
    names = _current_profile_names(actual)
    if names != retained_set:
        missing = sorted(retained_set - names)
        extra = sorted(names - retained_set)
        detail = f" missing={missing}" if missing else ""
        detail += f" extra={extra}" if extra else ""
        raise FreezeError(f"current profile tree is not the approved 27-to-3 cutover{detail}")
    base_by_path = {entry["path"]: entry for entry in snapshot["entries"]}
    base_files = snapshot["blobs"]
    # ``git ls-tree -r`` only reports files, while ``parse_git_tree`` adds
    # their parent directories.  Select the cutover surface explicitly:
    # README plus the three retained directories and their two seed files.
    # In particular, do not select every direct child of ``configs/`` here;
    # that would accidentally make the 24 retired directory entries required
    # after the 27-to-3 cutover.
    prefix = CONFIGS_REL.as_posix() + "/"
    expected_paths = {
        path: entry
        for path, entry in base_by_path.items()
        if path == CUTOVER_README_REL or any(
            path == f"{prefix}{profile}" or
            path.startswith(f"{prefix}{profile}/")
            for profile in retained_set
        )
    }
    expected_file_paths = {
        path for path, entry in expected_paths.items() if entry["type"] == "file"
    }
    expected_seed_paths = {
        f"{CONFIGS_REL.as_posix()}/{profile}/{filename}"
        for profile in retained_set
        for filename in CUTOVER_SEED_FILE_NAMES
    }
    if expected_file_paths != expected_seed_paths | {CUTOVER_README_REL}:
        raise FreezeError("cutover must contain README plus six retained seed files")
    actual_by_path = {entry["path"]: entry for entry in actual}
    if set(actual_by_path) != set(expected_paths):
        missing = sorted(set(expected_paths) - set(actual_by_path))
        extra = sorted(set(actual_by_path) - set(expected_paths))
        raise FreezeError(f"retained seed tree differs from approved Git object: missing={missing} extra={extra}")
    for path, expected in expected_paths.items():
        observed = actual_by_path[path]
        if observed.get("type") != expected["type"] or observed.get("mode") != expected["mode"]:
            raise FreezeError(f"retained seed entry changed: {path}")
        if expected["type"] == "file" and path != CUTOVER_README_REL:
            if observed.get("sha256") != expected["sha256"]:
                raise FreezeError(f"retained seed bytes changed: {path}")
            if path not in base_files:
                raise FreezeError(f"approved retained seed blob is missing: {path}")
    return actual


def _validate_cutover_manifest_shape(manifest: dict[str, Any]) -> None:
    if set(manifest) != CUTOVER_MANIFEST_KEYS:
        raise FreezeError("cutover manifest schema keys are not exact")
    if (manifest["schema"] != CUTOVER_MANIFEST_SCHEMA or
            manifest["kind"] != "bk7258-profile-cutover-freeze" or
            manifest["status"] != CUTOVER_STATUS or
            manifest["root"] != CONFIGS_REL.as_posix()):
        raise FreezeError("cutover manifest status/root mismatch")
    root_entry = ensure_object(manifest["root_entry"], "cutover root entry")
    if set(root_entry) != {"path", "type", "mode", "git_mode", "git_object"}:
        raise FreezeError("cutover root entry schema mismatch")
    if root_entry["path"] != CONFIGS_REL.as_posix() or root_entry["type"] != "directory":
        raise FreezeError("cutover root entry is malformed")
    if root_entry["mode"] != ROOT_MODE or root_entry["git_mode"] != "040000":
        raise FreezeError("cutover root entry mode mismatch")
    ensure_git_object(root_entry["git_object"], "cutover root Git object")
    rules = ensure_object(manifest["rules"], "cutover rules")
    if set(rules) != {
        "approved_base_is_historical_only", "retained_seed_count_is_exact",
        "retired_profile_paths_are_forbidden", "new_config_directories_forbidden",
        "retained_seed_file_bytes_are_sha256_pinned",
    } or any(value is not True for value in rules.values()):
        raise FreezeError("cutover rules are malformed")
    baseline = ensure_object(manifest["baseline"], "cutover baseline")
    if set(baseline) != BASELINE_KEYS or baseline["commit"] != APPROVED_BASE_COMMIT:
        raise FreezeError("cutover baseline is not bound to the approved Git object")
    if baseline["remote_fetch_verified"] is not True:
        raise FreezeError("cutover baseline must record verified remote provenance")
    for key in ("commit_tree", "configs_git_tree"):
        ensure_git_object(baseline[key], f"cutover baseline {key}")
    ensure_hash(baseline["entries_sha256"], "cutover baseline entries digest")
    retained = ensure_list(manifest["retained_profiles"], "cutover retained profiles")
    retired = ensure_list(manifest["retired_profiles"], "cutover retired profiles")
    for name in retained + retired:
        ensure_safe_identifier(name, "cutover profile name")
    if (retained != ["bl2_mcuboot", "t5ai_core_cp_base", "t5ai_core_ap_base"] or
            len(set(retired)) != 24 or set(retained).intersection(retired)):
        raise FreezeError("cutover profile lists are not the approved 27-to-3 mapping")
    if (not isinstance(manifest["historical_profile_count"], int) or
            isinstance(manifest["historical_profile_count"], bool) or
            not isinstance(manifest["profile_count"], int) or
            isinstance(manifest["profile_count"], bool) or
            manifest["historical_profile_count"] != 27 or
            manifest["profile_count"] != 3):
        raise FreezeError("cutover profile counts are not 27 historical / 3 retained")
    if (not isinstance(manifest["file_count"], int) or
            isinstance(manifest["file_count"], bool) or manifest["file_count"] != 7):
        raise FreezeError("cutover file count must include the README and six retained seed files")
    if manifest["migration_ledger"] != LEDGER_REL.as_posix():
        raise FreezeError("cutover migration ledger path mismatch")
    ensure_hash(manifest["migration_ledger_sha256"], "cutover migration ledger digest")
    ensure_hash(manifest["manifest_sha256"], "cutover manifest digest")
    if manifest["manifest_sha256"] != manifest_digest(manifest):
        raise FreezeError("cutover manifest self-digest mismatch")


def validate_cutover_ledger(root: Path, expected_profiles: list[dict[str, Any]],
                            manifest_path: str = MANIFEST_REL.as_posix()) -> tuple[dict[str, Any], str]:
    """Validate the 27 historical rows without treating them as active files."""
    path = root / LEDGER_REL
    ledger = load_json(path)
    if ledger.get("status") not in {"cutover-approved", "proposal-only"}:
        raise FreezeError("migration ledger is not an approved cutover record")
    rows = ensure_list(ledger.get("rows"), "cutover migration rows")
    expected_names = {record["name"] for record in expected_profiles}
    names = {row.get("legacy_profile") for row in rows if isinstance(row, dict)}
    if len(rows) != 27 or names != expected_names:
        raise FreezeError("cutover migration ledger must cover the historical 27 profiles")
    allowed_states = {"shadow-equivalent", "retire-blocked-hardware", "consolidation-review"}
    for row in rows:
        if not isinstance(row, dict) or row.get("migration_state") not in allowed_states:
            raise FreezeError("cutover migration row has an unsupported state")
        target = row.get("target")
        if not isinstance(target, dict) or target.get("decision") not in {
            "approved-cutover", "proposal"
        }:
            raise FreezeError("cutover migration target is not explicit")
    return ledger, sha256_file(path)


def validate_ledger(root: Path, expected_profiles: list[dict[str, Any]], manifest_path: str = MANIFEST_REL.as_posix()) -> tuple[dict[str, Any], str]:
    path = root / LEDGER_REL
    try:
        info = path.lstat()
    except OSError as error:
        raise FreezeError("migration ledger is missing") from error
    if stat.S_ISLNK(info.st_mode) or not stat.S_ISREG(info.st_mode):
        raise FreezeError("migration ledger must be a regular non-symlink file")
    if stat.S_IMODE(info.st_mode) != 0o644:
        raise FreezeError("migration ledger mode must be 0644")
    ledger = load_json(path)
    if set(ledger) != LEDGER_KEYS:
        raise FreezeError("migration ledger schema keys are not exact")
    if ledger["schema"] != 1 or ledger["kind"] != "bk7258-legacy-profile-migration-ledger":
        raise FreezeError("migration ledger schema/kind mismatch")
    if ledger["status"] != "proposal-only" or ledger["source_manifest"] != manifest_path:
        raise FreezeError("migration ledger status/source binding mismatch")
    rules = ensure_object(ledger["rules"], "migration ledger rules")
    expected_rules = {
        "row_count_is_exact": True,
        "every_legacy_profile_has_a_non_tbd_state": True,
        "targets_do_not_create_products_or_config_directories": True,
        "proposal_requires_p5_product_resource_review": True,
    }
    if rules != expected_rules:
        raise FreezeError("migration ledger rules mismatch")
    rows = ensure_list(ledger["rows"], "migration ledger rows")
    if len(rows) != len(expected_profiles):
        raise FreezeError("migration ledger must contain exactly all profile rows")
    expected_by_name = {record["name"]: record for record in expected_profiles}
    seen: set[str] = set()
    for row in rows:
        if not isinstance(row, dict) or set(row) != LEDGER_ROW_KEYS:
            raise FreezeError("migration ledger row schema mismatch")
        name = row["legacy_profile"]
        if not isinstance(name, str) or name in seen or name not in expected_by_name:
            raise FreezeError("migration ledger profile coverage is not unique")
        seen.add(name)
        metadata = expected_by_name[name]["metadata"]
        for row_key, metadata_key in {
            "board": "BK7258_PROFILE_BOARD", "role": "BK7258_PROFILE_ROLE",
            "boot": "BK7258_PROFILE_BOOT", "class": "BK7258_PROFILE_CLASS",
        }.items():
            if row[row_key] != metadata[metadata_key]:
                raise FreezeError(f"migration ledger metadata mismatch: {name}/{row_key}")
        if row["pair"] != metadata["BK7258_PROFILE_COMPAT"]:
            raise FreezeError(f"migration ledger pair mismatch: {name}")
        if not isinstance(row["migration_state"], str) or row["migration_state"] != "consolidation-review":
            raise FreezeError(f"migration row is not explicit consolidation-review: {name}")
        if not isinstance(row["review"], str) or not row["review"].strip() or "TBD" in row["review"].upper():
            raise FreezeError(f"migration review is empty/TBD: {name}")
        target = row["target"]
        if not isinstance(target, dict) or set(target) != TARGET_KEYS:
            raise FreezeError(f"migration target schema mismatch: {name}")
        for key in ("family", "resource_mode", "role", "pair", "decision", "owner"):
            ensure_safe_identifier(target[key], f"migration target {name}/{key}")
        if target["role"] != row["role"] or target["pair"] != row["pair"]:
            raise FreezeError(f"migration target role/pair mismatch: {name}")
        if target["decision"] != "proposal" or target["owner"] != "P5-product-resource-review":
            raise FreezeError(f"migration target decision owner mismatch: {name}")
        suite = target["validation_suite"]
        if suite is not None:
            ensure_safe_identifier(suite, f"migration target {name}/validation_suite")
        if row["class"] in {"validation", "ci"} and suite is None:
            raise FreezeError(f"validation/CI row lacks a separate suite: {name}")
        if row["class"] not in {"validation", "ci"} and suite is not None:
            raise FreezeError(f"runnable/infrastructure row has a validation suite: {name}")
        capabilities = target["capabilities"]
        if not isinstance(capabilities, list) or any(
            not isinstance(value, str) or not IDENT_RE.fullmatch(value) for value in capabilities
        ):
            raise FreezeError(f"migration capabilities are malformed: {name}")
    if seen != set(expected_by_name):
        raise FreezeError("migration ledger does not cover all legacy profiles")
    return ledger, sha256_file(path)


def validate_manifest_shape(manifest: dict[str, Any]) -> None:
    ensure_object(manifest, "manifest")
    if set(manifest) != MANIFEST_KEYS:
        raise FreezeError("manifest schema keys are not exact")
    if manifest["schema"] != MANIFEST_SCHEMA or manifest["kind"] != "bk7258-legacy-profile-freeze":
        raise FreezeError("unsupported legacy freeze manifest")
    if manifest["status"] != "frozen-baseline" or manifest["root"] != CONFIGS_REL.as_posix():
        raise FreezeError("manifest status/root mismatch")
    root_entry = ensure_object(manifest["root_entry"], "manifest root entry")
    if set(root_entry) != {"path", "type", "mode", "git_mode", "git_object"}:
        raise FreezeError("manifest root entry schema mismatch")
    ensure_relative_repo_path(root_entry["path"], "manifest root")
    if root_entry != {
        "path": CONFIGS_REL.as_posix(), "type": "directory", "mode": ROOT_MODE,
        "git_mode": "040000", "git_object": root_entry["git_object"],
    }:
        raise FreezeError("manifest root entry is malformed")
    ensure_git_object(root_entry["git_object"], "manifest root Git object")
    rules = ensure_object(manifest["rules"], "manifest rules")
    if set(rules) != RULE_KEYS or any(value is not True for value in rules.values()):
        raise FreezeError("manifest freeze rules are malformed")
    baseline = ensure_object(manifest["baseline"], "manifest baseline")
    if set(baseline) != BASELINE_KEYS:
        raise FreezeError("manifest baseline schema mismatch")
    if baseline["commit"] != APPROVED_BASE_COMMIT or not isinstance(baseline["remote_ref"], str):
        raise FreezeError("manifest baseline commit/ref mismatch")
    if not baseline["remote_ref"] or any(char.isspace() for char in baseline["remote_ref"]):
        raise FreezeError("manifest baseline remote ref is malformed")
    if baseline["remote_fetch_verified"] is not True:
        raise FreezeError("manifest must record independently verified remote baseline")
    for key in ("commit_tree", "configs_git_tree"):
        ensure_git_object(baseline[key], f"manifest baseline {key}")
    ensure_hash(baseline["entries_sha256"], "manifest baseline entries_sha256")
    ensure_relative_repo_path(manifest["migration_ledger"], "manifest migration ledger")
    if manifest["migration_ledger"] != LEDGER_REL.as_posix():
        raise FreezeError("manifest migration ledger path mismatch")
    ensure_hash(manifest["migration_ledger_sha256"], "manifest migration ledger digest")
    inventory = ensure_object(manifest["consumer_inventory"], "manifest consumer inventory")
    if set(inventory) != {"path"} or inventory["path"] != INVENTORY_REL.as_posix():
        raise FreezeError("manifest consumer inventory binding mismatch")
    if (
        not isinstance(manifest["profile_count"], int)
        or isinstance(manifest["profile_count"], bool)
        or not isinstance(manifest["file_count"], int)
        or isinstance(manifest["file_count"], bool)
    ):
        raise FreezeError("manifest counts are malformed")
    entries = ensure_list(manifest["entries"], "manifest entries")
    profiles = ensure_list(manifest["profiles"], "manifest profiles")
    seen: set[str] = set()
    for entry in entries:
        if not isinstance(entry, dict) or set(entry) not in (
            {"path", "type", "mode", "git_mode"},
            {"path", "type", "mode", "git_mode", "git_object", "sha256"},
        ):
            raise FreezeError("manifest entry schema mismatch")
        path = ensure_relative_repo_path(entry["path"], "manifest entry")
        if path in seen or not path.startswith(CONFIGS_REL.as_posix() + "/"):
            raise FreezeError("manifest entry path is duplicate or outside root")
        seen.add(path)
        if not isinstance(entry["type"], str) or entry["type"] not in {"directory", "file"}:
            raise FreezeError("manifest baseline cannot contain symlink/special entry")
        if entry["type"] == "directory":
            if not isinstance(entry["mode"], str) or not isinstance(entry["git_mode"], str):
                raise FreezeError("manifest directory mode is malformed")
            if entry["mode"] != DIR_MODE or entry["git_mode"] != "040000":
                raise FreezeError("manifest directory mode mismatch")
            if set(entry) != {"path", "type", "mode", "git_mode"}:
                raise FreezeError("manifest directory has file fields")
        else:
            if not isinstance(entry["mode"], str) or not isinstance(entry["git_mode"], str):
                raise FreezeError("manifest file mode is malformed")
            if entry["mode"] not in {"0644", "0755"} or entry["git_mode"] not in FILE_MODE:
                raise FreezeError("manifest file mode mismatch")
            if entry["mode"] != FILE_MODE[entry["git_mode"]]:
                raise FreezeError("manifest file mode/Git mode mismatch")
            ensure_git_object(entry["git_object"], f"manifest Git object {path}")
            ensure_hash(entry["sha256"], f"manifest file {path}")
    profile_names: set[str] = set()
    for profile in profiles:
        if not isinstance(profile, dict):
            raise FreezeError("manifest profile schema mismatch")
        if set(profile) != PROFILE_KEYS:
            raise FreezeError("manifest profile schema mismatch")
        name = ensure_safe_identifier(profile["name"], "manifest profile name")
        if name in profile_names:
            raise FreezeError("manifest profile names are not unique")
        profile_names.add(name)
        metadata = ensure_object(profile["metadata"], "manifest profile metadata")
        if not isinstance(metadata, dict) or set(metadata) not in (PROFILE_REQUIRED, PROFILE_REQUIRED | PROFILE_OPTIONAL):
            raise FreezeError("manifest profile metadata schema mismatch")
        for key, value in metadata.items():
            if key != "BK7258_PROFILE_SCHEMA":
                ensure_safe_identifier(value, f"manifest profile metadata {key}")
        if profile["metadata"].get("BK7258_PROFILE_SCHEMA") != "1":
            raise FreezeError("manifest profile metadata schema version mismatch")
        for key, allowed in PROFILE_VALUES.items():
            if metadata[key] not in allowed:
                raise FreezeError(f"manifest profile metadata value is invalid: {key}")
        role = metadata["BK7258_PROFILE_ROLE"]
        bundle = metadata.get("BK7258_PROFILE_SDK_BUNDLE")
        if role == "bl2" and bundle is not None:
            raise FreezeError("BL2 profile cannot select a runtime SDK bundle")
        if role == "cp" and bundle not in {None, "v3.1.1.9"}:
            raise FreezeError("CP profile SDK bundle is not supported")
        if role == "ap" and bundle not in {None, "v3.1.1.9", "v3.1.1.9-sdio4"}:
            raise FreezeError("AP profile SDK bundle is not supported")
        layout = ensure_object(profile["layout"], "manifest profile layout")
        if set(layout) != LAYOUT_KEYS:
            raise FreezeError("manifest profile layout schema mismatch")
        ensure_safe_identifier(layout["id"], "manifest layout id")
        if layout["partition_source"] != "none-bl2-makefile-owned":
            ensure_relative_repo_path(layout["partition_source"], "manifest partition source")
        pair = ensure_object(profile["pair"], "manifest profile pair")
        if set(pair) != PAIR_KEYS:
            raise FreezeError("manifest profile pair schema mismatch")
        ensure_safe_identifier(pair["id"], "manifest pair id")
        members = ensure_list(pair["members"], "manifest pair members")
        peers = ensure_list(pair["peers"], "manifest pair peers")
        for value in members + peers:
            ensure_safe_identifier(value, "manifest pair member")
        if len(set(members)) != len(members) or len(set(peers)) != len(peers):
            raise FreezeError("manifest pair members/peers are not unique")
        symbols = ensure_object(profile["defconfig_symbols"], "manifest defconfig symbols")
        for key, value in symbols.items():
            if not isinstance(key, str) or not key.startswith("CONFIG_") or value not in {None, "y"}:
                raise FreezeError("manifest defconfig symbol malformed")
        sdk = ensure_object(profile["sdk"], "manifest SDK metadata")
        if not isinstance(sdk, dict) or set(sdk) != SDK_KEYS:
            raise FreezeError("manifest SDK metadata schema mismatch")
        if sdk["bundle"] is not None:
            ensure_safe_identifier(sdk["bundle"], "manifest SDK bundle")
            ensure_safe_identifier(sdk["upstream_version"], "manifest SDK upstream version")
            ensure_safe_identifier(sdk["role"], "manifest SDK role")
            ensure_safe_identifier(sdk["bundle_profile"], "manifest SDK profile")
            ensure_relative_repo_path(sdk["provenance"], "manifest SDK provenance")
            ensure_hash(sdk["sha256_manifest"], "manifest SDK manifest digest")
            ensure_hash(sdk["provenance_sha256"], "manifest SDK provenance digest")
        else:
            if any(sdk[key] is not None for key in (
                "upstream_version", "bundle_profile", "sha256_manifest",
                "provenance", "provenance_sha256", "source_archive_sha256",
            )):
                raise FreezeError("SDK metadata without a bundle is not null-complete")
        profile_inputs = ensure_list(sdk["profile_inputs"], "manifest SDK profile inputs")
        for item in profile_inputs:
            if not isinstance(item, dict):
                raise FreezeError("manifest SDK profile input malformed")
            if set(item) != {"path", "sha256"}:
                raise FreezeError("manifest SDK profile input malformed")
            ensure_relative_repo_path(item["path"], "manifest SDK profile input")
            ensure_hash(item["sha256"], "manifest SDK profile input digest")
        replacements = ensure_object(sdk["object_replacements"], "manifest SDK replacements")
        for key, value in replacements.items():
            ensure_safe_identifier(key, "manifest SDK replacement key")
            if not isinstance(value, str):
                raise FreezeError("manifest SDK replacement value malformed")
            if key.endswith("sha256") and value not in {"not-recorded", "not-provided"}:
                ensure_hash(value, "manifest SDK replacement digest")
        source_archive_state = ensure_safe_identifier(
            sdk["source_archive_state"], "manifest SDK source archive state"
        )
        source_reproducibility = ensure_safe_identifier(
            sdk["source_reproducibility"], "manifest SDK source reproducibility"
        )
        if role == "bl2":
            if (
                sdk["bundle"] is not None
                or sdk["role"] != "bl2"
                or sdk["profile_inputs"]
                or replacements
                or source_archive_state != "not-applicable"
                or sdk["source_archive_sha256"] is not None
                or source_reproducibility != "not-applicable"
            ):
                raise FreezeError("BL2 SDK metadata is not the minimal no-runtime record")
        else:
            if sdk["bundle"] is None or sdk["role"] != role or sdk["upstream_version"] != "v3.1.1.9":
                raise FreezeError("profile SDK metadata is not role-bound")
            if role == "cp":
                if source_archive_state != "recorded-with-sha256" or source_reproducibility != "not-claimed":
                    raise FreezeError("CP SDK provenance state is not recorded accurately")
                ensure_hash(sdk["source_archive_sha256"], "CP SDK source archive digest")
                if sdk["profile_inputs"] or set(replacements) != {
                    "uart_patch_define", "uart_patched_object_sha256",
                    "libdriver_original_sha256", "libdriver_final_sha256",
                    "final_manifest_sha256",
                }:
                    raise FreezeError("CP SDK provenance replacements are incomplete")
            elif role == "ap":
                if source_archive_state != "not-provided" or source_reproducibility != "not-claimed":
                    raise FreezeError("AP SDK provenance state is not sealed-binary accurate")
                if sdk["source_archive_sha256"] != "not-provided":
                    raise FreezeError("AP SDK source archive digest must be not-provided")
                expected_inputs = 2 if sdk["bundle"] == "v3.1.1.9-sdio4" else 1
                if len(sdk["profile_inputs"]) != expected_inputs or set(replacements) != {
                    "uart_patch_define", "uart_patched_object_sha256",
                    "libdriver_original_sha256", "libdriver_final_sha256",
                    "final_manifest_sha256",
                }:
                    raise FreezeError("AP SDK provenance inputs/replacements are incomplete")
    for profile in profiles:
        metadata = profile["metadata"]
        pair = profile["pair"]
        members = pair["members"]
        peers = pair["peers"]
        if profile["name"] not in members or profile["name"] in peers:
            raise FreezeError("manifest pair does not contain its profile exactly once")
        if not set(members).issubset(profile_names):
            raise FreezeError("manifest pair names an unknown profile")
        if set(members) != {profile["name"], *peers}:
            raise FreezeError("manifest pair member/peer graph is inconsistent")
        if metadata["BK7258_PROFILE_COMPAT"] != pair["id"]:
            raise FreezeError("manifest pair is not bound to profile compatibility metadata")
    ensure_hash(manifest["manifest_sha256"], "manifest self-digest")
    if manifest["manifest_sha256"] != manifest_digest(manifest):
        raise FreezeError("manifest self-digest mismatch")


def validate_inventory(root: Path, manifest: dict[str, Any], require_git: bool) -> dict[str, Any]:
    path = root / INVENTORY_REL
    try:
        info = path.lstat()
    except OSError as error:
        raise FreezeError("consumer inventory is missing") from error
    if stat.S_ISLNK(info.st_mode) or not stat.S_ISREG(info.st_mode):
        raise FreezeError("consumer inventory must be a regular non-symlink file")
    inventory = load_json(path)
    expected_keys = {
        "schema", "kind", "status", "source_manifest", "source_manifest_sha256",
        "baseline", "scan_contract", "consumers", "reviewed_paths", "inventory_sha256",
    }
    if set(inventory) != expected_keys or inventory["schema"] != 1 or isinstance(inventory["schema"], bool):
        raise FreezeError("consumer inventory schema mismatch")
    if inventory["kind"] != "bk7258-legacy-profile-consumer-inventory" or inventory["status"] != "base-snapshot":
        raise FreezeError("consumer inventory kind/status mismatch")
    if (
        inventory["source_manifest"] != MANIFEST_REL.as_posix()
        or inventory["source_manifest_sha256"] != manifest["manifest_sha256"]
    ):
        raise FreezeError("consumer inventory manifest binding mismatch")
    ensure_hash(inventory["source_manifest_sha256"], "inventory source manifest digest")
    baseline = ensure_object(inventory["baseline"], "inventory baseline")
    if set(baseline) != BASELINE_KEYS:
        raise FreezeError("consumer inventory baseline schema mismatch")
    if baseline["commit"] != APPROVED_BASE_COMMIT:
        raise FreezeError("consumer inventory baseline commit mismatch")
    for key in ("commit_tree", "configs_git_tree"):
        ensure_git_object(baseline[key], f"inventory baseline {key}")
    ensure_hash(baseline["entries_sha256"], "inventory baseline entries digest")
    if baseline["remote_fetch_verified"] is not True:
        raise FreezeError("consumer inventory baseline mismatch")
    if not isinstance(baseline["remote_ref"], str):
        raise FreezeError("inventory remote ref is malformed")
    ensure_safe_identifier(baseline["remote_ref"].replace("/", "-"), "inventory remote ref")
    if baseline["entries_sha256"] != manifest["baseline"]["entries_sha256"]:
        raise FreezeError("inventory entries digest is not bound to manifest")
    contract = ensure_object(inventory["scan_contract"], "inventory scan contract")
    if not isinstance(contract, dict) or set(contract) != {
        "profile_term_count", "fixed_terms", "paths_are_repository_relative",
        "generated_paths_excluded", "manual_review_is_neutral",
    }:
        raise FreezeError("consumer inventory scan contract mismatch")
    if contract["paths_are_repository_relative"] is not True or contract["manual_review_is_neutral"] is not True:
        raise FreezeError("consumer inventory scan contract is unsafe")
    if (
        not isinstance(contract["profile_term_count"], int)
        or isinstance(contract["profile_term_count"], bool)
        or contract["profile_term_count"] != len(manifest["profiles"])
        or not isinstance(contract["fixed_terms"], list)
        or not isinstance(contract["generated_paths_excluded"], list)
    ):
        raise FreezeError("consumer inventory scan contract is malformed")
    from inventory_legacy_profile_consumers import FIXED_TERMS, GENERATED_PATHS

    if (
        contract["fixed_terms"] != list(FIXED_TERMS)
        or contract["generated_paths_excluded"] != list(GENERATED_PATHS)
    ):
        raise FreezeError("consumer inventory scan contract terms/paths are not exact")
    consumers = ensure_list(inventory["consumers"], "consumer inventory consumers")
    reviewed_paths = ensure_list(inventory["reviewed_paths"], "consumer inventory reviewed paths")
    consumer_paths: set[str] = set()
    for consumer in consumers:
        if not isinstance(consumer, dict) or set(consumer) != {"path", "status", "rationale", "references"}:
            raise FreezeError("consumer record schema mismatch")
        consumer_path = ensure_relative_repo_path(consumer["path"], "consumer path")
        if consumer_path in consumer_paths:
            raise FreezeError("consumer inventory contains duplicate paths")
        consumer_paths.add(consumer_path)
        ensure_safe_identifier(consumer["status"], "consumer status")
        if not isinstance(consumer["rationale"], str) or not consumer["rationale"]:
            raise FreezeError("consumer rationale missing")
        references = ensure_list(consumer["references"], "consumer references")
        if not references:
            raise FreezeError("consumer references are empty")
        for reference in references:
            if not isinstance(reference, dict) or set(reference) != {"line", "term"}:
                raise FreezeError("consumer reference malformed")
            if not isinstance(reference["line"], int) or isinstance(reference["line"], bool) or reference["line"] < 1:
                raise FreezeError("consumer reference malformed")
            if not isinstance(reference["term"], str) or not reference["term"]:
                raise FreezeError("consumer reference term missing")
    reviewed_seen: set[str] = set()
    for reviewed in reviewed_paths:
        if not isinstance(reviewed, dict) or set(reviewed) != {"path", "status", "rationale", "included_in_consumer_count"}:
            raise FreezeError("reviewed path schema mismatch")
        reviewed_path = ensure_relative_repo_path(reviewed["path"], "reviewed path")
        if reviewed_path in reviewed_seen:
            raise FreezeError("reviewed path is duplicated")
        reviewed_seen.add(reviewed_path)
        ensure_safe_identifier(reviewed["status"], "reviewed path status")
        if not isinstance(reviewed["rationale"], str) or not reviewed["rationale"]:
            raise FreezeError("reviewed path rationale missing")
        if not isinstance(reviewed["included_in_consumer_count"], bool):
            raise FreezeError("reviewed path inclusion flag malformed")
    ensure_hash(inventory["inventory_sha256"], "consumer inventory self-digest")
    if inventory["inventory_sha256"] != sha256_bytes(canonical_json({key: inventory[key] for key in inventory if key != "inventory_sha256"})):
        raise FreezeError("consumer inventory self-digest mismatch")
    if require_git:
        base = verify_approved_base(root)
        if (
            baseline["commit_tree"] != base["commit_tree"]
            or baseline["configs_git_tree"] != base["configs_git_tree"]
        ):
            raise FreezeError("consumer inventory is not bound to the approved Git tree")
        # Recompute the exact Git-object scan rather than trusting a count or
        # a self-consistent edited JSON snapshot.
        from inventory_legacy_profile_consumers import build_inventory

        expected = build_inventory(root, manifest)
        if inventory != expected:
            raise FreezeError("consumer inventory differs from the approved Git-object scan")
    return inventory


def check_manifest(root: Path, manifest: dict[str, Any], require_git: bool = True) -> dict[str, Any]:
    if manifest.get("status") == CUTOVER_STATUS:
        _validate_cutover_manifest_shape(manifest)
        if not require_git:
            raise FreezeError("cutover verification requires the approved Git baseline")
        snapshot = base_snapshot(root)
        base = snapshot["base"]
        baseline = manifest["baseline"]
        if (baseline["commit_tree"] != base["commit_tree"] or
                baseline["configs_git_tree"] != base["configs_git_tree"] or
                baseline["entries_sha256"] != snapshot["entries_sha256"]):
            raise FreezeError("cutover baseline differs from approved Git object")
        if manifest["root_entry"] != snapshot["root_entry"]:
            raise FreezeError("cutover root entry differs from approved Git object")
        historical_names = {record["name"] for record in snapshot["profiles"]}
        expected_retired = historical_names - set(manifest["retained_profiles"])
        if set(manifest["retired_profiles"]) != expected_retired:
            raise FreezeError(
                "cutover retired profile list differs from the historical 27-profile set"
            )
        actual = assert_current_matches_cutover(
            root, snapshot, manifest["retained_profiles"]
        )
        _, ledger_digest = validate_cutover_ledger(root, snapshot["profiles"])
        if ledger_digest != manifest["migration_ledger_sha256"]:
            raise FreezeError("cutover migration ledger digest mismatch")
        return {
            "profiles": 3,
            "files": 7,
            "entries": len(actual),
            "entries_sha256": snapshot["entries_sha256"],
            "ledger_sha256": ledger_digest,
            "consumers": 0,
            "manifest_sha256": manifest["manifest_sha256"],
        }
    validate_manifest_shape(manifest)
    snapshot = base_snapshot(root) if require_git else None
    if snapshot is not None:
        base = snapshot["base"]
        baseline = manifest["baseline"]
        if baseline["commit_tree"] != base["commit_tree"] or baseline["configs_git_tree"] != base["configs_git_tree"]:
            raise FreezeError("manifest Git tree binding mismatch")
        if baseline["entries_sha256"] != snapshot["entries_sha256"]:
            raise FreezeError("manifest baseline entries digest mismatch")
        if manifest["root_entry"] != snapshot["root_entry"] or manifest["entries"] != snapshot["entries"]:
            raise FreezeError("manifest entries differ from approved Git object")
        if manifest["profiles"] != snapshot["profiles"]:
            raise FreezeError("manifest profile metadata differs from approved Git object")
        if manifest["profile_count"] != snapshot["profile_count"] or manifest["file_count"] != snapshot["file_count"]:
            raise FreezeError("manifest declarative counts differ from approved Git object")
        actual = assert_current_matches_snapshot(root, snapshot)
        expected_profiles = snapshot["profiles"]
    else:
        actual = walk_current_entries(root)
        expected_profiles = manifest["profiles"]
        expected_entries = manifest["entries"]
        if len(actual) != len(expected_entries):
            raise FreezeError("fixture/current entry count differs from manifest")
        expected_by_path = {entry["path"]: entry for entry in expected_entries}
        actual_by_path = {entry["path"]: entry for entry in actual}
        if set(actual_by_path) != set(expected_by_path):
            raise FreezeError("fixture/current tree paths differ from manifest")
        for path, expected_entry in expected_by_path.items():
            actual_entry = actual_by_path[path]
            if actual_entry["type"] != expected_entry["type"] or actual_entry["mode"] != expected_entry["mode"]:
                raise FreezeError(f"fixture/current entry identity differs: {path}")
            if expected_entry["type"] == "file" and actual_entry.get("sha256") != expected_entry["sha256"]:
                raise FreezeError(f"fixture/current file bytes differ: {path}")
    _, ledger_digest = validate_ledger(root, expected_profiles)
    if ledger_digest != manifest["migration_ledger_sha256"]:
        raise FreezeError("manifest migration ledger digest mismatch")
    inventory = validate_inventory(root, manifest, require_git=require_git)
    profile_count = sum(
        entry["type"] == "directory"
        for entry in actual
        if "/" not in entry["path"][len(CONFIGS_REL.as_posix()) + 1:]
    )
    file_count = sum(entry["type"] == "file" for entry in actual)
    if profile_count != manifest["profile_count"] or file_count != manifest["file_count"]:
        raise FreezeError("recomputed current profile/file counts differ")
    return {
        "profiles": profile_count,
        "files": file_count,
        "entries": len(actual),
        "entries_sha256": manifest["baseline"]["entries_sha256"],
        "ledger_sha256": ledger_digest,
        "consumers": len(inventory["consumers"]),
        "manifest_sha256": manifest["manifest_sha256"],
    }


def build_manifest(root: Path) -> dict[str, Any]:
    snapshot = base_snapshot(root)
    # This is intentionally before any output write: dirty legacy configs can
    # never be used to mint a new baseline.
    assert_current_matches_snapshot(root, snapshot)
    _, ledger_digest = validate_ledger(root, snapshot["profiles"])
    manifest: dict[str, Any] = {
        "schema": MANIFEST_SCHEMA,
        "kind": "bk7258-legacy-profile-freeze",
        "status": "frozen-baseline",
        "root": CONFIGS_REL.as_posix(),
        "root_entry": snapshot["root_entry"],
        "rules": {
            "profile_directory_count_may_only_decrease": True,
            "new_profile_directory_requires_architecture_adr": True,
            "new_driver_or_validator_profile_forbidden": True,
            "exact_case_and_type_are_part_of_identity": True,
            "all_file_bytes_are_sha256_pinned": True,
            "baseline_is_git_object_not_worktree": True,
        },
        "baseline": {
            **snapshot["base"],
            "entries_sha256": snapshot["entries_sha256"],
            "remote_ref": "origin/dev-ai-contest-2026",
            "remote_fetch_verified": True,
        },
        "profile_count": snapshot["profile_count"],
        "file_count": snapshot["file_count"],
        "entries": snapshot["entries"],
        "profiles": snapshot["profiles"],
        "migration_ledger": LEDGER_REL.as_posix(),
        "migration_ledger_sha256": ledger_digest,
        "consumer_inventory": {"path": INVENTORY_REL.as_posix()},
    }
    manifest["manifest_sha256"] = manifest_digest(manifest)
    # The manifest and exact-base consumer inventory are a single P0
    # checkpoint.  Do not mint a new manifest while its mandatory companion
    # snapshot is missing, stale or edited.
    validate_inventory(root, manifest, require_git=True)
    return manifest


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=repo_root())
    parser.add_argument("--manifest", type=Path, default=None)
    parser.add_argument("--generate", action="store_true")
    parser.add_argument("--base-commit", default=APPROVED_BASE_COMMIT)
    parser.add_argument("--no-git", action="store_true", help="fixture-only current-tree check")
    args = parser.parse_args(argv)
    root = args.root.resolve()
    manifest_path = args.manifest or root / MANIFEST_REL
    try:
        if args.base_commit != APPROVED_BASE_COMMIT:
            raise FreezeError("only the owner-approved P0 base commit is accepted")
        if args.generate:
            manifest = build_manifest(root)
            manifest_path.parent.mkdir(parents=True, exist_ok=True)
            manifest_path.write_bytes(canonical_json(manifest))
            print(f"BK7258 legacy freeze manifest generated: {manifest_path}")
            print(f"manifest_sha256={manifest['manifest_sha256']}")
            return 0
        result = check_manifest(root, load_json(manifest_path), require_git=not args.no_git)
        print(
            "BK7258 legacy freeze PASS: "
            f"profiles={result['profiles']} files={result['files']} entries={result['entries']} "
            f"consumers={result['consumers']} entries_sha256={result['entries_sha256']} "
            f"ledger_sha256={result['ledger_sha256']} manifest_sha256={result['manifest_sha256']}"
        )
        return 0
    except FreezeError as error:
        print(f"BK7258 legacy freeze FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
