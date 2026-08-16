#!/usr/bin/env python3
"""Host-only checker for BK7258 ``bkvalidate`` descriptors.

The target runner is a small app in ``app/hello_app``.  This module validates
the versioned descriptor contract and the frozen 27-profile migration ledger;
it never invokes a vendor SDK or a device operation.  Target commands may
wait on a versioned diagnostic record published by an optional validator.
"""

from __future__ import annotations

import argparse
import stat
from pathlib import Path
from typing import Any

from bk7258_framework import (
    FrameworkError,
    array,
    canonical_json,
    exact,
    identifier,
    identifiers,
    load_validation_suites,
    load_json,
    relative_path,
    sha256,
)


VALIDATION_SCHEMA = "bk7258.validation/1"
OUTCOME_SCHEMA = "bk7258.validation-outcome/1"
DESCRIPTOR_FIELDS = {
    "id", "version", "role", "requirements", "category", "timeout",
    "prepare", "run", "cancel", "cleanup", "status", "resource_claims",
    "entrypoint",
}
CATEGORIES = {"auto", "interactive", "fixture", "destructive-fault"}
ROLES = {"cp", "ap", "cp_ap", "board"}
STATUSES = {"planned", "ready", "disabled"}
TAGS = {"devpath:", "operator:", "fixture:", "fault:"}
COMMAND_PREFIX = "public_api:"
FORBIDDEN_COMMAND_TERMS = ("vendor", "sdk", "bk_", "board/", "chip/")
STANDARD_ARTIFACT_CONTRACT = {
    "cp": "vela_nuttx_cp.bin",
    "ap": "vela_nuttx_ap.bin",
    "manifest": "vela_nuttx_manifest.json",
}


def _regular(path: Path, field: str) -> None:
    try:
        mode = path.lstat().st_mode
    except OSError as error:
        raise FrameworkError(f"missing {field}: {path}") from error
    if stat.S_ISLNK(mode) or not stat.S_ISREG(mode):
        raise FrameworkError(f"{field} must be regular and non-symlink: {path}")


def _strings(value: Any, field: str, allow_empty: bool = True) -> list[str]:
    values = array(value, field)
    if not allow_empty and not values:
        raise FrameworkError(f"{field} must not be empty")
    result: list[str] = []
    for item in values:
        if not isinstance(item, str) or not item:
            raise FrameworkError(f"{field} contains a non-empty string violation")
        result.append(item)
    return result


def _command(value: Any, field: str, prefix: str | None = None) -> str:
    if not isinstance(value, str) or not value:
        raise FrameworkError(f"{field} must be a non-empty command token")
    lowered = value.lower()
    if any(term in lowered for term in FORBIDDEN_COMMAND_TERMS):
        raise FrameworkError(f"{field} contains a vendor or layer-private call")
    if prefix is not None and not value.startswith(prefix):
        raise FrameworkError(f"{field} must use the public-device API adapter")
    return value


def _identity(value: dict[str, Any], field: str) -> dict[str, Any]:
    supplied = value.get("identity_sha256")
    if not isinstance(supplied, str) or len(supplied) != 64:
        raise FrameworkError(f"{field} identity is malformed")
    body = dict(value)
    del body["identity_sha256"]
    if sha256(canonical_json(body)) != supplied:
        raise FrameworkError(f"{field} identity mismatch")
    return value


def validate_legacy_profile_mapping(repository: Path, descriptor_set: dict[str, Any]) -> dict[str, Any]:
    path = repository / descriptor_set["legacy_ledger"]
    ledger = load_json(path)
    expected_keys = {"schema", "kind", "status", "source_manifest", "rules", "rows"}
    exact(ledger, expected_keys, "legacy profile migration ledger")
    if (ledger["schema"] != 1 or ledger["kind"] != "bk7258-legacy-profile-migration-ledger" or
            ledger["status"] not in {"cutover-approved", "proposal-only"}):
        raise FrameworkError("legacy profile migration ledger is not an approved historical record")
    rows = array(ledger["rows"], "legacy profile migration rows")
    profiles_root = repository / "board/bk7258/configs"
    try:
        profiles = sorted(
            path.name for path in profiles_root.iterdir()
            if path.is_dir() and (path / "profile.conf").is_file()
        )
    except OSError as error:
        raise FrameworkError("canonical profile root is unavailable") from error
    if len(rows) != 27 or profiles != ["bl2_mcuboot", "t5ai_core_ap_base", "t5ai_core_cp_base"]:
        raise FrameworkError("cutover must retain exactly the three canonical seed profiles")
    suite_catalog = load_validation_suites(repository)
    seen: set[str] = set()
    families: set[str] = set()
    suites: set[str] = set()
    for index, raw in enumerate(rows):
        row = raw if isinstance(raw, dict) else None
        if row is None:
            raise FrameworkError(f"legacy profile row {index} is malformed")
        exact(row, {"legacy_profile", "board", "role", "boot", "class",
                    "pair", "migration_state", "target", "review"},
              f"legacy profile row {index}")
        name = row["legacy_profile"]
        if not isinstance(name, str) or name in seen:
            raise FrameworkError(f"legacy profile coverage is not unique: {name}")
        seen.add(name)
        if row["migration_state"] not in {
            "shadow-equivalent", "retire-blocked-hardware", "consolidation-review"
        }:
            raise FrameworkError(f"legacy profile state is not an approved historical state: {name}")
        target = row["target"]
        if not isinstance(target, dict):
            raise FrameworkError(f"legacy profile target is malformed: {name}")
        exact(target, {"family", "resource_mode", "validation_suite", "role",
                       "pair", "decision", "owner", "capabilities"},
              f"legacy profile target {name}")
        for field in ("family", "resource_mode", "role", "pair"):
            identifier(target[field], f"legacy profile target {name}.{field}")
        if target["role"] != row["role"] or target["pair"] != row["pair"]:
            raise FrameworkError(f"legacy profile target role/pair mismatch: {name}")
        if (target["decision"] not in {"proposal", "approved-cutover"} or
                target["owner"] not in {"P5-product-resource-review", "P9b-profile-cutover"}):
            raise FrameworkError(f"legacy profile target decision mismatch: {name}")
        suite = target["validation_suite"]
        if suite is not None:
            identifier(suite, f"legacy profile target {name}.validation_suite")
            normalized = suite.replace("-", "_")
            if normalized not in suite_catalog and not (
                normalized == "tf" and
                {"tf_1bit", "tf_4bit"}.issubset(suite_catalog)
            ):
                raise FrameworkError(f"legacy profile suite has no canonical carrier: {name}")
            suites.add(normalized)
        if row["class"] in {"validation", "ci"} and suite is None:
            raise FrameworkError(f"validation/CI profile has no suite: {name}")
        if row["class"] not in {"validation", "ci"} and suite is not None:
            raise FrameworkError(f"runnable profile has an unexpected suite: {name}")
        families.add(target["family"])
    freeze = load_json(repository / "board/bk7258/scripts/legacy_profile_freeze_manifest.json")
    historical_names = set(freeze.get("retained_profiles", [])) | set(
        freeze.get("retired_profiles", []))
    if seen != historical_names:
        raise FrameworkError("legacy profile mapping does not cover the historical cutover set")
    return {
        "profiles": len(profiles),
        "families": sorted(families),
        "suites": sorted(suites),
        "legacy_state": "cutover-approved",
        "migration_state": "canonical-suite-only",
    }


def validate_descriptor_set(repository: Path, value: dict[str, Any]) -> dict[str, Any]:
    exact(value, {"schema", "kind", "version", "serialization",
                  "migration_policy", "descriptors", "legacy_ledger",
                  "identity_sha256"}, "validation descriptor set")
    if (value["schema"] != VALIDATION_SCHEMA or
            value["kind"] != "validation-descriptor-set" or value["version"] != 1):
        raise FrameworkError("unsupported validation descriptor schema")
    serialization = value["serialization"]
    if not isinstance(serialization, dict):
        raise FrameworkError("validation serialization policy is malformed")
    exact(serialization, {"scope", "claim_policy", "outcome_schema"},
          "validation serialization policy")
    if (serialization["scope"] != "global" or serialization["claim_policy"] != "exclusive" or
            serialization["outcome_schema"] != OUTCOME_SCHEMA):
        raise FrameworkError("validation serialization policy is unsafe")
    migration = value["migration_policy"]
    if not isinstance(migration, dict):
        raise FrameworkError("validation migration policy is malformed")
    exact(migration, {"legacy_validation", "production_auto_start",
                      "legacy_profile_state", "legacy_bytes"},
          "validation migration policy")
    if (migration["legacy_validation"] != "canonical_suite_only" or
            migration["production_auto_start"] != "canonical-only" or
            migration["legacy_profile_state"] != "cutover-approved" or
            migration["legacy_bytes"] != "historical-only"):
        raise FrameworkError("validation migration policy is not fail-closed")
    relative_path(value["legacy_ledger"], "validation legacy_ledger")
    descriptors = array(value["descriptors"], "validation descriptors")
    if not descriptors:
        raise FrameworkError("validation descriptor set is empty")
    seen: set[str] = set()
    claim_owners: dict[str, list[str]] = {}
    for index, raw in enumerate(descriptors):
        descriptor = raw if isinstance(raw, dict) else None
        if descriptor is None:
            raise FrameworkError(f"validation descriptor {index} is malformed")
        exact(descriptor, DESCRIPTOR_FIELDS, f"validation descriptor {index}")
        descriptor_id = identifier(descriptor["id"], f"validation descriptor {index}.id")
        if descriptor_id in seen:
            raise FrameworkError(f"duplicate validation descriptor: {descriptor_id}")
        seen.add(descriptor_id)
        if descriptor["version"] != 1 or descriptor["role"] not in ROLES:
            raise FrameworkError(f"validation descriptor version/role is invalid: {descriptor_id}")
        requirements = _strings(descriptor["requirements"], f"validation descriptor {descriptor_id}.requirements")
        for requirement in requirements:
            if not requirement.startswith(tuple(TAGS)):
                raise FrameworkError(f"validation requirement is not typed: {descriptor_id}")
        if descriptor["category"] not in CATEGORIES or descriptor["status"] not in STATUSES:
            raise FrameworkError(f"validation category/status is invalid: {descriptor_id}")
        if (not isinstance(descriptor["timeout"], int) or isinstance(descriptor["timeout"], bool) or
                descriptor["timeout"] <= 0):
            raise FrameworkError(f"validation timeout is invalid: {descriptor_id}")
        _command(descriptor["prepare"], f"validation descriptor {descriptor_id}.prepare")
        _command(descriptor["run"], f"validation descriptor {descriptor_id}.run", COMMAND_PREFIX)
        _command(descriptor["cancel"], f"validation descriptor {descriptor_id}.cancel")
        _command(descriptor["cleanup"], f"validation descriptor {descriptor_id}.cleanup")
        claims = identifiers(descriptor["resource_claims"],
                             f"validation descriptor {descriptor_id}.resource_claims")
        if not claims:
            raise FrameworkError(f"validation descriptor has no resource claim: {descriptor_id}")
        for claim in claims:
            claim_owners.setdefault(claim, []).append(descriptor_id)
        entrypoint = relative_path(descriptor["entrypoint"],
                                   f"validation descriptor {descriptor_id}.entrypoint")
        if not entrypoint.startswith("app/hello_app/") or any(
                part in {"board", "chip", "boards"} for part in entrypoint.split("/")
        ):
            raise FrameworkError(f"validation descriptor is placed in chip/board code: {descriptor_id}")
        _regular(repository / entrypoint, f"validation descriptor {descriptor_id}.entrypoint")
    mapping = validate_legacy_profile_mapping(repository, value)
    body = dict(value)
    supplied = body.pop("identity_sha256")
    if sha256(canonical_json(body)) != supplied:
        raise FrameworkError("validation descriptor set identity mismatch")
    return {
        "descriptors": len(descriptors),
        "claims": claim_owners,
        "legacy": mapping,
        "standard_artifacts": dict(STANDARD_ARTIFACT_CONTRACT),
    }


def validation_outcome(descriptor: dict[str, Any], status: str, reason: str) -> dict[str, Any]:
    if status not in {"PASS", "SKIP", "FAIL"} or not reason:
        raise FrameworkError("invalid validation outcome")
    return {
        "schema": OUTCOME_SCHEMA,
        "kind": "validation-outcome",
        "version": 1,
        "id": descriptor["id"],
        "status": status,
        "reason": reason,
        "role": descriptor["role"],
        "category": descriptor["category"],
        "timeout": descriptor["timeout"],
        "requirements": descriptor["requirements"],
        "resource_claims": descriptor["resource_claims"],
        "prepare": descriptor["prepare"],
        "run": descriptor["run"],
        "cancel": descriptor["cancel"],
        "cleanup": descriptor["cleanup"],
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--descriptors", type=Path)
    parser.add_argument("command", choices=("check",))
    args = parser.parse_args(argv)
    root = args.root.resolve()
    path = args.descriptors or root / "board/bk7258/scripts/bk7258_validation_descriptors.json"
    if not path.is_absolute():
        path = root / path
    try:
        value = load_json(path)
        result = validate_descriptor_set(root, value)
        print(canonical_json({
            "schema": OUTCOME_SCHEMA,
            "kind": "validation-check",
            "version": 1,
            "status": "PASS",
            "descriptor_count": result["descriptors"],
            "legacy_profiles": result["legacy"]["profiles"],
            "migration_state": result["legacy"]["migration_state"],
        }).decode(), end="")
        return 0
    except FrameworkError as error:
        print(f"bk7258-validation: FAIL: {error}")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
