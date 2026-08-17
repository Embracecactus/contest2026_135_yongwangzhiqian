#!/usr/bin/env python3
"""Canonical BK7258 legacy-profile materializer.

The composition framework owns product resolution.  This module renders the
already-resolved CP/AP IR into a caller-owned root from a retained seed
``defconfig`` or an absolute final ``<role>.config``; it never synthesizes
Kconfig values.  The historical :func:`materialize` entry point remains
available for older host checks; new callers should use
:func:`materialize_plan`.

This module is the single implementation: the former
``materialize_aidk_profiles.py`` re-export shim was merged here.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any

import bk7258_framework as framework

__all__ = [
    "BOARD_SYMBOLS", "PAIR",
    "COMPAT", "VALIDATION_SUITE_COMPAT", "materialize", "materialize_plan",
    "materialized_hashes", "main", "profile_name", "seed_record",
]


PAIR = {
    "cp": "t5ai_core_cp_base",
    "ap": "t5ai_core_ap_base",
}
COMPAT = "aidk_ai_toy_mcuboot_ab_v1"
# These are the compatibility identities consumed by the existing dual-image
# shell.  They remain data-only metadata; validation suites never inject
# Kconfig symbols anymore (the final .config is the only config authority).
VALIDATION_SUITE_COMPAT = {
    "audio_dac": "t5_board_audio_dac_validation_mcuboot_v2",
    "jpeg_m2m": "t5_board_jpeg_m2m_validation_mcuboot_v1",
    "saradc_key": "t5_board_saradc_key_validation_mcuboot_v1",
    "psram": "t5ai_core_psram_validation_raw_v1",
    "temperature": "t5_board_app_mcuboot_v1",
    "camera": "t5_board_app_mcuboot_v1",
    "camera_h264": "t5_board_app_mcuboot_v1",
    "pwm": "t5_board_app_mcuboot_v1",
    "tf_1bit": "t5_board_app_mcuboot_v1",
    "tf_4bit": "t5_board_app_mcuboot_v1",
    "driver_coverage": "t5_board_app_mcuboot_v1",
    "wifi": "t5_board_app_mcuboot_v1",
}
BOARD_SYMBOLS = {
    "aidk_ai_toy": "CONFIG_BK7258_BOARD_AIDK_AI_TOY",
    "t5_board": "CONFIG_BK7258_BOARD_T5_BOARD",
    "t5ai_core": "CONFIG_BK7258_BOARD_T5AI_CORE",
}

def _canonical_json(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def profile_name(product: str, role: str, boot: str) -> str:
    if product.endswith("_bringup"):
        product = product[:-len("_bringup")]
    return f"{product}_{role}_{boot}"


def _file_sha256(path: Path) -> str:
    try:
        return hashlib.sha256(path.read_bytes()).hexdigest()
    except OSError as error:
        raise ValueError(f"cannot read legacy seed: {path}") from error


def seed_record(seed_root: Path, seed_profile: str, role: str, product: str, boot: str,
                compat: str | None = None,
                sdk_bundle: str | None = None,
                board: str = "aidk_ai_toy") -> dict[str, Any]:
    """Build a hash-bound adapter record for one source profile."""
    if role not in {"cp", "ap"}:
        raise ValueError(f"unsupported materialized role: {role}")
    if not seed_profile or "/" in seed_profile or ".." in seed_profile:
        raise ValueError(f"unsafe seed profile: {seed_profile}")
    source = seed_root / seed_profile
    profile = source / "profile.conf"
    defconfig = source / "defconfig"
    if not profile.is_file() or not defconfig.is_file():
        raise ValueError(f"missing legacy seed profile: {source}")
    result = {
        "seed_profile": seed_profile,
        "source": f"board/bk7258/configs/{seed_profile}",
        "profile_sha256": _file_sha256(profile),
        "defconfig_sha256": _file_sha256(defconfig),
        "target_profile": profile_name(product, role, boot),
        "compat": compat,
        "sdk_bundle": sdk_bundle,
    }
    result.update(materialized_hashes(
        profile.read_text(encoding="utf-8"),
        defconfig.read_text(encoding="utf-8"), role, board=board,
        boot=boot, compat=compat, sdk_bundle=sdk_bundle))
    return result


def _rewrite_profile(text: str, role: str, *, board: str = "aidk_ai_toy",
                     boot: str = "mcuboot", compat: str | None = COMPAT,
                     sdk_bundle: str | None = None) -> str:
    if board not in BOARD_SYMBOLS or role not in {"cp", "ap"}:
        raise ValueError(f"unsupported materialized profile: {board}/{role}")
    lines = text.splitlines()
    fields = {
        "BK7258_PROFILE_BOARD": board,
        "BK7258_PROFILE_ROLE": role,
        "BK7258_PROFILE_BOOT": boot,
        "BK7258_PROFILE_CLASS": "runnable",
        "BK7258_PROFILE_COMPAT": compat or "legacy",
    }
    if sdk_bundle:
        fields["BK7258_PROFILE_SDK_BUNDLE"] = sdk_bundle
    seen: set[str] = set()
    output: list[str] = []
    for line in lines:
        key = line.split("=", 1)[0] if "=" in line else ""
        if key in fields:
            if key not in seen:
                output.append(f"{key}={fields[key]}")
                seen.add(key)
            continue
        output.append(line)
    # Old seeds do not carry SDK metadata.  Required profile identity fields
    # are repaired deterministically rather than guessed by the shell.
    for key in fields:
        if key not in seen:
            output.append(f"{key}={fields[key]}")
    return "\n".join(output) + "\n"


def materialized_hashes(profile_text: str, defconfig_text: str, role: str,
                        *, board: str, boot: str, compat: str | None,
                        sdk_bundle: str | None) -> dict[str, str]:
    """Hash the exact profile metadata and retained config input."""
    expanded_profile = _rewrite_profile(
        profile_text, role, board=board, boot=boot, compat=compat,
        sdk_bundle=sdk_bundle)
    return {
        "materialized_profile_sha256": hashlib.sha256(
            expanded_profile.encode()).hexdigest(),
        "materialized_defconfig_sha256": hashlib.sha256(
            defconfig_text.encode()).hexdigest(),
    }


def _rewrite_validation_profile(text: str, suite: str, compat: str) -> str:
    """Bind a resolved suite to the legacy shell's profile metadata."""
    fields = {
        "BK7258_PROFILE_CLASS": "validation",
        "BK7258_PROFILE_COMPAT": compat,
        "BK7258_PROFILE_VALIDATION_SUITE": suite,
    }
    seen: set[str] = set()
    output: list[str] = []
    for line in text.splitlines():
        key = line.split("=", 1)[0] if "=" in line else ""
        if key in fields:
            if key not in seen:
                output.append(f"{key}={fields[key]}")
                seen.add(key)
            continue
        output.append(line)
    for key, value in fields.items():
        if key not in seen:
            output.append(f"{key}={value}")
    return "\n".join(output) + "\n"


def _validate_plan(plan: dict[str, Any]) -> tuple[dict[str, Any], dict[str, Any]]:
    if plan.get("kind") != "isolated-build-plan":
        raise ValueError("materializer requires an isolated build plan")
    inputs = plan.get("identity_inputs")
    adapter = plan.get("config_inputs")
    if not isinstance(inputs, dict) or not isinstance(adapter, dict):
        raise ValueError("build plan lacks compatibility adapter metadata")
    profiles = adapter.get("seed_profiles")
    if not isinstance(profiles, dict) or set(profiles) != {"cp", "ap"}:
        raise ValueError("build plan seed_profiles must contain CP and AP")
    required = {"seed_profile", "source", "profile_sha256", "defconfig_sha256",
                "target_profile", "compat",
                "sdk_bundle", "materialized_profile_sha256",
                "materialized_defconfig_sha256"}
    for role in ("cp", "ap"):
        row = profiles[role]
        if not isinstance(row, dict) or set(row) != required:
            raise ValueError(f"malformed seed profile record: {role}")
        source = row["source"]
        if (adapter.get("mode") != "resolved-config" or
                row["compat"] != framework.CANONICAL_CONFIG_COMPAT or
                not (source.startswith("board/bk7258/configs/") or
                     (source.startswith("/") and source.endswith(f"{role}.config")))):
            raise ValueError(
                "config input is not a canonical retained seed or final .config")
    return inputs, profiles


def materialize_plan(plan: dict[str, Any], seed_root: Path, output_root: Path,
                     make_defs: Path, validation_suite: str | None = None) -> Path:
    """Render CP/AP role configs named by a canonical build plan."""
    inputs, profiles = _validate_plan(plan)
    product = inputs.get("product")
    board = inputs.get("board")
    boot = inputs.get("boot")
    if not all(isinstance(item, str) for item in (product, board, boot)):
        raise ValueError("build plan identity inputs are incomplete")
    if board not in BOARD_SYMBOLS or boot not in {"raw", "mcuboot"}:
        raise ValueError("build plan has unsupported board/boot")
    make_defs = make_defs.resolve()
    if not make_defs.is_file():
        raise ValueError(f"missing canonical board Make.defs: {make_defs}")
    repository = make_defs.parents[3]
    plan_suite = inputs.get("validation_suite")
    if plan_suite is not None and not isinstance(plan_suite, str):
        raise ValueError("build plan validation suite is malformed")
    if validation_suite is not None and plan_suite not in {None, validation_suite}:
        raise ValueError("materializer validation suite differs from build plan")
    selected_suite = validation_suite or plan_suite
    suite_compat = None
    if selected_suite is not None:
        suite_compat = VALIDATION_SUITE_COMPAT.get(selected_suite)
        if suite_compat is None:
            raise ValueError(f"unsupported validation suite compatibility identity: {selected_suite}")
        suites = framework.load_validation_suites(repository)
        suite = suites.get(selected_suite)
        if suite is None:
            raise ValueError(f"unknown validation suite: {selected_suite}")
        if suite["product"] != product:
            raise ValueError(
                f"validation suite {selected_suite} is bound to {suite['product']}, not {product}")
    del seed_root
    if output_root.exists() and any(output_root.iterdir()):
        raise ValueError(f"output root is not empty: {output_root}")
    output_root.mkdir(parents=True, exist_ok=True)
    scripts_root = output_root.parent / "scripts"
    scripts_root.mkdir(exist_ok=True)
    make_defs_link = scripts_root / "Make.defs"
    if make_defs_link.exists() or make_defs_link.is_symlink():
        make_defs_link.unlink()
    make_defs_link.symlink_to(make_defs)

    suite_profiles: dict[str, dict[str, str]] = {}
    for role in ("cp", "ap"):
        row = profiles[role]
        if selected_suite is None:
            ir = framework.resolve(repository, product, role, board, inputs["mode"])
        else:
            ir = framework.resolve_validation_suite(
                repository, product, selected_suite, role, board, inputs["mode"])
        source = row["source"]
        if source.startswith("board/bk7258/configs/"):
            config_path = repository / source / "defconfig"
        else:
            config_path = Path(source)
        config = framework.config_document(ir, repository=repository,
                                           config_path=config_path)
        profile_text = framework._canonical_profile_text(ir)
        if selected_suite is None:
            if hashlib.sha256(profile_text.encode()).hexdigest() != row["profile_sha256"]:
                raise ValueError(f"canonical profile digest mismatch: {role}")
            if config["defconfig_sha256"] != row["defconfig_sha256"]:
                raise ValueError(f"canonical defconfig digest mismatch: {role}")
        else:
            # Suite profiles are intentionally temporary.  Their hashes are
            # bound to the resolved suite IR and recorded in the sidecar
            # below, rather than replacing the product-only plan seed hashes.
            profile_text = _rewrite_validation_profile(
                profile_text, selected_suite, suite_compat)
        target = output_root / row["target_profile"]
        target.mkdir()
        expanded_profile = profile_text
        expanded_defconfig = config["defconfig"]
        if selected_suite is None:
            if hashlib.sha256(expanded_profile.encode()).hexdigest() != row["materialized_profile_sha256"]:
                raise ValueError(f"materialized profile digest mismatch: {role}")
            if hashlib.sha256(expanded_defconfig.encode()).hexdigest() != row["materialized_defconfig_sha256"]:
                raise ValueError(f"materialized defconfig digest mismatch: {role}")
        target.joinpath("profile.conf").write_text(expanded_profile, encoding="utf-8")
        target.joinpath("defconfig").write_text(expanded_defconfig, encoding="utf-8")
        if selected_suite is not None:
            suite_profiles[role] = {
                "profile_sha256": hashlib.sha256(expanded_profile.encode()).hexdigest(),
                "defconfig_sha256": hashlib.sha256(expanded_defconfig.encode()).hexdigest(),
                "compat": suite_compat,
                "class": "validation",
            }
    if selected_suite is not None:
        catalog_path = repository / framework.VALIDATION_SUITE_REL
        catalog = framework.load_json(catalog_path)
        body = {
            "schema": "bk7258.materialized-validation/1",
            "kind": "materialized-validation-suite",
            "product": product,
            "suite": selected_suite,
            "catalog_identity_sha256": catalog["identity_sha256"],
            "plan_identity_sha256": plan["identity_sha256"],
            "profiles": suite_profiles,
        }
        body["identity_sha256"] = hashlib.sha256(
            _canonical_json(body)).hexdigest()
        output_root.joinpath("bk7258-validation-suite.json").write_bytes(
            _canonical_json(body))
    return output_root


def materialize(seed_root: Path, output_root: Path, make_defs: Path) -> Path:
    del seed_root, output_root, make_defs
    raise ValueError(
        "legacy profile materialization is retired; use a canonical build "
        "plan with retained seeds or final .configs (legacy names are shadow-only)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seed-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--make-defs", type=Path, required=True)
    parser.add_argument("--plan", type=Path,
                        help="framework isolated build plan JSON")
    parser.add_argument("--validation-suite",
                        help="temporary validation-suite metadata profile")
    args = parser.parse_args()
    try:
        if args.plan is None:
            materialize(args.seed_root.resolve(), args.output.resolve(),
                        args.make_defs.resolve())
        else:
            plan = json.loads(args.plan.read_text(encoding="utf-8"))
            materialize_plan(plan, args.seed_root.resolve(), args.output.resolve(),
                             args.make_defs.resolve(), args.validation_suite)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.error(str(error))
    print(f"BK7258 legacy profiles materialized under {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
