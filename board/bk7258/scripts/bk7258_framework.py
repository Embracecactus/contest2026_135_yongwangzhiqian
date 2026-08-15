#!/usr/bin/env python3
"""Host-only BK7258 composition framework.

This module is intentionally a small standard-library tool.  Its inputs are
repository-relative, strict JSON documents and its final configuration value
map is suitable for a later adapter to render as an ordinary NuttX
``defconfig``.  It does not replace the legacy builder or touch SDK bytes.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import stat
import sys
from pathlib import Path
from typing import Any, Callable


SCHEMA = "bk7258.composition/1"
ROLES = frozenset({"cp", "ap", "bl2"})
MODES = frozenset({"bringup", "application", "validation", "factory"})
BOOTS = frozenset({"raw", "mcuboot"})
SDK_REGISTRY_SCHEMA = "bk7258.sdk-registry/1"
SDK_SET_SCHEMA = "bk7258.sdk-set/1"
SDK_LOCK_SCHEMA = "bk7258.sdk-lock/1"
SDK_IMPORT_SCHEMA = "bk7258.sdk-import/1"
CONFIG_SCHEMA = "bk7258.config/1"
BUILD_PLAN_SCHEMA = "bk7258.build-plan/1"
BKPACK_SCHEMA = "bk7258.bkpack/1"
SDK_ROLES = frozenset({"cp", "ap"})
SDK_MANIFEST_ROOT = "board/bk7258/scripts/sdk-manifests"
PRIVATE_MIRROR_URL = "https://github.com/Embracecactus/vendor-bk-avdk-smp.git"
SDK_ENTRY_KINDS = frozenset({"official", "derived", "sealed-binary"})
SDK_REQUIRED_DIRS = frozenset({"include", "config", "libs"})
RETIRED_REPOSITORY_ROOTS = ("board/bk7258_t5ai",)
TOKEN_RE = re.compile(r"^[a-z][a-z0-9_]*$")
SDK_VERSION_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")
ID_RE = re.compile(r"^[a-z][a-z0-9_-]*$")
SYMBOL_RE = re.compile(r"^CONFIG_[A-Z0-9_]+$")
HASH_RE = re.compile(r"^[0-9a-f]{64}$")
STAGES = {"common": 0, "role": 1, "board": 2, "boot": 3, "feature": 4,
          "app": 5, "validation": 6, "factory": 7}
BOARD_SELECTORS = {
    "aidk_ai_toy": "CONFIG_BK7258_BOARD_AIDK_AI_TOY",
    "t5_board": "CONFIG_BK7258_BOARD_T5_BOARD",
    "t5ai_core": "CONFIG_BK7258_BOARD_T5AI_CORE",
}

# P6 is deliberately a metadata-only package boundary.  Keep the standard
# build outputs visible in the package contract; the optional ``.bkpack``
# entry is an additive vendor extension and never replaces those outputs.
PACK_ROLES = ("bl1", "bl2", "cp", "ap")
PACK_KINDS = frozenset({"application", "factory"})
PACK_ROLE_PARTITIONS = {
    "bl1": "primary_bootloader",
    "bl2": "bl2",
    "cp": "primary_cp_app",
    "ap": "primary_ap_app",
}
PACK_ARTIFACTS = {
    "libarch.a": ("static_archive", "chip", ("cp", "ap")),
    "libboards.a": ("static_archive", "board", ("cp", "ap")),
    "vela_bl1.bin": ("firmware_binary", "board", ("bl1",)),
    "vela_bl2.bin": ("firmware_binary", "board", ("bl2",)),
    "vela_cp.bin": ("firmware_binary", "board", ("cp",)),
    "vela_ap.bin": ("firmware_binary", "board", ("ap",)),
}
TRANSPORT_SCHEMA = "bk7258.transport/1"
TRANSPORT_HOSTS = frozenset({"linux", "darwin", "windows", "wsl"})
TRANSPORT_IDENTITY_KEYS = ("vid", "pid", "serial_prefix", "interface", "location")
TRANSPORT_CAPABILITY_KEYS = ("rts", "dtr", "reset", "rts_reset")
SHADOW_SCHEMA = "bk7258.shadow/1"
SHADOW_REPORT_SCHEMA = "bk7258.shadow-report/1"
SHADOW_LEDGER_REL = "board/bk7258/scripts/bk7258_shadow_ledger.json"
SHADOW_STATUS_ORDER = ("EXACT", "EQUIVALENT_WITH_REASON", "MIGRATION_PENDING",
                       "RETIRE_PROPOSED")
SHADOW_STATUS = frozenset(SHADOW_STATUS_ORDER)
FRAMEWORK_CHECK_SCHEMA = "bk7258.framework-check/1"


class FrameworkError(ValueError):
    """Any malformed, ambiguous, or unsupported framework input."""


def canonical_json(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _unique_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise FrameworkError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=_unique_pairs)
    except FrameworkError:
        raise
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise FrameworkError(f"cannot load JSON: {path}") from error
    if not isinstance(value, dict):
        raise FrameworkError(f"JSON root is not an object: {path}")
    return value


def obj(value: Any, field: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise FrameworkError(f"{field} must be an object")
    return value


def array(value: Any, field: str) -> list[Any]:
    if not isinstance(value, list):
        raise FrameworkError(f"{field} must be an array")
    return value


def exact(value: dict[str, Any], keys: set[str], field: str) -> None:
    if set(value) != keys:
        raise FrameworkError(f"{field} keys are not exact")


def identifier(value: Any, field: str) -> str:
    if not isinstance(value, str) or not ID_RE.fullmatch(value):
        raise FrameworkError(f"unsafe identifier in {field}")
    return value


def identifiers(value: Any, field: str) -> list[str]:
    values = array(value, field)
    result = [identifier(item, f"{field}[]") for item in values]
    if len(result) != len(set(result)):
        raise FrameworkError(f"duplicate identifier in {field}")
    return result


def relative_path(value: Any, field: str) -> str:
    if (not isinstance(value, str) or not value or "\\" in value or
            "\x00" in value or any(char.isspace() for char in value) or
            value.startswith("/")):
        raise FrameworkError(f"unsafe repository path in {field}")
    parts = value.split("/")
    if any(part in {"", ".", ".."} for part in parts):
        raise FrameworkError(f"unsafe repository path in {field}")
    if any(value == root or value.startswith(root + "/")
           for root in RETIRED_REPOSITORY_ROOTS):
        raise FrameworkError(f"retired repository path in {field}")
    return value


def _sdk_metadata_path(value: Any, field: str) -> str:
    """Validate a product's repository-relative SDK set/lock metadata path."""
    path = relative_path(value, field)
    name = path.rsplit("/", 1)[-1]
    kind = "set" if field.endswith("sdk_set") else "lock"
    if (not path.startswith("board/bk7258/scripts/bk7258_sdk_") or
            not name.endswith(".json") or
            not (name == f"bk7258_sdk_{kind}.json" or
                 name.startswith(f"bk7258_sdk_{kind}_"))):
        raise FrameworkError(f"{field} must name an in-tree SDK set/lock metadata file")
    return path


def digest(value: Any, field: str) -> str:
    if not isinstance(value, str) or not HASH_RE.fullmatch(value):
        raise FrameworkError(f"invalid SHA-256 in {field}")
    return value


def symbols(value: Any, field: str = "symbols") -> dict[str, str | None]:
    values = obj(value, field)
    result: dict[str, str | None] = {}
    for key, item in values.items():
        if not isinstance(key, str) or not SYMBOL_RE.fullmatch(key):
            raise FrameworkError(f"invalid Kconfig symbol in {field}")
        if item is not None and (not isinstance(item, str) or
                                 not re.fullmatch(r"[A-Za-z0-9_./:+,-]+", item)):
            raise FrameworkError(f"invalid Kconfig value for {key}")
        result[key] = item
    return result


def validate_board_selector_symbols(board_id: str,
                                    values: dict[str, str | None],
                                    field: str) -> None:
    """Require the resolved Kconfig board selector to match the IR board."""
    if board_id not in BOARD_SELECTORS:
        raise FrameworkError(f"unsupported board selector in {field}: {board_id}")

    selected: list[str] = []
    for candidate, selector in BOARD_SELECTORS.items():
        value = values.get(selector)
        if value not in (None, "y"):
            raise FrameworkError(
                f"{field}.{selector} must be absent, null, or y")
        if value == "y":
            selected.append(candidate)

    if selected != [board_id]:
        raise FrameworkError(
            f"{field} must select exactly board {board_id}: {selected}")


def validate_board(value: dict[str, Any]) -> dict[str, Any]:
    exact(value, {"schema", "kind", "id", "soc", "variant", "bindings",
                  "resource_claims", "transport"}, "board")
    if value["schema"] != SCHEMA or value["kind"] != "board":
        raise FrameworkError("unsupported board schema")
    identifier(value["id"], "board.id")
    if value["soc"] != "bk7258":
        raise FrameworkError("board.soc must be bk7258")
    identifier(value["variant"], "board.variant")
    bindings = obj(value["bindings"], "board.bindings")
    exact(bindings, {"console", "debug"}, "board.bindings")
    console = obj(bindings["console"], "board.console")
    exact(console, {"uart", "baud", "flow_control", "rts_reset"}, "board.console")
    identifier(console["uart"], "board.console.uart")
    if not isinstance(console["baud"], int) or isinstance(console["baud"], bool) or console["baud"] <= 0:
        raise FrameworkError("board.console.baud must be positive")
    if console["flow_control"] is not False or console["rts_reset"] is not False:
        raise FrameworkError("board console flow control and RTS reset must be false")
    debug = obj(bindings["debug"], "board.debug")
    exact(debug, {"swd", "boot_hold", "rtt"}, "board.debug")
    if any(debug[key] is not False for key in debug):
        raise FrameworkError("board debug controls must be explicit false")
    claims = array(value["resource_claims"], "board.resource_claims")
    seen: set[tuple[str, str]] = set()
    for index, raw in enumerate(claims):
        claim = obj(raw, f"board.resource_claims[{index}]")
        exact(claim, {"resource", "owner", "phases"}, f"board.resource_claims[{index}]")
        resource = identifier(claim["resource"], f"claim[{index}].resource")
        owner = identifier(claim["owner"], f"claim[{index}].owner")
        identifiers(claim["phases"], f"claim[{index}].phases")
        if (resource, owner) in seen:
            raise FrameworkError("duplicate board resource claim")
        seen.add((resource, owner))
    transport = obj(value["transport"], "board.transport")
    exact(transport, {"capabilities", "identity_hints"}, "board.transport")
    identifiers(transport["capabilities"], "board.transport.capabilities")
    hints = obj(transport["identity_hints"], "board.transport.identity_hints")
    if set(hints) - {"vid", "pid", "serial_prefix", "interface", "location"}:
        raise FrameworkError("unknown transport identity hint")
    for key, item in hints.items():
        if not isinstance(item, str) or not item or any(char.isspace() for char in item):
            raise FrameworkError(f"invalid transport identity hint: {key}")
    return value


def _role(value: Any, field: str) -> None:
    role = obj(value, field)
    exact(role, {"fragments", "legacy_profile"}, field)
    identifiers(role["fragments"], f"{field}.fragments")
    if role["legacy_profile"] is not None:
        identifier(role["legacy_profile"], f"{field}.legacy_profile")


def validate_product(value: dict[str, Any]) -> dict[str, Any]:
    exact(value, {"schema", "kind", "id", "family", "mode", "board", "boot",
                  "roles", "fragments", "features", "validation_suite",
                  "sdk_set", "sdk_lock"}, "product")
    if value["schema"] != SCHEMA or value["kind"] != "product":
        raise FrameworkError("unsupported product schema")
    for field in ("id", "family", "mode", "board"):
        identifier(value[field], f"product.{field}")
    if value["mode"] not in MODES:
        raise FrameworkError("product.mode is not in the versioned mode enum")
    if value["boot"] not in BOOTS:
        raise FrameworkError("product.boot must be raw or mcuboot")
    roles = obj(value["roles"], "product.roles")
    exact(roles, {"cp", "ap", "bl2"}, "product.roles")
    for role in roles:
        _role(roles[role], f"product.roles.{role}")
    identifiers(value["fragments"], "product.fragments")
    identifiers(value["features"], "product.features")
    if value["validation_suite"] is not None:
        identifier(value["validation_suite"], "product.validation_suite")
    _sdk_metadata_path(value["sdk_set"], "product.sdk_set")
    _sdk_metadata_path(value["sdk_lock"], "product.sdk_lock")
    return value


def validate_fragment(value: dict[str, Any]) -> dict[str, Any]:
    exact(value, {"schema", "kind", "id", "scope", "symbols", "requires"}, "fragment")
    if value["schema"] != SCHEMA or value["kind"] != "config-fragment":
        raise FrameworkError("unsupported fragment schema")
    identifier(value["id"], "fragment.id")
    identifier(value["scope"], "fragment.scope")
    if value["scope"] not in STAGES:
        raise FrameworkError(f"unsupported fragment scope: {value['scope']}")
    symbols(value["symbols"], "fragment.symbols")
    identifiers(value["requires"], "fragment.requires")
    return value


def validate_ir(value: dict[str, Any]) -> dict[str, Any]:
    exact(value, {"schema", "kind", "inputs", "fragments", "symbols",
                  "resource_claims", "source_view", "identity_sha256"}, "IR")
    if value["schema"] != SCHEMA or value["kind"] != "resolved-config-ir":
        raise FrameworkError("unsupported IR schema")
    inputs = obj(value["inputs"], "IR.inputs")
    exact(inputs, {"product", "family", "mode", "board", "role", "boot",
                   "features", "validation_suite", "legacy_profile"}, "IR.inputs")
    for field in ("product", "family", "mode", "board"):
        identifier(inputs[field], f"IR.inputs.{field}")
    identifier(inputs["role"], "IR.inputs.role")
    if inputs["role"] not in ROLES:
        raise FrameworkError("unsupported IR role")
    if inputs["mode"] not in MODES:
        raise FrameworkError("unsupported IR mode")
    if inputs["boot"] not in BOOTS:
        raise FrameworkError("unsupported IR boot")
    identifiers(inputs["features"], "IR.inputs.features")
    for field in ("validation_suite", "legacy_profile"):
        if inputs[field] is not None:
            identifier(inputs[field], f"IR.inputs.{field}")
    fragments = array(value["fragments"], "IR.fragments")
    ids: set[str] = set()
    for index, raw in enumerate(fragments):
        fragment = obj(raw, f"IR.fragments[{index}]")
        exact(fragment, {"id", "scope", "sha256"}, f"IR.fragments[{index}]")
        item_id = identifier(fragment["id"], f"IR.fragments[{index}].id")
        if item_id in ids:
            raise FrameworkError("duplicate IR fragment")
        ids.add(item_id)
        identifier(fragment["scope"], f"IR.fragments[{index}].scope")
        digest(fragment["sha256"], f"IR.fragments[{index}].sha256")
    resolved_symbols = symbols(value["symbols"], "IR.symbols")
    validate_board_selector_symbols(inputs["board"], resolved_symbols,
                                    "IR.symbols")
    claims = array(value["resource_claims"], "IR.resource_claims")
    claim_keys: set[tuple[str, str]] = set()
    for index, raw in enumerate(claims):
        claim = obj(raw, f"IR.resource_claims[{index}]")
        exact(claim, {"resource", "owner", "phases"}, f"IR.resource_claims[{index}]")
        resource = identifier(claim["resource"], f"IR.claim[{index}].resource")
        owner = identifier(claim["owner"], f"IR.claim[{index}].owner")
        identifiers(claim["phases"], f"IR.claim[{index}].phases")
        if (resource, owner) in claim_keys:
            raise FrameworkError("duplicate IR resource claim")
        claim_keys.add((resource, owner))
    source = obj(value["source_view"], "IR.source_view")
    exact(source, {"canonical_backend", "classic_backend", "board_root",
                   "board_variant", "chip_root", "role", "source_read_only",
                   "build_role_isolated", "shared_config_forbidden"},
          "IR.source_view")
    if source["canonical_backend"] != "cmake" or source["classic_backend"] != "adapter-only":
        raise FrameworkError("invalid IR backend policy")
    for field in ("board_root", "board_variant", "chip_root"):
        relative_path(source[field], f"IR.source_view.{field}")
    if source["role"] != inputs["role"]:
        raise FrameworkError("IR source role mismatch")
    for field in ("source_read_only", "build_role_isolated",
                  "shared_config_forbidden"):
        if source[field] is not True:
            raise FrameworkError(f"IR source-view policy {field} must be true")
    digest(value["identity_sha256"], "IR.identity_sha256")
    body = dict(value)
    del body["identity_sha256"]
    if sha256(canonical_json(body)) != value["identity_sha256"]:
        raise FrameworkError("IR identity mismatch")
    return value


def catalog_root(repository: Path) -> Path:
    return repository / "board/bk7258/scripts"


def _collection(root: Path, prefix: str, validator: Callable[[dict[str, Any]], dict[str, Any]]) -> dict[str, dict[str, Any]]:
    result: dict[str, dict[str, Any]] = {}
    paths = sorted(root.glob(f"bk7258_{prefix}_catalog_*.json"))
    if not paths:
        raise FrameworkError(f"empty {prefix} catalog")
    for path in paths:
        document = validator(load_json(path))
        item_id = document["id"]
        if item_id in result:
            raise FrameworkError(f"duplicate {prefix} identifier: {item_id}")
        result[item_id] = document
    return result


def load_catalog(repository: Path) -> dict[str, dict[str, dict[str, Any]]]:
    root = catalog_root(repository)
    return {
        "boards": _collection(root, "board", validate_board),
        "products": _collection(root, "product", validate_product),
        "fragments": _collection(root, "fragment", validate_fragment),
    }


def _fragment_order(value: dict[str, Any]) -> tuple[int, str]:
    return STAGES[value["scope"]], value["id"]


def _selected(catalog: dict[str, Any], product: dict[str, Any], role: str) -> list[dict[str, Any]]:
    ids = list(product["fragments"]) + list(product["roles"][role]["fragments"])
    if len(ids) != len(set(ids)):
        raise FrameworkError("product selects a fragment more than once")
    selected: dict[str, dict[str, Any]] = {}
    for item_id in ids:
        if item_id not in catalog["fragments"]:
            raise FrameworkError(f"missing selected fragment: {item_id}")
        selected[item_id] = catalog["fragments"][item_id]
    for fragment in selected.values():
        missing = sorted(set(fragment["requires"]) - set(selected))
        if missing:
            raise FrameworkError(f"fragment {fragment['id']} missing requirements: {missing}")
    ordered: list[dict[str, Any]] = []
    visiting: set[str] = set()
    visited: set[str] = set()

    def visit(item_id: str) -> None:
        if item_id in visited:
            return
        if item_id in visiting:
            raise FrameworkError(f"cyclic fragment dependency: {item_id}")
        visiting.add(item_id)
        fragment = selected[item_id]
        for requirement in sorted(fragment["requires"], key=lambda key: _fragment_order(selected[key])):
            visit(requirement)
        visiting.remove(item_id)
        visited.add(item_id)
        ordered.append(fragment)

    for fragment in sorted(selected.values(), key=_fragment_order):
        visit(fragment["id"])
    return ordered


def merge_symbols(fragments: list[dict[str, Any]]) -> dict[str, str | None]:
    result: dict[str, str | None] = {}
    for fragment in fragments:
        for key, value in sorted(fragment["symbols"].items()):
            if key in result and result[key] != value:
                raise FrameworkError(f"conflicting explicit symbol: {key}")
            result[key] = value
    return dict(sorted(result.items()))


def resolve(repository: Path, product_id: str, role: str, board_id: str | None = None,
            mode: str | None = None) -> dict[str, Any]:
    identifier(product_id, "product")
    identifier(role, "role")
    if role not in ROLES:
        raise FrameworkError("role must be cp, ap, or bl2")
    catalog = load_catalog(repository)
    if product_id not in catalog["products"]:
        raise FrameworkError(f"unknown product: {product_id}")
    product = catalog["products"][product_id]
    selected_board = product["board"]
    if board_id is not None:
        identifier(board_id, "board")
        if board_id != selected_board:
            raise FrameworkError(f"board is not exactly one: {selected_board} vs {board_id}")
    if selected_board not in catalog["boards"]:
        raise FrameworkError(f"missing board: {selected_board}")
    if mode is not None:
        identifier(mode, "mode")
        if mode != product["mode"]:
            raise FrameworkError(f"mode differs from product: {mode}")
    board = catalog["boards"][selected_board]
    fragments = _selected(catalog, product, role)
    merged_symbols = merge_symbols(fragments)
    validate_board_selector_symbols(board["id"], merged_symbols,
                                    "resolved symbols")
    body: dict[str, Any] = {
        "schema": SCHEMA,
        "kind": "resolved-config-ir",
        "inputs": {
            "product": product["id"], "family": product["family"], "mode": product["mode"],
            "board": board["id"], "role": role, "boot": product["boot"],
            "features": product["features"], "validation_suite": product["validation_suite"],
            "legacy_profile": product["roles"][role]["legacy_profile"],
        },
        "fragments": [{"id": item["id"], "scope": item["scope"],
                       "sha256": sha256(canonical_json(item))} for item in fragments],
        "symbols": merged_symbols,
        "resource_claims": sorted((dict(item) for item in board["resource_claims"]),
                                   key=lambda item: (item["resource"], item["owner"])),
        "source_view": {
            "canonical_backend": "cmake", "classic_backend": "adapter-only",
            "board_root": "board/bk7258", "board_variant": f"board/bk7258/boards/{board['variant']}",
            "chip_root": "board/bk7258/chip", "role": role,
            "source_read_only": True, "build_role_isolated": True,
            "shared_config_forbidden": True,
        },
    }
    result = dict(body)
    result["identity_sha256"] = sha256(canonical_json(body))
    return validate_ir(result)


def cmake_view(ir: dict[str, Any]) -> str:
    validate_ir(ir)
    inputs = ir["inputs"]
    source = ir["source_view"]
    role = inputs["role"]
    lines = ["# Generated by bk7258_framework.py; do not edit.",
             "# This is an adapter include; it does not invoke the legacy builder.",
             f"set(BK7258_COMPOSITION_IR_SHA256 \"{ir['identity_sha256']}\")"]
    for key in ("product", "family", "mode", "board", "role", "boot"):
        lines.append(f"set(BK7258_COMPOSITION_{key.upper()} \"{inputs[key]}\")")
    lines += [
        "set(BK7258_COMPOSITION_CANONICAL_BACKEND \"cmake\")",
        "set(BK7258_COMPOSITION_CLASSIC_MODE \"adapter-only\")",
        f"set(BK7258_COMPOSITION_SOURCE_ROOT \"${{CMAKE_SOURCE_DIR}}/{source['board_root']}\")",
        f"set(BK7258_COMPOSITION_BOARD_VARIANT_ROOT \"${{CMAKE_SOURCE_DIR}}/{source['board_variant']}\")",
        f"set(BK7258_COMPOSITION_CHIP_ROOT \"${{CMAKE_SOURCE_DIR}}/{source['chip_root']}\")",
        # The source view is the existing read-only board tree.  Role
        # selection is carried by the resolved symbols; no source directory
        # is copied or invented for a role.
        "set(BK7258_COMPOSITION_ROLE_SOURCE_VIEW \"${BK7258_COMPOSITION_SOURCE_ROOT}\")",
        f"set(BK7258_COMPOSITION_ROLE_BUILD_VIEW \"${{CMAKE_BINARY_DIR}}/bk7258-role-{role}\")",
        "set(BK7258_COMPOSITION_ROLE_CONFIG \"${BK7258_COMPOSITION_ROLE_BUILD_VIEW}/.config\")",
        "set(BK7258_COMPOSITION_ROLE_ARTIFACTS \"${BK7258_COMPOSITION_ROLE_BUILD_VIEW}/artifacts\")",
        "set(BK7258_COMPOSITION_SOURCE_READ_ONLY TRUE)",
        "set(BK7258_COMPOSITION_BUILD_ROLE_ISOLATED TRUE)",
        "set(BK7258_COMPOSITION_SHARED_CONFIG_FORBIDDEN TRUE)",
        "set(BK7258_COMPOSITION_LEGACY_BUILDER_INVOKED FALSE)",
        "file(MAKE_DIRECTORY \"${BK7258_COMPOSITION_ROLE_BUILD_VIEW}\")",
        "file(MAKE_DIRECTORY \"${BK7258_COMPOSITION_ROLE_ARTIFACTS}\")",
        ""]
    return "\n".join(lines)


def role_view_manifest(ir: dict[str, Any]) -> dict[str, Any]:
    """Describe an isolated CMake role view without touching the legacy tree."""
    validate_ir(ir)
    source = ir["source_view"]
    role = ir["inputs"]["role"]
    body: dict[str, Any] = {
        "schema": SCHEMA,
        "kind": "role-source-build-view",
        "ir_identity_sha256": ir["identity_sha256"],
        "role": role,
        "source_view": {
            "root": source["board_root"],
            "board_variant": source["board_variant"],
            "chip_root": source["chip_root"],
            "materialized": False,
            "read_only": True,
        },
        "build_view": {
            "root_template": f"${{CMAKE_BINARY_DIR}}/bk7258-role-{role}",
            "config_template": "${BK7258_COMPOSITION_ROLE_BUILD_VIEW}/.config",
            "artifacts_template": "${BK7258_COMPOSITION_ROLE_BUILD_VIEW}/artifacts",
            "role_local": True,
            "shared_config": False,
        },
        "legacy_semantics": {
            "builder": "board/bk7258/scripts/build_dual_image.sh",
            "invoked": False,
            "modified": False,
        },
    }
    result = dict(body)
    result["identity_sha256"] = sha256(canonical_json(body))
    return validate_role_view(result)


def validate_role_view(value: dict[str, Any]) -> dict[str, Any]:
    exact(value, {"schema", "kind", "ir_identity_sha256", "role", "source_view",
                  "build_view", "legacy_semantics", "identity_sha256"},
          "role source/build view")
    if value["schema"] != SCHEMA or value["kind"] != "role-source-build-view":
        raise FrameworkError("unsupported role source/build view schema")
    digest(value["ir_identity_sha256"], "role view IR identity")
    role = identifier(value["role"], "role view role")
    if role not in ROLES:
        raise FrameworkError("unsupported role view role")
    source = obj(value["source_view"], "role view source")
    exact(source, {"root", "board_variant", "chip_root", "materialized", "read_only"},
          "role view source")
    for field in ("root", "board_variant", "chip_root"):
        relative_path(source[field], f"role view source.{field}")
    if source["materialized"] is not False or source["read_only"] is not True:
        raise FrameworkError("role source view must be existing and read-only")
    build = obj(value["build_view"], "role view build")
    exact(build, {"root_template", "config_template", "artifacts_template",
                  "role_local", "shared_config"}, "role view build")
    for field in ("root_template", "config_template", "artifacts_template"):
        if not isinstance(build[field], str) or not build[field]:
            raise FrameworkError(f"invalid role view build template: {field}")
    if build["role_local"] is not True or build["shared_config"] is not False:
        raise FrameworkError("role build view must be isolated")
    legacy = obj(value["legacy_semantics"], "role view legacy semantics")
    exact(legacy, {"builder", "invoked", "modified"}, "role view legacy semantics")
    if legacy["builder"] != "board/bk7258/scripts/build_dual_image.sh":
        raise FrameworkError("unexpected legacy builder binding")
    if legacy["invoked"] is not False or legacy["modified"] is not False:
        raise FrameworkError("role view must not alter legacy semantics")
    digest(value["identity_sha256"], "role view identity")
    body = dict(value)
    del body["identity_sha256"]
    if sha256(canonical_json(body)) != value["identity_sha256"]:
        raise FrameworkError("role view identity mismatch")
    return value


def classic_report(repository: Path) -> dict[str, Any]:
    source = "board/bk7258/scripts/build_dual_image.sh"
    if not (repository / source).is_file():
        raise FrameworkError(f"missing Classic source: {source}")
    body = {"schema": SCHEMA, "kind": "classic-isolation-feasibility",
            "status": "feasible-with-adapter", "proven": False,
            "backend": "classic-make",
            "canonical_backend_candidate": "cmake", "source": source,
            "observed_risks": [
                "legacy CP/AP selection is environment-driven",
                "legacy builder configures shared NuttX/apps trees",
                "legacy dual build restores shared configuration",
                "minimal BL2 Makefile has no SDK consumption",
            ],
            "required_adapter_contract": [
                "resolve one immutable IR before backend selection",
                "allocate independent BL1/BL2/CP/AP output directories",
                "never share or restore a root .config",
                "pass a standard defconfig/.config to NuttX",
            ], "repository_relative_source_view": True,
            "isolation_proven": False,
            "build_semantics_changed": False}
    result = dict(body)
    result["identity_sha256"] = sha256(canonical_json(body))
    return validate_classic_report(result)


def validate_classic_report(value: dict[str, Any]) -> dict[str, Any]:
    exact(value, {"schema", "kind", "status", "proven", "backend",
                  "canonical_backend_candidate", "source", "observed_risks",
                  "required_adapter_contract", "repository_relative_source_view",
                  "isolation_proven", "build_semantics_changed", "identity_sha256"},
          "Classic feasibility report")
    if value["schema"] != SCHEMA or value["kind"] != "classic-isolation-feasibility":
        raise FrameworkError("unsupported Classic feasibility report")
    if value["status"] != "feasible-with-adapter" or value["proven"] is not False:
        raise FrameworkError("Classic feasibility must remain unproven")
    if value["backend"] != "classic-make" or value["canonical_backend_candidate"] != "cmake":
        raise FrameworkError("Classic backend report binding mismatch")
    relative_path(value["source"], "Classic report source")
    if not isinstance(value["observed_risks"], list) or not value["observed_risks"]:
        raise FrameworkError("Classic report risks are missing")
    if not isinstance(value["required_adapter_contract"], list) or not value["required_adapter_contract"]:
        raise FrameworkError("Classic adapter contract is missing")
    if value["repository_relative_source_view"] is not True or value["isolation_proven"] is not False:
        raise FrameworkError("Classic isolation report must not claim proof")
    if value["build_semantics_changed"] is not False:
        raise FrameworkError("Classic semantics change is not allowed")
    digest(value["identity_sha256"], "Classic report identity")
    body = dict(value)
    del body["identity_sha256"]
    if sha256(canonical_json(body)) != value["identity_sha256"]:
        raise FrameworkError("Classic report identity mismatch")
    return value


def _sdk_regular(path: Path, field: str) -> None:
    try:
        mode = path.lstat().st_mode
    except OSError as error:
        raise FrameworkError(f"missing {field}: {path}") from error
    if stat.S_ISLNK(mode) or not stat.S_ISREG(mode):
        raise FrameworkError(f"{field} must be a regular non-symlink file: {path}")


def _sdk_directory(path: Path, field: str) -> None:
    try:
        mode = path.lstat().st_mode
    except OSError as error:
        raise FrameworkError(f"missing {field}: {path}") from error
    if stat.S_ISLNK(mode) or not stat.S_ISDIR(mode):
        raise FrameworkError(f"{field} must be a real directory: {path}")


def _sdk_file_sha256(path: Path, field: str) -> str:
    _sdk_regular(path, field)
    try:
        return sha256(path.read_bytes())
    except (OSError, UnicodeError) as error:
        raise FrameworkError(f"cannot read {field}: {path}") from error


def _sdk_provenance(path: Path) -> dict[str, str]:
    _sdk_regular(path, "SDK provenance")
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        raise FrameworkError(f"cannot read SDK provenance: {path}") from error
    if not text.endswith("\n"):
        raise FrameworkError(f"SDK provenance must end with a newline: {path}")
    result: dict[str, str] = {}
    for index, line in enumerate(text.splitlines(), 1):
        if not line or "=" not in line:
            raise FrameworkError(f"malformed SDK provenance line {index}: {path}")
        key, value = line.split("=", 1)
        if not TOKEN_RE.fullmatch(key) or key in result or "\x00" in value:
            raise FrameworkError(f"invalid or duplicate SDK provenance key: {path}:{index}")
        result[key] = value
    return result


def _sdk_manifest_entries(path: Path) -> dict[str, str]:
    _sdk_regular(path, "SDK checksum manifest")
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        raise FrameworkError(f"cannot read SDK checksum manifest: {path}") from error
    if not text.endswith("\n"):
        raise FrameworkError(f"SDK checksum manifest must end with a newline: {path}")
    result: dict[str, str] = {}
    for index, line in enumerate(text.splitlines(), 1):
        match = re.fullmatch(r"([0-9a-f]{64})  ([^ \t].*)", line)
        if match is None:
            raise FrameworkError(f"malformed SDK checksum line {index}: {path}")
        item_hash, item_path = match.groups()
        relative_path(item_path, f"SDK checksum path {path}:{index}")
        if item_path.split("/", 1)[0] not in SDK_REQUIRED_DIRS:
            raise FrameworkError(f"SDK checksum path escapes bundle roots: {item_path}")
        if item_path in result:
            raise FrameworkError(f"duplicate SDK checksum path: {item_path}")
        result[item_path] = item_hash
    if not result:
        raise FrameworkError(f"SDK checksum manifest is empty: {path}")
    return result


def _sdk_entry_id(value: Any, field: str) -> str:
    if not isinstance(value, str) or not re.fullmatch(r"sha256:[0-9a-f]{64}", value):
        raise FrameworkError(f"invalid content-addressed SDK id in {field}")
    return value


def _sdk_version(value: Any, field: str) -> str:
    if not isinstance(value, str) or not SDK_VERSION_RE.fullmatch(value):
        raise FrameworkError(f"invalid SDK version in {field}")
    return value


def _sdk_path(value: Any, field: str) -> str:
    path = relative_path(value, field)
    if not path.startswith(SDK_MANIFEST_ROOT + "/"):
        raise FrameworkError(f"SDK metadata path is outside manifest root: {field}")
    return path


def validate_sdk_registry(repository: Path, value: dict[str, Any]) -> dict[str, Any]:
    exact(value, {"schema", "kind", "version", "policy", "entries"}, "SDK registry")
    if value["schema"] != SDK_REGISTRY_SCHEMA or value["kind"] != "sdk-registry" or value["version"] != 1:
        raise FrameworkError("unsupported SDK registry schema")
    policy = obj(value["policy"], "SDK registry policy")
    exact(policy, {"content_addressed", "sdk_bytes_tracked", "replacement", "network", "private_mirror"},
          "SDK registry policy")
    if policy["content_addressed"] is not True or policy["sdk_bytes_tracked"] is not False:
        raise FrameworkError("SDK registry must be content-addressed metadata only")
    if policy["replacement"] != "forbidden" or policy["network"] != "forbidden":
        raise FrameworkError("SDK registry replacement/network policy is unsafe")
    mirror = obj(policy["private_mirror"], "SDK private mirror")
    exact(mirror, {"url", "destination", "redistribution_authorized"}, "SDK private mirror")
    if (mirror["url"] != PRIVATE_MIRROR_URL or mirror["destination"] != "metadata-only" or
            mirror["redistribution_authorized"] is not False):
        raise FrameworkError("private mirror is not metadata-only and unauthorized")
    entries = array(value["entries"], "SDK registry entries")
    if not entries:
        raise FrameworkError("SDK registry has no entries")
    ids: set[str] = set()
    keys: set[tuple[str, str]] = set()
    for index, raw in enumerate(entries):
        entry = obj(raw, f"SDK registry entry {index}")
        exact(entry, {"id", "version", "role", "artifact_kind", "provenance_kind",
                      "source_reproducible", "manifest_path", "provenance_path",
                      "manifest_sha256", "provenance_sha256", "content_digest",
                      "source_archive_sha256", "parent_id"}, f"SDK registry entry {index}")
        entry_id = _sdk_entry_id(entry["id"], f"entry {index}.id")
        if entry_id in ids:
            raise FrameworkError("duplicate SDK registry id")
        ids.add(entry_id)
        version = _sdk_version(entry["version"], f"entry {index}.version")
        role = identifier(entry["role"], f"entry {index}.role")
        if role not in SDK_ROLES or (version, role) in keys:
            raise FrameworkError("duplicate or unsupported SDK registry version/role")
        keys.add((version, role))
        if entry["artifact_kind"] != "sdk-bundle" or entry["provenance_kind"] not in SDK_ENTRY_KINDS:
            raise FrameworkError("unsupported SDK artifact/provenance kind")
        if not isinstance(entry["source_reproducible"], bool):
            raise FrameworkError("SDK source_reproducible must be boolean")
        manifest_rel = _sdk_path(entry["manifest_path"], f"entry {index}.manifest_path")
        provenance_rel = _sdk_path(entry["provenance_path"], f"entry {index}.provenance_path")
        manifest_path = repository / manifest_rel
        provenance_path = repository / provenance_rel
        manifest_hash = _sdk_file_sha256(manifest_path, "SDK checksum manifest")
        provenance_hash = _sdk_file_sha256(provenance_path, "SDK provenance")
        if manifest_hash != entry["manifest_sha256"] or provenance_hash != entry["provenance_sha256"]:
            raise FrameworkError(f"SDK registry metadata digest mismatch: {entry_id}")
        if entry["content_digest"] != entry_id or entry["content_digest"] != f"sha256:{manifest_hash}":
            raise FrameworkError(f"SDK content id is not bound to its manifest: {entry_id}")
        digest(entry["manifest_sha256"], f"entry {index}.manifest_sha256")
        digest(entry["provenance_sha256"], f"entry {index}.provenance_sha256")
        _sdk_manifest_entries(manifest_path)
        provenance = _sdk_provenance(provenance_path)
        if provenance.get("bundle_version") != version or provenance.get("role") != role:
            raise FrameworkError(f"SDK provenance identity mismatch: {entry_id}")
        if provenance.get("final_manifest_sha256") != manifest_hash:
            raise FrameworkError(f"SDK provenance manifest binding mismatch: {entry_id}")
        archive = provenance.get("source_archive")
        archive_hash = provenance.get("source_archive_sha256")
        if entry["source_reproducible"]:
            if (entry["provenance_kind"] != "official" or not archive or
                    archive in {"not-provided", "not-recorded"} or
                    not archive_hash or not HASH_RE.fullmatch(archive_hash)):
                raise FrameworkError(f"source-reproducible SDK lacks source archive proof: {entry_id}")
        elif (entry["provenance_kind"] == "official" or
              archive not in {"not-provided", "not-recorded"} or
              archive_hash not in {"not-provided", "not-recorded"}):
            raise FrameworkError(f"sealed/derived SDK source claim is inconsistent: {entry_id}")
        parent = entry["parent_id"]
        if entry["provenance_kind"] == "derived":
            if parent is None or parent == entry_id:
                raise FrameworkError(f"derived SDK must name a distinct parent: {entry_id}")
            _sdk_entry_id(parent, f"entry {index}.parent_id")
        elif parent is not None:
            raise FrameworkError(f"only derived SDK entries may have a parent: {entry_id}")
    for entry in entries:
        if entry["provenance_kind"] == "derived" and entry["parent_id"] not in ids:
            raise FrameworkError(f"derived SDK parent is not in registry: {entry['id']}")
    return value


def validate_sdk_set(value: dict[str, Any], registry: dict[str, Any]) -> dict[str, Any]:
    exact(value, {"schema", "kind", "version", "id", "board", "product", "mode", "roles"},
          "SDK set")
    if value["schema"] != SDK_SET_SCHEMA or value["kind"] != "sdk-set" or value["version"] != 1:
        raise FrameworkError("unsupported SDK set schema")
    identifier(value["id"], "SDK set.id")
    for field in ("board", "product", "mode"):
        identifier(value[field], f"SDK set.{field}")
    if value["mode"] not in MODES:
        raise FrameworkError("SDK set mode is not in the versioned mode enum")
    roles = obj(value["roles"], "SDK set.roles")
    exact(roles, {"cp", "ap", "bl2"}, "SDK set.roles")
    registry_ids = {_sdk_entry_id(item["id"], "registry entry") for item in registry["entries"]}
    for role in ("cp", "ap"):
        _sdk_entry_id(roles[role], f"SDK set.roles.{role}")
        if roles[role] not in registry_ids:
            raise FrameworkError(f"SDK set references unknown {role} entry")
    if roles["bl2"] is not None:
        raise FrameworkError("BL2 must have no runtime SDK")
    return value


def validate_sdk_lock(repository: Path, registry_path: Path, set_path: Path,
                      value: dict[str, Any], registry: dict[str, Any],
                      sdk_set: dict[str, Any]) -> dict[str, Any]:
    exact(value, {"schema", "kind", "version", "id", "set_id", "registry_path",
                  "registry_sha256", "set_path", "set_sha256", "roles",
                  "no_runtime_sdk_roles", "identity_sha256"}, "SDK lock")
    if value["schema"] != SDK_LOCK_SCHEMA or value["kind"] != "sdk-lock" or value["version"] != 1:
        raise FrameworkError("unsupported SDK lock schema")
    identifier(value["id"], "SDK lock.id")
    if value["set_id"] != sdk_set["id"]:
        raise FrameworkError("SDK lock set binding mismatch")
    if value["registry_path"] != "board/bk7258/scripts/bk7258_sdk_registry.json":
        raise FrameworkError("SDK lock registry path mismatch")
    try:
        expected_set_path = set_path.resolve().relative_to(repository.resolve()).as_posix()
    except ValueError as error:
        raise FrameworkError("SDK lock set path is outside the repository") from error
    if value["set_path"] != expected_set_path:
        raise FrameworkError("SDK lock set path mismatch")
    digest(value["registry_sha256"], "SDK lock registry_sha256")
    digest(value["set_sha256"], "SDK lock set_sha256")
    if _sdk_file_sha256(registry_path, "SDK registry") != value["registry_sha256"]:
        raise FrameworkError("SDK lock registry digest mismatch")
    if _sdk_file_sha256(set_path, "SDK set") != value["set_sha256"]:
        raise FrameworkError("SDK lock set digest mismatch")
    roles = obj(value["roles"], "SDK lock.roles")
    exact(roles, {"cp", "ap", "bl2"}, "SDK lock.roles")
    by_id = {_sdk_entry_id(item["id"], "registry entry"): item for item in registry["entries"]}
    for role in ("cp", "ap"):
        row = obj(roles[role], f"SDK lock.roles.{role}")
        exact(row, {"registry_id", "manifest_sha256", "provenance_sha256"}, f"SDK lock.roles.{role}")
        if row["registry_id"] != sdk_set["roles"][role] or row["registry_id"] not in by_id:
            raise FrameworkError(f"SDK lock {role} registry binding mismatch")
        entry = by_id[row["registry_id"]]
        if row["manifest_sha256"] != entry["manifest_sha256"] or row["provenance_sha256"] != entry["provenance_sha256"]:
            raise FrameworkError(f"SDK lock {role} digest binding mismatch")
        digest(row["manifest_sha256"], f"SDK lock {role} manifest")
        digest(row["provenance_sha256"], f"SDK lock {role} provenance")
    if roles["bl2"] != {"registry_id": None, "manifest_sha256": None, "provenance_sha256": None}:
        raise FrameworkError("SDK lock must encode BL2 as no-runtime-SDK")
    if value["no_runtime_sdk_roles"] != ["bl2"]:
        raise FrameworkError("SDK lock no-runtime roles must contain only BL2")
    digest(value["identity_sha256"], "SDK lock identity")
    body = dict(value)
    del body["identity_sha256"]
    if sha256(canonical_json(body)) != value["identity_sha256"]:
        raise FrameworkError("SDK lock identity mismatch")
    return value


def verify_sdk_bundle(repository: Path, entry: dict[str, Any], bundle_dir: Path) -> dict[str, Any]:
    """Verify one external SDK bundle without copying or modifying it."""
    _sdk_directory(bundle_dir, "SDK bundle root")
    manifest_path = repository / entry["manifest_path"]
    expected = _sdk_manifest_entries(manifest_path)
    top_level = list(bundle_dir.iterdir())
    names = {path.name for path in top_level}
    if names != SDK_REQUIRED_DIRS or len(top_level) != len(names):
        raise FrameworkError("SDK bundle has missing or extra top-level entries")
    for path in top_level:
        _sdk_directory(path, "SDK bundle root entry")
    actual: dict[str, Path] = {}

    def visit(directory: Path, prefix: str) -> None:
        try:
            children = list(directory.iterdir())
        except OSError as error:
            raise FrameworkError(f"cannot scan SDK bundle: {directory}") from error
        for child in children:
            relative = f"{prefix}/{child.name}" if prefix else child.name
            try:
                mode = child.lstat().st_mode
            except OSError as error:
                raise FrameworkError(f"cannot stat SDK bundle entry: {child}") from error
            if stat.S_ISLNK(mode) or stat.S_ISSOCK(mode) or stat.S_ISFIFO(mode) or stat.S_ISCHR(mode) or stat.S_ISBLK(mode):
                raise FrameworkError(f"symlink or special SDK bundle entry: {relative}")
            if stat.S_ISDIR(mode):
                visit(child, relative)
            elif stat.S_ISREG(mode):
                if relative in actual:
                    raise FrameworkError(f"duplicate SDK bundle path: {relative}")
                actual[relative] = child
            else:
                raise FrameworkError(f"unsupported SDK bundle entry: {relative}")

    for name in sorted(SDK_REQUIRED_DIRS):
        visit(bundle_dir / name, name)
    if set(actual) != set(expected):
        missing = sorted(set(expected) - set(actual))
        extra = sorted(set(actual) - set(expected))
        raise FrameworkError(f"SDK bundle file set mismatch: missing={missing[:3]} extra={extra[:3]}")
    for relative, path in actual.items():
        observed = _sdk_file_sha256(path, f"SDK bundle file {relative}")
        if observed != expected[relative]:
            raise FrameworkError(f"SDK bundle checksum mismatch: {relative}")
    return {"entry_id": entry["id"], "file_count": len(actual), "manifest_sha256": entry["manifest_sha256"]}


def sdk_import_receipt(entry: dict[str, Any], result: dict[str, Any]) -> dict[str, Any]:
    body = {
        "schema": SDK_IMPORT_SCHEMA,
        "kind": "sdk-import-receipt",
        "registry_id": entry["id"],
        "content_digest": entry["content_digest"],
        "manifest_sha256": entry["manifest_sha256"],
        "provenance_sha256": entry["provenance_sha256"],
        "source_reproducible": entry["source_reproducible"],
        "file_count": result["file_count"],
        "bytes_copied": False,
        "network_used": False,
        "replacement": "forbidden",
    }
    output = dict(body)
    output["identity_sha256"] = sha256(canonical_json(body))
    return validate_sdk_import_receipt(output)


def validate_sdk_import_receipt(value: dict[str, Any]) -> dict[str, Any]:
    exact(value, {"schema", "kind", "registry_id", "content_digest", "manifest_sha256",
                  "provenance_sha256", "source_reproducible", "file_count",
                  "bytes_copied", "network_used", "replacement", "identity_sha256"},
          "SDK import receipt")
    if value["schema"] != SDK_IMPORT_SCHEMA or value["kind"] != "sdk-import-receipt":
        raise FrameworkError("unsupported SDK import receipt schema")
    _sdk_entry_id(value["registry_id"], "SDK receipt.registry_id")
    if value["content_digest"] != value["registry_id"]:
        raise FrameworkError("SDK receipt content identity mismatch")
    digest(value["manifest_sha256"], "SDK receipt manifest")
    digest(value["provenance_sha256"], "SDK receipt provenance")
    if (not isinstance(value["source_reproducible"], bool) or
            not isinstance(value["file_count"], int) or
            isinstance(value["file_count"], bool) or value["file_count"] <= 0):
        raise FrameworkError("SDK receipt counts/provenance are malformed")
    if value["bytes_copied"] is not False or value["network_used"] is not False or value["replacement"] != "forbidden":
        raise FrameworkError("SDK receipt records an unsafe import")
    digest(value["identity_sha256"], "SDK receipt identity")
    body = dict(value)
    del body["identity_sha256"]
    if sha256(canonical_json(body)) != value["identity_sha256"]:
        raise FrameworkError("SDK receipt identity mismatch")
    return value


def _defconfig_text(inputs: dict[str, Any], config_symbols: dict[str, str | None],
                    ir_identity: str) -> str:
    lines = [
        "# Generated by bk7258_framework.py; do not edit.",
        f"# BK7258_IR_IDENTITY_SHA256={ir_identity}",
        f"# BK7258_PRODUCT={inputs['product']}",
        f"# BK7258_MODE={inputs['mode']}",
        f"# BK7258_BOARD={inputs['board']}",
        f"# BK7258_ROLE={inputs['role']}",
    ]
    for key, value in sorted(config_symbols.items()):
        lines.append(f"# {key} is not set" if value is None else f"{key}={value}")
    return "\n".join(lines) + "\n"


def config_document(ir: dict[str, Any]) -> dict[str, Any]:
    """Generate a deterministic role config document without touching configs/."""
    validate_ir(ir)
    body: dict[str, Any] = {
        "schema": CONFIG_SCHEMA,
        "kind": "resolved-role-config",
        "version": 1,
        "ir_identity_sha256": ir["identity_sha256"],
        "inputs": dict(ir["inputs"]),
        "symbols": dict(ir["symbols"]),
        "defconfig": _defconfig_text(ir["inputs"], ir["symbols"], ir["identity_sha256"]),
    }
    body["defconfig_sha256"] = sha256(body["defconfig"].encode())
    result = dict(body)
    result["identity_sha256"] = sha256(canonical_json(body))
    return validate_config_document(result)


def validate_config_document(value: dict[str, Any]) -> dict[str, Any]:
    exact(value, {"schema", "kind", "version", "ir_identity_sha256", "inputs",
                  "symbols", "defconfig", "defconfig_sha256", "identity_sha256"},
          "resolved role config")
    if value["schema"] != CONFIG_SCHEMA or value["kind"] != "resolved-role-config" or value["version"] != 1:
        raise FrameworkError("unsupported resolved role config schema")
    digest(value["ir_identity_sha256"], "role config IR identity")
    inputs = obj(value["inputs"], "role config inputs")
    exact(inputs, {"product", "family", "mode", "board", "role", "boot",
                   "features", "validation_suite", "legacy_profile"}, "role config inputs")
    for field in ("product", "family", "mode", "board", "role"):
        identifier(inputs[field], f"role config inputs.{field}")
    if inputs["role"] not in ROLES:
        raise FrameworkError("unsupported role config role")
    if inputs["mode"] not in MODES or inputs["boot"] not in BOOTS:
        raise FrameworkError("unsupported role config mode/boot")
    identifiers(inputs["features"], "role config features")
    for field in ("validation_suite", "legacy_profile"):
        if inputs[field] is not None:
            identifier(inputs[field], f"role config {field}")
    config_symbols = symbols(value["symbols"], "role config symbols")
    validate_board_selector_symbols(inputs["board"], config_symbols,
                                    "role config symbols")
    if not isinstance(value["defconfig"], str) or "\x00" in value["defconfig"]:
        raise FrameworkError("role config defconfig is malformed")
    digest(value["defconfig_sha256"], "role config defconfig_sha256")
    if sha256(value["defconfig"].encode()) != value["defconfig_sha256"]:
        raise FrameworkError("role config defconfig digest mismatch")
    expected = _defconfig_text(inputs, config_symbols, value["ir_identity_sha256"])
    if value["defconfig"] != expected:
        raise FrameworkError("role config defconfig is not canonical")
    digest(value["identity_sha256"], "role config identity")
    body = dict(value)
    del body["identity_sha256"]
    if sha256(canonical_json(body)) != value["identity_sha256"]:
        raise FrameworkError("role config identity mismatch")
    return value


def _boot_config_identity(product_ir: dict[str, Any], role: str, makefile: str,
                          kind: str) -> str:
    body = {
        "schema": CONFIG_SCHEMA,
        "kind": kind,
        "version": 1,
        "product": product_ir["inputs"]["product"],
        "family": product_ir["inputs"]["family"],
        "mode": product_ir["inputs"]["mode"],
        "board": product_ir["inputs"]["board"],
        "role": role,
        "makefile": makefile,
        "sdk": None,
        "fake_nuttx_seed": False,
    }
    return sha256(canonical_json(body))


def _plan_source_views(board_variant: str) -> dict[str, dict[str, Any]]:
    board_root = "board/bk7258"
    variant_root = f"{board_root}/boards/{board_variant}"
    common = [f"{board_root}/src", f"{board_root}/chip/common", variant_root]
    roots = {
        "bl1": [f"{board_root}/bootloader"],
        "bl2": [f"{board_root}/bootloader/bl2"],
        "cp": common + [f"{board_root}/chip/cp"],
        "ap": common + [f"{board_root}/chip/ap"],
    }
    return {
        role: {
            "view_id": f"bk7258-role-source-{role}",
            "roots": values,
            "materialized": False,
            "read_only": True,
        }
        for role, values in roots.items()
    }


def _load_plan_sdk(repository: Path, set_path: Path | None = None,
                   lock_path: Path | None = None) -> tuple[dict[str, Any], dict[str, Any]]:
    registry_path = repository / "board/bk7258/scripts/bk7258_sdk_registry.json"
    actual_set_path = set_path or repository / "board/bk7258/scripts/bk7258_sdk_set.json"
    actual_lock_path = lock_path or repository / "board/bk7258/scripts/bk7258_sdk_lock.json"
    if not actual_set_path.is_absolute():
        actual_set_path = repository / actual_set_path
    if not actual_lock_path.is_absolute():
        actual_lock_path = repository / actual_lock_path
    registry = validate_sdk_registry(repository, load_json_checked(registry_path, "SDK registry"))
    sdk_set = validate_sdk_set(load_json_checked(actual_set_path, "SDK set"), registry)
    lock = validate_sdk_lock(repository, registry_path, actual_set_path,
                             load_json_checked(actual_lock_path, "SDK lock"), registry, sdk_set)
    return sdk_set, lock


def build_plan(repository: Path, product_id: str, board_id: str | None = None,
               mode: str | None = None, set_path: Path | None = None,
               lock_path: Path | None = None) -> dict[str, Any]:
    """Resolve all boot/runtime roles into an isolated, metadata-only plan."""
    cp_ir = resolve(repository, product_id, "cp", board_id, mode)
    ap_ir = resolve(repository, product_id, "ap", board_id, mode)
    bl2_ir = resolve(repository, product_id, "bl2", board_id, mode)
    for other in (ap_ir, bl2_ir):
        if other["inputs"]["product"] != cp_ir["inputs"]["product"] or \
                other["inputs"]["family"] != cp_ir["inputs"]["family"] or \
                other["inputs"]["mode"] != cp_ir["inputs"]["mode"] or \
                other["inputs"]["board"] != cp_ir["inputs"]["board"] or \
                other["inputs"]["boot"] != cp_ir["inputs"]["boot"]:
            raise FrameworkError("role resolution produced mismatched product inputs")
    product_catalog = load_catalog(repository)["products"]
    product_metadata = product_catalog.get(product_id)
    if product_metadata is None:
        raise FrameworkError(f"unknown product: {product_id}")
    if set_path is None:
        set_path = repository / product_metadata["sdk_set"]
    if lock_path is None:
        lock_path = repository / product_metadata["sdk_lock"]
    sdk_set, lock = _load_plan_sdk(repository, set_path, lock_path)
    inputs = cp_ir["inputs"]
    if (sdk_set["product"] != inputs["product"] or sdk_set["board"] != inputs["board"] or
            sdk_set["mode"] != inputs["mode"]):
        raise FrameworkError("SDK set does not exactly match resolved product/mode/board")
    role_ir = {"cp": cp_ir, "ap": ap_ir, "bl2": bl2_ir}
    config_ids = {
        "cp": config_document(cp_ir)["identity_sha256"],
        "ap": config_document(ap_ir)["identity_sha256"],
        "bl1": _boot_config_identity(cp_ir, "bl1", "board/bk7258/bootloader/Makefile", "boot-policy"),
        "bl2": _boot_config_identity(cp_ir, "bl2", "board/bk7258/bootloader/bl2/Makefile", "minimal-make-inputs"),
    }
    board_variant = cp_ir["source_view"]["board_variant"].rsplit("/", 1)[-1]
    source_views = _plan_source_views(board_variant)
    for source in source_views.values():
        for source_root in source["roots"]:
            _sdk_directory(repository / source_root, "build plan source root")
    build_roles: dict[str, dict[str, Any]] = {}
    for role in ("bl1", "bl2", "cp", "ap"):
        root_template = f"${{BUILD_ROOT}}/bk7258/{inputs['product']}/{inputs['mode']}/{role}"
        sdk = None if role in {"bl1", "bl2"} else dict(lock["roles"][role])
        build_roles[role] = {
            "source_view_id": source_views[role]["view_id"],
            "build_root_template": root_template,
            "artifact_root_template": f"{root_template}/artifacts",
            "config_path_template": f"{root_template}/.config",
            "config_kind": "nuttx-defconfig" if role in {"cp", "ap"} else
                           ("boot-policy" if role == "bl1" else "minimal-make-inputs"),
            "config_identity_sha256": config_ids[role],
            "sdk": sdk,
            "backend": "cmake" if role in {"cp", "ap"} else
                        ("bootloader-adapter" if role == "bl1" else "minimal-make"),
            "fake_nuttx_seed": False,
        }
    body: dict[str, Any] = {
        "schema": BUILD_PLAN_SCHEMA,
        "kind": "isolated-build-plan",
        "version": 1,
        "identity_inputs": {
            "product": inputs["product"],
            "family": inputs["family"],
            "mode": inputs["mode"],
            "board": inputs["board"],
            "boot": inputs["boot"],
            "role_ir_sha256": {role: role_ir[role]["identity_sha256"] for role in ("cp", "ap", "bl2")},
            "sdk_set_id": sdk_set["id"],
            "sdk_lock_id": lock["id"],
        },
        "board": {"id": inputs["board"], "variant": board_variant},
        "sdk": {
            "set_id": sdk_set["id"],
            "lock_id": lock["id"],
            "lock_identity_sha256": lock["identity_sha256"],
            "roles": {role: sdk_set["roles"][role] for role in ("cp", "ap", "bl2")},
        },
        "source_views": source_views,
        "roles": build_roles,
        "legacy_adapter": {
            "builder": "board/bk7258/scripts/build_dual_image.sh",
            "mode": "compatibility-only",
            "invoked": False,
            "modified": False,
        },
    }
    result = dict(body)
    result["identity_sha256"] = sha256(canonical_json(body))
    return validate_build_plan(result)


def validate_build_plan(value: dict[str, Any]) -> dict[str, Any]:
    exact(value, {"schema", "kind", "version", "identity_inputs", "board", "sdk",
                  "source_views", "roles", "legacy_adapter", "identity_sha256"},
          "isolated build plan")
    if value["schema"] != BUILD_PLAN_SCHEMA or value["kind"] != "isolated-build-plan" or value["version"] != 1:
        raise FrameworkError("unsupported isolated build plan schema")
    inputs = obj(value["identity_inputs"], "build plan identity_inputs")
    exact(inputs, {"product", "family", "mode", "board", "boot", "role_ir_sha256",
                   "sdk_set_id", "sdk_lock_id"}, "build plan identity_inputs")
    for field in ("product", "family", "mode", "board", "sdk_set_id", "sdk_lock_id"):
        identifier(inputs[field], f"build plan identity_inputs.{field}")
    if inputs["mode"] not in MODES or inputs["boot"] not in BOOTS:
        raise FrameworkError("unsupported build plan mode/boot")
    role_ids = obj(inputs["role_ir_sha256"], "build plan role IR identities")
    exact(role_ids, {"cp", "ap", "bl2"}, "build plan role IR identities")
    for role in role_ids:
        digest(role_ids[role], f"build plan {role} IR identity")
    board = obj(value["board"], "build plan board")
    exact(board, {"id", "variant"}, "build plan board")
    if board["id"] != inputs["board"]:
        raise FrameworkError("build plan board binding mismatch")
    identifier(board["variant"], "build plan board.variant")
    sdk = obj(value["sdk"], "build plan SDK")
    exact(sdk, {"set_id", "lock_id", "lock_identity_sha256", "roles"}, "build plan SDK")
    if sdk["set_id"] != inputs["sdk_set_id"] or sdk["lock_id"] != inputs["sdk_lock_id"]:
        raise FrameworkError("build plan SDK identity mismatch")
    digest(sdk["lock_identity_sha256"], "build plan SDK lock identity")
    sdk_roles = obj(sdk["roles"], "build plan SDK roles")
    exact(sdk_roles, {"cp", "ap", "bl2"}, "build plan SDK roles")
    for role in ("cp", "ap"):
        _sdk_entry_id(sdk_roles[role], f"build plan SDK {role}")
    if sdk_roles["bl2"] is not None:
        raise FrameworkError("build plan BL2 SDK must be null")
    source_views = obj(value["source_views"], "build plan source views")
    exact(source_views, {"bl1", "bl2", "cp", "ap"}, "build plan source views")
    view_ids: set[str] = set()
    for role, raw in source_views.items():
        source = obj(raw, f"build plan source view {role}")
        exact(source, {"view_id", "roots", "materialized", "read_only"}, f"build plan source view {role}")
        view_id = identifier(source["view_id"], f"build plan source view {role}.view_id")
        if view_id in view_ids:
            raise FrameworkError("build plan source view ids are not unique")
        view_ids.add(view_id)
        roots = array(source["roots"], f"build plan source view {role}.roots")
        if not roots or any(not isinstance(path, str) for path in roots):
            raise FrameworkError(f"build plan source view {role}.roots is malformed")
        for path in roots:
            relative_path(path, f"build plan source view {role}.root")
        if source["materialized"] is not False or source["read_only"] is not True:
            raise FrameworkError("build plan source views must be existing and read-only")
    roles = obj(value["roles"], "build plan roles")
    exact(roles, {"bl1", "bl2", "cp", "ap"}, "build plan roles")
    build_paths: set[str] = set()
    artifact_paths: set[str] = set()
    config_paths: set[str] = set()
    for role, raw in roles.items():
        item = obj(raw, f"build plan role {role}")
        exact(item, {"source_view_id", "build_root_template", "artifact_root_template",
                     "config_path_template", "config_kind", "config_identity_sha256",
                     "sdk", "backend", "fake_nuttx_seed"}, f"build plan role {role}")
        if item["source_view_id"] not in view_ids:
            raise FrameworkError(f"build plan role {role} source view is unknown")
        for field, seen in (("build_root_template", build_paths),
                            ("artifact_root_template", artifact_paths),
                            ("config_path_template", config_paths)):
            path = item[field]
            if (not isinstance(path, str) or not path.startswith("${BUILD_ROOT}/bk7258/") or
                    any(char.isspace() for char in path) or path in seen):
                raise FrameworkError(f"build plan role {role} path is not isolated: {field}")
            seen.add(path)
        digest(item["config_identity_sha256"], f"build plan role {role} config identity")
        if item["config_kind"] not in {"nuttx-defconfig", "boot-policy", "minimal-make-inputs"}:
            raise FrameworkError(f"unsupported build plan config kind: {role}")
        if item["backend"] not in {"cmake", "bootloader-adapter", "minimal-make"}:
            raise FrameworkError(f"unsupported build plan backend: {role}")
        if item["fake_nuttx_seed"] is not False:
            raise FrameworkError("build plan must not create a fake NuttX seed")
        if role in {"bl1", "bl2"}:
            if item["sdk"] is not None:
                raise FrameworkError(f"{role} must have sdk=null")
        else:
            sdk_row = obj(item["sdk"], f"build plan role {role}.sdk")
            exact(sdk_row, {"registry_id", "manifest_sha256", "provenance_sha256"},
                  f"build plan role {role}.sdk")
            _sdk_entry_id(sdk_row["registry_id"], f"build plan role {role}.sdk.registry_id")
            if sdk_row["registry_id"] != sdk_roles[role]:
                raise FrameworkError(f"build plan role {role} SDK binding mismatch")
            digest(sdk_row["manifest_sha256"], f"build plan role {role}.sdk.manifest_sha256")
            digest(sdk_row["provenance_sha256"], f"build plan role {role}.sdk.provenance_sha256")
    legacy = obj(value["legacy_adapter"], "build plan legacy adapter")
    exact(legacy, {"builder", "mode", "invoked", "modified"}, "build plan legacy adapter")
    if (legacy["builder"] != "board/bk7258/scripts/build_dual_image.sh" or
            legacy["mode"] != "compatibility-only" or legacy["invoked"] is not False or
            legacy["modified"] is not False):
        raise FrameworkError("build plan legacy adapter boundary is unsafe")
    digest(value["identity_sha256"], "build plan identity")
    body = dict(value)
    del body["identity_sha256"]
    if sha256(canonical_json(body)) != value["identity_sha256"]:
        raise FrameworkError("build plan identity mismatch")
    return value


def _pack_template(value: Any, field: str) -> str:
    """Validate a build-artifact template without touching the artifact."""
    if (not isinstance(value, str) or not value or value.startswith("/") or
            "\\" in value or "\x00" in value or
            any(char.isspace() for char in value)):
        raise FrameworkError(f"unsafe package template in {field}")
    if any(part in {"", ".", ".."} for part in value.split("/")):
        raise FrameworkError(f"unsafe package template in {field}")
    return value


def _pack_int(value: Any, field: str, *, positive: bool = False) -> int:
    if not isinstance(value, int) or isinstance(value, bool):
        raise FrameworkError(f"{field} must be an integer")
    if value < (1 if positive else 0):
        raise FrameworkError(f"{field} must be non-negative" if not positive else
                             f"{field} must be positive")
    return value


def _pack_source_build_id(value: dict[str, Any]) -> str:
    """Derive the unsigned source/build identity from package metadata only."""
    apps = dict(value["apps_plan"])
    apps.pop("source_build_id", None)
    body = {
        "build_plan_identity_sha256": value["build_plan_identity_sha256"],
        "board": value["board"],
        "product": value["product"],
        "mode": value["mode"],
        "sdk_lock": value["sdk_lock"],
        "partition": value["partition"],
        "plan": value["plan"],
        "apps_plan": apps,
        "artifacts": value["artifacts"],
        "ranges": value["ranges"],
    }
    return sha256(canonical_json(body))


def _pack_partition_layout(repository: Path, partition_path: Path | None) -> tuple[Any, str]:
    """Load the existing partition source as read-only metadata."""
    try:
        from gen_bk7258_partitions import load_layout  # noqa: PLC0415
    except ImportError as error:
        raise FrameworkError("partition metadata adapter is unavailable") from error
    path = partition_path or repository / "board/bk7258/partitions/bk7258/auto_partitions.csv"
    if not path.is_absolute():
        path = repository / path
    path = path.resolve()
    try:
        layout = load_layout(path)
    except (OSError, ValueError, RuntimeError) as error:
        raise FrameworkError(f"cannot load package partition layout: {path}") from error
    try:
        source = path.relative_to(repository.resolve()).as_posix()
    except ValueError as error:
        raise FrameworkError("package partition source is outside repository") from error
    return layout, source


def _pack_layout_role(layout: Any, role: str) -> Any:
    """Map package roles to one and only one executable partition."""
    candidates = {
        "bl1": ("boot", "bl1_control"),
        "bl2": ("bl2", "bl1_primary_bl2"),
        "cp": ("slot_a_cp", "primary_cp_app"),
        "ap": ("slot_a_ap", "primary_ap_app"),
    }[role]
    matches = [item for item in layout.partitions if item.role in candidates]
    if len(matches) != 1:
        raise FrameworkError(f"package role {role} does not map to exactly one partition")
    return matches[0]


def _pack_artifact_templates(plan: dict[str, Any]) -> list[dict[str, Any]]:
    roles = plan["roles"]

    def role_path(role: str, filename: str) -> str:
        return f"{roles[role]['artifact_root_template']}/{filename}"

    rows: list[dict[str, Any]] = []
    for name, (kind, owner, mapped_roles) in PACK_ARTIFACTS.items():
        source_role = mapped_roles[0]
        rows.append({
            "name": name,
            "kind": kind,
            "owner": owner,
            "roles": list(mapped_roles),
            "path_template": role_path(source_role, name),
            "required": True,
        })
    rows.append({
        "name": ".bkpack",
        "kind": "vendor_package_extension",
        "owner": "board",
        "roles": [],
        "path_template": ".bkpack",
        "required": False,
    })
    return rows


def _pack_ranges(layout: Any) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    partition_roles: dict[str, Any] = {}
    ranges: list[dict[str, Any]] = []
    for role in PACK_ROLES:
        partition = _pack_layout_role(layout, role)
        row = {
            "partition": partition.name,
            "start": partition.offset,
            "end": partition.end,
        }
        partition_roles[role] = row
        ranges.append({
            "artifact": f"vela_{role}.bin",
            "role": role,
            "partition": partition.name,
            "start": partition.offset,
            "end": partition.end,
        })
    return partition_roles, ranges


def validate_bkpack(value: dict[str, Any]) -> dict[str, Any]:
    """Validate one metadata-only BK7258 package manifest.

    This validator intentionally checks names, identities and bounded ranges;
    it never opens an image, invokes a signer, accesses a key, or writes
    Flash.  ``factory`` is a separate package kind and plan, even though the
    current skeleton uses the same active executable layout as application.
    """
    exact(value, {"schema", "kind", "version", "package_id", "board", "product",
                  "mode", "roles", "plan", "apps_plan", "build_plan_identity_sha256",
                  "source_build_id", "signed_digest", "sdk_lock", "partition", "ranges",
                  "artifacts", "tool", "trust", "hardware_verified", "identity_sha256"},
          "BK7258 package")
    if value["schema"] != BKPACK_SCHEMA or value["kind"] not in PACK_KINDS or value["version"] != 1:
        raise FrameworkError("unsupported BK7258 package schema")
    package_id = identifier(value["package_id"], "package.package_id")
    for field in ("product", "mode"):
        identifier(value[field], f"package.{field}")
    if value["mode"] not in MODES:
        raise FrameworkError("package.mode is not in the versioned mode enum")
    if value["roles"] != list(PACK_ROLES):
        raise FrameworkError("package roles must be the exact BL1/BL2/CP/AP set")
    board = obj(value["board"], "package.board")
    exact(board, {"id", "variant"}, "package.board")
    identifier(board["id"], "package.board.id")
    identifier(board["variant"], "package.board.variant")
    plan = obj(value["plan"], "package.plan")
    exact(plan, {"kind", "name", "version", "range_policy", "artifact_policy"},
          "package.plan")
    if (plan["kind"] != value["kind"] or plan["name"] != package_id or
            plan["version"] != 1):
        raise FrameworkError("package kind/plan identity mismatch")
    if plan["range_policy"] != "exact-partitions" or \
            plan["artifact_policy"] != "standard-plus-additive-extension":
        raise FrameworkError("package plan policy is unsafe")
    apps = obj(value["apps_plan"], "package.apps_plan")
    exact(apps, {"name", "kind", "version", "roles", "artifacts", "source_build_id"},
          "package.apps_plan")
    identifier(apps["name"], "package.apps_plan.name")
    if apps["kind"] != "named-apps-plan" or apps["version"] != 1:
        raise FrameworkError("package must contain exactly one named apps plan")
    if apps["roles"] != ["cp", "ap"]:
        raise FrameworkError("apps plan roles are ambiguous")
    if apps["artifacts"] != ["libarch.a", "libboards.a", "vela_cp.bin", "vela_ap.bin"]:
        raise FrameworkError("apps plan must name the standard CP/AP artifacts exactly once")
    digest(value["build_plan_identity_sha256"], "package build plan identity")
    digest(value["source_build_id"], "package source_build_id")
    if apps["source_build_id"] != value["source_build_id"]:
        raise FrameworkError("apps plan/source_build_id binding mismatch")
    if value["signed_digest"] is not None:
        digest(value["signed_digest"], "package signed_digest")
        raise FrameworkError("signed package artifacts are not enabled by P6")
    sdk = obj(value["sdk_lock"], "package.sdk_lock")
    exact(sdk, {"id", "identity_sha256", "roles"}, "package.sdk_lock")
    identifier(sdk["id"], "package.sdk_lock.id")
    digest(sdk["identity_sha256"], "package.sdk_lock.identity_sha256")
    sdk_roles = obj(sdk["roles"], "package.sdk_lock.roles")
    exact(sdk_roles, {"cp", "ap", "bl2"}, "package.sdk_lock.roles")
    for role in ("cp", "ap"):
        _sdk_entry_id(sdk_roles[role], f"package.sdk_lock.roles.{role}")
    if sdk_roles["bl2"] is not None:
        raise FrameworkError("package SDK lock must encode BL2 as no-runtime-SDK")
    partition = obj(value["partition"], "package.partition")
    exact(partition, {"source", "layout_id", "layout_sha256", "flash_size", "erase_size",
                      "role_partitions", "range_policy"}, "package.partition")
    relative_path(partition["source"], "package.partition.source")
    identifier(partition["layout_id"], "package.partition.layout_id")
    digest(partition["layout_sha256"], "package.partition.layout_sha256")
    flash_size = _pack_int(partition["flash_size"], "package.partition.flash_size", positive=True)
    _pack_int(partition["erase_size"], "package.partition.erase_size", positive=True)
    if partition["range_policy"] != "exact-partitions":
        raise FrameworkError("package partition range policy is unsafe")
    expected_partitions = obj(partition["role_partitions"], "package.partition.role_partitions")
    exact(expected_partitions, set(PACK_ROLES), "package.partition.role_partitions")
    for role in PACK_ROLES:
        row = obj(expected_partitions[role], f"package.partition.role_partitions.{role}")
        exact(row, {"partition", "start", "end"}, f"package.partition.role_partitions.{role}")
        if row["partition"] != PACK_ROLE_PARTITIONS[role]:
            raise FrameworkError(f"package role {role} has an ambiguous partition mapping")
        start = _pack_int(row["start"], f"package.partition.{role}.start")
        end = _pack_int(row["end"], f"package.partition.{role}.end")
        if start >= end or end > flash_size:
            raise FrameworkError(f"package partition range is outside Flash: {role}")
    ranges = array(value["ranges"], "package.ranges")
    if len(ranges) != len(PACK_ROLES):
        raise FrameworkError("package must contain exactly one range per role")
    seen_roles: set[str] = set()
    intervals: list[tuple[int, int, str]] = []
    for index, raw in enumerate(ranges):
        row = obj(raw, f"package.ranges[{index}]")
        exact(row, {"artifact", "role", "partition", "start", "end"},
              f"package.ranges[{index}]")
        role = identifier(row["role"], f"package.ranges[{index}].role")
        if role not in PACK_ROLES or role in seen_roles:
            raise FrameworkError("package ranges contain an ambiguous or duplicate role")
        seen_roles.add(role)
        if row["artifact"] != f"vela_{role}.bin":
            raise FrameworkError(f"package range artifact does not map to role: {role}")
        expected = expected_partitions[role]
        if row["partition"] != expected["partition"]:
            raise FrameworkError(f"package range partition mapping is ambiguous: {role}")
        start = _pack_int(row["start"], f"package.ranges[{index}].start")
        end = _pack_int(row["end"], f"package.ranges[{index}].end")
        if start >= end or end > flash_size:
            raise FrameworkError(f"package range is outside Flash: {role}")
        if start != expected["start"] or end != expected["end"]:
            raise FrameworkError(f"package range does not equal its partition: {role}")
        intervals.append((start, end, role))
    if seen_roles != set(PACK_ROLES):
        raise FrameworkError("package ranges omit a role")
    intervals.sort()
    for previous, current in zip(intervals, intervals[1:]):
        if current[0] < previous[1]:
            raise FrameworkError(f"package ranges overlap: {previous[2]} and {current[2]}")
    artifacts = array(value["artifacts"], "package.artifacts")
    names: set[str] = set()
    expected_names = set(PACK_ARTIFACTS) | {".bkpack"}
    for index, raw in enumerate(artifacts):
        row = obj(raw, f"package.artifacts[{index}]")
        exact(row, {"name", "kind", "owner", "roles", "path_template", "required"},
              f"package.artifacts[{index}]")
        name = row["name"]
        if not isinstance(name, str) or name in names:
            raise FrameworkError("package artifacts contain a duplicate or ambiguous name")
        names.add(name)
        if name not in expected_names:
            raise FrameworkError(f"package contains an extra artifact: {name}")
        _pack_template(row["path_template"], f"package.artifacts[{index}].path_template")
        if not isinstance(row["required"], bool):
            raise FrameworkError("package artifact required flag must be boolean")
        if name == ".bkpack":
            if (row["kind"], row["owner"], row["roles"], row["required"]) != \
                    ("vendor_package_extension", "board", [], False):
                raise FrameworkError(".bkpack must remain an additive optional extension")
        else:
            kind, owner, mapped_roles = PACK_ARTIFACTS[name]
            if (row["kind"], row["owner"], row["roles"], row["required"]) != \
                    (kind, owner, list(mapped_roles), True):
                raise FrameworkError(f"standard artifact mapping is wrong: {name}")
    if names != expected_names:
        raise FrameworkError("package standard artifacts are incomplete")
    tool = obj(value["tool"], "package.tool")
    exact(tool, {"backend", "packer", "signer", "network_used", "bytes_written"},
          "package.tool")
    if (tool["backend"] != "cmake" or tool["packer"] != "bk7258_framework.py" or
            tool["signer"] is not None or tool["network_used"] is not False or
            tool["bytes_written"] is not False):
        raise FrameworkError("package tool metadata claims an unsafe operation")
    trust = obj(value["trust"], "package.trust")
    exact(trust, {"mode", "signed", "signed_digest", "key_id", "flash_authorized"},
          "package.trust")
    if (trust["mode"] != "host-reference-only" or trust["signed"] is not False or
            trust["signed_digest"] is not None or trust["key_id"] is not None or
            trust["flash_authorized"] is not False):
        raise FrameworkError("package trust metadata claims signing or Flash authority")
    if value["hardware_verified"] is not False:
        raise FrameworkError("package hardware_verified must remain false")
    if _pack_source_build_id(value) != value["source_build_id"]:
        raise FrameworkError("package source_build_id does not match unsigned metadata")
    digest(value["identity_sha256"], "package identity")
    body = dict(value)
    del body["identity_sha256"]
    if sha256(canonical_json(body)) != value["identity_sha256"]:
        raise FrameworkError("package identity mismatch")
    return value


def pack_prepare(repository: Path, product_id: str, kind: str = "application",
                 board_id: str | None = None, mode: str | None = None,
                 partition_path: Path | None = None) -> dict[str, Any]:
    """Prepare a deterministic package manifest without bytes or signing."""
    if kind not in PACK_KINDS:
        raise FrameworkError("package kind must be application or factory")
    plan = build_plan(repository, product_id, board_id, mode)
    inputs = plan["identity_inputs"]
    sdk_roles = dict(plan["sdk"]["roles"])
    layout, partition_source = _pack_partition_layout(repository, partition_path)
    role_partitions, ranges = _pack_ranges(layout)
    package_id = f"{inputs['product']}-{kind}-package"
    apps_name = f"{inputs['product']}-apps"
    artifacts = _pack_artifact_templates(plan)
    package_plan = {
        "kind": kind,
        "name": package_id,
        "version": 1,
        "range_policy": "exact-partitions",
        "artifact_policy": "standard-plus-additive-extension",
    }
    apps_plan = {
        "name": apps_name,
        "kind": "named-apps-plan",
        "version": 1,
        "roles": ["cp", "ap"],
        "artifacts": ["libarch.a", "libboards.a", "vela_cp.bin", "vela_ap.bin"],
        "source_build_id": "0" * 64,
    }
    partition = {
        "source": partition_source,
        "layout_id": layout.layout_id,
        "layout_sha256": layout.layout_sha256,
        "flash_size": layout.flash_size,
        "erase_size": layout.erase_size,
        "role_partitions": role_partitions,
        "range_policy": "exact-partitions",
    }
    body: dict[str, Any] = {
        "schema": BKPACK_SCHEMA,
        "kind": kind,
        "version": 1,
        "package_id": package_id,
        "board": dict(plan["board"]),
        "product": inputs["product"],
        "mode": inputs["mode"],
        "roles": list(PACK_ROLES),
        "plan": package_plan,
        "apps_plan": apps_plan,
        "build_plan_identity_sha256": plan["identity_sha256"],
        "source_build_id": "0" * 64,
        "signed_digest": None,
        "sdk_lock": {
            "id": plan["sdk"]["lock_id"],
            "identity_sha256": plan["sdk"]["lock_identity_sha256"],
            "roles": sdk_roles,
        },
        "partition": partition,
        "ranges": ranges,
        "artifacts": artifacts,
        "tool": {
            "backend": "cmake",
            "packer": "bk7258_framework.py",
            "signer": None,
            "network_used": False,
            "bytes_written": False,
        },
        "trust": {
            "mode": "host-reference-only",
            "signed": False,
            "signed_digest": None,
            "key_id": None,
            "flash_authorized": False,
        },
        "hardware_verified": False,
    }
    source_build_id = _pack_source_build_id(body)
    body["source_build_id"] = source_build_id
    body["apps_plan"]["source_build_id"] = source_build_id
    body["identity_sha256"] = sha256(canonical_json(body))
    return validate_bkpack(body)


def pack_verify(repository: Path | None, package: dict[str, Any] | Path) -> dict[str, Any]:
    """Verify package metadata and optional current-repository bindings only."""
    value = load_json(package) if isinstance(package, Path) else package
    validate_bkpack(value)
    if repository is not None:
        plan = build_plan(repository, value["product"], value["board"]["id"], value["mode"])
        if plan["identity_sha256"] != value["build_plan_identity_sha256"]:
            raise FrameworkError("package build plan identity differs from repository")
        if plan["sdk"]["lock_id"] != value["sdk_lock"]["id"] or \
                plan["sdk"]["lock_identity_sha256"] != value["sdk_lock"]["identity_sha256"]:
            raise FrameworkError("package SDK lock differs from repository")
        layout, _ = _pack_partition_layout(repository, repository / value["partition"]["source"])
        if layout.layout_sha256 != value["partition"]["layout_sha256"]:
            raise FrameworkError("package partition layout differs from repository")
    return {
        "package_id": value["package_id"],
        "kind": value["kind"],
        "source_build_id": value["source_build_id"],
        "hardware_verified": value["hardware_verified"],
        "signed": False,
        "bytes_read": False,
        "network_used": False,
    }


def _transport_host(host: str | None = None) -> dict[str, Any]:
    """Return an explicit host/backend identity without opening a port."""
    if host is not None:
        normalized = host.lower()
        aliases = {"macos": "darwin", "mac": "darwin", "win32": "windows"}
        normalized = aliases.get(normalized, normalized)
        if normalized not in TRANSPORT_HOSTS:
            raise FrameworkError(
                f"unsupported host {host!r}; use --host linux|darwin|windows|wsl "
                "or provide a supported explicit port")
    elif sys.platform == "win32":
        normalized = "windows"
    elif sys.platform == "darwin":
        normalized = "darwin"
    elif sys.platform.startswith("linux"):
        try:
            proc_version = Path("/proc/version").read_text(encoding="utf-8").lower()
        except (OSError, UnicodeError):
            proc_version = ""
        normalized = "wsl" if (os.environ.get("WSL_INTEROP") or "microsoft" in proc_version) else "linux"
    else:
        raise FrameworkError(
            f"unsupported host {sys.platform!r}; use --host linux|darwin|windows|wsl "
            "and an explicit supported port, or run the host adapter")
    return {"os": normalized, "wsl": normalized == "wsl", "backend": "native"}


def _transport_port(value: Any, host: str, field: str = "port") -> str:
    if not isinstance(value, str) or not value or "\x00" in value or any(char.isspace() for char in value):
        raise FrameworkError(f"invalid {field}")
    if host in {"linux", "wsl"}:
        if re.fullmatch(r"/dev/tty[^/]*", value) is None:
            if not (host == "wsl" and re.fullmatch(r"(?i:COM[1-9][0-9]*)", value)):
                raise FrameworkError(f"{field} must match /dev/tty* on {host}")
    elif host == "darwin":
        if re.fullmatch(r"/dev/cu\.[^/]*", value) is None:
            raise FrameworkError(f"{field} must match /dev/cu.* on darwin")
    elif host == "windows":
        if re.fullmatch(r"(?i:COM[1-9][0-9]*)", value) is None:
            raise FrameworkError(f"{field} must match COM* on windows")
    else:
        raise FrameworkError(f"unsupported transport host: {host}")
    return value


def _transport_identity(value: Any, field: str) -> dict[str, str | None]:
    if value is None:
        return {key: None for key in TRANSPORT_IDENTITY_KEYS}
    raw = obj(value, field)
    if set(raw) - set(TRANSPORT_IDENTITY_KEYS):
        raise FrameworkError(f"unknown USB identity field in {field}")
    result: dict[str, str | None] = {}
    for key in TRANSPORT_IDENTITY_KEYS:
        item = raw.get(key)
        if item is not None and (not isinstance(item, str) or not item or
                                 any(char.isspace() for char in item) or "\x00" in item):
            raise FrameworkError(f"invalid USB identity field {field}.{key}")
        result[key] = item
    return result


def _transport_capabilities(value: Any, field: str = "capabilities") -> dict[str, bool]:
    raw = obj(value, field)
    exact(raw, set(TRANSPORT_CAPABILITY_KEYS), field)
    result: dict[str, bool] = {}
    for key in TRANSPORT_CAPABILITY_KEYS:
        if not isinstance(raw[key], bool):
            raise FrameworkError(f"{field}.{key} must be explicit boolean")
        result[key] = raw[key]
    return result


def _transport_candidate(value: Any, host: str, field: str,
                         *, source: str = "native") -> dict[str, Any]:
    raw = obj(value, field)
    exact(raw, {"port", "identity", "capabilities", "source"}, field)
    port = _transport_port(raw["port"], host, f"{field}.port")
    identity = _transport_identity(raw["identity"], f"{field}.identity")
    capabilities = _transport_capabilities(raw["capabilities"], f"{field}.capabilities")
    if raw["source"] not in {"native", "explicit", "powershell-adapter"}:
        raise FrameworkError(f"unsupported transport candidate source: {field}")
    if source != "native" and raw["source"] != source:
        raise FrameworkError(f"transport candidate source mismatch: {field}")
    return {"port": port, "identity": identity, "capabilities": capabilities,
            "source": raw["source"]}


def _transport_candidate_from_port(port: str, host: str, source: str = "explicit") -> dict[str, Any]:
    return {
        "port": _transport_port(port, host),
        "identity": _transport_identity(None, "candidate.identity"),
        "capabilities": {key: False for key in TRANSPORT_CAPABILITY_KEYS},
        "source": source,
    }


def validate_transport_list(value: dict[str, Any]) -> dict[str, Any]:
    exact(value, {"schema", "kind", "version", "host", "candidates",
                  "hardware_accessed", "identity_sha256"}, "transport port list")
    if value["schema"] != TRANSPORT_SCHEMA or value["kind"] != "port-list" or value["version"] != 1:
        raise FrameworkError("unsupported transport port-list schema")
    host = obj(value["host"], "transport list host")
    exact(host, {"os", "wsl", "backend"}, "transport list host")
    if host["os"] not in TRANSPORT_HOSTS or host["wsl"] is not (host["os"] == "wsl"):
        raise FrameworkError("transport host identity is malformed")
    if host["backend"] not in {"native", "powershell-adapter"}:
        raise FrameworkError("unsupported transport host backend")
    candidates = array(value["candidates"], "transport candidates")
    seen: set[str] = set()
    for index, raw in enumerate(candidates):
        candidate = _transport_candidate(raw, host["os"], f"transport candidates[{index}]")
        if candidate["port"] in seen:
            raise FrameworkError("duplicate transport candidate port")
        seen.add(candidate["port"])
    if value["hardware_accessed"] is not False:
        raise FrameworkError("transport discovery must not access hardware")
    digest(value["identity_sha256"], "transport list identity")
    body = dict(value)
    del body["identity_sha256"]
    if sha256(canonical_json(body)) != value["identity_sha256"]:
        raise FrameworkError("transport list identity mismatch")
    return value


def _transport_device_paths(host: str, device_root: Path,
                            supplied: list[str] | None,
                            windows_ports: list[str] | None,
                            powershell_adapter: bool = False) -> list[str]:
    if supplied is not None:
        return list(supplied)
    if host == "windows":
        # Windows enumeration is intentionally an adapter input.  A Linux
        # host must not pretend that COM ports are visible; callers may pass
        # --port COMn or --windows-port COMn for a deterministic dry-run.
        env_ports = os.environ.get("BK7258_PORT_CANDIDATES", "")
        return list(windows_ports or [item for item in env_ports.split(",") if item])
    if host == "wsl" and powershell_adapter and windows_ports:
        # A caller-provided adapter result is metadata input only.  The
        # framework does not invoke PowerShell or probe a COM handle.
        return list(windows_ports)
    pattern = "cu.*" if host == "darwin" else "tty*"
    try:
        return sorted(path.as_posix() for path in device_root.glob(pattern))
    except OSError as error:
        raise FrameworkError(f"cannot enumerate {host} serial candidates under {device_root}") from error


def port_list(host: str | None = None, *, device_root: Path = Path("/dev"),
              candidates: list[Any] | None = None,
              windows_ports: list[str] | None = None,
              powershell_adapter: bool = False) -> dict[str, Any]:
    """Enumerate candidate names only; no serial device is opened."""
    host_identity = _transport_host(host)
    raw_paths = _transport_device_paths(host_identity["os"], device_root,
                                        candidates, windows_ports, powershell_adapter)
    rows: list[dict[str, Any]] = []
    for index, item in enumerate(raw_paths):
        if isinstance(item, str):
            row = _transport_candidate_from_port(item, host_identity["os"])
        else:
            row = _transport_candidate(item, host_identity["os"], f"candidate[{index}]")
        if row["port"] in {candidate["port"] for candidate in rows}:
            raise FrameworkError("duplicate transport candidate port")
        rows.append(row)
    if powershell_adapter:
        if host_identity["os"] != "wsl":
            raise FrameworkError("PowerShell adapter is only valid for WSL")
        host_identity["backend"] = "powershell-adapter"
        for row in rows:
            row["source"] = "powershell-adapter"
    body: dict[str, Any] = {
        "schema": TRANSPORT_SCHEMA,
        "kind": "port-list",
        "version": 1,
        "host": host_identity,
        "candidates": rows,
        "hardware_accessed": False,
    }
    result = dict(body)
    result["identity_sha256"] = sha256(canonical_json(body))
    return validate_transport_list(result)


def _transport_filter(values: dict[str, str | None] | None) -> dict[str, str | None]:
    result = _transport_identity(values, "transport filter")
    if all(item is None for item in result.values()):
        raise FrameworkError("USB identity filter must select at least one field")
    return result


def _transport_matches(candidate: dict[str, Any], identity: dict[str, str | None]) -> bool:
    actual = candidate["identity"]
    for key, expected in identity.items():
        if expected is None:
            continue
        observed = actual.get(key)
        if observed is None:
            return False
        if key == "serial_prefix":
            if not observed.startswith(expected):
                return False
        elif observed.lower() != expected.lower():
            return False
    return True


def validate_transport_resolution(value: dict[str, Any]) -> dict[str, Any]:
    exact(value, {"schema", "kind", "version", "host", "board_identity",
                  "port_identity", "candidate", "selection", "hardware_accessed",
                  "identity_sha256"}, "transport port resolution")
    if value["schema"] != TRANSPORT_SCHEMA or value["kind"] != "port-resolution" or value["version"] != 1:
        raise FrameworkError("unsupported transport port-resolution schema")
    host = obj(value["host"], "transport resolution host")
    exact(host, {"os", "wsl", "backend"}, "transport resolution host")
    if host["os"] not in TRANSPORT_HOSTS or host["wsl"] is not (host["os"] == "wsl"):
        raise FrameworkError("transport resolution host is malformed")
    board = value["board_identity"]
    if board is not None:
        board = obj(board, "transport resolution board identity")
        exact(board, {"id"}, "transport resolution board identity")
        identifier(board["id"], "transport resolution board identity.id")
    port_identity = obj(value["port_identity"], "transport resolution port identity")
    exact(port_identity, {"port", "identity"}, "transport resolution port identity")
    _transport_port(port_identity["port"], host["os"], "transport resolution port")
    _transport_identity(port_identity["identity"], "transport resolution USB identity")
    candidate = _transport_candidate(value["candidate"], host["os"], "transport resolution candidate")
    if candidate["port"] != port_identity["port"] or candidate["identity"] != port_identity["identity"]:
        raise FrameworkError("transport resolution port identity mismatch")
    selection = obj(value["selection"], "transport resolution selection")
    exact(selection, {"mode", "filter"}, "transport resolution selection")
    if selection["mode"] not in {"explicit", "auto", "usb-identity"}:
        raise FrameworkError("unsupported transport selection mode")
    if selection["filter"] is not None:
        _transport_filter(selection["filter"])
    if selection["mode"] == "explicit" and selection["filter"] is not None:
        raise FrameworkError("explicit port selection cannot be ambiguous with an identity filter")
    if selection["mode"] == "usb-identity" and selection["filter"] is None:
        raise FrameworkError("USB identity selection requires a filter")
    if value["hardware_accessed"] is not False:
        raise FrameworkError("transport resolution must not access hardware")
    digest(value["identity_sha256"], "transport resolution identity")
    body = dict(value)
    del body["identity_sha256"]
    if sha256(canonical_json(body)) != value["identity_sha256"]:
        raise FrameworkError("transport resolution identity mismatch")
    return value


def port_resolve(host: str | None = None, *, port: str | None = None,
                 board_id: str | None = None,
                 identity: dict[str, str | None] | None = None,
                 device_root: Path = Path("/dev"),
                 candidates: list[Any] | None = None,
                 windows_ports: list[str] | None = None,
                 powershell_adapter: bool = False) -> dict[str, Any]:
    """Resolve one port deterministically while keeping board identity separate."""
    host_identity = _transport_host(host)
    if board_id is not None:
        identifier(board_id, "board identity")
    if port is not None and identity is not None:
        raise FrameworkError("--port and USB identity filter are mutually exclusive")
    filter_identity = _transport_filter(identity) if identity is not None else None
    listing = port_list(host_identity["os"], device_root=device_root, candidates=candidates,
                        windows_ports=windows_ports, powershell_adapter=powershell_adapter)
    listed = listing["candidates"]
    if port is not None:
        selected_port = _transport_port(port, host_identity["os"])
        matches = [candidate for candidate in listed if candidate["port"] == selected_port]
        selected = matches[0] if len(matches) == 1 else _transport_candidate_from_port(selected_port, host_identity["os"])
        mode = "explicit"
    else:
        matching = listed if filter_identity is None else [
            candidate for candidate in listed if _transport_matches(candidate, filter_identity)]
        if len(matching) == 0:
            hint = "; pass --port explicitly or provide a USB identity filter"
            raise FrameworkError(f"no transport candidate matched{hint}")
        if len(matching) != 1:
            raise FrameworkError(
                "ambiguous transport candidates; pass --port or a deterministic USB identity filter")
        selected = matching[0]
        mode = "usb-identity" if filter_identity is not None else "auto"
    body: dict[str, Any] = {
        "schema": TRANSPORT_SCHEMA,
        "kind": "port-resolution",
        "version": 1,
        "host": listing["host"],
        "board_identity": None if board_id is None else {"id": board_id},
        "port_identity": {"port": selected["port"], "identity": dict(selected["identity"])},
        "candidate": selected,
        "selection": {"mode": mode, "filter": filter_identity},
        "hardware_accessed": False,
    }
    result = dict(body)
    result["identity_sha256"] = sha256(canonical_json(body))
    return validate_transport_resolution(result)


def validate_transport_plan(value: dict[str, Any]) -> dict[str, Any]:
    exact(value, {"schema", "kind", "version", "board_identity", "port_identity",
                  "host", "resolution_identity_sha256", "capabilities", "policy",
                  "sequence", "hardware_accessed", "identity_sha256"}, "transport plan")
    if value["schema"] != TRANSPORT_SCHEMA or value["kind"] != "transport-plan" or value["version"] != 1:
        raise FrameworkError("unsupported transport-plan schema")
    board = obj(value["board_identity"], "transport plan board identity")
    exact(board, {"id", "variant"}, "transport plan board identity")
    identifier(board["id"], "transport plan board.id")
    identifier(board["variant"], "transport plan board.variant")
    host = obj(value["host"], "transport plan host")
    exact(host, {"os", "wsl", "backend"}, "transport plan host")
    if host["os"] not in TRANSPORT_HOSTS or host["wsl"] is not (host["os"] == "wsl"):
        raise FrameworkError("transport plan host is malformed")
    digest(value["resolution_identity_sha256"], "transport plan resolution identity")
    port_identity = obj(value["port_identity"], "transport plan port identity")
    exact(port_identity, {"port", "identity"}, "transport plan port identity")
    _transport_port(port_identity["port"], host["os"], "transport plan port")
    _transport_identity(port_identity["identity"], "transport plan USB identity")
    capabilities = _transport_capabilities(value["capabilities"], "transport plan capabilities")
    policy = obj(value["policy"], "transport plan policy")
    exact(policy, {"exclusive", "capture_before_loader", "loader_closes_before_console",
                   "port_released_before_console", "aidk", "rts_reset_allowed"},
          "transport plan policy")
    for key in ("exclusive", "capture_before_loader", "loader_closes_before_console",
                "port_released_before_console", "aidk", "rts_reset_allowed"):
        if not isinstance(policy[key], bool):
            raise FrameworkError(f"transport plan policy {key} must be boolean")
    if (policy["exclusive"] is not True or policy["capture_before_loader"] is not False or
            policy["loader_closes_before_console"] is not True or
            policy["port_released_before_console"] is not True):
        raise FrameworkError("transport plan permits loader/console port conflict")
    if policy["aidk"] is True:
        if policy["rts_reset_allowed"] is not False or capabilities["rts_reset"] is not False:
            raise FrameworkError("AIDK forbids RTS reset")
    sequence = array(value["sequence"], "transport plan sequence")
    expected = [("loader", "open"), ("loader", "close-release"),
                ("console", "open"), ("console", "capture")]
    if len(sequence) != len(expected):
        raise FrameworkError("transport plan sequence is incomplete")
    for index, (owner, action) in enumerate(expected):
        row = obj(sequence[index], f"transport plan sequence[{index}]")
        exact(row, {"owner", "action", "exclusive"}, f"transport plan sequence[{index}]")
        if row["owner"] != owner or row["action"] != action or row["exclusive"] is not True:
            raise FrameworkError("transport plan sequence violates exclusive loader/console order")
    if value["hardware_accessed"] is not False:
        raise FrameworkError("transport plan must remain a dry-run")
    digest(value["identity_sha256"], "transport plan identity")
    body = dict(value)
    del body["identity_sha256"]
    if sha256(canonical_json(body)) != value["identity_sha256"]:
        raise FrameworkError("transport plan identity mismatch")
    return value


def transport_plan(repository: Path, board_id: str, *, host: str | None = None,
                   port: str | None = None,
                   identity: dict[str, str | None] | None = None,
                   device_root: Path = Path("/dev"),
                   candidates: list[Any] | None = None,
                   windows_ports: list[str] | None = None,
                   powershell_adapter: bool = False,
                   aidk: bool = False,
                   capabilities: dict[str, bool] | None = None) -> dict[str, Any]:
    """Create a dry-run exclusive loader -> console transport plan."""
    identifier(board_id, "board")
    catalog = load_catalog(repository)
    if board_id not in catalog["boards"]:
        raise FrameworkError(f"unknown board for transport plan: {board_id}")
    board = catalog["boards"][board_id]
    resolution = port_resolve(host, port=port, board_id=board_id, identity=identity,
                              device_root=device_root, candidates=candidates,
                              windows_ports=windows_ports,
                              powershell_adapter=powershell_adapter)
    selected_capabilities = capabilities or resolution["candidate"]["capabilities"]
    selected_capabilities = _transport_capabilities(selected_capabilities, "transport capabilities")
    if aidk and selected_capabilities["rts_reset"]:
        raise FrameworkError("AIDK forbids RTS reset")
    body: dict[str, Any] = {
        "schema": TRANSPORT_SCHEMA,
        "kind": "transport-plan",
        "version": 1,
        "board_identity": {"id": board["id"], "variant": board["variant"]},
        "port_identity": dict(resolution["port_identity"]),
        "host": dict(resolution["host"]),
        "resolution_identity_sha256": resolution["identity_sha256"],
        "capabilities": selected_capabilities,
        "policy": {
            "exclusive": True,
            "capture_before_loader": False,
            "loader_closes_before_console": True,
            "port_released_before_console": True,
            "aidk": bool(aidk),
            "rts_reset_allowed": not aidk,
        },
        "sequence": [
            {"owner": "loader", "action": "open", "exclusive": True},
            {"owner": "loader", "action": "close-release", "exclusive": True},
            {"owner": "console", "action": "open", "exclusive": True},
            {"owner": "console", "action": "capture", "exclusive": True},
        ],
        "hardware_accessed": False,
    }
    result = dict(body)
    result["identity_sha256"] = sha256(canonical_json(body))
    return validate_transport_plan(result)


def _shadow_digest(path: Path, field: str) -> str:
    """Hash one tracked metadata file without accepting a symlink."""
    try:
        info = path.lstat()
    except OSError as error:
        raise FrameworkError(f"missing {field}: {path}") from error
    if not stat.S_ISREG(info.st_mode):
        raise FrameworkError(f"{field} must be a regular file: {path}")
    return sha256(path.read_bytes())


def _shadow_path(repository: Path, value: str, field: str) -> Path:
    path = relative_path(value, field)
    candidate = repository / path
    try:
        info = candidate.lstat()
    except OSError as error:
        raise FrameworkError(f"missing {field}: {candidate}") from error
    if stat.S_ISLNK(info.st_mode):
        raise FrameworkError(f"{field} must not be a symlink: {candidate}")
    try:
        candidate.resolve().relative_to(repository.resolve())
    except ValueError as error:
        raise FrameworkError(f"{field} escaped repository") from error
    return candidate


def validate_shadow_ledger(repository: Path, value: dict[str, Any]) -> dict[str, Any]:
    """Validate the immutable P9a mapping ledger.

    The ledger is deliberately a mapping/provenance input, not a claim that a
    new product is already equivalent.  The detailed old/new evidence is
    produced by :func:`shadow_parity`; a non-EXACT row must always retain a
    human-readable rationale.
    """
    exact(value, {"schema", "kind", "version", "status", "source_manifest",
                  "source_manifest_sha256", "source_migration_ledger",
                  "source_migration_ledger_sha256", "profile_count", "statuses",
                  "rows", "identity_sha256"}, "shadow ledger")
    if (value["schema"] != SHADOW_SCHEMA or
            value["kind"] != "legacy-profile-shadow-ledger" or
            value["version"] != 1 or value["status"] != "shadow-only"):
        raise FrameworkError("unsupported shadow ledger schema/status")
    source_manifest = _shadow_path(repository, value["source_manifest"],
                                   "shadow source manifest")
    source_migration = _shadow_path(repository, value["source_migration_ledger"],
                                    "shadow source migration ledger")
    digest(value["source_manifest_sha256"], "shadow source manifest digest")
    digest(value["source_migration_ledger_sha256"],
           "shadow source migration ledger digest")
    if _shadow_digest(source_manifest, "shadow source manifest") != value["source_manifest_sha256"]:
        raise FrameworkError("shadow source manifest digest mismatch")
    if (_shadow_digest(source_migration, "shadow source migration ledger") !=
            value["source_migration_ledger_sha256"]):
        raise FrameworkError("shadow source migration ledger digest mismatch")
    if (not isinstance(value["profile_count"], int) or
            isinstance(value["profile_count"], bool) or value["profile_count"] != 27):
        raise FrameworkError("shadow ledger profile count must be exactly 27")
    statuses = array(value["statuses"], "shadow ledger statuses")
    if statuses != list(SHADOW_STATUS_ORDER) or set(statuses) != SHADOW_STATUS:
        raise FrameworkError("shadow ledger status enum is not stable")
    rows = array(value["rows"], "shadow ledger rows")
    if len(rows) != value["profile_count"]:
        raise FrameworkError("shadow ledger row count is not exactly 27")
    seen: set[str] = set()
    for index, raw in enumerate(rows):
        row = obj(raw, f"shadow ledger row {index}")
        exact(row, {"legacy_profile", "family", "resource_mode", "validation_suite",
                    "target_product", "target_role", "status", "rationale"},
              f"shadow ledger row {index}")
        name = identifier(row["legacy_profile"], f"shadow row {index}.legacy_profile")
        if name in seen:
            raise FrameworkError(f"duplicate shadow profile: {name}")
        seen.add(name)
        identifier(row["family"], f"shadow row {index}.family")
        identifier(row["resource_mode"], f"shadow row {index}.resource_mode")
        if row["validation_suite"] is not None:
            identifier(row["validation_suite"], f"shadow row {index}.validation_suite")
        if row["target_product"] is not None:
            identifier(row["target_product"], f"shadow row {index}.target_product")
        if row["target_role"] not in ROLES:
            raise FrameworkError(f"shadow row {index} has an unsupported target role")
        if row["status"] not in SHADOW_STATUS:
            raise FrameworkError(f"shadow row {index} has an unsupported status")
        if (not isinstance(row["rationale"], str) or not row["rationale"].strip()):
            raise FrameworkError(f"shadow row {index} rationale is missing")
        if row["status"] != "EXACT" and not row["rationale"].strip():
            raise FrameworkError(f"shadow row {index} non-EXACT rationale is missing")
    manifest = load_json(source_manifest)
    if manifest.get("profile_count") != 27 or not isinstance(manifest.get("profiles"), list):
        raise FrameworkError("shadow source manifest does not describe 27 profiles")
    migration = load_json(source_migration)
    if not isinstance(migration.get("rows"), list) or len(migration["rows"]) != 27:
        raise FrameworkError("shadow source migration ledger does not describe 27 rows")
    manifest_names = {item.get("name") for item in manifest["profiles"]}
    migration_by_name = {
        item.get("legacy_profile"): item for item in migration["rows"]
        if isinstance(item, dict)
    }
    if len(migration_by_name) != 27 or seen != manifest_names or seen != set(migration_by_name):
        raise FrameworkError("shadow ledger coverage differs from frozen legacy profiles")
    for row in rows:
        source = migration_by_name[row["legacy_profile"]]
        target = source.get("target")
        if (not isinstance(target, dict) or
                row["family"] != target.get("family") or
                row["resource_mode"] != target.get("resource_mode") or
                row["validation_suite"] != target.get("validation_suite") or
                row["target_role"] != target.get("role")):
            raise FrameworkError(f"shadow row mapping differs from migration ledger: {row['legacy_profile']}")
    body = dict(value)
    supplied = body.pop("identity_sha256")
    digest(supplied, "shadow ledger identity")
    if sha256(canonical_json(body)) != supplied:
        raise FrameworkError("shadow ledger identity mismatch")
    return value


def _shadow_inventory_closure(repository: Path, profile: str) -> list[str]:
    """Return repository-relative inventory consumers naming one profile."""
    inventory_path = repository / "board/bk7258/scripts/legacy_profile_consumers.json"
    inventory = load_json(inventory_path)
    paths = {
        row["path"] for row in inventory.get("consumers", [])
        if isinstance(row, dict) and any(
            isinstance(reference, dict) and reference.get("term") == profile
            for reference in row.get("references", [])
        )
    }
    paths.update({
        f"board/bk7258/configs/{profile}/profile.conf",
        f"board/bk7258/configs/{profile}/defconfig",
    })
    return sorted(paths)


def _shadow_graph(repository: Path, product_id: str) -> dict[str, Any] | None:
    catalog = load_catalog(repository)
    product = catalog["products"].get(product_id)
    if product is None:
        return None
    path = repository / "board/bk7258/scripts" / f"bk7258_resource_graph_{product['board']}.json"
    if not path.is_file():
        return None
    return load_json(path)


def _shadow_graph_evidence(graph: dict[str, Any] | None) -> tuple[list[Any] | None, list[Any] | None]:
    if graph is None:
        return None, None
    resources = graph.get("resources", {})
    devpaths = sorted((dict(item) for item in resources.get("devpaths_minors", [])),
                      key=lambda item: item.get("id", ""))
    claims: list[Any] = []
    for category in sorted(resources):
        if category == "bom":
            continue
        for item in resources.get(category, []):
            row = dict(item)
            row["category"] = category
            claims.append(row)
    claims.sort(key=lambda item: (item.get("category", ""), item.get("id", "")))
    return devpaths, claims


def _shadow_new_evidence(repository: Path, product_id: str | None,
                         role: str) -> dict[str, Any]:
    fields: dict[str, Any] = {
        "metadata": None, "resolved_defconfig": None, "source_closure": None, "device_nodes": None,
        "resource_claims": None, "sdk_role": None, "package_plan": None,
    }
    if product_id is None:
        return fields
    ir = resolve(repository, product_id, role)
    plan = build_plan(repository, product_id)
    fragment_paths = []
    for fragment in ir["fragments"]:
        matches = sorted((path.relative_to(repository).as_posix()
                          for path in (repository / "board/bk7258/scripts").glob(
                              f"bk7258_fragment_catalog_{fragment['id']}.json")))
        fragment_paths.extend(matches)
    source = ir["source_view"]
    fields["resolved_defconfig"] = {
        "symbols": dict(ir["symbols"]), "identity_sha256": ir["identity_sha256"],
    }
    fields["source_closure"] = sorted(set(fragment_paths + [
        source["board_root"], source["board_variant"], source["chip_root"],
    ]))
    graph = _shadow_graph(repository, product_id)
    fields["device_nodes"], fields["resource_claims"] = _shadow_graph_evidence(graph)
    sdk_role = plan["sdk"]["roles"].get(role)
    if sdk_role is None:
        fields["sdk_role"] = None
    else:
        fields["sdk_role"] = {"registry_id": sdk_role,
                               "manifest_sha256": sdk_role.removeprefix("sha256:")}
    try:
        package = pack_prepare(repository, product_id)
    except FrameworkError:
        package = None
    if package is not None:
        fields["package_plan"] = {
            "package_id": package["package_id"], "kind": package["kind"],
            "plan": package["plan"], "apps_plan": package["apps_plan"],
            "identity_sha256": package["identity_sha256"],
        }
    return fields


def _shadow_comparison(old: dict[str, Any], new: dict[str, Any]) -> dict[str, str]:
    result: dict[str, str] = {}
    result["metadata"] = "NOT_COMPARABLE"
    old_symbols = old["resolved_defconfig"]
    new_symbols = new["resolved_defconfig"]
    if old_symbols is None or new_symbols is None:
        result["resolved_defconfig"] = "NOT_AVAILABLE"
    else:
        old_map = {key: item for key, item in old_symbols.items() if item is not None}
        new_map = new_symbols.get("symbols", {})
        new_map = {key: item for key, item in new_map.items() if item is not None}
        result["resolved_defconfig"] = ("MATCH" if all(
            new_map.get(key) == item for key, item in old_map.items()
        ) else "DIFFERENT")
    for field in ("source_closure", "device_nodes", "resource_claims", "sdk_role",
                  "package_plan"):
        old_value, new_value = old[field], new[field]
        if old_value is None or new_value is None:
            result[field] = "NOT_AVAILABLE"
        elif field == "sdk_role":
            old_digest = old_value.get("sha256_manifest") if isinstance(old_value, dict) else None
            new_digest = new_value.get("manifest_sha256") if isinstance(new_value, dict) else None
            result[field] = ("MATCH" if old_digest is not None and old_digest == new_digest
                             else "DIFFERENT")
        else:
            result[field] = "MATCH" if old_value == new_value else "DIFFERENT"
    return result


def shadow_parity(repository: Path, ledger_path: Path | None = None,
                  *, require_git: bool = True) -> dict[str, Any]:
    """Produce deterministic old/new evidence for every frozen profile."""
    root = repository.resolve()
    path = ledger_path or root / SHADOW_LEDGER_REL
    if not path.is_absolute():
        path = root / path
    ledger = validate_shadow_ledger(root, load_json(path))
    manifest_path = root / ledger["source_manifest"]
    manifest = load_json(manifest_path)
    if require_git:
        try:
            from verify_legacy_profile_freeze import check_manifest  # noqa: PLC0415
            check_manifest(root, manifest, require_git=True)
        except FrameworkError:
            raise
        except Exception as error:
            # Keep the CLI contract stable while preserving the original
            # reason.  No report is emitted from an unverified freeze.
            raise FrameworkError(f"frozen profile verification failed: {error}") from error
    migration = load_json(root / ledger["source_migration_ledger"])
    migration_by_name = {row["legacy_profile"]: row for row in migration["rows"]}
    profiles_by_name = {row["name"]: row for row in manifest["profiles"]}
    rows: list[dict[str, Any]] = []
    for mapping in sorted(ledger["rows"], key=lambda item: item["legacy_profile"]):
        name = mapping["legacy_profile"]
        profile = profiles_by_name[name]
        old_sdk = profile["sdk"]
        target_product = mapping["target_product"]
        old = {
            "metadata": dict(profile["metadata"]),
            "resolved_defconfig": dict(profile["defconfig_symbols"]),
            "source_closure": _shadow_inventory_closure(root, name),
            "device_nodes": None,
            "resource_claims": None,
            "sdk_role": dict(old_sdk),
            "package_plan": None,
        }
        new = _shadow_new_evidence(root, target_product, mapping["target_role"])
        comparison = _shadow_comparison(old, new)
        rows.append({
            "legacy_profile": name,
            "mapping": {key: mapping[key] for key in (
                "family", "resource_mode", "validation_suite", "target_product", "target_role")},
            "old": old,
            "new": new,
            "comparison": comparison,
            "status": mapping["status"],
            "rationale": mapping["rationale"],
        })
    body: dict[str, Any] = {
        "schema": SHADOW_REPORT_SCHEMA,
        "kind": "legacy-profile-shadow-report",
        "version": 1,
        "status": "shadow-only",
        "profile_count": len(rows),
        "statuses": list(SHADOW_STATUS_ORDER),
        "ledger_identity_sha256": ledger["identity_sha256"],
        "rows": rows,
        "hardware_accessed": False,
        "network_used": False,
    }
    result = dict(body)
    result["identity_sha256"] = sha256(canonical_json(body))
    return validate_shadow_report(result)


def validate_shadow_report(value: dict[str, Any]) -> dict[str, Any]:
    exact(value, {"schema", "kind", "version", "status", "profile_count", "statuses",
                  "ledger_identity_sha256", "rows", "hardware_accessed", "network_used",
                  "identity_sha256"}, "shadow report")
    if (value["schema"] != SHADOW_REPORT_SCHEMA or
            value["kind"] != "legacy-profile-shadow-report" or value["version"] != 1 or
            value["status"] != "shadow-only"):
        raise FrameworkError("unsupported shadow report schema/status")
    if value["profile_count"] != 27 or len(value["rows"]) != 27:
        raise FrameworkError("shadow report must contain exactly 27 rows")
    if value["statuses"] != list(SHADOW_STATUS_ORDER) or set(value["statuses"]) != SHADOW_STATUS:
        raise FrameworkError("shadow report status enum is not stable")
    digest(value["ledger_identity_sha256"], "shadow report ledger identity")
    if value["hardware_accessed"] is not False or value["network_used"] is not False:
        raise FrameworkError("shadow report claims hardware or network access")
    seen: set[str] = set()
    for index, raw in enumerate(value["rows"]):
        row = obj(raw, f"shadow report row {index}")
        exact(row, {"legacy_profile", "mapping", "old", "new", "comparison", "status",
                    "rationale"}, f"shadow report row {index}")
        name = identifier(row["legacy_profile"], f"shadow report row {index}.profile")
        if name in seen:
            raise FrameworkError(f"duplicate shadow report profile: {name}")
        seen.add(name)
        mapping = obj(row["mapping"], f"shadow report row {index}.mapping")
        exact(mapping, {"family", "resource_mode", "validation_suite", "target_product",
                        "target_role"}, f"shadow report row {index}.mapping")
        identifier(mapping["family"], "shadow report family")
        identifier(mapping["resource_mode"], "shadow report resource mode")
        if mapping["validation_suite"] is not None:
            identifier(mapping["validation_suite"], "shadow report validation suite")
        if mapping["target_product"] is not None:
            identifier(mapping["target_product"], "shadow report target product")
        if mapping["target_role"] not in ROLES:
            raise FrameworkError("shadow report target role is invalid")
        for side in ("old", "new"):
            evidence = obj(row[side], f"shadow report row {index}.{side}")
            exact(evidence, {"metadata", "resolved_defconfig", "source_closure",
                             "device_nodes", "resource_claims", "sdk_role", "package_plan"},
                  f"shadow report row {index}.{side}")
        comparison = obj(row["comparison"], f"shadow report row {index}.comparison")
        exact(comparison, {"metadata", "resolved_defconfig", "source_closure",
                           "device_nodes", "resource_claims", "sdk_role", "package_plan"},
              f"shadow report row {index}.comparison")
        if any(not isinstance(item, str) or not item for item in comparison.values()):
            raise FrameworkError("shadow comparison status is malformed")
        if row["status"] not in SHADOW_STATUS:
            raise FrameworkError("shadow report row status is invalid")
        if row["status"] != "EXACT" and (not isinstance(row["rationale"], str) or
                                           not row["rationale"].strip()):
            raise FrameworkError("shadow report non-EXACT rationale is missing")
    digest(value["identity_sha256"], "shadow report identity")
    body = dict(value)
    supplied = body.pop("identity_sha256")
    if sha256(canonical_json(body)) != supplied:
        raise FrameworkError("shadow report identity mismatch")
    return value


def _framework_check_step(checks: list[dict[str, Any]], check_id: str,
                          detail: str, callback: Callable[[], Any]) -> None:
    callback()
    checks.append({"id": check_id, "status": "PASS", "detail": detail})


def framework_check(repository: Path) -> dict[str, Any]:
    """Run the bounded P0-P8 metadata/framework smoke contract."""
    root = repository.resolve()
    checks: list[dict[str, Any]] = []
    manifest_path = root / "board/bk7258/scripts/legacy_profile_freeze_manifest.json"
    manifest = load_json(manifest_path)
    try:
        from verify_legacy_profile_freeze import check_manifest  # noqa: PLC0415
        _framework_check_step(checks, "p0-freeze", "27 frozen profiles and consumer inventory",
                              lambda: check_manifest(root, manifest, require_git=True))
    except ImportError as error:
        raise FrameworkError(f"P0 verifier unavailable: {error}") from error
    catalog = load_catalog(root)
    _framework_check_step(checks, "p1-catalog", "strict board/product/fragment catalogs",
                          lambda: None)
    for product in ("t5ai_core_bringup", "aidk_ai_toy_bringup"):
        for role in ("cp", "ap", "bl2"):
            resolve(root, product, role)
    _framework_check_step(checks, "p1-resolve", "T5 and AIDK role resolution", lambda: None)
    registry_path = root / "board/bk7258/scripts/bk7258_sdk_registry.json"
    set_path = root / "board/bk7258/scripts/bk7258_sdk_set.json"
    lock_path = root / "board/bk7258/scripts/bk7258_sdk_lock.json"
    registry = validate_sdk_registry(root, load_json(registry_path))
    sdk_set = validate_sdk_set(load_json(set_path), registry)
    validate_sdk_lock(root, registry_path, set_path, load_json(lock_path), registry, sdk_set)
    _framework_check_step(checks, "p2-sdk-metadata", "SDK registry/set/lock invariants", lambda: None)
    for product in ("t5ai_core_bringup", "aidk_ai_toy_bringup"):
        plan = build_plan(root, product)
        validate_build_plan(plan)
        for role in ("cp", "ap"):
            validate_config_document(config_document(resolve(root, product, role)))
    _framework_check_step(checks, "p3-build-plans", "representative T5/AIDK config and role plans", lambda: None)
    from bk7258_resource_graph import (  # noqa: PLC0415
        validate_migration_ledger, validate_ownership_manifest, validate_resource_graph,
    )
    validate_ownership_manifest(root, load_json(root / "board/bk7258/scripts/bk7258_layer_ownership.json"))
    validate_migration_ledger(root, load_json(root / "board/bk7258/scripts/bk7258_compatibility_migration_ledger.json"))
    _framework_check_step(checks, "p4-ownership-migration", "ownership and migration metadata", lambda: None)
    for board in ("t5ai_core", "aidk_ai_toy"):
        graph = load_json(root / "board/bk7258/scripts" / f"bk7258_resource_graph_{board}.json")
        validate_resource_graph(root, graph)
    _framework_check_step(checks, "p4-resource-graphs", "T5/AIDK resource graph schemas", lambda: None)
    from bk7258_validation import validate_descriptor_set  # noqa: PLC0415
    validate_descriptor_set(root, load_json(root / "board/bk7258/scripts/bk7258_validation_descriptors.json"))
    _framework_check_step(checks, "p5-validation", "validation descriptors and 27-profile coverage", lambda: None)
    for product in ("t5ai_core_bringup", "aidk_ai_toy_bringup"):
        pack_prepare(root, product)
    _framework_check_step(checks, "p6-package-plan", "metadata-only package plan generation", lambda: None)
    transport_plan(root, "aidk_ai_toy", host="linux", candidates=["/dev/ttyBK7258"], aidk=True)
    _framework_check_step(checks, "p7-transport", "dry-run transport capability/sequence", lambda: None)
    _framework_check_step(checks, "p8-aidk-binding", "AIDK board selector and binding metadata",
                          lambda: load_catalog(root)["boards"]["aidk_ai_toy"])
    body: dict[str, Any] = {
        "schema": FRAMEWORK_CHECK_SCHEMA,
        "kind": "framework-check",
        "version": 1,
        "status": "PASS",
        "checks": checks,
        "hardware_accessed": False,
        "network_used": False,
    }
    result = dict(body)
    result["identity_sha256"] = sha256(canonical_json(body))
    return validate_framework_check(result)


def validate_framework_check(value: dict[str, Any]) -> dict[str, Any]:
    exact(value, {"schema", "kind", "version", "status", "checks",
                  "hardware_accessed", "network_used", "identity_sha256"},
          "framework check")
    if (value["schema"] != FRAMEWORK_CHECK_SCHEMA or
            value["kind"] != "framework-check" or value["version"] != 1 or
            value["status"] != "PASS"):
        raise FrameworkError("unsupported framework-check schema/status")
    checks = array(value["checks"], "framework check checks")
    if not checks:
        raise FrameworkError("framework check has no checks")
    seen: set[str] = set()
    for index, raw in enumerate(checks):
        check = obj(raw, f"framework check row {index}")
        exact(check, {"id", "status", "detail"}, f"framework check row {index}")
        check_id = identifier(check["id"], f"framework check row {index}.id")
        if check_id in seen:
            raise FrameworkError(f"duplicate framework check id: {check_id}")
        seen.add(check_id)
        if check["status"] != "PASS" or not isinstance(check["detail"], str) or not check["detail"]:
            raise FrameworkError(f"framework check row {index} is not a stable PASS")
    if value["hardware_accessed"] is not False or value["network_used"] is not False:
        raise FrameworkError("framework check claims hardware or network access")
    digest(value["identity_sha256"], "framework check identity")
    body = dict(value)
    supplied = body.pop("identity_sha256")
    if sha256(canonical_json(body)) != supplied:
        raise FrameworkError("framework check identity mismatch")
    return value


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(canonical_json(value))


def load_json_checked(path: Path, field: str) -> dict[str, Any]:
    _sdk_regular(path, field)
    return load_json(path)


def write_new_json(path: Path, value: dict[str, Any]) -> None:
    try:
        path.lstat()
    except FileNotFoundError:
        pass
    except OSError as error:
        raise FrameworkError(f"cannot inspect output path: {path}") from error
    else:
        raise FrameworkError(f"refusing to replace existing output: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(canonical_json(value))


def _cli_transport_filter(args: argparse.Namespace) -> dict[str, str | None] | None:
    values = {
        "vid": args.vid,
        "pid": args.pid,
        "serial_prefix": args.serial_prefix,
        "interface": args.interface,
        "location": args.location,
    }
    return None if all(item is None for item in values.values()) else values


def _cli_emit_transport(value: dict[str, Any], output: Path | None) -> None:
    if output is not None:
        write_json(output, value)
    else:
        print(canonical_json(value).decode(), end="")


def cli(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    commands = parser.add_subparsers(dest="command", required=True)
    resolve_parser = commands.add_parser("resolve")
    resolve_parser.add_argument("--product", required=True)
    resolve_parser.add_argument("--role", required=True, choices=("cp", "ap", "bl2"))
    resolve_parser.add_argument("--board")
    resolve_parser.add_argument("--mode")
    resolve_parser.add_argument("--out", type=Path, required=True)
    cmake_parser = commands.add_parser("cmake-view")
    for argument in ("product", "role"):
        cmake_parser.add_argument(f"--{argument}", required=True)
    cmake_parser.add_argument("--board")
    cmake_parser.add_argument("--mode")
    cmake_parser.add_argument("--out", type=Path, required=True)
    report_parser = commands.add_parser("classic-report")
    report_parser.add_argument("--out", type=Path, required=True)
    view_parser = commands.add_parser("role-view")
    view_parser.add_argument("--product", required=True)
    view_parser.add_argument("--role", required=True, choices=tuple(sorted(ROLES)))
    view_parser.add_argument("--board")
    view_parser.add_argument("--mode")
    view_parser.add_argument("--out", type=Path, required=True)
    view_parser.add_argument("--cmake-out", type=Path)
    config_parser = commands.add_parser("config", aliases=("generate-config",))
    config_parser.add_argument("--product", required=True)
    config_parser.add_argument("--role", required=True, choices=("cp", "ap"))
    config_parser.add_argument("--board")
    config_parser.add_argument("--mode")
    config_parser.add_argument("--out", type=Path, required=True)
    config_parser.add_argument("--defconfig-out", type=Path)
    plan_parser = commands.add_parser("build-plan", aliases=("plan",))
    plan_parser.add_argument("--product", required=True)
    plan_parser.add_argument("--board")
    plan_parser.add_argument("--mode")
    plan_parser.add_argument("--set", dest="sdk_set", type=Path)
    plan_parser.add_argument("--lock", type=Path)
    plan_parser.add_argument("--out", type=Path, required=True)
    pack_prepare_parser = commands.add_parser("pack-prepare")
    pack_prepare_parser.add_argument("--product", required=True)
    pack_prepare_parser.add_argument("--kind", choices=tuple(sorted(PACK_KINDS)),
                                     default="application")
    pack_prepare_parser.add_argument("--board")
    pack_prepare_parser.add_argument("--mode")
    pack_prepare_parser.add_argument("--partition", type=Path)
    pack_prepare_parser.add_argument("--out", type=Path, required=True)
    pack_verify_parser = commands.add_parser("pack-verify")
    pack_verify_parser.add_argument("--package", type=Path, required=True)
    def add_transport_common(parser_object: argparse.ArgumentParser) -> None:
        parser_object.add_argument("--host")
        parser_object.add_argument("--port")
        parser_object.add_argument("--vid")
        parser_object.add_argument("--pid")
        parser_object.add_argument("--serial-prefix")
        parser_object.add_argument("--interface")
        parser_object.add_argument("--location")
        parser_object.add_argument("--device-root", type=Path, default=Path("/dev"))
        parser_object.add_argument("--candidate", action="append", default=[])
        parser_object.add_argument("--windows-port", action="append", default=[])
        parser_object.add_argument("--powershell-adapter", action="store_true")
        parser_object.add_argument("--out", type=Path)

    port_list_parser = commands.add_parser("port-list")
    port_list_parser.add_argument("--host")
    port_list_parser.add_argument("--device-root", type=Path, default=Path("/dev"))
    port_list_parser.add_argument("--candidate", action="append", default=[])
    port_list_parser.add_argument("--windows-port", action="append", default=[])
    port_list_parser.add_argument("--powershell-adapter", action="store_true")
    port_list_parser.add_argument("--out", type=Path)
    port_resolve_parser = commands.add_parser("port-resolve")
    add_transport_common(port_resolve_parser)
    transport_parser = commands.add_parser("transport-plan")
    transport_parser.add_argument("--board", required=True)
    add_transport_common(transport_parser)
    transport_parser.add_argument("--aidk", action="store_true")
    transport_parser.add_argument("--rts", action="store_true")
    transport_parser.add_argument("--dtr", action="store_true")
    transport_parser.add_argument("--reset", action="store_true")
    transport_parser.add_argument("--rts-reset", action="store_true")
    sdk_import_parser = commands.add_parser("sdk-import", aliases=("import-sdk",))
    sdk_import_parser.add_argument("--registry", type=Path)
    sdk_import_parser.add_argument("--entry", required=True)
    sdk_import_parser.add_argument("--bundle-dir", type=Path, required=True)
    sdk_import_parser.add_argument("--out", type=Path, required=True)
    sdk_verify_parser = commands.add_parser("sdk-verify", aliases=("verify-sdk",))
    sdk_verify_parser.add_argument("--registry", type=Path)
    sdk_verify_parser.add_argument("--set", dest="sdk_set", type=Path)
    sdk_verify_parser.add_argument("--lock", type=Path)
    sdk_verify_parser.add_argument("--bundle", action="append", default=[])
    sdk_verify_parser.add_argument("--bundle-root", type=Path)
    layer_parser = commands.add_parser("layer-check", aliases=("ownership-check",))
    layer_parser.add_argument("--manifest", type=Path)
    migration_parser = commands.add_parser("migration-check")
    migration_parser.add_argument("--ledger", type=Path)
    resource_check_parser = commands.add_parser("resource-check", aliases=("graph-check",))
    resource_check_parser.add_argument("--graph", type=Path)
    resource_resolve_parser = commands.add_parser("resource-resolve", aliases=("graph-resolve",))
    resource_resolve_parser.add_argument("--graph", type=Path)
    resource_resolve_parser.add_argument("--out", type=Path, required=True)
    validation_parser = commands.add_parser("validation-check")
    validation_parser.add_argument("--descriptors", type=Path)
    shadow_parser = commands.add_parser(
        "shadow-check", aliases=("shadow-parity", "shadow-report", "parity",
                                 "parity-check", "legacy-shadow", "legacy-parity"))
    shadow_parser.add_argument("--ledger", type=Path)
    shadow_parser.add_argument("--no-git", action="store_true",
                               help="skip the approved Git-object freeze check")
    shadow_parser.add_argument("--out", type=Path)
    framework_check_parser = commands.add_parser(
        "framework-check", aliases=("check-framework",))
    framework_check_parser.add_argument("--out", type=Path)
    commands.add_parser("validate")
    args = parser.parse_args(argv)
    root = args.root.resolve()
    try:
        if args.command == "validate":
            catalog = load_catalog(root)
            print(f"bk7258-framework: OK boards={len(catalog['boards'])} products={len(catalog['products'])} fragments={len(catalog['fragments'])}")
        elif args.command == "classic-report":
            write_json(args.out, classic_report(root))
        elif args.command == "role-view":
            ir = resolve(root, args.product, args.role, args.board, args.mode)
            write_json(args.out, role_view_manifest(ir))
            if args.cmake_out is not None:
                args.cmake_out.parent.mkdir(parents=True, exist_ok=True)
                args.cmake_out.write_text(cmake_view(ir), encoding="utf-8")
        elif args.command in {"config", "generate-config"}:
            ir = resolve(root, args.product, args.role, args.board, args.mode)
            document = config_document(ir)
            write_json(args.out, document)
            if args.defconfig_out is not None:
                args.defconfig_out.parent.mkdir(parents=True, exist_ok=True)
                args.defconfig_out.write_text(document["defconfig"], encoding="utf-8")
        elif args.command in {"build-plan", "plan"}:
            plan = build_plan(root, args.product, args.board, args.mode,
                              args.sdk_set, args.lock)
            write_json(args.out, plan)
        elif args.command == "pack-prepare":
            package = pack_prepare(root, args.product, args.kind, args.board, args.mode,
                                   args.partition)
            write_json(args.out, package)
        elif args.command == "pack-verify":
            result = pack_verify(root, args.package.resolve())
            print("bk7258-package: VERIFY PASS "
                  f"package={result['package_id']} kind={result['kind']} "
                  f"source_build_id={result['source_build_id']}")
        elif args.command == "port-list":
            result = port_list(
                args.host,
                device_root=args.device_root,
                candidates=args.candidate or None,
                windows_ports=args.windows_port or None,
                powershell_adapter=args.powershell_adapter,
            )
            _cli_emit_transport(result, args.out)
        elif args.command == "port-resolve":
            result = port_resolve(
                args.host,
                port=args.port,
                identity=_cli_transport_filter(args),
                device_root=args.device_root,
                candidates=args.candidate or None,
                windows_ports=args.windows_port or None,
                powershell_adapter=args.powershell_adapter,
            )
            _cli_emit_transport(result, args.out)
        elif args.command == "transport-plan":
            result = transport_plan(
                root,
                args.board,
                host=args.host,
                port=args.port,
                identity=_cli_transport_filter(args),
                device_root=args.device_root,
                candidates=args.candidate or None,
                windows_ports=args.windows_port or None,
                powershell_adapter=args.powershell_adapter,
                aidk=args.aidk,
                capabilities={
                    "rts": args.rts, "dtr": args.dtr, "reset": args.reset,
                    "rts_reset": args.rts_reset,
                } if any((args.rts, args.dtr, args.reset, args.rts_reset)) else None,
            )
            _cli_emit_transport(result, args.out)
        elif args.command in {"sdk-import", "import-sdk"}:
            registry_path = (args.registry or
                             root / "board/bk7258/scripts/bk7258_sdk_registry.json").resolve()
            registry = validate_sdk_registry(root, load_json_checked(registry_path, "SDK registry"))
            entry_id = _sdk_entry_id(args.entry, "--entry")
            matches = [item for item in registry["entries"] if item["id"] == entry_id]
            if len(matches) != 1:
                raise FrameworkError(f"SDK registry entry is not exactly one: {entry_id}")
            result = verify_sdk_bundle(root, matches[0], args.bundle_dir.absolute())
            write_new_json(args.out, sdk_import_receipt(matches[0], result))
            print(f"bk7258-sdk: IMPORT VERIFIED {entry_id} files={result['file_count']}")
        elif args.command in {"sdk-verify", "verify-sdk"}:
            registry_path = (args.registry or
                             root / "board/bk7258/scripts/bk7258_sdk_registry.json").resolve()
            set_path = (args.sdk_set or
                        root / "board/bk7258/scripts/bk7258_sdk_set.json").resolve()
            lock_path = (args.lock or
                         root / "board/bk7258/scripts/bk7258_sdk_lock.json").resolve()
            registry = validate_sdk_registry(root, load_json_checked(registry_path, "SDK registry"))
            sdk_set = validate_sdk_set(load_json_checked(set_path, "SDK set"), registry)
            lock = validate_sdk_lock(root, registry_path, set_path,
                                     load_json_checked(lock_path, "SDK lock"), registry, sdk_set)
            by_id = {_sdk_entry_id(item["id"], "registry entry"): item for item in registry["entries"]}
            bundle_args: dict[str, Path] = {}
            for spec in args.bundle:
                role, separator, raw_path = spec.partition("=")
                if not separator or role not in {"cp", "ap", "bl2"} or not raw_path:
                    raise FrameworkError("--bundle must be ROLE=PATH for cp, ap, or bl2")
                if role in bundle_args:
                    raise FrameworkError(f"duplicate --bundle role: {role}")
                bundle_args[role] = Path(raw_path)
            if args.bundle_root is not None:
                for role in ("cp", "ap"):
                    if role in bundle_args:
                        raise FrameworkError(f"--bundle and --bundle-root both select {role}")
                    entry = by_id[lock["roles"][role]["registry_id"]]
                    bundle_args[role] = args.bundle_root / "versions" / entry["version"] / role
            for role, bundle_dir in bundle_args.items():
                if role == "bl2":
                    raise FrameworkError("BL2 has no SDK bundle to verify")
                entry = by_id[lock["roles"][role]["registry_id"]]
                verify_sdk_bundle(root, entry, bundle_dir.absolute())
            print(f"bk7258-sdk: VERIFY PASS set={sdk_set['id']} lock={lock['id']}")
        elif args.command in {"layer-check", "ownership-check", "migration-check",
                              "resource-check", "graph-check", "resource-resolve", "graph-resolve"}:
            # Keep the ownership/resource checker in its own existing scripts
            # module; this lazy import avoids a framework/resource import cycle
            # while exposing one canonical host CLI.
            from bk7258_resource_graph import (  # noqa: PLC0415
                resolve_resource_graph,
                validate_migration_ledger,
                validate_ownership_manifest,
                validate_resource_graph,
            )

            def _rooted(path: Path | None, default: Path) -> Path:
                actual = path or default
                return actual if actual.is_absolute() else root / actual

            if args.command in {"layer-check", "ownership-check"}:
                manifest = load_json_checked(
                    _rooted(args.manifest, root / "board/bk7258/scripts/bk7258_layer_ownership.json"),
                    "layer ownership manifest")
                validate_ownership_manifest(root, manifest)
                print("bk7258-framework: LAYER OWNERSHIP PASS")
            elif args.command == "migration-check":
                ledger = load_json_checked(
                    _rooted(args.ledger, root / "board/bk7258/scripts/bk7258_compatibility_migration_ledger.json"),
                    "compatibility migration ledger")
                validate_migration_ledger(root, ledger)
                print("bk7258-framework: MIGRATION LEDGER PASS")
            else:
                graph = load_json_checked(
                    _rooted(args.graph, root / "board/bk7258/scripts/bk7258_resource_graph_t5ai_core.json"),
                    "resource graph")
                if args.command in {"resource-check", "graph-check"}:
                    validate_resource_graph(root, graph)
                    print("bk7258-framework: RESOURCE GRAPH PASS")
                else:
                    write_json(args.out, resolve_resource_graph(root, graph))
                    print(f"bk7258-framework: RESOURCE GRAPH RESOLVED {args.out}")
        elif args.command == "validation-check":
            from bk7258_validation import (  # noqa: PLC0415
                validate_descriptor_set,
            )

            descriptor_path = (args.descriptors or
                               root / "board/bk7258/scripts/bk7258_validation_descriptors.json")
            if not descriptor_path.is_absolute():
                descriptor_path = root / descriptor_path
            descriptor_set = load_json_checked(descriptor_path, "validation descriptors")
            result = validate_descriptor_set(root, descriptor_set)
            print("bk7258-framework: VALIDATION PASS "
                  f"descriptors={result['descriptors']} "
                  f"legacy_profiles={result['legacy']['profiles']}")
        elif args.command in {"shadow-check", "shadow-parity", "shadow-report", "parity",
                              "parity-check", "legacy-shadow", "legacy-parity"}:
            result = shadow_parity(root, args.ledger, require_git=not args.no_git)
            if args.out is not None:
                write_json(args.out, result)
            else:
                print(canonical_json(result).decode(), end="")
        elif args.command in {"framework-check", "check-framework"}:
            result = framework_check(root)
            if args.out is not None:
                write_json(args.out, result)
            else:
                print(canonical_json(result).decode(), end="")
        else:
            ir = resolve(root, args.product, args.role, args.board, args.mode)
            if args.command == "resolve":
                write_json(args.out, ir)
            else:
                args.out.parent.mkdir(parents=True, exist_ok=True)
                args.out.write_text(cmake_view(ir), encoding="utf-8")
        return 0
    except FrameworkError as error:
        print(f"bk7258-framework: FAIL: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(cli())
