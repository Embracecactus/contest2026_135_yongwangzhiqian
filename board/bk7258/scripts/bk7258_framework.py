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
import re
import sys
from pathlib import Path
from typing import Any, Callable


SCHEMA = "bk7258.composition/1"
ROLES = frozenset({"cp", "ap", "bl2"})
MODES = frozenset({"bringup", "application", "validation", "factory"})
BOOTS = frozenset({"raw", "mcuboot"})
ID_RE = re.compile(r"^[a-z][a-z0-9_-]*$")
SYMBOL_RE = re.compile(r"^CONFIG_[A-Z0-9_]+$")
HASH_RE = re.compile(r"^[0-9a-f]{64}$")
STAGES = {"common": 0, "role": 1, "board": 2, "boot": 3, "feature": 4,
          "app": 5, "validation": 6, "factory": 7}


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
            any(char.isspace() for char in value) or value.startswith("/")):
        raise FrameworkError(f"unsafe repository path in {field}")
    parts = value.split("/")
    if any(part in {"", ".", ".."} for part in parts):
        raise FrameworkError(f"unsafe repository path in {field}")
    return value


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
                  "roles", "fragments", "features", "validation_suite"}, "product")
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
    symbols(value["symbols"], "IR.symbols")
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
        "symbols": merge_symbols(fragments),
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


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(canonical_json(value))


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
