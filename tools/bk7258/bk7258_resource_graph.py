#!/usr/bin/env python3
"""Host-only BK7258 ownership and resource-graph checks.

The manifests in this module are migration metadata.  They describe current
paths and ownership boundaries without creating a new source-layer directory
or changing a production driver/build path.
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
    digest,
    exact,
    identifier,
    identifiers,
    load_json,
    obj,
    relative_path,
    sha256,
    symbols,
)
from bk7258_framework import build_plan


OWNERSHIP_SCHEMA = "bk7258.ownership/2"
LEDGER_SCHEMA = "bk7258.compatibility-ledger/1"
GRAPH_SCHEMA = "bk7258.resource-graph/1"
RESOLVED_GRAPH_SCHEMA = "bk7258.resolved-resource-graph/1"
FORMAL_LAYERS = ("architecture", "chip", "board")
RESPONSIBILITY_TAGS = ("vendor_common_glue", "build_adapter",
                       "external_upstream_needed", "migration_pending")
# Keep the short name for callers that used the P4 checker internals; it now
# means formal layers only and deliberately excludes responsibility tags.
LAYERS = FORMAL_LAYERS
OWNER_VALUES = set(FORMAL_LAYERS) | set(RESPONSIBILITY_TAGS)
FORBIDDEN_NEW_PATH_PARTS = {"architecture", "platform", "services", "drivers",
                            "validation", "products"}
ROLES = ("bl1", "bl2", "cp", "ap")
PHASES = ("download", "boot", "hold", "runtime", "suspend", "restart")
RESOURCE_CATEGORIES = (
    "pins_functions",
    "devpaths_minors",
    "irq_dma_clock_power",
    "sdk_singletons",
    "mailbox",
    "memory_psram",
    "bom",
)


def _regular(path: Path, field: str) -> None:
    try:
        mode = path.lstat().st_mode
    except OSError as error:
        raise FrameworkError(f"missing {field}: {path}") from error
    if stat.S_ISLNK(mode) or not stat.S_ISREG(mode):
        raise FrameworkError(f"{field} must be regular and non-symlink: {path}")


def _identity(value: dict[str, Any], field: str) -> dict[str, Any]:
    digest(value["identity_sha256"], f"{field} identity")
    body = dict(value)
    del body["identity_sha256"]
    if sha256(canonical_json(body)) != value["identity_sha256"]:
        raise FrameworkError(f"{field} identity mismatch")
    return value


def _phases(value: Any, field: str) -> list[str]:
    phases = identifiers(value, field)
    if any(phase not in PHASES for phase in phases):
        raise FrameworkError(f"unsupported phase in {field}")
    return phases


def _content_id(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value.startswith("sha256:"):
        raise FrameworkError(f"content id is malformed in {field}")
    digest(value[7:], field)
    return value


def _existing_paths(repository: Path, value: Any, field: str,
                    allow_empty: bool = False) -> list[str]:
    paths = array(value, field)
    if not paths and not allow_empty:
        raise FrameworkError(f"{field} must not be empty")
    result: list[str] = []
    for raw in paths:
        path = relative_path(raw, f"{field}[]")
        if any(part in FORBIDDEN_NEW_PATH_PARTS for part in path.split("/")):
            raise FrameworkError(f"{field} invents a formal layer path: {path}")
        target = repository / path
        try:
            mode = target.lstat().st_mode
        except OSError as error:
            raise FrameworkError(f"{field} path is absent: {path}") from error
        if stat.S_ISLNK(mode) or not (stat.S_ISDIR(mode) or stat.S_ISREG(mode)):
            raise FrameworkError(f"{field} path is unsafe: {path}")
        result.append(path)
    if len(result) != len(set(result)):
        raise FrameworkError(f"duplicate path in {field}")
    return result


def validate_ownership_manifest(repository: Path, value: dict[str, Any]) -> dict[str, Any]:
    exact(value, {"schema", "kind", "version", "layers", "responsibility_tags",
                  "dependency_rules", "init_phases", "build_contract",
                  "config_policy", "identity_sha256"}, "architecture ownership manifest")
    if (value["schema"] != OWNERSHIP_SCHEMA or
            value["kind"] != "architecture-ownership" or value["version"] != 2):
        raise FrameworkError("unsupported ownership manifest schema")
    layers = value["layers"]
    if not isinstance(layers, dict) or set(layers) != set(LAYERS):
        raise FrameworkError("formal ownership layers are not exact")
    expected_parent = {"architecture": None, "chip": "architecture", "board": "chip"}
    expected_source = {"architecture": "upstream_nuttx",
                       "chip": "existing_board_tree",
                       "board": "existing_board_tree"}
    for layer in LAYERS:
        row = layers[layer]
        if not isinstance(row, dict):
            raise FrameworkError(f"formal layer is not an object: {layer}")
        exact(row, {"owner", "parent", "source", "current_paths",
                    "allowed_dependencies", "forbidden_dependencies", "status",
                    "scope"}, f"formal layer {layer}")
        if (row["owner"] != layer or row["parent"] != expected_parent[layer] or
                row["source"] != expected_source[layer]):
            raise FrameworkError(f"formal layer hierarchy/source mismatch: {layer}")
        expected_status = ("external_upstream_needed" if layer == "architecture"
                           else "transitional-ledger-only")
        if row["status"] != expected_status or not isinstance(row["scope"], str) or not row["scope"]:
            raise FrameworkError(f"formal layer status/scope mismatch: {layer}")
        _existing_paths(repository, row["current_paths"],
                        f"formal layer {layer}.current_paths", layer == "architecture")
        identifiers(row["allowed_dependencies"], f"ownership layer {layer}.allowed_dependencies")
        forbidden = identifiers(row["forbidden_dependencies"], f"ownership layer {layer}.forbidden_dependencies")
        if layer in row["allowed_dependencies"] or layer in forbidden:
            raise FrameworkError(f"ownership layer self-dependency is invalid: {layer}")
    tags = value["responsibility_tags"]
    if not isinstance(tags, dict) or set(tags) != set(RESPONSIBILITY_TAGS):
        raise FrameworkError("ownership responsibility tags are not exact")
    for tag in RESPONSIBILITY_TAGS:
        row = tags[tag]
        if not isinstance(row, dict):
            raise FrameworkError(f"responsibility tag is not an object: {tag}")
        exact(row, {"formal_layers", "current_paths", "target_paths", "status",
                    "scope", "notes"}, f"responsibility tag {tag}")
        formal_layers = identifiers(row["formal_layers"], f"responsibility tag {tag}.formal_layers")
        if not formal_layers or any(item not in LAYERS for item in formal_layers):
            raise FrameworkError(f"responsibility tag names a non-formal layer: {tag}")
        _existing_paths(repository, row["current_paths"],
                        f"responsibility tag {tag}.current_paths",
                        tag == "external_upstream_needed")
        _existing_paths(repository, row["target_paths"],
                        f"responsibility tag {tag}.target_paths",
                        tag == "external_upstream_needed")
        allowed_status = {
            "vendor_common_glue": "migration_pending",
            "build_adapter": "adapter_only",
            "external_upstream_needed": "record_only",
            "migration_pending": "migration_pending",
        }[tag]
        if row["status"] != allowed_status or not isinstance(row["scope"], str) or not row["scope"]:
            raise FrameworkError(f"responsibility tag status/scope mismatch: {tag}")
        if not isinstance(row["notes"], str) or not row["notes"]:
            raise FrameworkError(f"responsibility tag notes are missing: {tag}")
    rules = value["dependency_rules"]
    if not isinstance(rules, list) or not rules:
        raise FrameworkError("ownership dependency rules are missing")
    for index, raw in enumerate(rules):
        rule = raw if isinstance(raw, dict) else None
        if rule is None:
            raise FrameworkError(f"ownership dependency rule {index} is malformed")
        exact(rule, {"from", "to", "allowed", "reason"}, f"ownership dependency rule {index}")
        if rule["from"] not in LAYERS or rule["to"] not in LAYERS:
            raise FrameworkError(f"ownership dependency rule {index} names unknown layer")
        if not isinstance(rule["allowed"], bool) or not isinstance(rule["reason"], str) or not rule["reason"]:
            raise FrameworkError(f"ownership dependency rule {index} is malformed")
        source = layers[rule["from"]]
        if rule["allowed"]:
            if rule["to"] not in source["allowed_dependencies"] or rule["to"] in source["forbidden_dependencies"]:
                raise FrameworkError(f"allowed ownership dependency is not declared: {rule['from']}->{rule['to']}")
        elif rule["to"] not in source["forbidden_dependencies"]:
            raise FrameworkError(f"forbidden ownership dependency is not declared: {rule['from']}->{rule['to']}")
    init_phases = array(value["init_phases"], "ownership init_phases")
    if any(not isinstance(row, dict) for row in init_phases):
        raise FrameworkError("ownership init phase is malformed")
    if [row.get("name") for row in init_phases] != [
            "board_early_initialize", "board_late_initialize",
            "board_app_initialize", "board_app_finalinitialize"]:
        raise FrameworkError("official board init phase order is incomplete")
    for index, raw in enumerate(init_phases):
        row = raw if isinstance(raw, dict) else None
        if row is None:
            raise FrameworkError(f"init phase {index} is malformed")
        exact(row, {"name", "owner", "status", "current_paths", "notes"},
              f"init phase {index}")
        identifier(row["name"], f"init phase {index}.name")
        if row["owner"] != "board" or row["status"] not in {"defined", "migration_pending"}:
            raise FrameworkError(f"init phase {index} ownership/status is invalid")
        _existing_paths(repository, row["current_paths"], f"init phase {index}.current_paths")
        if not isinstance(row["notes"], str) or not row["notes"]:
            raise FrameworkError(f"init phase {index}.notes is missing")
    contract = value["build_contract"]
    if not isinstance(contract, dict):
        raise FrameworkError("build contract is malformed")
    exact(contract, {"adapters", "artifacts", "extension_policy"}, "build contract")
    adapters = array(contract["adapters"], "build contract adapters")
    if any(not isinstance(row, dict) for row in adapters):
        raise FrameworkError("build adapter is malformed")
    if [row.get("name") for row in adapters] != ["cmake", "classic"]:
        raise FrameworkError("build adapter order is not canonical")
    for index, raw in enumerate(adapters):
        row = raw if isinstance(raw, dict) else None
        if row is None:
            raise FrameworkError(f"build adapter {index} is malformed")
        exact(row, {"name", "status", "semantics", "generates", "invokes"},
              f"build adapter {index}")
        if row["name"] == "cmake":
            expected = ("recommended_adapter", "existing_cmake")
        else:
            expected = ("compatibility_adapter", "existing_make")
        if (row["status"], row["semantics"]) != expected:
            raise FrameworkError(f"build adapter {index} changes legacy semantics")
        identifiers(row["generates"], f"build adapter {index}.generates")
        identifiers(row["invokes"], f"build adapter {index}.invokes")
    artifacts = array(contract["artifacts"], "build contract artifacts")
    if any(not isinstance(row, dict) for row in artifacts):
        raise FrameworkError("build artifact is malformed")
    expected_artifacts = {
        "libarch.a", "libboards.a", "libboard.a", "vela_*.bin", ".bkpack"
    }
    if {row.get("name") for row in artifacts} != expected_artifacts:
        raise FrameworkError("build artifact mapping is incomplete")
    for index, raw in enumerate(artifacts):
        row = raw if isinstance(raw, dict) else None
        if row is None:
            raise FrameworkError(f"build artifact {index} is malformed")
        exact(row, {"name", "owner", "kind", "status"}, f"build artifact {index}")
        if row["owner"] not in LAYERS or not isinstance(row["kind"], str) or not row["kind"]:
            raise FrameworkError(f"build artifact {index} ownership is invalid")
        if row["name"] == ".bkpack":
            if row["status"] != "later_vendor_extension":
                raise FrameworkError(".bkpack must remain a later vendor extension")
        elif row["name"] == "libboards.a":
            if row["status"] != "classic_backend_internal":
                raise FrameworkError(
                    "libboards.a must remain a Classic-backend internal archive")
        elif row["status"] != "required":
            raise FrameworkError(f"required artifact status is invalid: {row['name']}")
    if contract["extension_policy"] != "vendor extensions are additive and not replacement":
        raise FrameworkError("vendor artifact extension policy is unsafe")
    config = value["config_policy"]
    if not isinstance(config, dict):
        raise FrameworkError("config policy is malformed")
    exact(config, {"seed_policy", "legacy_profiles", "new_seed_requires", "validation"},
          "config policy")
    if (config["seed_policy"] != "few_reviewed_modes" or
            config["legacy_profiles"] != "frozen_compatibility_surface" or
            config["validation"] != "not_a_formal_layer" or
            not isinstance(config["new_seed_requires"], str) or not config["new_seed_requires"]):
        raise FrameworkError("config policy is not aligned with the adaptation guide")
    return _identity(value, "architecture ownership manifest")


def validate_migration_ledger(repository: Path, value: dict[str, Any]) -> dict[str, Any]:
    exact(value, {"schema", "kind", "version", "status", "source_root",
                  "rows", "identity_sha256"}, "compatibility migration ledger")
    if value["schema"] != LEDGER_SCHEMA or value["kind"] != "compatibility-migration-ledger" or value["version"] != 1:
        raise FrameworkError("unsupported compatibility ledger schema")
    if value["status"] != "inventory-only" or value["source_root"] != "board/bk7258":
        raise FrameworkError("compatibility ledger is not inventory-only")
    rows = array(value["rows"], "compatibility ledger rows")
    if not rows:
        raise FrameworkError("compatibility ledger has no rows")
    seen: set[str] = set()
    for index, raw in enumerate(rows):
        row = raw if isinstance(raw, dict) else None
        if row is None:
            raise FrameworkError(f"compatibility row {index} is malformed")
        exact(row, {"path", "symbols", "current_owner", "target_owner", "status", "notes"},
              f"compatibility row {index}")
        path = relative_path(row["path"], f"compatibility row {index}.path")
        if path in seen:
            raise FrameworkError(f"duplicate compatibility path: {path}")
        seen.add(path)
        if not path.startswith("board/bk7258/") or any(part in FORBIDDEN_NEW_PATH_PARTS for part in path.split("/")):
            raise FrameworkError(f"compatibility row points at a new layer path: {path}")
        _regular(repository / path, f"compatibility row {index}.path")
        symbols(row["symbols"], f"compatibility row {index}.symbols")
        if row["current_owner"] not in OWNER_VALUES or row["target_owner"] not in LAYERS:
            raise FrameworkError(f"compatibility row {index} owner is unknown")
        if row["status"] != "inventory-only" or not isinstance(row["notes"], str) or not row["notes"]:
            raise FrameworkError(f"compatibility row {index} is not inventory-only")
    return _identity(value, "compatibility migration ledger")


def _resource_row(value: Any, index: int, category: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise FrameworkError(f"resource {category}[{index}] is malformed")
    if "id" not in value:
        raise FrameworkError(f"resource {category}[{index}] has no id")
    identifier(value["id"], f"resource {category}[{index}].id")
    return value


def _validate_aidk_board_constraints(value: Any) -> None:
    constraints = obj(value, "AIDK board constraints")
    exact(constraints, {"console", "debug", "port_identity", "conflicts", "unsupported"},
          "AIDK board constraints")
    console = obj(constraints["console"], "AIDK console")
    exact(console, {"uart", "baud", "data_bits", "parity", "stop_bits",
                    "flow_control", "rts_reset"}, "AIDK console")
    if (console["uart"] != "uart0" or console["baud"] != 115200 or
            console["data_bits"] != 8 or console["parity"] != "none" or
            console["stop_bits"] != 1 or console["flow_control"] is not False or
            console["rts_reset"] is not False):
        raise FrameworkError("AIDK console binding is not the fixed UART0 115200 8N1 contract")
    debug = obj(constraints["debug"], "AIDK debug")
    exact(debug, {"swd", "boot_hold", "rtt"}, "AIDK debug")
    if any(debug[key] is not False for key in ("swd", "boot_hold", "rtt")):
        raise FrameworkError("AIDK SWD/boot-hold/RTT must remain disabled")
    port = obj(constraints["port_identity"], "AIDK port identity")
    exact(port, {"board_id", "port", "persistent"}, "AIDK port identity")
    if (port["board_id"] != "aidk_ai_toy" or port["port"] != "dynamic-usb-serial" or
            port["persistent"] is not False):
        raise FrameworkError("AIDK port identity must remain dynamic and non-persistent")
    conflicts = array(constraints["conflicts"], "AIDK board conflicts")
    expected = {
        "p20_p21_sc7a20_swd", "p0_p1_mfrc522_cn1", "p8_p9_32k_key3_motor", "usb0_unknown"
    }
    seen: set[str] = set()
    for index, raw in enumerate(conflicts):
        row = obj(raw, f"AIDK conflict[{index}]")
        exact(row, {"id", "pins", "functions", "status", "policy", "notes"},
              f"AIDK conflict[{index}]")
        conflict_id = identifier(row["id"], f"AIDK conflict[{index}].id")
        if conflict_id in seen or conflict_id not in expected:
            raise FrameworkError("AIDK conflict coverage is duplicate or incomplete")
        seen.add(conflict_id)
        identifiers(row["pins"], f"AIDK conflict[{index}].pins")
        identifiers(row["functions"], f"AIDK conflict[{index}].functions")
        if row["status"] != "unknown" or row["policy"] != "do-not-claim" or \
                not isinstance(row["notes"], str) or not row["notes"]:
            raise FrameworkError(f"AIDK conflict policy is unsafe: {conflict_id}")
    if seen != expected:
        raise FrameworkError("AIDK conflict coverage is incomplete")
    unsupported = identifiers(constraints["unsupported"], "AIDK unsupported BOM")
    if unsupported != ["sd_nand", "lcd", "camera", "mfrc522", "sc7a20", "usb0"]:
        raise FrameworkError("AIDK unsupported BOM list is not canonical")


def validate_resource_graph(repository: Path, value: dict[str, Any]) -> dict[str, Any]:
    graph_keys = {"schema", "kind", "version", "board", "product", "mode",
                  "board_selection", "phases", "roles", "nodes", "edges",
                  "resources", "temporal_handoff", "identity_sha256"}
    if value.get("board") == "aidk_ai_toy":
        graph_keys.add("board_constraints")
    exact(value, graph_keys, "resource graph")
    if value["schema"] != GRAPH_SCHEMA or value["kind"] != "resource-graph" or value["version"] != 1:
        raise FrameworkError("unsupported resource graph schema")
    for field in ("board", "product", "mode"):
        identifier(value[field], f"resource graph.{field}")
    if value["mode"] not in {"bringup", "application", "validation", "factory"}:
        raise FrameworkError("resource graph mode is unsupported")
    if value["phases"] != list(PHASES) or value["roles"] != list(ROLES):
        raise FrameworkError("resource graph phase/role order is not canonical")
    selection = value["board_selection"]
    if not isinstance(selection, dict):
        raise FrameworkError("resource graph board selection is malformed")
    exact(selection, {"selected", "candidates", "exactly_one", "fallback"}, "resource graph board selection")
    candidates = identifiers(selection["candidates"], "resource graph board candidates")
    if (selection["selected"] != value["board"] or candidates != [value["board"]] or
            selection["exactly_one"] is not True or selection["fallback"] != "forbidden"):
        raise FrameworkError("resource graph board selection is not exactly-one/fail-closed")
    if value["board"] == "aidk_ai_toy":
        _validate_aidk_board_constraints(value["board_constraints"])
    nodes = array(value["nodes"], "resource graph nodes")
    if len(nodes) != len(ROLES):
        raise FrameworkError("resource graph must contain exactly BL1/BL2/CP/AP nodes")
    node_ids: set[str] = set()
    provides: set[str] = set()
    requires: list[str] = []
    for index, raw in enumerate(nodes):
        node = raw if isinstance(raw, dict) else None
        if node is None:
            raise FrameworkError(f"resource graph node {index} is malformed")
        exact(node, {"id", "role", "sdk", "provides", "requires"}, f"resource graph node {index}")
        node_id = identifier(node["id"], f"resource graph node {index}.id")
        if node_id not in ROLES or node_id in node_ids or node["role"] != node_id:
            raise FrameworkError(f"resource graph node role/id mismatch: {node_id}")
        node_ids.add(node_id)
        if node["sdk"] is not None:
            _content_id(node["sdk"], f"resource graph node {node_id}.sdk")
        node_provides = identifiers(node["provides"], f"resource graph node {node_id}.provides")
        node_requires = identifiers(node["requires"], f"resource graph node {node_id}.requires")
        provides.update(node_provides)
        requires.extend(node_requires)
    if node_ids != set(ROLES):
        raise FrameworkError("resource graph node coverage is incomplete")
    edges = array(value["edges"], "resource graph edges")
    edge_ids: set[str] = set()
    for index, raw in enumerate(edges):
        edge = raw if isinstance(raw, dict) else None
        if edge is None:
            raise FrameworkError(f"resource graph edge {index} is malformed")
        exact(edge, {"id", "from", "to", "phase", "requires", "provides"}, f"resource graph edge {index}")
        edge_id = identifier(edge["id"], f"resource graph edge {index}.id")
        if edge_id in edge_ids or edge["from"] not in ROLES or edge["to"] not in ROLES or edge["phase"] not in PHASES:
            raise FrameworkError(f"resource graph edge {index} is invalid")
        edge_ids.add(edge_id)
        edge_provides = identifiers(edge["provides"], f"resource graph edge {edge_id}.provides")
        edge_requires = identifiers(edge["requires"], f"resource graph edge {edge_id}.requires")
        provides.update(edge_provides)
        requires.extend(edge_requires)
    if any(item not in provides for item in requires):
        missing = sorted({item for item in requires if item not in provides})
        raise FrameworkError(f"resource graph has unsatisfied provides/requires: {missing}")
    resources = value["resources"]
    if not isinstance(resources, dict) or set(resources) != set(RESOURCE_CATEGORIES):
        raise FrameworkError("resource graph resource categories are not exact")
    resource_ids: set[str] = set()
    pin_claims: list[tuple[str, set[str], str]] = []
    devpaths: set[str] = set()
    minors: set[int] = set()
    for category in RESOURCE_CATEGORIES:
        rows = array(resources[category], f"resource graph.{category}")
        for index, raw in enumerate(rows):
            row = _resource_row(raw, index, category)
            resource_id = row["id"]
            if resource_id in resource_ids:
                raise FrameworkError(f"duplicate resource id: {resource_id}")
            resource_ids.add(resource_id)
            if category == "pins_functions":
                exact(row, {"id", "pins", "function", "owner", "consumers", "phases", "status"}, f"pins_functions[{index}]")
                pins = identifiers(row["pins"], f"pins_functions[{index}].pins")
                consumers = identifiers(row["consumers"], f"pins_functions[{index}].consumers")
                if row["owner"] != "board" or not consumers or row["status"] not in {"fitted", "unknown"}:
                    raise FrameworkError(f"invalid pin/function ownership: {resource_id}")
                phases = set(_phases(row["phases"], f"pins_functions[{index}].phases"))
                for pin in pins:
                    pin_claims.append((pin, phases, resource_id))
            elif category == "devpaths_minors":
                exact(row, {"id", "devpath", "minor", "owner", "consumers", "phases", "status"}, f"devpaths_minors[{index}]")
                if (not isinstance(row["devpath"], str) or not row["devpath"].startswith("/dev/") or
                        not isinstance(row["minor"], int) or isinstance(row["minor"], bool) or row["minor"] < 0):
                    raise FrameworkError(f"invalid devpath/minor: {resource_id}")
                if row["devpath"] in devpaths or row["minor"] in minors:
                    raise FrameworkError(f"devpath/minor conflict: {resource_id}")
                devpaths.add(row["devpath"])
                minors.add(row["minor"])
                identifiers(row["consumers"], f"devpaths_minors[{index}].consumers")
                _phases(row["phases"], f"devpaths_minors[{index}].phases")
                if row["status"] not in {"fitted", "unknown"}:
                    raise FrameworkError(f"invalid devpath status: {resource_id}")
            elif category == "irq_dma_clock_power":
                exact(row, {"id", "kind", "resource", "owner", "max_instances", "phases", "status"}, f"irq_dma_clock_power[{index}]")
                if row["kind"] not in {"irq", "dma", "clock", "power"} or row["owner"] not in ROLES or row["max_instances"] != 1:
                    raise FrameworkError(f"singleton IRQ/DMA/clock/power policy is invalid: {resource_id}")
                identifier(row["resource"], f"irq_dma_clock_power[{index}].resource")
                _phases(row["phases"], f"irq_dma_clock_power[{index}].phases")
                if row["status"] not in {"fitted", "unknown"}:
                    raise FrameworkError(f"invalid IRQ/DMA/clock/power status: {resource_id}")
            elif category == "sdk_singletons":
                exact(row, {"id", "provider", "owner", "max_instances", "ownership", "phases"}, f"sdk_singletons[{index}]")
                if row["owner"] not in ROLES or row["max_instances"] != 1 or row["ownership"] == "shared":
                    raise FrameworkError(f"SDK singleton must remain one-instance/owned: {resource_id}")
                _content_id(row["provider"], f"sdk_singletons[{index}].provider")
                _phases(row["phases"], f"sdk_singletons[{index}].phases")
            elif category == "mailbox":
                exact(row, {"id", "provides", "requires", "owner", "consumers", "phases"}, f"mailbox[{index}]")
                if row["owner"] not in ROLES:
                    raise FrameworkError(f"mailbox owner is invalid: {resource_id}")
                identifiers(row["provides"], f"mailbox[{index}].provides")
                identifiers(row["requires"], f"mailbox[{index}].requires")
                identifiers(row["consumers"], f"mailbox[{index}].consumers")
                _phases(row["phases"], f"mailbox[{index}].phases")
            elif category == "memory_psram":
                exact(row, {"id", "kind", "region", "owner", "capacity", "max_instances", "phases", "status"}, f"memory_psram[{index}]")
                if row["kind"] not in {"memory", "psram"} or row["owner"] not in ROLES or row["max_instances"] != 1:
                    raise FrameworkError(f"memory/PSRAM ownership is invalid: {resource_id}")
                identifier(row["region"], f"memory_psram[{index}].region")
                if not isinstance(row["capacity"], str) or not row["capacity"]:
                    raise FrameworkError(f"memory/PSRAM capacity is missing: {resource_id}")
                _phases(row["phases"], f"memory_psram[{index}].phases")
                if row["status"] not in {"fitted", "unknown"}:
                    raise FrameworkError(f"invalid memory/PSRAM status: {resource_id}")
            else:
                exact(row, {"id", "component", "status", "owner", "phases"}, f"bom[{index}]")
                if row["status"] not in {"fitted", "unknown"} or row["owner"] != "board":
                    raise FrameworkError(f"invalid BOM status/owner: {resource_id}")
                _phases(row["phases"], f"bom[{index}].phases")
    for index, (pin, phases, resource_id) in enumerate(pin_claims):
        for other_pin, other_phases, other_id in pin_claims[index + 1:]:
            if pin == other_pin and phases & other_phases:
                raise FrameworkError(f"pin/function conflict: {pin} ({resource_id}, {other_id})")
    temporal = array(value["temporal_handoff"], "resource graph temporal_handoff")
    if not temporal:
        raise FrameworkError("resource graph temporal handoff is missing")
    temporal_ids: set[str] = set()
    for index, raw in enumerate(temporal):
        row = raw if isinstance(raw, dict) else None
        if row is None:
            raise FrameworkError(f"temporal handoff {index} is malformed")
        exact(row, {"id", "from", "to", "phase", "resource", "preconditions", "release"}, f"temporal handoff {index}")
        handoff_id = identifier(row["id"], f"temporal handoff {index}.id")
        if handoff_id in temporal_ids or row["from"] not in ROLES or row["to"] not in ROLES or row["phase"] not in PHASES:
            raise FrameworkError(f"temporal handoff {index} is invalid")
        temporal_ids.add(handoff_id)
        identifier(row["resource"], f"temporal handoff {index}.resource")
        identifiers(row["preconditions"], f"temporal handoff {index}.preconditions")
        if not isinstance(row["release"], str) or not row["release"]:
            raise FrameworkError(f"temporal handoff {index}.release is missing")
    return _identity(value, "resource graph")


def resolve_resource_graph(repository: Path, value: dict[str, Any],
                           *, config_root: Path | None = None) -> dict[str, Any]:
    validate_resource_graph(repository, value)
    plan = build_plan(repository, value["product"], value["board"], value["mode"],
                      config_root=config_root)
    if (plan["board"]["id"] != value["board"] or
            plan["identity_inputs"]["product"] != value["product"] or
            plan["identity_inputs"]["mode"] != value["mode"]):
        raise FrameworkError("resource graph does not resolve to exactly one plan board/product/mode")
    graph_sdk = value["resources"]["sdk_singletons"]
    plan_sdk = plan["sdk"]["roles"]
    for node in value["nodes"]:
        expected_sdk = plan_sdk.get(node["role"])
        if node["sdk"] != expected_sdk:
            raise FrameworkError(f"resource graph node SDK does not match the locked plan: {node['role']}")
    for row in graph_sdk:
        if row["provider"] not in {item for item in plan_sdk.values() if item is not None}:
            raise FrameworkError(f"resource graph SDK singleton is not in the locked plan: {row['id']}")
    body = dict(value)
    body["schema"] = RESOLVED_GRAPH_SCHEMA
    body["kind"] = "resolved-resource-graph"
    body["resolved_plan_identity_sha256"] = plan["identity_sha256"]
    body["resolved"] = True
    body.pop("identity_sha256", None)
    result = dict(body)
    result["identity_sha256"] = sha256(canonical_json(body))
    return result


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(canonical_json(value))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--ledger", type=Path)
    parser.add_argument("--graph", type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("command", choices=("ownership-check", "migration-check", "graph-check", "graph-resolve"))
    args = parser.parse_args(argv)
    root = args.root.resolve()
    try:
        if args.command == "ownership-check":
            path = args.manifest or root / "tools/bk7258/bk7258_layer_ownership.json"
            validate_ownership_manifest(root, load_json(path))
        elif args.command == "migration-check":
            path = args.ledger or root / "tools/bk7258/bk7258_compatibility_migration_ledger.json"
            validate_migration_ledger(root, load_json(path))
        else:
            path = args.graph or root / "tools/bk7258/bk7258_resource_graph_t5ai_core.json"
            graph = load_json(path)
            if args.command == "graph-check":
                validate_resource_graph(root, graph)
            else:
                if args.out is None:
                    parser.error("graph-resolve requires --out")
                write_json(args.out, resolve_resource_graph(root, graph))
        print(f"bk7258-resource-graph: {args.command} PASS")
        return 0
    except FrameworkError as error:
        print(f"bk7258-resource-graph: FAIL: {error}")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
