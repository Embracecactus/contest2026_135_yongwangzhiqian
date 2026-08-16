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

from bk7258_common import (
    FrameworkError,
    array,
    canonical_json,
    exact,
    identifier,
    identifiers,
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


def validate_descriptor_set(repository: Path, value: dict[str, Any]) -> dict[str, Any]:
    exact(value, {"schema", "kind", "version", "serialization",
                  "descriptors", "identity_sha256"}, "validation descriptor set")
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
    body = dict(value)
    supplied = body.pop("identity_sha256")
    if sha256(canonical_json(body)) != supplied:
        raise FrameworkError("validation descriptor set identity mismatch")
    return {
        "descriptors": len(descriptors),
        "claims": claim_owners,
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
    path = args.descriptors or root / "tools/bk7258/bk7258_validation_descriptors.json"
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
