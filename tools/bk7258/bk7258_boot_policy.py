#!/usr/bin/env python3
"""Validate and resolve non-secret BK7258 product boot-policy metadata.

The policy documents are deliberately descriptive.  This module is the small
host-only boundary an isolated executor can use before it creates role
workspaces:

* :func:`load_policy` selects one exact product policy and validates it;
* :func:`resolve_policy` binds the policy to the product's resolved layout and
  derives BL2 primary/secondary geometry from that layout; and
* :func:`validate_all` checks the complete checked-in product policy set.

No function in this module reads a key, invokes a signer, invokes a build or
package tool, accesses hardware, or uses the network.  Credentials are
represented only by stable IDs and requirements.  A caller that wants to
perform a later side effect must obtain authorization separately and provide
the secret material through its own credential boundary.
"""

from __future__ import annotations


from bk7258_paths import Bk7258Layout, load_board_script

import argparse
import copy
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any, Mapping


SCRIPT_DIR = Path(__file__).resolve().parent
CONTEST_ROOT = Bk7258Layout().contest_root
POLICY_SCHEMA = "bk7258.boot-policy/1"
POLICY_KIND = "product-boot-policy"
BUILD_PLAN_SCHEMA = "bk7258.build-plan/1"
BUILD_PLAN_KIND = "isolated-build-plan"
RESOLVED_SCHEMA = "bk7258.resolved-boot-policy/1"
RESOLVED_KIND = "resolved-product-boot-policy"
POLICY_FILES = {
    "aidk_ai_toy_bringup": "bk7258_boot_policy_aidk_ai_toy_bringup.json",
    "t5_board_bringup": "bk7258_boot_policy_t5_board_bringup.json",
    "t5ai_core_bringup": "bk7258_boot_policy_t5ai_core_bringup.json",
}
PRODUCT_CATALOGS = {
    "aidk_ai_toy_bringup": "tools/bk7258/bk7258_product_catalog_aidk_ai_toy_bringup.json",
    "t5_board_bringup": "tools/bk7258/bk7258_product_catalog_t5_board_bringup.json",
    "t5ai_core_bringup": "tools/bk7258/bk7258_product_catalog_t5ai_core_bringup.json",
}
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
ID_RE = re.compile(r"^[a-z][a-z0-9_-]*$")
RELATIVE_RE = re.compile(r"^[^/\\\x00]+(?:/[^/\\\x00]+)*$")
KEY_PATH_RE = re.compile(r"(?:^|/)[^/]+\.(?:pem|key|der|p8|p12)$", re.IGNORECASE)
PERMISSIONS = {"allow", "forbidden", "requires-user-authorization"}
STATES = {"required", "not-applicable", "policy-default", "forbidden"}
ACTIVE_ROLES = ("bl1", "bl2", "cp", "ap")
ALL_ROLES = frozenset(ACTIVE_ROLES)
INTEGRATION_STATUS = "BLOCKED"
INTEGRATION_BLOCKED_UNTIL = "plan/profile-reconciliation"


class BootPolicyError(ValueError):
    """Raised for an invalid, ambiguous or unresolved boot policy."""


def _unique_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise BootPolicyError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def canonical_json(value: Any) -> bytes:
    """Return the policy identity encoding used by all metadata files."""

    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def sha256(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def _load_json(path: Path, field: str) -> dict[str, Any]:
    try:
        value = json.loads(
            path.read_text(encoding="utf-8"), object_pairs_hook=_unique_pairs
        )
    except BootPolicyError:
        raise
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise BootPolicyError(f"cannot load {field}: {path}") from error
    if not isinstance(value, dict):
        raise BootPolicyError(f"{field} must be a JSON object: {path}")
    return value


def load_json(path: Path) -> dict[str, Any]:
    """Public strict JSON loader used by lightweight tests and callers."""

    return _load_json(Path(path), "JSON document")


def _exact(value: Mapping[str, Any], keys: set[str], field: str) -> None:
    if set(value) != keys:
        missing = sorted(keys - set(value))
        unknown = sorted(set(value) - keys)
        detail = []
        if missing:
            detail.append(f"missing={missing}")
        if unknown:
            detail.append(f"unknown={unknown}")
        raise BootPolicyError(f"{field} keys are not exact ({', '.join(detail)})")


def _string(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value:
        raise BootPolicyError(f"{field} must be a non-empty string")
    return value


def _identifier(value: Any, field: str) -> str:
    result = _string(value, field)
    if not ID_RE.fullmatch(result):
        raise BootPolicyError(f"{field} is not a safe identifier: {result}")
    return result


def _relative_path(value: Any, field: str) -> str:
    result = _string(value, field)
    if (result.startswith("/") or ".." in result.split("/") or
            not RELATIVE_RE.fullmatch(result) or any(
                part in {"", ".", ".."} for part in result.split("/")
            )):
        raise BootPolicyError(f"{field} must be a safe repository-relative path")
    return result


def _boolean(value: Any, field: str) -> bool:
    if not isinstance(value, bool):
        raise BootPolicyError(f"{field} must be boolean")
    return value


def _integer(value: Any, field: str, *, allow_none: bool = False) -> int | None:
    if value is None and allow_none:
        return None
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise BootPolicyError(f"{field} must be a non-negative integer")
    return value


def _string_list(value: Any, field: str, *, allow_empty: bool = True) -> list[str]:
    if not isinstance(value, list) or (not allow_empty and not value):
        raise BootPolicyError(f"{field} must be a list of strings")
    result = []
    for index, item in enumerate(value):
        result.append(_string(item, f"{field}[{index}]"))
    if len(result) != len(set(result)):
        raise BootPolicyError(f"{field} contains duplicate values")
    return result


def _digest(value: Any, field: str) -> str:
    result = _string(value, field)
    if not SHA256_RE.fullmatch(result):
        raise BootPolicyError(f"{field} must be a lowercase SHA-256 digest")
    return result


def _policy_identity(value: Mapping[str, Any]) -> str:
    body = dict(value)
    body.pop("identity_sha256", None)
    return sha256(canonical_json(body))


def _validate_identity(value: Mapping[str, Any]) -> None:
    digest = _digest(value.get("identity_sha256"), "identity_sha256")
    if digest != _policy_identity(value):
        raise BootPolicyError("boot policy identity_sha256 does not match its content")


def _validate_no_key_paths(value: Any, field: str = "credentials") -> None:
    """Reject path/material fields in the credential subtree, fail-closed."""

    if isinstance(value, dict):
        for key, child in value.items():
            normalized = key.lower().replace("-", "_")
            if ("path" in normalized or "filename" in normalized or
                    normalized.endswith("_file") or normalized in {
                        "file", "key", "private_key", "key_material",
                        "secret", "secret_material",
                    }):
                raise BootPolicyError(f"{field} contains a forbidden key-path field: {key}")
            _validate_no_key_paths(child, f"{field}.{key}")
        return
    if isinstance(value, list):
        for index, child in enumerate(value):
            _validate_no_key_paths(child, f"{field}[{index}]")
        return
    if isinstance(value, str) and (KEY_PATH_RE.search(value) or value.startswith("/")):
        raise BootPolicyError(f"{field} contains key material path-like data")


def _validate_mechanism(value: Any, boot: str) -> None:
    if not isinstance(value, dict):
        raise BootPolicyError("mechanism must be an object")
    _exact(value, {"application_format", "mcuboot", "bl1", "bl2"}, "mechanism")
    if value["application_format"] != boot:
        raise BootPolicyError("mechanism.application_format disagrees with boot")
    mcuboot = value["mcuboot"]
    bl1 = value["bl1"]
    bl2 = value["bl2"]
    if not isinstance(mcuboot, dict) or not isinstance(bl1, dict) or not isinstance(bl2, dict):
        raise BootPolicyError("mechanism stage entries must be objects")
    _exact(mcuboot, {"enabled", "cp_ap_pair", "loader"}, "mechanism.mcuboot")
    _exact(bl1, {"enabled", "handoff"}, "mechanism.bl1")
    _exact(bl2, {"enabled", "loader", "role"}, "mechanism.bl2")
    for field in ("enabled", "cp_ap_pair"):
        _boolean(mcuboot[field], f"mechanism.mcuboot.{field}")
    _boolean(bl1["enabled"], "mechanism.bl1.enabled")
    _boolean(bl2["enabled"], "mechanism.bl2.enabled")
    if boot == "mcuboot":
        if (mcuboot["enabled"] is not True or mcuboot["cp_ap_pair"] is not True or
                mcuboot["loader"] != "bl2" or bl1["enabled"] is not True or
                bl1["handoff"] != "bl2" or bl2["enabled"] is not True or
                bl2["loader"] != "mcuboot" or bl2["role"] != "bl2"):
            raise BootPolicyError("MCUboot policy must explicitly use BL1 -> BL2 -> MCUboot")
    else:
        if (mcuboot["enabled"] is not False or mcuboot["cp_ap_pair"] is not False or
                mcuboot["loader"] != "none" or bl1["enabled"] is not True or
                bl1["handoff"] != "direct-image" or bl2["enabled"] is not False or
                bl2["loader"] != "not-used" or bl2["role"] != "not-applicable"):
            raise BootPolicyError("raw policy must fail closed without MCUboot/BL2")


def _validate_active_roles(value: Any, boot: str) -> list[str]:
    """Validate the roles which a future executor may actually activate.

    The framework build plan still describes the common BL1/BL2/CP/AP role
    set.  Raw products deliberately mark BL2 as not applicable here so a
    future executor can skip it instead of inferring applicability from the
    framework's superset of roles.
    """

    roles = _string_list(value, "active_roles", allow_empty=False)
    expected = list(ACTIVE_ROLES if boot == "mcuboot" else ("bl1", "cp", "ap"))
    if roles != expected:
        raise BootPolicyError("active_roles do not match the boot mechanism")
    return roles


def _validate_manifest(value: Any, boot: str) -> None:
    if not isinstance(value, dict):
        raise BootPolicyError("manifest must be an object")
    _exact(value, {"enforce", "format", "records", "raw_page", "source"}, "manifest")
    _boolean(value["enforce"], "manifest.enforce")
    _boolean(value["raw_page"], "manifest.raw_page")
    _string(value["source"], "manifest.source")
    if boot == "mcuboot":
        expected = (True, "beken-candidate-v1", "primary-secondary", False)
    else:
        expected = (False, "none", "none", False)
    observed = (value["enforce"], value["format"], value["records"], value["raw_page"])
    if observed != expected:
        raise BootPolicyError("manifest policy does not match the selected boot mechanism")


IO_EXPECTED: dict[str, dict[str, Any]] = {
    "aidk_ai_toy": {
        "swd": (False, "none", "none", [], {}),
        "console": ("uart", "uart0", 115200, "dynamic-usb-serial", "non-persistent"),
        "loader": ("com-uart", "serial-uart", False, "uart0", 115200,
                    "BKFIL/bk_loader"),
    },
    "t5ai_core": {
        "swd": (False, "none", "none", [], {}),
        "console": ("uart", "uart1", 460800, "board-uart", "board-local"),
        "loader": ("serial-uart", "bkfil", False, "uart0", 115200,
                    "BKFIL/bk_loader"),
    },
    "t5_board": {
        "swd": (True, "p0_p1", "cp", ["bl1", "bl2", "cp"],
                {"bl1": False, "bl2": True, "cp": True}),
        "console": ("rtt", None, None, "swd-rtt", "swd-session"),
        "loader": ("serial-uart", "bkfil", False, "uart0", 115200,
                    "BKFIL/bk_loader"),
    },
}


def _validate_debug_io(value: Any, board: str) -> None:
    if not isinstance(value, dict):
        raise BootPolicyError("debug_io must be an object")
    _exact(value, {"swd", "console", "loader"}, "debug_io")
    expected = IO_EXPECTED.get(board)
    if expected is None:
        raise BootPolicyError(f"no board-specific debug policy exists for {board}")

    swd = value["swd"]
    if not isinstance(swd, dict):
        raise BootPolicyError("debug_io.swd must be an object")
    _exact(swd, {"source", "enabled", "pin_group", "target", "stages", "stage_holds"},
           "debug_io.swd")
    if swd["source"] != "resolved-profile":
        raise BootPolicyError("SWD source must be resolved-profile")
    _boolean(swd["enabled"], "debug_io.swd.enabled")
    if swd["pin_group"] not in {"none", "p0_p1", "p20_p21"}:
        raise BootPolicyError("unsupported SWD pin group")
    if swd["target"] not in {"none", "cp", "ap0", "ap1"}:
        raise BootPolicyError("unsupported SWD target")
    stages = _string_list(swd["stages"], "debug_io.swd.stages")
    stage_holds = swd["stage_holds"]
    if not isinstance(stage_holds, dict):
        raise BootPolicyError("debug_io.swd.stage_holds must be an object")
    if set(stage_holds) != set(stages):
        raise BootPolicyError("debug_io.swd.stage_holds must cover exactly its stages")
    for stage, hold in stage_holds.items():
        _boolean(hold, f"debug_io.swd.stage_holds.{stage}")
    if (swd["enabled"], swd["pin_group"], swd["target"], stages, stage_holds) != expected["swd"]:
        raise BootPolicyError("board SWD policy does not match its resolved profile contract")

    console = value["console"]
    if not isinstance(console, dict):
        raise BootPolicyError("debug_io.console must be an object")
    _exact(console, {
        "source", "kind", "uart", "baud", "data_bits", "parity", "stop_bits",
        "flow_control", "rts_reset", "port_strategy", "port_identity",
        "loader_console_exclusive",
    }, "debug_io.console")
    if console["source"] != "resolved-profile":
        raise BootPolicyError("console source must be resolved-profile")
    if console["kind"] not in {"uart", "rtt", "none"}:
        raise BootPolicyError("unsupported console kind")
    if console["uart"] is not None and console["uart"] not in {"uart0", "uart1", "uart2"}:
        raise BootPolicyError("unsupported console UART")
    if console["baud"] is not None:
        _integer(console["baud"], "debug_io.console.baud")
    for field in ("data_bits", "stop_bits"):
        if console[field] is not None:
            _integer(console[field], f"debug_io.console.{field}")
    if console["parity"] is not None and console["parity"] not in {"none", "even", "odd"}:
        raise BootPolicyError("unsupported console parity")
    _boolean(console["flow_control"], "debug_io.console.flow_control")
    _boolean(console["rts_reset"], "debug_io.console.rts_reset")
    _boolean(console["loader_console_exclusive"], "debug_io.console.loader_console_exclusive")
    if (console["flow_control"] is not False or console["rts_reset"] is not False or
            console["loader_console_exclusive"] is not True):
        raise BootPolicyError("console flow-control/reset/exclusivity must be safe")
    if console["kind"] == "uart":
        if (console["uart"] is None or console["baud"] is None or
                console["data_bits"] is None or console["parity"] is None or
                console["stop_bits"] is None):
            raise BootPolicyError("UART console requires complete serial parameters")
    elif any(console[field] is not None for field in ("uart", "baud", "data_bits", "parity", "stop_bits")):
        raise BootPolicyError("RTT/none console must not carry UART parameters")
    observed_console = (
        console["kind"], console["uart"], console["baud"], console["port_strategy"],
        console["port_identity"],
    )
    if observed_console != expected["console"]:
        raise BootPolicyError("board console policy does not match its resolved profile contract")

    loader = value["loader"]
    if not isinstance(loader, dict):
        raise BootPolicyError("debug_io.loader must be an object")
    _exact(loader, {
        "transport", "download", "uart", "baud", "tool", "requires_swd",
        "rts_reset", "dtr_reset", "shared_port",
    },
           "debug_io.loader")
    for field in ("requires_swd", "rts_reset", "dtr_reset", "shared_port"):
        _boolean(loader[field], f"debug_io.loader.{field}")
    if loader["uart"] not in {"uart0", "uart1", "uart2"}:
        raise BootPolicyError("unsupported loader UART")
    _integer(loader["baud"], "debug_io.loader.baud")
    if loader["tool"] != "BKFIL/bk_loader":
        raise BootPolicyError("loader must name the BKFIL/bk_loader boundary")
    observed_loader = (
        loader["transport"], loader["download"], loader["requires_swd"],
        loader["uart"], loader["baud"], loader["tool"],
    )
    if observed_loader != expected["loader"]:
        raise BootPolicyError("board loader transport policy is not parseable/expected")
    if loader["rts_reset"] or loader["dtr_reset"] or loader["shared_port"]:
        raise BootPolicyError("loader reset and port sharing are forbidden by policy")


def _validate_version_policy(value: Any, boot: str) -> None:
    if not isinstance(value, dict):
        raise BootPolicyError("version_policy must be an object")
    _exact(value, {
        "application_version", "security_counter", "bl1_manifest_version",
        "bl2_security_counter_floor",
    }, "version_policy")
    application = value["application_version"]
    counter = value["security_counter"]
    manifest = value["bl1_manifest_version"]
    floor = value["bl2_security_counter_floor"]
    for item, field in ((application, "application_version"), (counter, "security_counter")):
        if not isinstance(item, dict):
            raise BootPolicyError(f"version_policy.{field} must be an object")
        _exact(item, {"state", "source", "format", "pair_rule", "monotonic"},
               f"version_policy.{field}")
        if item["state"] not in STATES:
            raise BootPolicyError(f"invalid version policy state: {field}")
        for child in ("source", "format", "pair_rule", "monotonic"):
            _string(item[child], f"version_policy.{field}.{child}")
    if not isinstance(manifest, dict) or not isinstance(floor, dict):
        raise BootPolicyError("manifest/counter-floor version policy entries must be objects")
    _exact(manifest, {"state", "source", "format", "default", "monotonic"},
           "version_policy.bl1_manifest_version")
    _exact(floor, {"state", "source", "format", "default", "otp_write"},
           "version_policy.bl2_security_counter_floor")
    for item, field in ((manifest, "bl1_manifest_version"), (floor, "bl2_security_counter_floor")):
        if item["state"] not in STATES:
            raise BootPolicyError(f"invalid version policy state: {field}")
        for child in ("source", "format"):
            _string(item[child], f"version_policy.{field}.{child}")
    _boolean(floor["otp_write"], "version_policy.bl2_security_counter_floor.otp_write")
    if floor["otp_write"] is not False:
        raise BootPolicyError("security counter policy must not write OTP/eFuse")

    if boot == "mcuboot":
        if (application["state"], counter["state"], manifest["state"], floor["state"]) != (
                "required", "required", "required", "policy-default"):
            raise BootPolicyError("MCUboot version/counter policy is not fail-closed")
        if (application["source"], counter["source"], application["pair_rule"],
                counter["pair_rule"], application["monotonic"], counter["monotonic"]) != (
                    "user-authorized", "user-authorized", "cp-ap-equal", "cp-ap-equal",
                    "non-decreasing", "non-decreasing"):
            raise BootPolicyError("MCUboot version/counter sources or pair rules are unsafe")
        if manifest["default"] != 5 or floor["default"] != 0:
            raise BootPolicyError("unsupported BL1/BL2 security defaults")
    else:
        if (application["state"], counter["state"], manifest["state"], floor["state"]) != (
                "not-applicable", "not-applicable", "not-applicable", "not-applicable"):
            raise BootPolicyError("raw boot must not accept version/counter inputs")
        if any(item["default"] is not None for item in (manifest, floor)):
            raise BootPolicyError("raw boot must not carry manifest/counter defaults")


def _validate_layout_binding(value: Any) -> None:
    if not isinstance(value, dict):
        raise BootPolicyError("layout_binding must be an object")
    _exact(value, {"source", "layout_id", "layout_sha256"}, "layout_binding")
    expected = {
        "source": "product.partition_layout",
        "layout_id": "product.partition_layout.layout_id",
        "layout_sha256": "product.partition_layout.layout_sha256",
    }
    if value != expected:
        raise BootPolicyError("layout binding must resolve from product/build-plan identity")


def _validate_geometry(value: Any, boot: str) -> None:
    if not isinstance(value, dict):
        raise BootPolicyError("bl2_geometry must be an object")
    _exact(value, {"required", "source", "layout_ref", "primary", "secondary"}, "bl2_geometry")
    _boolean(value["required"], "bl2_geometry.required")
    if value["required"] is not (boot == "mcuboot"):
        raise BootPolicyError("BL2 geometry applicability disagrees with boot mechanism")
    if boot == "raw":
        if (value["source"], value["layout_ref"], value["primary"], value["secondary"]) != (
                "not-applicable", "not-applicable", None, None):
            raise BootPolicyError("raw policy must mark BL2 geometry not-applicable")
        return
    if value["source"] != "resolved-layout" or value["layout_ref"] != "build_plan.partition_layout":
        raise BootPolicyError("BL2 geometry must be resolved from the build-plan layout")
    primary = value["primary"]
    secondary = value["secondary"]
    if not isinstance(primary, dict) or not isinstance(secondary, dict):
        raise BootPolicyError("BL2 geometry entries must be objects")
    _exact(primary, {"role", "physical_start", "physical_size", "xip_start", "logical_size"},
           "bl2_geometry.primary")
    _exact(secondary, {
        "physical_start", "physical_size", "xip_start", "logical_size", "boundary_role",
        "boundary", "same_capacity_as",
    }, "bl2_geometry.secondary")
    expected_primary = {
        "role": "bl2",
        "physical_start": "offset",
        "physical_size": "size",
        "xip_start": "xip_start",
        "logical_size": "logical_size",
    }
    expected_secondary = {
        "physical_start": "primary.offset + primary.size",
        "physical_size": "primary.size",
        "xip_start": "primary.xip_start + primary.logical_size",
        "logical_size": "primary.logical_size",
        "boundary_role": "littlefs",
        "boundary": "offset",
        "same_capacity_as": "primary.logical_size",
    }
    if primary != expected_primary or secondary != expected_secondary:
        raise BootPolicyError("BL2 geometry contains literals or an unsupported derivation")
    # A geometry policy is a reference expression, never an integer-bearing
    # layout snapshot.  This catches a future field added without a schema
    # update even when its key happens to be accepted by a loose parser.
    for section_name, section in (("primary", primary), ("secondary", secondary)):
        if any(isinstance(item, (int, float)) and not isinstance(item, bool)
               for item in section.values()):
            raise BootPolicyError(f"bl2_geometry.{section_name} contains a hard-coded number")


def _credential_entry(value: Any, index: int) -> None:
    if not isinstance(value, dict):
        raise BootPolicyError(f"credentials.required[{index}] must be an object")
    _exact(value, {"id", "kind", "purpose", "required_for", "supply", "state"},
           f"credentials.required[{index}]")
    _identifier(value["id"], f"credentials.required[{index}].id")
    for field in ("kind", "purpose", "supply"):
        _string(value[field], f"credentials.required[{index}].{field}")
    if value["state"] != "required":
        raise BootPolicyError("credential requirement state must be required")
    required_for = _string_list(value["required_for"], f"credentials.required[{index}].required_for",
                                allow_empty=False)
    if required_for != ["sign"]:
        raise BootPolicyError("credential requirements may only authorize the sign stage")
    expected = {
        "bk7258-mcuboot-signing": ("private-signing-key", "sign-cp-ap"),
        "bk7258-bl1-manifest-signing": ("private-signing-key", "authorize-bl2"),
    }
    if value["id"] not in expected or (value["kind"], value["purpose"]) != expected[value["id"]]:
        raise BootPolicyError("unknown or mismatched credential requirement")
    if value["supply"] != "external-user-authorized":
        raise BootPolicyError("credentials must be supplied by an external authorized boundary")


def _validate_credentials(value: Any, boot: str) -> None:
    if not isinstance(value, dict):
        raise BootPolicyError("credentials must be an object")
    _exact(value, {"policy", "read_private_material", "required", "optional"}, "credentials")
    if value["policy"] != "identifier-only-external-supply":
        raise BootPolicyError("credential policy is not identifier-only")
    if value["read_private_material"] is not False:
        raise BootPolicyError("policy validation may not read private material")
    required = value["required"]
    optional = value["optional"]
    if not isinstance(required, list) or not isinstance(optional, list):
        raise BootPolicyError("credential required/optional must be lists")
    _validate_no_key_paths(value)
    for index, item in enumerate(required):
        _credential_entry(item, index)
    if optional:
        raise BootPolicyError("optional credentials are not accepted; add a required ID explicitly")
    required_ids = [item["id"] for item in required]
    if len(required_ids) != len(set(required_ids)):
        raise BootPolicyError("duplicate credential ID")
    expected = {"bk7258-mcuboot-signing", "bk7258-bl1-manifest-signing"} if boot == "mcuboot" else set()
    if set(required_ids) != expected:
        raise BootPolicyError("credential requirements do not match the boot mechanism")


def _validate_permissions(value: Any, boot: str) -> None:
    if not isinstance(value, dict):
        raise BootPolicyError("permissions must be an object")
    fields = {
        "resolve", "prepare", "materialize", "compile", "sign", "package", "flash",
        "hardware", "network", "key_read",
    }
    _exact(value, fields, "permissions")
    for field in fields:
        if value[field] not in PERMISSIONS:
            raise BootPolicyError(f"invalid permission state: {field}")
    expected = {
        "resolve": "allow",
        "prepare": "allow",
        "materialize": "allow",
        "compile": "requires-user-authorization",
        "sign": "requires-user-authorization" if boot == "mcuboot" else "forbidden",
        "package": "requires-user-authorization",
        "flash": "forbidden",
        "hardware": "forbidden",
        "network": "forbidden",
        "key_read": "forbidden",
    }
    if value != expected:
        raise BootPolicyError("permission state is not fail-closed or does not match boot mode")


def _validate_executor(value: Any, boot: str) -> None:
    if not isinstance(value, dict):
        raise BootPolicyError("executor must be an object")
    _exact(value, {
        "interface_version", "input_kind", "selection", "layout_resolution", "geometry_output",
        "preauthorization_side_effects", "authorization_fields", "no_key_paths",
        "authority", "integration_status", "blocked_until",
    }, "executor")
    if value["interface_version"] != 1:
        raise BootPolicyError("unsupported boot-policy executor interface")
    expected = {
        "input_kind": "resolved-isolated-build-plan",
        "selection": "exact-product-id",
        "layout_resolution": "build_plan.partition_layout",
        "geometry_output": "resolved-from-layout",
        "preauthorization_side_effects": "none",
    }
    for field, expected_value in expected.items():
        if value[field] != expected_value:
            raise BootPolicyError(f"executor.{field} is not fail-closed")
    if value["no_key_paths"] is not True:
        raise BootPolicyError("executor must declare no_key_paths=true")
    if (value["authority"] != "not-executor-authoritative" or
            value["integration_status"] != INTEGRATION_STATUS or
            value["blocked_until"] != INTEGRATION_BLOCKED_UNTIL):
        raise BootPolicyError("executor integration must remain metadata-only and blocked")
    fields = _string_list(value["authorization_fields"], "executor.authorization_fields", allow_empty=False)
    if any("/" in item or "path" in item.lower() for item in fields):
        raise BootPolicyError("executor authorization fields must not contain key paths")
    required = {
        "version_policy.application_version",
        "version_policy.security_counter",
        "version_policy.bl1_manifest_version",
        "credential-id:bk7258-mcuboot-signing",
        "credential-id:bk7258-bl1-manifest-signing",
        "permissions.package",
    } if boot == "mcuboot" else {"permissions.package"}
    if set(fields) != required:
        raise BootPolicyError("executor authorization fields are incomplete or over-broad")


def _catalog_path(repository: Path, policy: Mapping[str, Any]) -> Path:
    relative = _relative_path(policy["catalog"], "catalog")
    if relative != PRODUCT_CATALOGS.get(policy["product"]):
        raise BootPolicyError("policy catalog does not match the exact product catalog")
    path = repository / relative
    try:
        resolved = path.resolve(strict=True)
    except OSError as error:
        raise BootPolicyError(f"missing product catalog: {relative}") from error
    if resolved != path or not resolved.is_file():
        raise BootPolicyError("product catalog must be an in-tree regular file")
    return resolved


def _validate_catalog(repository: Path, policy: Mapping[str, Any]) -> dict[str, Any]:
    catalog = _load_json(_catalog_path(repository, policy), "product catalog")
    if catalog.get("kind") != "product" or catalog.get("schema") != "bk7258.composition/1":
        raise BootPolicyError("unsupported product catalog schema")
    for field in ("id", "family", "board", "boot"):
        if catalog.get(field) != policy[field if field != "id" else "product"]:
            raise BootPolicyError(f"policy/catalog identity mismatch: {field}")
    layout = catalog.get("partition_layout")
    if not isinstance(layout, dict):
        raise BootPolicyError("product catalog has no partition_layout")
    _exact(layout, {"source", "layout_id", "layout_sha256"}, "product.partition_layout")
    source = _relative_path(layout["source"], "product.partition_layout.source")
    if not source.startswith("board/bk7258/partitions/") or not source.endswith(".csv"):
        raise BootPolicyError("product partition source is outside the board-owned partition tree")
    _identifier(layout["layout_id"], "product.partition_layout.layout_id")
    _digest(layout["layout_sha256"], "product.partition_layout.layout_sha256")
    return catalog


def _load_layout(repository: Path, catalog: Mapping[str, Any]):
    layout_ref = catalog["partition_layout"]
    source = repository / layout_ref["source"]
    try:
        resolved = source.resolve(strict=True)
    except OSError as error:
        raise BootPolicyError(f"missing resolved partition layout: {source}") from error
    if resolved != source or not resolved.is_file():
        raise BootPolicyError("resolved partition layout must be an in-tree regular file")
    try:
        gen_bk7258_partitions = load_board_script("gen_bk7258_partitions")
        from gen_bk7258_partitions import load_layout
        from gen_bk7258_partitions import PartitionLayoutError
        layout = load_layout(resolved)
    except (ImportError, OSError, ValueError, RuntimeError) as error:
        raise BootPolicyError(f"cannot resolve partition layout: {source}") from error
    if (layout.layout_id != layout_ref["layout_id"] or
            layout.layout_sha256 != layout_ref["layout_sha256"]):
        raise BootPolicyError("resolved partition layout identity differs from product catalog")
    return layout


def _looks_like_build_plan(value: Mapping[str, Any]) -> bool:
    """Return whether a third positional resolve argument is a full plan."""

    return any(key in value for key in ("identity_inputs", "roles", "source_views"))


def _validate_build_plan_binding(
    value: Any,
    policy: Mapping[str, Any],
    catalog: Mapping[str, Any],
) -> dict[str, Any]:
    """Validate only the identity/profile boundary needed by this metadata layer.

    The framework owns the complete build-plan validator.  This checker is
    intentionally independent and fail-closed: a caller cannot make a boot
    policy look executable by supplying just a layout object or by changing a
    product, board, boot mode, layout digest, or role set in an otherwise valid
    plan.
    """

    if not isinstance(value, Mapping):
        raise BootPolicyError("build_plan must be a mapping")
    plan = dict(value)
    if (plan.get("schema") != BUILD_PLAN_SCHEMA or
            plan.get("kind") != BUILD_PLAN_KIND or
            type(plan.get("version")) is not int or plan.get("version") != 1):
        raise BootPolicyError("unsupported build_plan schema")
    plan_identity = plan.get("identity_sha256")
    _digest(plan_identity, "build_plan.identity_sha256")
    body = dict(plan)
    body.pop("identity_sha256", None)
    if sha256(canonical_json(body)) != plan_identity:
        raise BootPolicyError("build_plan identity does not match its content")
    inputs = plan.get("identity_inputs")
    if not isinstance(inputs, Mapping):
        raise BootPolicyError("build_plan.identity_inputs is required")
    for field in ("product", "family", "board", "boot"):
        if inputs.get(field) != policy[field]:
            raise BootPolicyError(f"build_plan {field} does not match boot policy")
    expected_image_size = policy["bl2_image_logical_size"]
    if (plan.get("bl2_image_logical_size") != expected_image_size or
            inputs.get("bl2_image_logical_size") != expected_image_size):
        raise BootPolicyError("build_plan BL2 image logical size is not policy-bound")
    if inputs.get("mode") != catalog.get("mode", "bringup"):
        raise BootPolicyError("build_plan mode does not match product catalog")
    board = plan.get("board")
    if not isinstance(board, Mapping) or board.get("id") != policy["board"]:
        raise BootPolicyError("build_plan board identity does not match boot policy")

    expected_layout = catalog["partition_layout"]
    layout = plan.get("partition_layout")
    if not isinstance(layout, Mapping) or dict(layout) != expected_layout:
        raise BootPolicyError("build_plan partition layout identity does not match policy")
    if (inputs.get("partition_layout_id") != expected_layout["layout_id"] or
            inputs.get("partition_layout_sha256") != expected_layout["layout_sha256"]):
        raise BootPolicyError("build_plan identity_inputs layout binding is not exact")

    roles = plan.get("roles")
    if not isinstance(roles, Mapping) or set(roles) != ALL_ROLES:
        raise BootPolicyError("build_plan roles must expose BL1/BL2/CP/AP exactly")
    expected_active_roles = list(policy["active_roles"])
    if plan.get("active_roles") != expected_active_roles or \
            inputs.get("active_roles") != expected_active_roles:
        raise BootPolicyError("build_plan active_roles do not match boot policy")
    for role in ACTIVE_ROLES:
        row = roles[role]
        if not isinstance(row, Mapping):
            raise BootPolicyError(f"build_plan role is malformed: {role}")
        expected_active = role in expected_active_roles
        if row.get("activation") != ("active" if expected_active else "inactive") or \
                row.get("applicability") != ("required" if expected_active else "not-applicable"):
            raise BootPolicyError(f"build_plan role applicability is not policy-bound: {role}")
    role_ids = inputs.get("role_ir_sha256")
    if not isinstance(role_ids, Mapping) or not {"cp", "ap", "bl2"} <= set(role_ids):
        raise BootPolicyError("build_plan role identity coverage is incomplete")
    for role in ("cp", "ap", "bl2"):
        _digest(role_ids[role], f"build_plan.identity_inputs.role_ir_sha256.{role}")

    return plan


def _resolved_geometry(layout: Any) -> dict[str, Any]:
    try:
        primary = layout.by_role("bl2")
        boundary = layout.by_role("littlefs")
        logical_offset = layout.logical_offset(primary)
        logical_size = layout.logical_size(primary)
        primary_xip = layout.xip_base + logical_offset
        secondary_physical_start = primary.offset + primary.size
        secondary_physical_end = secondary_physical_start + primary.size
    except Exception as error:  # layout implementations expose a stable public API
        raise BootPolicyError("resolved layout cannot provide BL2 geometry") from error
    if logical_size <= 0 or primary.size <= 0:
        raise BootPolicyError("resolved BL2 geometry is empty")
    if secondary_physical_end > boundary.offset:
        raise BootPolicyError("resolved secondary BL2 overlaps the layout boundary")
    return {
        "source": "resolved-layout",
        "layout_id": layout.layout_id,
        "layout_sha256": layout.layout_sha256,
        "primary": {
            "role": primary.role,
            "physical_start": primary.offset,
            "physical_size": primary.size,
            "xip_start": primary_xip,
            "logical_size": logical_size,
        },
        "secondary": {
            "role": "bl2-secondary-derived",
            "physical_start": secondary_physical_start,
            "physical_size": primary.size,
            "xip_start": primary_xip + logical_size,
            "logical_size": logical_size,
            "boundary_role": boundary.role,
            "boundary_offset": boundary.offset,
        },
    }


def validate_policy(
    value: Mapping[str, Any] | Path,
    repository: Path | None = None,
    *,
    expected_product: str | None = None,
    resolved_layout: Mapping[str, Any] | None = None,
) -> dict[str, Any]:
    """Strictly validate one policy, optionally against its product/layout.

    ``resolved_layout`` is the small ``partition_layout`` object emitted by a
    framework build plan.  Passing it is optional for static metadata checks,
    but an executor should pass it so a policy cannot be detached from the
    exact plan it is about to consume.
    """

    if isinstance(value, Path):
        value = _load_json(value, "boot policy")
    if not isinstance(value, Mapping):
        raise BootPolicyError("boot policy must be an object")
    policy = dict(value)
    _exact(policy, {
        "schema", "kind", "version", "policy_id", "product", "family", "board", "catalog",
        "boot", "bl2_image_logical_size", "active_roles", "metadata_only", "executor_authoritative",
        "integration_status", "integration_blocked_until", "mechanism", "manifest",
        "debug_io", "version_policy", "layout_binding", "bl2_geometry", "credentials",
        "permissions", "executor", "identity_sha256",
    }, "boot policy")
    if (policy["schema"], policy["kind"], policy["version"]) != (
            POLICY_SCHEMA, POLICY_KIND, 1):
        raise BootPolicyError("unsupported boot policy schema")
    _identifier(policy["policy_id"], "policy_id")
    product = _identifier(policy["product"], "product")
    family = _identifier(policy["family"], "family")
    board = _identifier(policy["board"], "board")
    if expected_product is not None and product != expected_product:
        raise BootPolicyError("boot policy product does not match requested product")
    if product not in POLICY_FILES:
        raise BootPolicyError(f"unsupported product policy: {product}")
    if policy["boot"] not in {"raw", "mcuboot"}:
        raise BootPolicyError("unsupported boot mode")
    image_size = policy["bl2_image_logical_size"]
    if policy["boot"] == "mcuboot":
        if (isinstance(image_size, bool) or not isinstance(image_size, int) or
                image_size != 0x3000):
            raise BootPolicyError(
                "MCUboot bl2_image_logical_size must be the board 0x3000 window")
    elif image_size is not None:
        raise BootPolicyError("raw boot must not carry a BL2 image logical size")
    _boolean(policy["metadata_only"], "metadata_only")
    if policy["metadata_only"] is not True:
        raise BootPolicyError("boot policy must remain metadata-only")
    _boolean(policy["executor_authoritative"], "executor_authoritative")
    if policy["executor_authoritative"] is not False:
        raise BootPolicyError("boot policy must not be executor-authoritative")
    if policy["integration_status"] != INTEGRATION_STATUS:
        raise BootPolicyError("boot policy executor integration must remain BLOCKED")
    if policy["integration_blocked_until"] != INTEGRATION_BLOCKED_UNTIL:
        raise BootPolicyError("boot policy must identify plan/profile reconciliation as its blocker")
    _validate_active_roles(policy["active_roles"], policy["boot"])
    _relative_path(policy["catalog"], "catalog")
    _validate_mechanism(policy["mechanism"], policy["boot"])
    _validate_manifest(policy["manifest"], policy["boot"])
    _validate_debug_io(policy["debug_io"], board)
    _validate_version_policy(policy["version_policy"], policy["boot"])
    _validate_layout_binding(policy["layout_binding"])
    _validate_geometry(policy["bl2_geometry"], policy["boot"])
    _validate_credentials(policy["credentials"], policy["boot"])
    _validate_permissions(policy["permissions"], policy["boot"])
    _validate_executor(policy["executor"], policy["boot"])
    _validate_identity(policy)

    if repository is not None:
        repository = Path(repository).resolve()
        catalog = _validate_catalog(repository, policy)
        if catalog["id"] != product or catalog["family"] != family or catalog["board"] != board:
            raise BootPolicyError("policy identity does not match product catalog")
        if resolved_layout is not None:
            if not isinstance(resolved_layout, Mapping):
                raise BootPolicyError("resolved_layout must be a mapping")
            expected = catalog["partition_layout"]
            if dict(resolved_layout) != expected:
                raise BootPolicyError("resolved build-plan layout identity differs from product catalog")
    return policy


def policy_path(repository: Path, product: str) -> Path:
    """Return the only accepted in-tree policy path for ``product``."""

    if product not in POLICY_FILES:
        raise BootPolicyError(f"unknown BK7258 product: {product}")
    repository = Path(repository).resolve()
    path = repository / "tools/bk7258" / POLICY_FILES[product]
    try:
        resolved = path.resolve(strict=True)
    except OSError as error:
        raise BootPolicyError(f"missing boot policy for {product}") from error
    if resolved != path or not resolved.is_file():
        raise BootPolicyError("boot policy must be an in-tree regular file")
    return resolved


def load_policy(repository: Path, product: str) -> dict[str, Any]:
    """Load and validate the exact checked-in product policy."""

    path = policy_path(repository, product)
    return validate_policy(_load_json(path, "boot policy"), Path(repository),
                           expected_product=product)


def resolve_policy(
    repository: Path,
    product: str,
    resolved_layout: Mapping[str, Any] | None = None,
    *,
    build_plan: Mapping[str, Any] | None = None,
    plan: Mapping[str, Any] | None = None,
) -> dict[str, Any]:
    """Resolve metadata while keeping executor integration explicitly blocked.

    A complete isolated ``build_plan`` may be supplied either by keyword or as
    the third positional argument.  A layout-only argument remains useful for
    geometry checks, but it can never make this metadata authoritative or
    executable; the returned integration status is still ``BLOCKED``.
    """

    repository = Path(repository).resolve()
    if build_plan is not None and plan is not None:
        raise BootPolicyError("supply only one of build_plan and plan")
    if plan is not None:
        build_plan = plan
    if build_plan is None and resolved_layout is not None and \
            _looks_like_build_plan(resolved_layout):
        build_plan = resolved_layout
        resolved_layout = None
    policy = load_policy(repository, product)
    catalog = _validate_catalog(repository, policy)
    if build_plan is not None:
        plan_value = _validate_build_plan_binding(build_plan, policy, catalog)
        resolved_layout = plan_value["partition_layout"]
    elif resolved_layout is not None:
        if not isinstance(resolved_layout, Mapping):
            raise BootPolicyError("resolved_layout must be a mapping")
        if dict(resolved_layout) != catalog["partition_layout"]:
            raise BootPolicyError("resolved layout identity differs from product catalog")
        plan_value = None
    else:
        plan_value = None
    validate_policy(policy, repository, expected_product=product,
                    resolved_layout=resolved_layout)
    layout = _load_layout(repository, catalog)
    geometry = (_resolved_geometry(layout) if policy["boot"] == "mcuboot" else {
        "required": False,
        "source": "not-applicable",
        "layout_ref": "not-applicable",
        "primary": None,
        "secondary": None,
    })
    body: dict[str, Any] = {
        "schema": RESOLVED_SCHEMA,
        "kind": RESOLVED_KIND,
        "version": 1,
        "policy_id": policy["policy_id"],
        "product": policy["product"],
        "board": policy["board"],
        "bl2_image_logical_size": policy["bl2_image_logical_size"],
        "active_roles": list(policy["active_roles"]),
        "metadata_only": True,
        "executor_authoritative": False,
        "integration_status": INTEGRATION_STATUS,
        "integration_blocked_until": INTEGRATION_BLOCKED_UNTIL,
        "executable": False,
        "integration": {
            "status": INTEGRATION_STATUS,
            "blocked_until": INTEGRATION_BLOCKED_UNTIL,
            "plan_profile_reconciliation": (
                "validated" if plan_value is not None else "required"
            ),
        },
        "build_plan_identity_sha256": (
            plan_value.get("identity_sha256") if plan_value is not None else None
        ),
        "policy": copy.deepcopy(policy),
        "resolved_layout": {
            "source": catalog["partition_layout"]["source"],
            "layout_id": layout.layout_id,
            "layout_sha256": layout.layout_sha256,
        },
        "bl2_geometry": geometry,
        "side_effects": {
            "compile": "NOT_RUN",
            "sign": "NOT_RUN",
            "package": "NOT_RUN",
            "hardware": "NOT_RUN",
            "network": "NOT_RUN",
            "key_read": "NOT_RUN",
        },
    }
    result = dict(body)
    result["identity_sha256"] = sha256(canonical_json(body))
    return result


def validate_all(repository: Path) -> dict[str, dict[str, Any]]:
    """Validate every existing product policy and return them by product ID."""

    result: dict[str, dict[str, Any]] = {}
    for product in POLICY_FILES:
        result[product] = load_policy(Path(repository), product)
    return result


# Friendly aliases for an executor integration that prefers explicit names.
validate_boot_policy = validate_policy
resolve_boot_policy = resolve_policy


def _main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=CONTEST_ROOT,
                        help="contest repository root")
    parser.add_argument("--product", choices=tuple(POLICY_FILES),
                        help="validate one product; default validates all")
    parser.add_argument("--resolve", action="store_true",
                        help="also resolve the product layout and derived geometry")
    args = parser.parse_args()
    try:
        if args.product is None:
            if args.resolve:
                output = {product: resolve_policy(args.root, product)
                          for product in POLICY_FILES}
            else:
                output = validate_all(args.root)
        elif args.resolve:
            output = resolve_policy(args.root, args.product)
        else:
            output = load_policy(args.root, args.product)
    except BootPolicyError as error:
        print(f"bk7258-boot-policy: FAIL: {error}", file=sys.stderr)
        return 2
    print(json.dumps(output, sort_keys=True, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(_main())
