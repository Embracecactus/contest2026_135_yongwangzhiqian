#!/usr/bin/env python3
"""Inventory legacy profile consumers from the approved Git object only."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path
from typing import Any

from verify_legacy_profile_freeze import (
    APPROVED_BASE_COMMIT,
    CONFIGS_REL,
    MANIFEST_REL,
    FreezeError,
    canonical_json,
    load_json,
    manifest_digest,
    repo_root,
    sha256_bytes,
    validate_manifest_shape,
    verify_approved_base,
)


OUTPUT_REL = Path("tools/bk7258/legacy_profile_consumers.json")
FIXED_TERMS = (
    "CP_CONFIG_NAME", "AP_CONFIG_NAME", "BK7258_CONFIG_ROOT", "BK7258_PROFILE_",
    "profile.conf", "board/bk7258/configs", "sdk-profiles", "sdk-bundles",
)
GENERATED_PATHS = (
    "tools/bk7258/legacy_profile_consumers.json",
    "tools/bk7258/legacy_profile_freeze_manifest.json",
    "tools/bk7258/legacy_profile_migration_ledger.json",
    "tools/bk7258/verify_legacy_profile_freeze.py",
    "tools/bk7258/inventory_legacy_profile_consumers.py",
    "board/bk7258/tests/test_legacy_profile_freeze.py",
    "progress/verification/2026-08-15-bk7258-platform-v2-p0.md",
)
MANUAL_REVIEW_PATHS = (
    "board/bk7258/boards/README.md",
    "board/bk7258/bootloader/README.md",
    "board/bk7258/bk_idk/README.md",
    "memory/ARCHITECTURE.md",
    "memory/OPERATIONS.md",
)
HISTORICAL_ONLY = {
    "board/bk7258/chip/ap/PWM_BLOCKED_ROOT_CAUSE.md":
        "Resolved PWM root cause; retained as evidence, not an active parser.",
    "docs/bk7258-t5ai/nuttx-port/n8-cold-reset-session-handoff-2026-07-30.md":
        "Completed N8 handoff; retained as historical evidence, not an active parser.",
    "docs/bk7258-t5ai/nuttx-port/n8-cold-reset-resolution-report.md":
        "Completed N8 resolution; retained as historical evidence, not an active parser.",
}


def git_ls_files(root: Path, base: str) -> list[tuple[str, str, str]]:
    """Return regular Git-tree files; fail closed if the object is unavailable."""

    try:
        raw = subprocess.check_output(
            ["git", "-C", str(root), "ls-tree", "-r", "-z", "--full-tree", base],
            stderr=subprocess.PIPE,
        )
    except (OSError, subprocess.CalledProcessError) as error:
        raise FreezeError("cannot scan exact approved Git tree") from error
    result: list[tuple[str, str, str]] = []
    for record in raw.split(b"\0"):
        if not record:
            continue
        try:
            metadata, path_bytes = record.split(b"\t", 1)
            mode, object_type, object_id = metadata.decode("ascii").split(" ")
            path = path_bytes.decode("utf-8")
        except (UnicodeError, ValueError) as error:
            raise FreezeError("malformed approved Git inventory tree") from error
        if object_type == "blob" and mode in {"100644", "100755"}:
            result.append((path, mode, object_id))
        elif object_type not in {"tree"}:
            raise FreezeError(f"unsupported non-regular object in inventory tree: {path}")
    return result


def git_blob(root: Path, base: str, path: str) -> bytes:
    try:
        return subprocess.check_output(
            ["git", "-C", str(root), "show", f"{base}:{path}"],
            stderr=subprocess.PIPE,
        )
    except (OSError, subprocess.CalledProcessError) as error:
        raise FreezeError(f"cannot read approved Git blob: {path}") from error


def reviewed_status(path: str) -> tuple[str, str]:
    if path in HISTORICAL_ONLY:
        return "historical-only", HISTORICAL_ONLY[path]
    if path.startswith("board/bk7258/configs/"):
        return "legacy-profile-data", "Profile metadata/defconfig is the frozen legacy input surface."
    if path.startswith("tools/bk7258/") or path.startswith("board/bk7258/chip/"):
        return "legacy-parser-or-build-reference", "Tracked build or verifier code still names the legacy schema."
    if path.startswith("board/bk7258/tests/"):
        return "host-test-reference", "Host test retains a legacy contract reference."
    if path.startswith("progress/") or path.startswith("memory/decisions/"):
        return "historical-evidence", "Evidence or decision history retains exact legacy names."
    if path.startswith("docs/") or path in {"README.md", "memory/ARCHITECTURE.md", "memory/OPERATIONS.md"}:
        return "documentation-reference", "Current documentation describes or routes the legacy contract."
    return "neutral-reviewed-reference", "Lexical legacy reference retained for migration review."


def build_inventory(root: Path, manifest: dict[str, Any]) -> dict[str, Any]:
    base = verify_approved_base(root)
    validate_manifest_shape(manifest)
    if manifest.get("manifest_sha256") != manifest_digest(manifest):
        raise FreezeError("source manifest self-digest mismatch")
    profiles = sorted(item["name"] for item in manifest["profiles"])
    terms = tuple(dict.fromkeys((*profiles, *FIXED_TERMS)))
    consumers: list[dict[str, Any]] = []
    for path, _mode, _object_id in git_ls_files(root, base["commit"]):
        if path in GENERATED_PATHS:
            continue
        data = git_blob(root, base["commit"], path)
        try:
            text = data.decode("utf-8")
        except UnicodeDecodeError:
            continue
        references: list[dict[str, Any]] = []
        for line_number, line in enumerate(text.splitlines(), 1):
            for term in terms:
                if term in line:
                    references.append({"line": line_number, "term": term})
        if not references:
            continue
        status, rationale = reviewed_status(path)
        consumers.append({
            "path": path,
            "status": status,
            "rationale": rationale,
            "references": references,
        })
    consumer_paths = {item["path"] for item in consumers}
    reviewed_paths: list[dict[str, Any]] = []
    for path in MANUAL_REVIEW_PATHS:
        if path in consumer_paths:
            status, rationale = reviewed_status(path)
            included = True
        else:
            status = "manual-review"
            rationale = "Explicitly reviewed because board/platform ownership is relevant even without a lexical hit."
            included = False
        reviewed_paths.append({
            "path": path,
            "status": status,
            "rationale": rationale,
            "included_in_consumer_count": included,
        })
    inventory: dict[str, Any] = {
        "schema": 1,
        "kind": "bk7258-legacy-profile-consumer-inventory",
        "status": "base-snapshot",
        "source_manifest": MANIFEST_REL.as_posix(),
        "source_manifest_sha256": manifest["manifest_sha256"],
        "baseline": {
            "commit": base["commit"],
            "commit_tree": base["commit_tree"],
            "configs_git_tree": base["configs_git_tree"],
            "entries_sha256": manifest["baseline"]["entries_sha256"],
            "remote_ref": "origin/dev-ai-contest-2026",
            "remote_fetch_verified": True,
        },
        "scan_contract": {
            "profile_term_count": len(profiles),
            "fixed_terms": list(FIXED_TERMS),
            "paths_are_repository_relative": True,
            "generated_paths_excluded": list(GENERATED_PATHS),
            "manual_review_is_neutral": True,
        },
        "consumers": consumers,
        "reviewed_paths": reviewed_paths,
    }
    inventory["inventory_sha256"] = sha256_bytes(canonical_json(inventory))
    return inventory


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=repo_root())
    parser.add_argument("--manifest", type=Path, default=None)
    parser.add_argument("--output", type=Path, default=None)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)
    root = args.root.resolve()
    manifest_path = args.manifest or root / MANIFEST_REL
    output = args.output or root / OUTPUT_REL
    try:
        manifest = load_json(manifest_path)
        inventory = build_inventory(root, manifest)
        if args.check:
            if load_json(output) != inventory:
                raise FreezeError("legacy consumer inventory is stale")
            print(f"BK7258 legacy consumer inventory PASS: consumers={len(inventory['consumers'])}")
            return 0
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(canonical_json(inventory))
        print(f"BK7258 legacy consumer inventory generated: {output}")
        print(f"consumers={len(inventory['consumers'])}")
        return 0
    except FreezeError as error:
        print(f"BK7258 legacy consumer inventory FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
