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
from bk7258_common import (
    FrameworkError,
    HASH_RE,
    ID_RE,
    RETIRED_REPOSITORY_ROOTS,
    SYMBOL_RE,
    array,
    canonical_json,
    digest,
    exact,
    identifier,
    identifiers,
    load_json,
    obj,
    relative_path,
    sha256,
    symbols,
    write_json,
)

from bk7258_paths import load_board_script


SCHEMA = "bk7258.composition/1"
ROLES = frozenset({"cp", "ap", "bl2"})
MODES = frozenset({"bringup", "application", "validation", "factory"})
BOOTS = frozenset({"raw", "mcuboot"})
# The role set is part of the build-plan identity.  A raw T5AI composition has
# no BL2 stage; MCUboot compositions retain both boot roles.  Keep the mapping
# local and deterministic so plan generation does not consult ambient policy
# or execute any boot tooling.  The executor reconciles this binding with the
# checked-in boot-policy document before staging or running a role.
ACTIVE_ROLES_BY_BOOT = {
    "raw": ("bl1", "cp", "ap"),
    "mcuboot": ("bl1", "bl2", "cp", "ap"),
}
# The unsigned BL2 compile target has a fixed board-owned image window.  This
# is deliberately distinct from the resolved partition capacity (0x20000),
# which is a storage bound rather than the image's compile-time copy size.
BL2_IMAGE_LOGICAL_SIZE_BY_BOOT = {"raw": None, "mcuboot": 0x3000}
PLAN_ROLES = ("bl1", "bl2", "cp", "ap")
SDK_REGISTRY_SCHEMA = "bk7258.sdk-registry/1"
SDK_SET_SCHEMA = "bk7258.sdk-set/1"
SDK_LOCK_SCHEMA = "bk7258.sdk-lock/1"
SDK_IMPORT_SCHEMA = "bk7258.sdk-import/1"
CONFIG_SCHEMA = "bk7258.config/1"
BUILD_PLAN_SCHEMA = "bk7258.build-plan/1"
BKPACK_SCHEMA = "bk7258.bkpack/1"
EXECUTION_CONTEXT_SCHEMA = "bk7258.execution-context/1"
SDK_ROLES = frozenset({"cp", "ap"})
# Canonical SDK manifest store after the scripts/ convergence refactor.
# Registry entries reference this path directly; no runtime old-path
# compatibility remains (the legacy profile freeze reads the historical
# spelling only from the approved Git baseline commit).
SDK_MANIFEST_ROOT = "board/bk7258/bk_idk/manifests"
PRIVATE_MIRROR_URL = "https://github.com/Embracecactus/vendor-bk-avdk-smp.git"
SDK_ENTRY_KINDS = frozenset({"official", "derived", "sealed-binary"})
SDK_REQUIRED_DIRS = frozenset({"include", "config", "libs"})
TOKEN_RE = re.compile(r"^[a-z][a-z0-9_]*$")
SDK_VERSION_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")
BOARD_SELECTORS = {
    "aidk_ai_toy": "CONFIG_BK7258_BOARD_AIDK_AI_TOY",
    "t5_board": "CONFIG_BK7258_BOARD_T5_BOARD",
    "t5ai_core": "CONFIG_BK7258_BOARD_T5AI_CORE",
}
# The canonical role configuration is the retained seed defconfig (or a
# user-provided final .config); the final .config produced by Kconfig is the
# only configuration authority.  No fragment/overlay merge remains.
CANONICAL_CONFIG_COMPAT = "seed-config-v1"

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
PACK_ROLE_ARTIFACTS = {
    "bl1": "bootloader.bin",
    "bl2": "bl2.bin",
    "cp": "vela_nuttx_cp.bin",
    "ap": "vela_nuttx_ap.bin",
}
PACK_ARTIFACTS = {
    "libarch.a": ("static_archive", "chip", ("cp", "ap")),
    "libboard.a": ("static_archive", "board", ("cp", "ap")),
    "bootloader.bin": ("vendor_boot_binary", "board", ("bl1",)),
    "bl2.bin": ("vendor_boot_binary", "board", ("bl2",)),
    "vela_nuttx_cp.bin": ("firmware_binary", "board", ("cp",)),
    "vela_nuttx_ap.bin": ("firmware_binary", "board", ("ap",)),
}
TRANSPORT_SCHEMA = "bk7258.transport/1"
TRANSPORT_HOSTS = frozenset({"linux", "darwin", "windows", "wsl"})
TRANSPORT_IDENTITY_KEYS = ("vid", "pid", "serial_prefix", "interface", "location")
TRANSPORT_CAPABILITY_KEYS = ("rts", "dtr", "reset", "rts_reset")
FRAMEWORK_CHECK_SCHEMA = "bk7258.framework-check/1"
PARTITION_ROOT = "board/bk7258/partitions"
























def _sdk_metadata_path(value: Any, field: str) -> str:
    """Validate a product's repository-relative SDK set/lock metadata path."""
    path = relative_path(value, field)
    name = path.rsplit("/", 1)[-1]
    kind = "set" if field.endswith("sdk_set") else "lock"
    if (not path.startswith("tools/bk7258/bk7258_sdk_") or
            not name.endswith(".json") or
            not (name == f"bk7258_sdk_{kind}.json" or
                 name.startswith(f"bk7258_sdk_{kind}_"))):
        raise FrameworkError(f"{field} must name an in-tree SDK set/lock metadata file")
    return path


def _partition_layout_reference(value: Any, field: str) -> dict[str, Any]:
    """Validate one product-pinned, repository-owned partition layout."""
    layout = obj(value, field)
    exact(layout, {"source", "layout_id", "layout_sha256"}, field)
    source = relative_path(layout["source"], f"{field}.source")
    if (not source.startswith(PARTITION_ROOT + "/") or
            not source.endswith(".csv")):
        raise FrameworkError(
            f"{field}.source must name a repository-owned partition CSV")
    layout_id = identifier(layout["layout_id"], f"{field}.layout_id")
    layout_sha256 = digest(layout["layout_sha256"],
                           f"{field}.layout_sha256")
    if not layout_id.endswith("-" + layout_sha256[:16]):
        raise FrameworkError(f"{field} id/hash binding mismatch")
    return layout


def _load_product_partition_layout(repository: Path, reference: Any,
                                   field: str) -> Any:
    """Load and verify the exact layout selected by a product catalog."""
    layout_ref = _partition_layout_reference(reference, field)
    repository_root = repository.resolve()
    source = repository_root / layout_ref["source"]
    try:
        resolved_source = source.resolve(strict=True)
    except OSError as error:
        raise FrameworkError(
            f"cannot load product partition layout: {layout_ref['source']}") from error
    if resolved_source != source or not resolved_source.is_file():
        raise FrameworkError(
            f"product partition source is not a regular in-tree file: {layout_ref['source']}")
    try:
        gen_bk7258_partitions = load_board_script("gen_bk7258_partitions")
        from gen_bk7258_partitions import load_layout  # noqa: PLC0415

        layout = load_layout(resolved_source)
    except (ImportError, OSError, ValueError, RuntimeError) as error:
        raise FrameworkError(
            f"cannot load product partition layout: {layout_ref['source']}") from error
    if (layout.layout_id != layout_ref["layout_id"] or
            layout.layout_sha256 != layout_ref["layout_sha256"]):
        raise FrameworkError(
            f"product partition layout identity differs from source: {layout_ref['source']}")
    return layout






def validate_board_selector_symbols(board_id: str,
                                    values: dict[str, str | None],
                                    field: str) -> None:
    """Require a present board selector to match the IR board.

    Metadata documents may omit the selector when the board is the Kconfig
    default; the strict check happens on the final .config via
    :func:`verify_final_config`.
    """
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

    if selected not in ([], [board_id]):
        raise FrameworkError(
            f"{field} may select only board {board_id}: {selected}")


def validate_board(value: dict[str, Any]) -> dict[str, Any]:
    exact(value, {"schema", "kind", "id", "soc", "variant",
                  "resource_claims", "transport"}, "board")
    if value["schema"] != SCHEMA or value["kind"] != "board":
        raise FrameworkError("unsupported board schema")
    identifier(value["id"], "board.id")
    if value["soc"] != "bk7258":
        raise FrameworkError("board.soc must be bk7258")
    identifier(value["variant"], "board.variant")
    # Console/debug Kconfig facts are owned by the final .config, never by
    # board metadata.  The board catalog keeps only electrical/resource facts.
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
    # ``legacy_profile`` is the retained seed name for roles that have one.
    exact(role, {"legacy_profile"}, field)
    if role["legacy_profile"] is not None:
        identifier(role["legacy_profile"], f"{field}.legacy_profile")


def validate_product(value: dict[str, Any]) -> dict[str, Any]:
    exact(value, {"schema", "kind", "id", "family", "mode", "board", "boot",
                  "roles", "sdk_set", "sdk_lock", "partition_layout"},
          "product")
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
    _sdk_metadata_path(value["sdk_set"], "product.sdk_set")
    _sdk_metadata_path(value["sdk_lock"], "product.sdk_lock")
    _partition_layout_reference(value["partition_layout"],
                                "product.partition_layout")
    return value


def validate_ir(value: dict[str, Any]) -> dict[str, Any]:
    exact(value, {"schema", "kind", "inputs", "fragments", "symbols",
                  "resource_claims", "source_view", "identity_sha256"}, "IR")
    if value["schema"] != SCHEMA or value["kind"] != "resolved-config-ir":
        raise FrameworkError("unsupported IR schema")
    inputs = obj(value["inputs"], "IR.inputs")
    exact(inputs, {"product", "family", "mode", "board", "role", "boot",
                   "validation_suite", "legacy_profile", "partition_layout"},
          "IR.inputs")
    for field in ("product", "family", "mode", "board"):
        identifier(inputs[field], f"IR.inputs.{field}")
    identifier(inputs["role"], "IR.inputs.role")
    if inputs["role"] not in ROLES:
        raise FrameworkError("unsupported IR role")
    if inputs["mode"] not in MODES:
        raise FrameworkError("unsupported IR mode")
    if inputs["boot"] not in BOOTS:
        raise FrameworkError("unsupported IR boot")
    for field in ("validation_suite", "legacy_profile"):
        if inputs[field] is not None:
            identifier(inputs[field], f"IR.inputs.{field}")
    _partition_layout_reference(inputs["partition_layout"],
                                "IR.inputs.partition_layout")
    if value["fragments"] != []:
        raise FrameworkError("IR fragment lists are retired; no config overlay")
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
    return repository / "tools/bk7258"


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
    }


VALIDATION_SUITE_REL = Path("tools/bk7258/bk7258_validation_suite_catalog.json")


def load_validation_suites(repository: Path) -> dict[str, dict[str, Any]]:
    """Load the one-to-many validation suite map without creating products.

    A suite is a resource/behavior metadata overlay on one of the three
    canonical products.  It is deliberately separate from ``configs/`` and
    never injects Kconfig symbols: the final .config is the only configuration
    authority.
    """
    document = load_json(repository / VALIDATION_SUITE_REL)
    exact(document, {"schema", "kind", "version", "suites", "identity_sha256"},
          "validation suite catalog")
    if (document["schema"] != "bk7258.validation-suite/1" or
            document["kind"] != "validation-suite-catalog" or
            document["version"] != 1):
        raise FrameworkError("unsupported validation suite catalog")
    suites = array(document["suites"], "validation suite catalog suites")
    result: dict[str, dict[str, Any]] = {}
    for index, raw in enumerate(suites):
        suite = obj(raw, f"validation suite {index}")
        exact(suite, {"id", "product", "resources"},
              f"validation suite {index}")
        suite_id = identifier(suite["id"], f"validation suite {index}.id")
        if suite_id in result:
            raise FrameworkError(f"duplicate validation suite: {suite_id}")
        product = identifier(suite["product"], f"validation suite {suite_id}.product")
        resources = array(suite["resources"], f"validation suite {suite_id}.resources")
        for resource in resources:
            identifier(resource, f"validation suite {suite_id}.resource")
        result[suite_id] = suite
    body = dict(document)
    supplied = body.pop("identity_sha256")
    digest(supplied, "validation suite catalog identity")
    if sha256(canonical_json(body)) != supplied:
        raise FrameworkError("validation suite catalog identity mismatch")
    return result


def resolve_validation_suite(repository: Path, product_id: str, suite_id: str,
                             role: str, board_id: str | None = None,
                             mode: str | None = None) -> dict[str, Any]:
    """Resolve a canonical product plus one validation suite metadata overlay.

    The suite contributes only resource claims.  It never adds or overrides
    Kconfig symbols; application/driver selection lives in the final .config.
    """
    suites = load_validation_suites(repository)
    suite = suites.get(suite_id)
    if suite is None:
        raise FrameworkError(f"unknown validation suite: {suite_id}")
    if suite["product"] != product_id:
        raise FrameworkError(
            f"validation suite {suite_id} is bound to {suite['product']}, not {product_id}"
        )
    if role not in {"cp", "ap"}:
        raise FrameworkError("validation suites are runtime CP/AP overlays")
    base = resolve(repository, product_id, role, board_id, mode)
    inputs = dict(base["inputs"])
    inputs["validation_suite"] = suite_id
    claims = list(base["resource_claims"])
    for resource in suite["resources"]:
        claim = {"resource": resource, "owner": f"suite_{suite_id}",
                 "phases": ["validation"]}
        if any(item["resource"] == resource for item in claims):
            continue
        claims.append(claim)
    body = dict(base)
    body["inputs"] = inputs
    body["resource_claims"] = sorted(claims,
                                      key=lambda item: (item["resource"], item["owner"]))
    body.pop("identity_sha256", None)
    body["identity_sha256"] = sha256(canonical_json(body))
    return validate_ir(body)


def validation_suite_check(repository: Path, product_id: str,
                           suite_id: str) -> dict[str, Any]:
    """Validate one suite's catalog identity and CP/AP product binding."""
    suites = load_validation_suites(repository)
    suite = suites.get(suite_id)
    if suite is None:
        raise FrameworkError(f"unknown validation suite: {suite_id}")
    if suite["product"] != product_id:
        raise FrameworkError(
            f"validation suite {suite_id} is bound to {suite['product']}, not {product_id}")
    resolved = {
        role: resolve_validation_suite(repository, product_id, suite_id, role)
        for role in ("cp", "ap")
    }
    catalog = load_json(repository / VALIDATION_SUITE_REL)
    return {
        "suite": suite_id,
        "product": product_id,
        "catalog_identity_sha256": catalog["identity_sha256"],
        "role_ir_identity_sha256": {
            role: resolved[role]["identity_sha256"] for role in ("cp", "ap")
        },
    }


def resolve(repository: Path, product_id: str, role: str, board_id: str | None = None,
            mode: str | None = None) -> dict[str, Any]:
    """Resolve product metadata for one role without generating configs.

    The retained seed (``legacy_profile``) is the configuration input; the
    final .config produced by Kconfig is the only configuration authority.
    """
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
    _load_product_partition_layout(repository, product["partition_layout"],
                                   "product.partition_layout")
    partition_layout = dict(product["partition_layout"])
    body: dict[str, Any] = {
        "schema": SCHEMA,
        "kind": "resolved-config-ir",
        "inputs": {
            "product": product["id"], "family": product["family"], "mode": product["mode"],
            "board": board["id"], "role": role, "boot": product["boot"],
            "validation_suite": None,
            "legacy_profile": product["roles"][role]["legacy_profile"],
            "partition_layout": partition_layout,
        },
        "fragments": [],
        "symbols": {},
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
            "builder": "tools/bk7258/build_dual_image.sh",
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
    if legacy["builder"] != "tools/bk7258/build_dual_image.sh":
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
    source = "tools/bk7258/build_dual_image.sh"
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


def sdk_metadata_path(repository: Path, relative: str) -> Path:
    """Resolve a registry SDK metadata path to its on-disk location.

    Registry entries already reference the canonical store
    (``SDK_MANIFEST_ROOT``), so this is a direct repository-relative join.
    The historical ``scripts/sdk-manifests`` spelling is read only by the
    legacy profile freeze scanner from the approved Git baseline commit.
    """
    return repository / relative


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
        manifest_path = sdk_metadata_path(repository, manifest_rel)
        provenance_path = sdk_metadata_path(repository, provenance_rel)
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


def _sdk_lock_version_policy(roles: dict[str, Any],
                             by_id: dict[str, dict[str, Any]]) -> dict[str, str]:
    """Require CP/AP SDK bundles to share one version unless policy allows."""
    versions = {
        role: by_id[roles[role]["registry_id"]]["version"]
        for role in ("cp", "ap")
    }
    if versions["cp"] != versions["ap"]:
        raise FrameworkError(
            "SDK lock CP/AP versions differ: "
            f"cp={versions['cp']} ap={versions['ap']}")
    return versions


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
    if value["registry_path"] != "tools/bk7258/bk7258_sdk_registry.json":
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
    _sdk_lock_version_policy(roles, by_id)
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
    manifest_path = sdk_metadata_path(repository, entry["manifest_path"])
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


def config_document(ir: dict[str, Any], repository: Path | None = None,
                    config_path: Path | str | None = None) -> dict[str, Any]:
    """Bind one role config document to a retained seed or final .config.

    The framework never synthesizes Kconfig values.  A role either has a
    retained seed defconfig under ``configs/`` or is bound to a user-provided
    final .config; Kconfig resolves the actual final .config at build time.
    """
    validate_ir(ir)
    if config_path is None:
        seed = ir["inputs"]["legacy_profile"]
        if not seed:
            raise FrameworkError(
                f"role {ir['inputs']['role']} has no retained seed; "
                "supply config_path with the final .config")
        if repository is None:
            raise FrameworkError(
                "repository is required to resolve the retained seed")
        config_path = Path(repository) / "board/bk7258/configs" / seed / "defconfig"
    config_path = Path(config_path)
    if config_path.is_symlink() or not config_path.is_file():
        raise FrameworkError(f"role config is not a regular file: {config_path}")
    try:
        text = config_path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        raise FrameworkError(f"cannot read role config: {config_path}") from error
    config_symbols = parse_final_config(text)
    body: dict[str, Any] = {
        "schema": CONFIG_SCHEMA,
        "kind": "resolved-role-config",
        "version": 1,
        "ir_identity_sha256": ir["identity_sha256"],
        "inputs": dict(ir["inputs"]),
        "partition_layout": dict(ir["inputs"]["partition_layout"]),
        "symbols": config_symbols,
        "defconfig": text,
    }
    body["defconfig_sha256"] = sha256(body["defconfig"].encode())
    result = dict(body)
    result["identity_sha256"] = sha256(canonical_json(body))
    return validate_config_document(result)


def validate_config_document(value: dict[str, Any]) -> dict[str, Any]:
    exact(value, {"schema", "kind", "version", "ir_identity_sha256", "inputs",
                  "partition_layout", "symbols", "defconfig", "defconfig_sha256",
                  "identity_sha256"},
          "resolved role config")
    if value["schema"] != CONFIG_SCHEMA or value["kind"] != "resolved-role-config" or value["version"] != 1:
        raise FrameworkError("unsupported resolved role config schema")
    digest(value["ir_identity_sha256"], "role config IR identity")
    inputs = obj(value["inputs"], "role config inputs")
    exact(inputs, {"product", "family", "mode", "board", "role", "boot",
                   "validation_suite", "legacy_profile", "partition_layout"},
          "role config inputs")
    for field in ("product", "family", "mode", "board", "role"):
        identifier(inputs[field], f"role config inputs.{field}")
    if inputs["role"] not in ROLES:
        raise FrameworkError("unsupported role config role")
    if inputs["mode"] not in MODES or inputs["boot"] not in BOOTS:
        raise FrameworkError("unsupported role config mode/boot")
    for field in ("validation_suite", "legacy_profile"):
        if inputs[field] is not None:
            identifier(inputs[field], f"role config {field}")
    input_partition = _partition_layout_reference(
        inputs["partition_layout"], "role config inputs.partition_layout")
    document_partition = _partition_layout_reference(
        value["partition_layout"], "role config partition_layout")
    if document_partition != input_partition:
        raise FrameworkError("role config partition layout binding mismatch")
    config_symbols = symbols(value["symbols"], "role config symbols")
    validate_board_selector_symbols(inputs["board"], config_symbols,
                                    "role config symbols")
    if not isinstance(value["defconfig"], str) or "\x00" in value["defconfig"]:
        raise FrameworkError("role config defconfig is malformed")
    digest(value["defconfig_sha256"], "role config defconfig_sha256")
    if sha256(value["defconfig"].encode()) != value["defconfig_sha256"]:
        raise FrameworkError("role config defconfig digest mismatch")
    digest(value["identity_sha256"], "role config identity")
    body = dict(value)
    del body["identity_sha256"]
    if sha256(canonical_json(body)) != value["identity_sha256"]:
        raise FrameworkError("role config identity mismatch")
    return value


def parse_final_config(text: str) -> dict[str, str | None]:
    """Parse a NuttX final .config into ``CONFIG_*`` values.

    ``# CONFIG_X is not set`` lines map to ``None``; ``CONFIG_X=value`` lines
    keep their literal value.  This is deliberately independent of
    kconfiglib so host verification stays dependency-free and fail-closed.
    """
    result: dict[str, str | None] = {}
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        if line.startswith("# ") and line.endswith(" is not set"):
            name = line[2:-len(" is not set")]
            if name.startswith("CONFIG_"):
                result[name] = None
            continue
        if line.startswith("#"):
            continue
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        if key.startswith("CONFIG_"):
            result[key] = value
    return result


def verify_final_config(repository: Path, product_id: str, role: str,
                        config_path: Path, *,
                        expected_layout_id: str | None = None,
                        expected_sdk_version: str | None = None) -> dict[str, Any]:
    """Verify one built CP/AP/BL2 final .config against product metadata.

    The final .config produced by Kconfig is the single configuration
    authority.  This function checks exactly the locked facts that an
    application may never override (physical board, role, boot chain) and
    returns the config SHA-256 for build/package identity.  Layout and SDK
    bindings are supplied by the executor's plan and checked here as well.
    """
    identifier(product_id, "product")
    identifier(role, "role")
    if role not in ROLES:
        raise FrameworkError(f"role must be cp, ap, or bl2: {role}")
    config_path = Path(config_path)
    if config_path.is_symlink() or not config_path.is_file():
        raise FrameworkError(f"final config is not a regular file: {config_path}")
    try:
        raw = config_path.read_bytes()
    except OSError as error:
        raise FrameworkError(f"cannot read final config: {config_path}") from error
    text = raw.decode("utf-8", errors="strict")
    values = parse_final_config(text)
    catalog = load_catalog(repository)
    product = catalog["products"].get(product_id)
    if product is None:
        raise FrameworkError(f"unknown product: {product_id}")
    board_id = product["board"]
    selected = [
        candidate for candidate, selector in BOARD_SELECTORS.items()
        if values.get(selector) == "y"
    ]
    if selected != [board_id]:
        raise FrameworkError(
            f"final config selects board {selected or 'none'}, "
            f"product requires {board_id}")
    ap_core = values.get("CONFIG_BK7258_AP_CORE") == "y"
    if (role == "ap") != ap_core:
        raise FrameworkError(
            f"final config AP role mismatch: role={role} AP_CORE={ap_core}")
    if role == "bl2" and values.get("CONFIG_BK7258_BL2_IMAGE") != "y":
        raise FrameworkError("final BL2 config must select CONFIG_BK7258_BL2_IMAGE")
    mcuboot = values.get("CONFIG_BK7258_MCUBOOT_IMAGE") == "y"
    expected_mcuboot = product["boot"] == "mcuboot"
    if mcuboot != expected_mcuboot:
        raise FrameworkError(
            f"final config boot mismatch: MCUBOOT_IMAGE={mcuboot}, "
            f"product requires {product['boot']}")
    layout_id = product["partition_layout"]["layout_id"]
    if expected_layout_id is not None and expected_layout_id != layout_id:
        raise FrameworkError(
            f"expected partition layout {expected_layout_id} differs from "
            f"product {layout_id}")
    return {
        "product": product_id,
        "role": role,
        "board": board_id,
        "boot": product["boot"],
        "config_path": str(config_path),
        "config_sha256": sha256(raw),
        "config_size": len(raw),
        "layout_id": layout_id,
        "sdk_version": expected_sdk_version,
        "board_selector": BOARD_SELECTORS[board_id],
        "ap_core": ap_core,
        "mcuboot_image": mcuboot,
    }


def _boot_config_identity(product_ir: dict[str, Any], role: str, makefile: str,
                          kind: str) -> str:
    """Hash the resolved standalone-role configuration contract.

    BL1/BL2 have no NuttX ``.config``.  Their source bytes are bound by the
    isolated source-snapshot identity; this digest intentionally covers only
    the resolved product, layout, role and Make entry point.  Do not claim a
    partial source-file manifest as a configuration hash.
    """
    body = {
        "schema": CONFIG_SCHEMA,
        "kind": kind,
        "version": 1,
        "product": product_ir["inputs"]["product"],
        "family": product_ir["inputs"]["family"],
        "mode": product_ir["inputs"]["mode"],
        "board": product_ir["inputs"]["board"],
        "partition_layout": dict(product_ir["inputs"]["partition_layout"]),
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
    registry_path = repository / "tools/bk7258/bk7258_sdk_registry.json"
    actual_set_path = set_path or repository / "tools/bk7258/bk7258_sdk_set.json"
    actual_lock_path = lock_path or repository / "tools/bk7258/bk7258_sdk_lock.json"
    if not actual_set_path.is_absolute():
        actual_set_path = repository / actual_set_path
    if not actual_lock_path.is_absolute():
        actual_lock_path = repository / actual_lock_path
    registry = validate_sdk_registry(repository, load_json_checked(registry_path, "SDK registry"))
    sdk_set = validate_sdk_set(load_json_checked(actual_set_path, "SDK set"), registry)
    lock = validate_sdk_lock(repository, registry_path, actual_set_path,
                             load_json_checked(actual_lock_path, "SDK lock"), registry, sdk_set)
    return sdk_set, lock


def _sdk_lock_versions(repository: Path, lock: dict[str, Any]) -> dict[str, str | None]:
    """Resolve bundle versions from the validated SDK lock, never ambient env."""
    registry_path = repository / "tools/bk7258/bk7258_sdk_registry.json"
    registry = validate_sdk_registry(repository, load_json_checked(registry_path,
                                                                   "SDK registry"))
    by_id = {_sdk_entry_id(item["id"], "SDK registry entry"): item
             for item in registry["entries"]}
    versions: dict[str, str | None] = {}
    for role in ("cp", "ap"):
        registry_id = lock["roles"][role]["registry_id"]
        entry = by_id.get(registry_id)
        if entry is None:
            raise FrameworkError(f"SDK lock {role} entry is missing from registry")
        versions[role] = _sdk_version(entry["version"], f"SDK lock {role} version")
    versions["bl2"] = None
    return versions


def _profile_field(path: Path, key: str) -> str | None:
    """Read one non-secret compatibility profile field exactly once."""
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeError) as error:
        raise FrameworkError(f"cannot read legacy profile metadata: {path}") from error
    values = [line.split("=", 1)[1] for line in lines
              if line.startswith(f"{key}=")]
    if len(values) > 1:
        raise FrameworkError(f"legacy profile defines {key} more than once: {path}")
    return values[0] if values else None


def _canonical_profile_text(ir: dict[str, Any]) -> str:
    """Render the small, deterministic profile header for a resolved IR.

    This is intentionally metadata-only.  The complete Kconfig value map is
    emitted by :func:`config_document`; this header exists solely because the
    existing CMake adapter still accepts a directory containing both files.
    """
    validate_ir(ir)
    inputs = ir["inputs"]
    lines = [
        "# Generated from retained seed/final .config input; do not edit.",
        "BK7258_PROFILE_SCHEMA=1",
        f"BK7258_PROFILE_BOARD={inputs['board']}",
        f"BK7258_PROFILE_ROLE={inputs['role']}",
        f"BK7258_PROFILE_BOOT={inputs['boot']}",
        "BK7258_PROFILE_CLASS=runnable",
        f"BK7258_PROFILE_COMPAT={CANONICAL_CONFIG_COMPAT}",
        f"BK7258_PROFILE_IR_SHA256={ir['identity_sha256']}",
    ]
    sdk_version = inputs.get("sdk_bundle")
    if sdk_version is not None:
        lines.insert(-1, f"BK7258_PROFILE_SDK_BUNDLE={sdk_version}")
    return "\n".join(lines) + "\n"


def _canonical_seed_profiles(repository: Path, product: dict[str, Any],
                             role_ir: dict[str, dict[str, Any]],
                             sdk_versions: dict[str, str | None],
                             config_root: Path | None = None,
                             ) -> dict[str, dict[str, Any]]:
    """Describe CP/AP config inputs bound to retained seeds or final .configs.

    ``source`` is either a ``configs/<seed>`` directory (relative to the
    repository) or an absolute final ``<role>.config`` file.  The framework
    never synthesizes Kconfig values.
    """
    product_id = product["id"]
    result: dict[str, dict[str, Any]] = {}
    for role in ("cp", "ap"):
        ir = role_ir[role]
        if config_root is not None:
            config_path = Path(config_root) / f"{role}.config"
            source = str(config_path)
        else:
            seed = ir["inputs"]["legacy_profile"]
            if not seed:
                raise FrameworkError(
                    f"product {product_id} role {role} has no retained seed; "
                    "pass config_root containing <role>.config final configs")
            config_path = (repository / "board/bk7258/configs" / seed /
                           "defconfig")
            source = f"board/bk7258/configs/{seed}"
        config = config_document(ir, repository=repository,
                                 config_path=config_path)
        profile_text = _canonical_profile_text(ir)
        target = f"{product_id.removesuffix('_bringup')}_{role}_{product['boot']}"
        seed_profile = ir["inputs"]["legacy_profile"] or target
        profile_hash = sha256(profile_text.encode())
        defconfig_hash = config["defconfig_sha256"]
        result[role] = {
            "seed_profile": seed_profile,
            "source": source,
            "profile_sha256": profile_hash,
            "defconfig_sha256": defconfig_hash,
            "target_profile": target,
            "compat": CANONICAL_CONFIG_COMPAT,
            "sdk_bundle": sdk_versions[role],
            "materialized_profile_sha256": profile_hash,
            "materialized_defconfig_sha256": defconfig_hash,
        }
    return result


def build_plan(repository: Path, product_id: str, board_id: str | None = None,
               mode: str | None = None, set_path: Path | None = None,
               lock_path: Path | None = None,
               config_root: Path | None = None) -> dict[str, Any]:
    """Resolve all boot/runtime roles into an isolated, metadata-only plan."""
    cp_ir = resolve(repository, product_id, "cp", board_id, mode)
    ap_ir = resolve(repository, product_id, "ap", board_id, mode)
    bl2_ir = resolve(repository, product_id, "bl2", board_id, mode)
    for other in (ap_ir, bl2_ir):
        if other["inputs"]["product"] != cp_ir["inputs"]["product"] or \
                other["inputs"]["family"] != cp_ir["inputs"]["family"] or \
                other["inputs"]["mode"] != cp_ir["inputs"]["mode"] or \
                other["inputs"]["board"] != cp_ir["inputs"]["board"] or \
                other["inputs"]["boot"] != cp_ir["inputs"]["boot"] or \
                other["inputs"]["partition_layout"] != cp_ir["inputs"]["partition_layout"]:
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
    partition_layout = dict(inputs["partition_layout"])
    if (sdk_set["product"] != inputs["product"] or sdk_set["board"] != inputs["board"] or
            sdk_set["mode"] != inputs["mode"]):
        raise FrameworkError("SDK set does not exactly match resolved product/mode/board")
    sdk_versions = _sdk_lock_versions(repository, lock)
    role_ir = {"cp": cp_ir, "ap": ap_ir, "bl2": bl2_ir}
    active_roles = list(ACTIVE_ROLES_BY_BOOT[inputs["boot"]])
    config_ids = {
        "cp": None,
        "ap": None,
        "bl1": _boot_config_identity(
            cp_ir, "bl1", "board/bk7258/bootloader/Makefile", "boot-policy"),
        "bl2": _boot_config_identity(
            cp_ir, "bl2", "board/bk7258/bootloader/bl2/Makefile",
            "minimal-make-inputs"),
    }
    seed_profiles = _canonical_seed_profiles(
        repository, product_metadata, role_ir, sdk_versions,
        config_root=config_root)
    for role in ("cp", "ap"):
        row = seed_profiles[role]
        source = row["source"]
        if source.startswith("board/bk7258/configs/"):
            config_path = repository / source / "defconfig"
        else:
            config_path = Path(source)
        config_ids[role] = config_document(
            role_ir[role], repository=repository,
            config_path=config_path)["identity_sha256"]
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
            "activation": "active" if role in active_roles else "inactive",
            "applicability": "required" if role in active_roles else "not-applicable",
        }
    body: dict[str, Any] = {
        "schema": BUILD_PLAN_SCHEMA,
        "kind": "isolated-build-plan",
        "version": 1,
        "active_roles": active_roles,
        "bl2_image_logical_size": BL2_IMAGE_LOGICAL_SIZE_BY_BOOT[inputs["boot"]],
        "identity_inputs": {
            "product": inputs["product"],
            "family": inputs["family"],
            "mode": inputs["mode"],
            "board": inputs["board"],
            "boot": inputs["boot"],
            "bl2_image_logical_size": BL2_IMAGE_LOGICAL_SIZE_BY_BOOT[inputs["boot"]],
            "active_roles": active_roles,
            "role_ir_sha256": {role: role_ir[role]["identity_sha256"] for role in ("cp", "ap", "bl2")},
            "sdk_set_id": sdk_set["id"],
            "sdk_lock_id": lock["id"],
            "partition_layout_id": partition_layout["layout_id"],
            "partition_layout_sha256": partition_layout["layout_sha256"],
        },
        "board": {"id": inputs["board"], "variant": board_variant},
        "partition_layout": partition_layout,
        "sdk": {
            "set_id": sdk_set["id"],
            "lock_id": lock["id"],
            "lock_identity_sha256": lock["identity_sha256"],
            "roles": {role: sdk_set["roles"][role] for role in ("cp", "ap", "bl2")},
            "versions": sdk_versions,
        },
        "source_views": source_views,
        "roles": build_roles,
        "config_inputs": {
            "mode": "resolved-config",
            "seed_profiles": seed_profiles,
        },
    }
    result = dict(body)
    result["identity_sha256"] = sha256(canonical_json(body))
    return validate_build_plan(result)


def build_plan_verify(repository: Path, plan_path: Path,
                      product_id: str | None = None,
                      config_root: Path | None = None) -> dict[str, Any]:
    """Verify an external plan against the current product/SDK/layout inputs."""
    plan = load_json_checked(plan_path, "build plan")
    validate_build_plan(plan)
    selected_product = product_id or plan["identity_inputs"]["product"]
    if plan["identity_inputs"]["product"] != selected_product:
        raise FrameworkError("external build plan product binding mismatch")
    expected = build_plan(repository, selected_product,
                          plan["identity_inputs"]["board"],
                          plan["identity_inputs"]["mode"],
                          config_root=config_root)
    if expected["identity_sha256"] != plan["identity_sha256"]:
        raise FrameworkError("external build plan identity differs from repository")
    return plan


def validate_build_plan(value: dict[str, Any]) -> dict[str, Any]:
    exact(value, {"schema", "kind", "version", "active_roles", "bl2_image_logical_size", "identity_inputs", "board", "sdk",
                  "partition_layout", "source_views", "roles", "config_inputs",
                  "identity_sha256"},
          "isolated build plan")
    if value["schema"] != BUILD_PLAN_SCHEMA or value["kind"] != "isolated-build-plan" or value["version"] != 1:
        raise FrameworkError("unsupported isolated build plan schema")
    inputs = obj(value["identity_inputs"], "build plan identity_inputs")
    exact(inputs, {"product", "family", "mode", "board", "boot", "bl2_image_logical_size", "active_roles", "role_ir_sha256",
                   "sdk_set_id", "sdk_lock_id", "partition_layout_id",
                   "partition_layout_sha256"}, "build plan identity_inputs")
    for field in ("product", "family", "mode", "board", "sdk_set_id", "sdk_lock_id",
                  "partition_layout_id"):
        identifier(inputs[field], f"build plan identity_inputs.{field}")
    digest(inputs["partition_layout_sha256"],
           "build plan identity_inputs.partition_layout_sha256")
    if inputs["mode"] not in MODES or inputs["boot"] not in BOOTS:
        raise FrameworkError("unsupported build plan mode/boot")
    expected_image_size = BL2_IMAGE_LOGICAL_SIZE_BY_BOOT[inputs["boot"]]
    if (value["bl2_image_logical_size"] != expected_image_size or
            inputs["bl2_image_logical_size"] != expected_image_size):
        raise FrameworkError("build plan BL2 image logical size is not policy-bound")
    expected_active_roles = list(ACTIVE_ROLES_BY_BOOT[inputs["boot"]])
    if value["active_roles"] != expected_active_roles or inputs["active_roles"] != expected_active_roles:
        raise FrameworkError("build plan active role binding is unsafe")
    if (not isinstance(value["active_roles"], list) or
            any(role not in PLAN_ROLES for role in value["active_roles"]) or
            len(set(value["active_roles"])) != len(value["active_roles"])):
        raise FrameworkError("build plan active_roles is malformed")
    role_ids = obj(inputs["role_ir_sha256"], "build plan role IR identities")
    exact(role_ids, {"cp", "ap", "bl2"}, "build plan role IR identities")
    for role in role_ids:
        digest(role_ids[role], f"build plan {role} IR identity")
    board = obj(value["board"], "build plan board")
    exact(board, {"id", "variant"}, "build plan board")
    if board["id"] != inputs["board"]:
        raise FrameworkError("build plan board binding mismatch")
    identifier(board["variant"], "build plan board.variant")
    partition_layout = _partition_layout_reference(
        value["partition_layout"], "build plan partition_layout")
    if (partition_layout["layout_id"] != inputs["partition_layout_id"] or
            partition_layout["layout_sha256"] != inputs["partition_layout_sha256"]):
        raise FrameworkError("build plan partition layout identity mismatch")
    sdk = obj(value["sdk"], "build plan SDK")
    exact(sdk, {"set_id", "lock_id", "lock_identity_sha256", "roles", "versions"},
          "build plan SDK")
    if sdk["set_id"] != inputs["sdk_set_id"] or sdk["lock_id"] != inputs["sdk_lock_id"]:
        raise FrameworkError("build plan SDK identity mismatch")
    digest(sdk["lock_identity_sha256"], "build plan SDK lock identity")
    sdk_roles = obj(sdk["roles"], "build plan SDK roles")
    exact(sdk_roles, {"cp", "ap", "bl2"}, "build plan SDK roles")
    for role in ("cp", "ap"):
        _sdk_entry_id(sdk_roles[role], f"build plan SDK {role}")
    if sdk_roles["bl2"] is not None:
        raise FrameworkError("build plan BL2 SDK must be null")
    versions = obj(sdk["versions"], "build plan SDK versions")
    exact(versions, {"cp", "ap", "bl2"}, "build plan SDK versions")
    for role in ("cp", "ap"):
        _sdk_version(versions[role], f"build plan SDK {role} version")
    if versions["bl2"] is not None:
        raise FrameworkError("build plan BL2 SDK version must be null")
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
                     "sdk", "backend", "fake_nuttx_seed", "activation",
                     "applicability"}, f"build plan role {role}")
        role_active = role in expected_active_roles
        if item["activation"] != ("active" if role_active else "inactive"):
            raise FrameworkError(f"build plan role {role} activation is not bound")
        if item["applicability"] != ("required" if role_active else "not-applicable"):
            raise FrameworkError(f"build plan role {role} applicability is not bound")
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
        expected_backend = {
            "bl1": "bootloader-adapter",
            "bl2": "minimal-make",
            "cp": "cmake",
            "ap": "cmake",
        }[role]
        expected_config_kind = {
            "bl1": "boot-policy",
            "bl2": "minimal-make-inputs",
            "cp": "nuttx-defconfig",
            "ap": "nuttx-defconfig",
        }[role]
        if item["backend"] != expected_backend or item["config_kind"] != expected_config_kind:
            raise FrameworkError(
                f"build plan role {role} backend/config_kind is not role-bound")
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
    config_inputs = obj(value["config_inputs"], "build plan config inputs")
    exact(config_inputs, {"mode", "seed_profiles"},
          "build plan config inputs")
    if config_inputs["mode"] != "resolved-config":
        raise FrameworkError("build plan config input boundary is unsafe")
    seed_profiles = obj(config_inputs["seed_profiles"], "build plan seed profiles")
    exact(seed_profiles, {"cp", "ap"}, "build plan seed profiles")
    seed_fields = {"seed_profile", "source", "profile_sha256", "defconfig_sha256",
                   "target_profile", "compat",
                   "sdk_bundle", "materialized_profile_sha256",
                   "materialized_defconfig_sha256"}
    for role in ("cp", "ap"):
        seed = obj(seed_profiles[role], f"build plan seed profile {role}")
        exact(seed, seed_fields, f"build plan seed profile {role}")
        identifier(seed["seed_profile"], f"build plan seed profile {role}.seed_profile")
        source = seed["source"]
        if source.startswith("board/bk7258/configs/"):
            relative_path(source, f"build plan seed profile {role}.source")
        elif not (source.startswith("/") and source.endswith(f"{role}.config")):
            raise FrameworkError(
                f"build plan seed profile {role} source is not canonical")
        digest(seed["profile_sha256"], f"build plan seed profile {role}.profile_sha256")
        digest(seed["defconfig_sha256"],
               f"build plan seed profile {role}.defconfig_sha256")
        digest(seed["materialized_profile_sha256"],
               f"build plan seed profile {role}.materialized_profile_sha256")
        digest(seed["materialized_defconfig_sha256"],
               f"build plan seed profile {role}.materialized_defconfig_sha256")
        identifier(seed["target_profile"],
                   f"build plan seed profile {role}.target_profile")
        if seed["compat"] is not None:
            identifier(seed["compat"], f"build plan seed profile {role}.compat")
        if seed["sdk_bundle"] is not None:
            _sdk_version(seed["sdk_bundle"],
                         f"build plan seed profile {role}.sdk_bundle")
            if seed["sdk_bundle"] != versions[role]:
                raise FrameworkError(
                    f"build plan seed profile {role} SDK version mismatch")
    digest(value["identity_sha256"], "build plan identity")
    body = dict(value)
    del body["identity_sha256"]
    if sha256(canonical_json(body)) != value["identity_sha256"]:
        raise FrameworkError("build plan identity mismatch")
    return value


def _execution_path(template: str, build_root: str) -> str:
    if build_root == "${BUILD_ROOT}":
        return template
    return template.replace("${BUILD_ROOT}", build_root)


def execution_context(repository: Path, product_id: str,
                      build_root: str = "${BUILD_ROOT}",
                      output: str = "${OUTPUT}",
                      board_id: str | None = None,
                      mode: str | None = None,
                      set_path: Path | None = None,
                      lock_path: Path | None = None,
                      config_root: Path | None = None) -> dict[str, Any]:
    """Produce a deterministic, non-executing Classic compatibility context.

    This function intentionally does not inspect signing-key environment
    variables or invoke a subprocess.  It describes the shared-tree Classic
    adapter only; canonical source-read-only execution is owned by
    ``bk7258_isolated_executor.py``.
    """
    plan = build_plan(repository, product_id, board_id, mode, set_path, lock_path,
                      config_root=config_root)
    inputs = plan["identity_inputs"]
    roles: dict[str, dict[str, Any]] = {}
    for role in ("bl1", "bl2", "cp", "ap"):
        item = plan["roles"][role]
        roles[role] = {
            "backend": item["backend"],
            "source_view_id": item["source_view_id"],
            "build_root": _execution_path(item["build_root_template"], build_root),
            "artifact_root": _execution_path(item["artifact_root_template"], build_root),
            "config_path": _execution_path(item["config_path_template"], build_root),
            "config_identity_sha256": item["config_identity_sha256"],
            "sdk_bundle": plan["sdk"]["versions"][role] if role in {"cp", "ap"} else None,
        }
    seed_profiles = plan["config_inputs"]["seed_profiles"]
    profile_root = "config-input-owned"
    profile_names = {role: seed_profiles[role]["target_profile"] for role in ("cp", "ap")}
    # This is intentionally labeled compatibility-only: build_dual_image.sh
    # still uses the shared NuttX tree and is not the canonical CMake executor.
    commands = [{
        "stage": "classic-compatibility-build-and-package",
        "tool": "tools/bk7258/build_dual_image.sh",
        "arguments": [],
        "plan_validation": "build-plan-verify",
        "profile_materialization": "materialize_product_profiles.py",
        "compatibility": "shared-classic-adapter",
    }]
    body: dict[str, Any] = {
        "schema": EXECUTION_CONTEXT_SCHEMA,
        "kind": "execution-context",
        "version": 1,
        "execution_mode": "dry-run",
        "product": inputs["product"],
        "board": inputs["board"],
        "mode": inputs["mode"],
        "boot": inputs["boot"],
        "build_plan_identity_sha256": plan["identity_sha256"],
        "build_root": build_root,
        "output": output,
        "adapter_semantic_parity": "unproven",
        "adapter_execution": {
            "kind": "shared-classic-adapter",
            "consumes_role_build_roots": False,
            "role_paths_executed": False,
        },
        "partition_layout": dict(plan["partition_layout"]),
        "sdk": {
            "set_id": plan["sdk"]["set_id"],
            "lock_id": plan["sdk"]["lock_id"],
            "lock_identity_sha256": plan["sdk"]["lock_identity_sha256"],
            "versions": dict(plan["sdk"]["versions"]),
        },
        "profiles": {
            "root": profile_root,
            "cp": profile_names["cp"],
            "ap": profile_names["ap"],
            "seed_profiles": {
                role: {
                    "seed_profile": seed_profiles[role]["seed_profile"],
                    "profile_sha256": seed_profiles[role]["profile_sha256"],
                    "defconfig_sha256": seed_profiles[role]["defconfig_sha256"],
                    "materialized_profile_sha256": seed_profiles[role]["materialized_profile_sha256"],
                    "materialized_defconfig_sha256": seed_profiles[role]["materialized_defconfig_sha256"],
                } for role in ("cp", "ap")
            },
        },
        "roles": roles,
        "environment": {
            "BK7258_PRODUCT": inputs["product"],
            "BK7258_OUTPUT_ROOT": output,
        },
        "commands": commands,
        "key_requirements": (["MCUBOOT_SIGNING_KEY", "BL1_MANIFEST_KEY",
                              "MCUBOOT_VERSION"]
                             if inputs["boot"] == "mcuboot" else []),
        "package": {
            "tool": "tools/bk7258/bk7258_bkpack.py",
            "expected": inputs["boot"] == "mcuboot",
            "signing_performed": False,
        },
        "side_effects": {
            "compile_invoked": False,
            "key_read": False,
            "bytes_written": False,
            "network_used": False,
            "hardware_accessed": False,
        },
    }
    result = dict(body)
    result["identity_sha256"] = sha256(canonical_json(body))
    return validate_execution_context(result)


def validate_execution_context(value: dict[str, Any]) -> dict[str, Any]:
    """Validate the host-only execution context emitted by ``execute``."""
    exact(value, {"schema", "kind", "version", "execution_mode", "product",
                  "board", "mode", "boot", "build_plan_identity_sha256",
                  "build_root", "output", "adapter_semantic_parity",
                  "adapter_execution",
                  "partition_layout", "sdk", "profiles",
                  "roles", "environment", "commands", "key_requirements",
                  "package", "side_effects", "identity_sha256"},
          "execution context")
    if (value["schema"] != EXECUTION_CONTEXT_SCHEMA or
            value["kind"] != "execution-context" or value["version"] != 1 or
            value["execution_mode"] != "dry-run"):
        raise FrameworkError("unsupported execution context")
    for field in ("product", "board", "mode", "build_root", "output"):
        if field in {"build_root", "output"}:
            if not isinstance(value[field], str) or not value[field]:
                raise FrameworkError(f"invalid execution context {field}")
        else:
            identifier(value[field], f"execution context {field}")
    if value["boot"] not in BOOTS:
        raise FrameworkError("invalid execution context boot")
    if value["adapter_semantic_parity"] != "unproven":
        raise FrameworkError("execution context must not claim semantic parity")
    adapter = obj(value["adapter_execution"], "execution context adapter")
    exact(adapter, {"kind", "consumes_role_build_roots", "role_paths_executed"},
          "execution context adapter")
    if (adapter["kind"] != "shared-classic-adapter" or
            adapter["consumes_role_build_roots"] is not False or
            adapter["role_paths_executed"] is not False):
        raise FrameworkError("execution context adapter isolation is overstated")
    digest(value["build_plan_identity_sha256"], "execution context build plan identity")
    partition = obj(value["partition_layout"], "execution context partition")
    exact(partition, {"source", "layout_id", "layout_sha256"},
          "execution context partition")
    relative_path(partition["source"], "execution context partition.source")
    if not partition["source"].startswith(PARTITION_ROOT + "/"):
        raise FrameworkError("execution context partition source is not canonical")
    identifier(partition["layout_id"], "execution context partition.layout_id")
    digest(partition["layout_sha256"], "execution context partition.layout_sha256")
    if not partition["layout_id"].endswith("-" + partition["layout_sha256"][:16]):
        raise FrameworkError("execution context partition identity mismatch")
    sdk = obj(value["sdk"], "execution context SDK")
    exact(sdk, {"set_id", "lock_id", "lock_identity_sha256", "versions"},
          "execution context SDK")
    identifier(sdk["set_id"], "execution context SDK set")
    identifier(sdk["lock_id"], "execution context SDK lock")
    digest(sdk["lock_identity_sha256"], "execution context SDK lock identity")
    versions = obj(sdk["versions"], "execution context SDK versions")
    exact(versions, {"cp", "ap", "bl2"}, "execution context SDK versions")
    for role in ("cp", "ap"):
        _sdk_version(versions[role], f"execution context SDK {role}")
    if versions["bl2"] is not None:
        raise FrameworkError("execution context BL2 SDK must be null")
    profiles = obj(value["profiles"], "execution context profiles")
    exact(profiles, {"root", "cp", "ap", "seed_profiles"},
          "execution context profiles")
    if profiles["root"] != "config-input-owned":
        raise FrameworkError("execution context profile root ownership mismatch")
    for role in ("cp", "ap"):
        identifier(profiles[role], f"execution context {role} profile")
        expected_profile = value["product"]
        if expected_profile.endswith("_bringup"):
            expected_profile = expected_profile[:-len("_bringup")]
        expected_profile = f"{expected_profile}_{role}_{value['boot']}"
        if profiles[role] != expected_profile:
            raise FrameworkError(f"execution context profile name mismatch: {role}")
    seed_profiles = obj(profiles["seed_profiles"], "execution context seed profiles")
    exact(seed_profiles, {"cp", "ap"}, "execution context seed profiles")
    for role in ("cp", "ap"):
        seed = obj(seed_profiles[role], f"execution context seed {role}")
        exact(seed, {"seed_profile", "profile_sha256", "defconfig_sha256",
                     "materialized_profile_sha256", "materialized_defconfig_sha256"},
              f"execution context seed {role}")
        identifier(seed["seed_profile"], f"execution context seed {role}.name")
        digest(seed["profile_sha256"], f"execution context seed {role}.profile")
        digest(seed["defconfig_sha256"], f"execution context seed {role}.defconfig")
        digest(seed["materialized_profile_sha256"],
               f"execution context seed {role}.materialized_profile")
        digest(seed["materialized_defconfig_sha256"],
               f"execution context seed {role}.materialized_defconfig")
    if not isinstance(value["roles"], dict) or set(value["roles"]) != {"bl1", "bl2", "cp", "ap"}:
        raise FrameworkError("execution context roles are incomplete")
    expected_backend = {"bl1": "bootloader-adapter", "bl2": "minimal-make",
                        "cp": "cmake", "ap": "cmake"}
    for role, item in value["roles"].items():
        row = obj(item, f"execution context role {role}")
        exact(row, {"backend", "source_view_id", "build_root", "artifact_root",
                    "config_path", "config_identity_sha256", "sdk_bundle"},
              f"execution context role {role}")
        if row["backend"] != expected_backend[role]:
            raise FrameworkError(f"execution context backend mismatch: {role}")
        identifier(row["source_view_id"], f"execution context role {role}.source_view_id")
        for field in ("build_root", "artifact_root", "config_path"):
            if not isinstance(row[field], str) or not row[field]:
                raise FrameworkError(f"invalid execution context role path: {role}/{field}")
        expected_root = f"{value['build_root']}/bk7258/{value['product']}/{value['mode']}/{role}"
        if (row["build_root"] != expected_root or
                row["artifact_root"] != f"{expected_root}/artifacts" or
                row["config_path"] != f"{expected_root}/.config"):
            raise FrameworkError(f"execution context role path binding mismatch: {role}")
        digest(row["config_identity_sha256"],
               f"execution context role {role}.config_identity_sha256")
        if role in {"cp", "ap"}:
            if row["sdk_bundle"] != versions[role]:
                raise FrameworkError(f"execution context role SDK mismatch: {role}")
        elif row["sdk_bundle"] is not None:
            raise FrameworkError(f"execution context boot role has SDK: {role}")
    environment = obj(value["environment"], "execution context environment")
    exact(environment, {"BK7258_PRODUCT", "BK7258_OUTPUT_ROOT"},
          "execution context environment")
    if environment["BK7258_PRODUCT"] != value["product"]:
        raise FrameworkError("execution context product environment mismatch")
    if environment["BK7258_OUTPUT_ROOT"] != value["output"]:
        raise FrameworkError("execution context output environment mismatch")
    expected_keys = (["MCUBOOT_SIGNING_KEY", "BL1_MANIFEST_KEY", "MCUBOOT_VERSION"]
                     if value["boot"] == "mcuboot" else [])
    if value["key_requirements"] != expected_keys:
        raise FrameworkError("execution context key requirements mismatch")
    commands = array(value["commands"], "execution context commands")
    if len(commands) != 1:
        raise FrameworkError("execution context command sequence is incomplete")
    command = obj(commands[0], "execution context command 0")
    exact(command, {"stage", "tool", "arguments", "plan_validation",
                    "profile_materialization", "compatibility"},
          "execution context command 0")
    if (command["stage"] != "classic-compatibility-build-and-package" or
            command["tool"] != "tools/bk7258/build_dual_image.sh" or
            command["plan_validation"] != "build-plan-verify" or
            command["profile_materialization"] != "materialize_product_profiles.py" or
            command["compatibility"] != "shared-classic-adapter"):
        raise FrameworkError("execution context compatibility command is not valid")
    if not isinstance(command["arguments"], list) or any(
            not isinstance(item, str) or not item for item in command["arguments"]):
        raise FrameworkError("execution context command arguments are malformed")
    if command["arguments"] != []:
        raise FrameworkError("execution context compatibility command unexpectedly has arguments")
    if not isinstance(value["key_requirements"], list) or any(
            not isinstance(item, str) or not item for item in value["key_requirements"]):
        raise FrameworkError("execution context key requirements are malformed")
    if value["side_effects"] != {
            "compile_invoked": False, "key_read": False, "bytes_written": False,
            "network_used": False, "hardware_accessed": False}:
        raise FrameworkError("dry-run execution context claims side effects")
    package = obj(value["package"], "execution context package")
    exact(package, {"tool", "expected", "signing_performed"},
          "execution context package")
    if (package["signing_performed"] is not False or
            package["expected"] is not (value["boot"] == "mcuboot")):
        raise FrameworkError("execution context package claims signing")
    digest(value["identity_sha256"], "execution context identity")
    body = dict(value)
    del body["identity_sha256"]
    if sha256(canonical_json(body)) != value["identity_sha256"]:
        raise FrameworkError("execution context identity mismatch")
    return value


def execute(repository: Path, product_id: str, *, dry_run: bool = True,
            build_root: str = "${BUILD_ROOT}", output: str = "${OUTPUT}",
            board_id: str | None = None, mode: str | None = None,
            set_path: Path | None = None, lock_path: Path | None = None,
            config_root: Path | None = None) -> dict[str, Any]:
    """Emit the host-only execution context.

    Real image, signing, and packaging actions remain behind the explicit
    compatibility shell entry point.  Keeping this function dry-run-only
    prevents a caller from turning a plan inspection command into an
    unreviewed build by toggling a boolean.
    """
    context = execution_context(repository, product_id, build_root, output,
                                board_id, mode, set_path, lock_path,
                                config_root=config_root)
    if not dry_run:
        raise FrameworkError(
            "framework execute is host-only dry-run; invoke build_dual_image.sh "
            "explicitly for the shared compatibility adapter")
    return context


def isolated_prepare(repository: Path, product_id: str, build_root: Path,
                     output: Path, *, plan_path: Path | None = None,
                     workspace_root: Path | None = None,
                     config_root: Path | None = None) -> dict[str, Any]:
    """Prepare the canonical role-isolated execution contract.

    The import is intentionally lazy: the legacy framework planner remains a
    host-only compatibility surface, while the isolated prepare module owns
    its stricter source-snapshot and four-role manifest contract.  This entry
    point does not invoke a compiler, signer, packer, or hardware tool.
    """
    from bk7258_isolated_executor import prepare  # noqa: PLC0415

    return prepare(repository, product_id, build_root, output,
                   plan_path=plan_path, workspace_root=workspace_root,
                   config_root=config_root)


def isolated_materialize_sources(
        repository: Path, manifest: Path | dict[str, Any], *,
        workspace_root: Path | None = None) -> dict[str, Any]:
    """Materialize and audit the one entity source snapshot.

    This lazy bridge keeps the framework's planner API usable without
    importing the executor at module load time.  The delegated phase remains
    source-only and never signs, packages, accesses hardware, or uses network.
    """
    from bk7258_isolated_executor import materialize_sources  # noqa: PLC0415

    return materialize_sources(repository, manifest, workspace_root=workspace_root)


def isolated_compile_runtime(
        repository: Path, manifest: Path | dict[str, Any], *,
        authorize_compile: bool = False,
        cmake_executable: str | Path = "cmake",
        python_executable: str | Path = Path(sys.executable).resolve(),
        olddefconfig_executable: str | Path = "olddefconfig",
        kconfiglib_root: str | Path | None = None,
        make_executable: str | Path = "make",
        command_runner: Callable[..., Any] | None = None) -> dict[str, Any]:
    """Compile active isolated roles from a materialized manifest.

    This bridge deliberately keeps the executor import lazy and requires the
    explicit policy authorization bit; boot roles are limited to raw ELF/BIN
    compile-only targets, with no signing, packaging, hardware, or network.
    """
    from bk7258_isolated_executor import compile_runtime  # noqa: PLC0415

    return compile_runtime(
        repository, manifest, authorize_compile=authorize_compile,
        cmake_executable=cmake_executable,
        python_executable=python_executable,
        olddefconfig_executable=olddefconfig_executable,
        kconfiglib_root=kconfiglib_root,
        make_executable=make_executable,
        command_runner=command_runner)


# ``build-runtime`` is an operation-oriented spelling used by some callers.
isolated_build_runtime = isolated_compile_runtime


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


def _pack_partition_layout(repository: Path, reference: Any,
                           partition_path: Path | None = None) -> tuple[Any, str]:
    """Load only the product-resolved partition source as package metadata."""
    layout_ref = _partition_layout_reference(reference,
                                             "package product partition_layout")
    if partition_path is not None:
        if partition_path.is_absolute():
            raise FrameworkError(
                "package partition repeat must be a repository-relative path")
        candidate_source = relative_path(
            partition_path.as_posix(), "package partition repeat")
        if candidate_source != layout_ref["source"]:
            raise FrameworkError(
                "package partition override differs from the resolved product layout")
    layout = _load_product_partition_layout(
        repository, layout_ref, "package product partition_layout")
    return layout, layout_ref["source"]


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
    inputs = plan["identity_inputs"]

    def role_path(role: str, filename: str) -> str:
        return f"{roles[role]['artifact_root_template']}/{filename}"

    rows: list[dict[str, Any]] = []
    for name, (kind, owner, mapped_roles) in PACK_ARTIFACTS.items():
        if len(mapped_roles) == 1:
            path_template = role_path(mapped_roles[0], name)
        else:
            role_set = ",".join(mapped_roles)
            path_template = (
                f"${{BUILD_ROOT}}/bk7258/{inputs['product']}/"
                f"{inputs['mode']}/{{{role_set}}}/artifacts/{name}"
            )
        rows.append({
            "name": name,
            "kind": kind,
            "owner": owner,
            "roles": list(mapped_roles),
            "path_template": path_template,
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
            "artifact": PACK_ROLE_ARTIFACTS[role],
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
    if apps["artifacts"] != ["libarch.a", "libboard.a",
                             "vela_nuttx_cp.bin", "vela_nuttx_ap.bin"]:
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
    _partition_layout_reference({
        "source": partition["source"],
        "layout_id": partition["layout_id"],
        "layout_sha256": partition["layout_sha256"],
    }, "package.partition layout identity")
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
        if row["artifact"] != PACK_ROLE_ARTIFACTS[role]:
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
                raise FrameworkError(f"package artifact mapping is wrong: {name}")
            if len(mapped_roles) == 1:
                expected_template = (
                    f"${{BUILD_ROOT}}/bk7258/{value['product']}/{value['mode']}/"
                    f"{mapped_roles[0]}/artifacts/{name}"
                )
            else:
                role_set = ",".join(mapped_roles)
                expected_template = (
                    f"${{BUILD_ROOT}}/bk7258/{value['product']}/{value['mode']}/"
                    f"{{{role_set}}}/artifacts/{name}"
                )
            if row["path_template"] != expected_template:
                raise FrameworkError(
                    f"package artifact path template is wrong: {name}")
    if names != expected_names:
        raise FrameworkError("package artifacts are incomplete")
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
                 partition_path: Path | None = None,
                 config_root: Path | None = None) -> dict[str, Any]:
    """Prepare a deterministic package manifest without bytes or signing."""
    if kind not in PACK_KINDS:
        raise FrameworkError("package kind must be application or factory")
    plan = build_plan(repository, product_id, board_id, mode,
                      config_root=config_root)
    inputs = plan["identity_inputs"]
    sdk_roles = dict(plan["sdk"]["roles"])
    layout, partition_source = _pack_partition_layout(
        repository, plan["partition_layout"], partition_path)
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
        "artifacts": ["libarch.a", "libboard.a",
                      "vela_nuttx_cp.bin", "vela_nuttx_ap.bin"],
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


def pack_verify(repository: Path | None, package: dict[str, Any] | Path,
                *, config_root: Path | None = None) -> dict[str, Any]:
    """Verify package metadata and optional current-repository bindings only."""
    value = load_json(package) if isinstance(package, Path) else package
    validate_bkpack(value)
    if repository is not None:
        plan = build_plan(repository, value["product"], value["board"]["id"],
                          value["mode"], config_root=config_root)
        if plan["identity_sha256"] != value["build_plan_identity_sha256"]:
            raise FrameworkError("package build plan identity differs from repository")
        if plan["sdk"]["lock_id"] != value["sdk_lock"]["id"] or \
                plan["sdk"]["lock_identity_sha256"] != value["sdk_lock"]["identity_sha256"]:
            raise FrameworkError("package SDK lock differs from repository")
        package_layout = {
            "source": value["partition"]["source"],
            "layout_id": value["partition"]["layout_id"],
            "layout_sha256": value["partition"]["layout_sha256"],
        }
        if package_layout != plan["partition_layout"]:
            raise FrameworkError("package partition layout differs from build plan")
        layout, _ = _pack_partition_layout(repository, plan["partition_layout"])
        if (layout.layout_id != value["partition"]["layout_id"] or
                layout.layout_sha256 != value["partition"]["layout_sha256"]):
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


def _framework_check_step(checks: list[dict[str, Any]], check_id: str,
                          detail: str, callback: Callable[[], Any]) -> None:
    callback()
    checks.append({"id": check_id, "status": "PASS", "detail": detail})


def framework_check(repository: Path) -> dict[str, Any]:
    """Run the bounded P1-P8 metadata/framework smoke contract."""
    root = repository.resolve()
    checks: list[dict[str, Any]] = []
    catalog = load_catalog(root)
    _framework_check_step(checks, "p1-catalog", "strict board/product/fragment catalogs",
                          lambda: None)
    for product in ("t5ai_core_bringup", "t5_board_bringup", "aidk_ai_toy_bringup"):
        for role in ("cp", "ap", "bl2"):
            resolve(root, product, role)
    _framework_check_step(checks, "p1-resolve", "T5/T5-Board/AIDK role resolution", lambda: None)
    registry_path = root / "tools/bk7258/bk7258_sdk_registry.json"
    set_path = root / "tools/bk7258/bk7258_sdk_set.json"
    lock_path = root / "tools/bk7258/bk7258_sdk_lock.json"
    registry = validate_sdk_registry(root, load_json(registry_path))
    sdk_set = validate_sdk_set(load_json(set_path), registry)
    validate_sdk_lock(root, registry_path, set_path, load_json(lock_path), registry, sdk_set)
    _framework_check_step(checks, "p2-sdk-metadata", "SDK registry/set/lock invariants", lambda: None)
    plan = build_plan(root, "t5ai_core_bringup")
    validate_build_plan(plan)
    for role in ("cp", "ap"):
        validate_config_document(config_document(
            resolve(root, "t5ai_core_bringup", role), repository=root))
    _framework_check_step(
        checks, "p3-build-plans",
        "T5AI seed plan; T5-Board/AIDK plans require final .config inputs",
        lambda: None)
    from bk7258_resource_graph import (  # noqa: PLC0415
        validate_migration_ledger, validate_ownership_manifest, validate_resource_graph,
    )
    validate_ownership_manifest(root, load_json(root / "tools/bk7258/bk7258_layer_ownership.json"))
    validate_migration_ledger(root, load_json(root / "tools/bk7258/bk7258_compatibility_migration_ledger.json"))
    _framework_check_step(checks, "p4-ownership-migration", "ownership and migration metadata", lambda: None)
    for board in ("t5ai_core", "t5_board", "aidk_ai_toy"):
        graph = load_json(root / "tools/bk7258" / f"bk7258_resource_graph_{board}.json")
        validate_resource_graph(root, graph)
    _framework_check_step(checks, "p4-resource-graphs", "T5/T5-Board/AIDK resource graph schemas", lambda: None)
    from bk7258_validation import validate_descriptor_set  # noqa: PLC0415
    validate_descriptor_set(root, load_json(root / "tools/bk7258/bk7258_validation_descriptors.json"))
    _framework_check_step(
        checks, "p5-validation",
        "validation descriptor and command registry invariants",
        lambda: None)
    pack_prepare(root, "t5ai_core_bringup")
    _framework_check_step(
        checks, "p6-package-plan",
        "T5AI metadata-only package plan; T5-Board/AIDK require final .config",
        lambda: None)
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


def _build_cli_parsers() -> argparse.ArgumentParser:
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
    verify_config_parser = commands.add_parser("verify-config")
    verify_config_parser.add_argument("--product", required=True)
    verify_config_parser.add_argument("--role", required=True,
                                      choices=tuple(sorted(ROLES)))
    verify_config_parser.add_argument("--config", type=Path, required=True)
    verify_config_parser.add_argument("--layout-id")
    verify_config_parser.add_argument("--sdk-version")
    plan_parser = commands.add_parser("build-plan", aliases=("plan",))
    plan_parser.add_argument("--product", required=True)
    plan_parser.add_argument("--board")
    plan_parser.add_argument("--mode")
    plan_parser.add_argument("--set", dest="sdk_set", type=Path)
    plan_parser.add_argument("--lock", type=Path)
    plan_parser.add_argument("--config-root", type=Path)
    plan_parser.add_argument("--out", type=Path, required=True)
    plan_verify_parser = commands.add_parser("build-plan-verify")
    plan_verify_parser.add_argument("--plan", type=Path, required=True)
    plan_verify_parser.add_argument("--product")
    plan_verify_parser.add_argument("--config-root", type=Path)
    execute_parser = commands.add_parser("execute", allow_abbrev=False)
    execute_parser.add_argument("--product", required=True)
    execute_parser.add_argument("--board")
    execute_parser.add_argument("--mode")
    execute_parser.add_argument("--set", dest="sdk_set", type=Path)
    execute_parser.add_argument("--lock", type=Path)
    execute_parser.add_argument("--config-root", type=Path)
    execute_parser.add_argument("--build-root", default="${BUILD_ROOT}")
    execute_parser.add_argument("--output", default="${OUTPUT}")
    execute_parser.add_argument("--dry-run", action="store_true")
    execute_parser.add_argument("--out", type=Path, required=True)
    isolated_prepare_parser = commands.add_parser(
        "execute-prepare", allow_abbrev=False,
        help="prepare the canonical role-isolated four-role build contract")
    isolated_prepare_parser.add_argument("--product", required=True)
    isolated_prepare_parser.add_argument("--build-root", type=Path, required=True)
    isolated_prepare_parser.add_argument("--workspace-root", type=Path)
    isolated_prepare_parser.add_argument("--plan", type=Path)
    isolated_prepare_parser.add_argument("--config-root", type=Path)
    isolated_prepare_parser.add_argument("--out", type=Path, required=True)
    isolated_materialize_parser = commands.add_parser(
        "execute-materialize-sources", allow_abbrev=False,
        help="materialize and audit the one entity source snapshot")
    isolated_materialize_parser.add_argument("--manifest", type=Path, required=True)
    isolated_materialize_parser.add_argument("--workspace-root", type=Path)
    isolated_runtime_parser = commands.add_parser(
        "execute-compile-runtime", aliases=("execute-build-runtime",),
        allow_abbrev=False,
        help="compile isolated CP/AP runtime targets from a materialized manifest")
    isolated_runtime_parser.add_argument("--manifest", type=Path, required=True)
    isolated_runtime_parser.add_argument(
        "--authorize-compile", action="store_true",
        help="explicitly authorize the policy-gated runtime compile phase")
    isolated_runtime_parser.add_argument("--cmake", "--cmake-executable",
                                         dest="cmake_executable", default="cmake")
    isolated_runtime_parser.add_argument("--python", "--python-executable",
                                         dest="python_executable",
                                         default=str(Path(sys.executable).resolve()))
    isolated_runtime_parser.add_argument("--olddefconfig", "--olddefconfig-executable",
                                         dest="olddefconfig_executable",
                                         default="olddefconfig")
    isolated_runtime_parser.add_argument("--kconfiglib-root", type=Path)
    isolated_runtime_parser.add_argument("--make", "--make-executable",
                                         dest="make_executable", default="make")
    pack_prepare_parser = commands.add_parser("pack-prepare")
    pack_prepare_parser.add_argument("--product", required=True)
    pack_prepare_parser.add_argument("--kind", choices=tuple(sorted(PACK_KINDS)),
                                     default="application")
    pack_prepare_parser.add_argument("--board")
    pack_prepare_parser.add_argument("--mode")
    pack_prepare_parser.add_argument(
        "--partition", type=Path,
        help="optional repeat of the product-pinned partition CSV; alternate layouts are rejected")
    pack_prepare_parser.add_argument("--config-root", type=Path)
    pack_prepare_parser.add_argument("--out", type=Path, required=True)
    pack_verify_parser = commands.add_parser("pack-verify")
    pack_verify_parser.add_argument("--package", type=Path, required=True)
    pack_verify_parser.add_argument("--config-root", type=Path)
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
    suite_check_parser = commands.add_parser("validation-suite-check")
    suite_check_parser.add_argument("--product", required=True)
    suite_check_parser.add_argument("--suite", required=True)
    framework_check_parser = commands.add_parser(
        "framework-check", aliases=("check-framework",))
    framework_check_parser.add_argument("--out", type=Path)
    commands.add_parser("validate")
    return parser

def _run_cli_command(args: argparse.Namespace, root: Path) -> int:
    try:
        if args.command == "validate":
            catalog = load_catalog(root)
            print(f"bk7258-framework: OK boards={len(catalog['boards'])} products={len(catalog['products'])}")
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
            document = config_document(ir, repository=root)
            write_json(args.out, document)
            if args.defconfig_out is not None:
                args.defconfig_out.parent.mkdir(parents=True, exist_ok=True)
                args.defconfig_out.write_text(document["defconfig"], encoding="utf-8")
        elif args.command == "verify-config":
            config_path = args.config
            if not config_path.is_absolute():
                config_path = root / config_path
            result = verify_final_config(
                root, args.product, args.role, config_path,
                expected_layout_id=args.layout_id,
                expected_sdk_version=args.sdk_version)
            print("bk7258-framework: VERIFY-CONFIG PASS "
                  f"product={result['product']} role={result['role']} "
                  f"config_sha256={result['config_sha256']}")
        elif args.command in {"build-plan", "plan"}:
            plan = build_plan(root, args.product, args.board, args.mode,
                              args.sdk_set, args.lock,
                              config_root=args.config_root)
            write_json(args.out, plan)
        elif args.command == "build-plan-verify":
            plan = build_plan_verify(root, args.plan.resolve(), args.product,
                                     config_root=args.config_root)
            print("bk7258-framework: BUILD PLAN VERIFY PASS "
                  f"product={plan['identity_inputs']['product']} "
                  f"identity={plan['identity_sha256']}")
        elif args.command == "execute":
            context = execute(
                root, args.product, dry_run=True,
                build_root=args.build_root, output=args.output,
                board_id=args.board, mode=args.mode,
                set_path=args.sdk_set, lock_path=args.lock,
                config_root=args.config_root)
            write_json(args.out, context)
        elif args.command == "execute-prepare":
            isolated_prepare(root, args.product, args.build_root, args.out,
                             plan_path=args.plan,
                             workspace_root=args.workspace_root,
                             config_root=args.config_root)
        elif args.command == "execute-materialize-sources":
            manifest = isolated_materialize_sources(
                root, args.manifest, workspace_root=args.workspace_root)
            print("bk7258-framework: MATERIALIZE-SOURCES PASS "
                  f"identity={manifest['identity_sha256']} "
                  f"snapshot={manifest['source_view']['snapshot_identity_sha256']}")
        elif args.command in {"execute-compile-runtime", "execute-build-runtime"}:
            manifest = isolated_compile_runtime(
                root, args.manifest,
                authorize_compile=args.authorize_compile,
                cmake_executable=args.cmake_executable,
                python_executable=args.python_executable,
                olddefconfig_executable=args.olddefconfig_executable,
                kconfiglib_root=args.kconfiglib_root,
                make_executable=args.make_executable)
            print("bk7258-framework: COMPILE-RUNTIME PASS "
                  f"identity={manifest['identity_sha256']}")
        elif args.command == "pack-prepare":
            package = pack_prepare(root, args.product, args.kind, args.board, args.mode,
                                   args.partition, config_root=args.config_root)
            write_json(args.out, package)
        elif args.command == "pack-verify":
            result = pack_verify(root, args.package.resolve(),
                                 config_root=args.config_root)
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
                             root / "tools/bk7258/bk7258_sdk_registry.json").resolve()
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
                             root / "tools/bk7258/bk7258_sdk_registry.json").resolve()
            set_path = (args.sdk_set or
                        root / "tools/bk7258/bk7258_sdk_set.json").resolve()
            lock_path = (args.lock or
                         root / "tools/bk7258/bk7258_sdk_lock.json").resolve()
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
                    _rooted(args.manifest, root / "tools/bk7258/bk7258_layer_ownership.json"),
                    "layer ownership manifest")
                validate_ownership_manifest(root, manifest)
                print("bk7258-framework: LAYER OWNERSHIP PASS")
            elif args.command == "migration-check":
                ledger = load_json_checked(
                    _rooted(args.ledger, root / "tools/bk7258/bk7258_compatibility_migration_ledger.json"),
                    "compatibility migration ledger")
                validate_migration_ledger(root, ledger)
                print("bk7258-framework: MIGRATION LEDGER PASS")
            else:
                graph = load_json_checked(
                    _rooted(args.graph, root / "tools/bk7258/bk7258_resource_graph_t5ai_core.json"),
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
                               root / "tools/bk7258/bk7258_validation_descriptors.json")
            if not descriptor_path.is_absolute():
                descriptor_path = root / descriptor_path
            descriptor_set = load_json_checked(descriptor_path, "validation descriptors")
            result = validate_descriptor_set(root, descriptor_set)
            print("bk7258-framework: VALIDATION PASS "
                  f"descriptors={result['descriptors']}")
        elif args.command == "validation-suite-check":
            result = validation_suite_check(root, args.product, args.suite)
            print("bk7258-framework: VALIDATION SUITE PASS "
                  f"product={result['product']} suite={result['suite']} "
                  f"catalog={result['catalog_identity_sha256']}")
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

def cli(argv: list[str] | None = None) -> int:
    parser = _build_cli_parsers()
    args = parser.parse_args(argv)
    root = args.root.resolve()
    return _run_cli_command(args, root)


if __name__ == "__main__":
    raise SystemExit(cli())
