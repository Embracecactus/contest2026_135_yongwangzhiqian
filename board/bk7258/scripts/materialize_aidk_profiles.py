#!/usr/bin/env python3
"""Materialize the small AIDK CP/AP MCUboot profile pair in a temp root.

The repository keeps the reviewed t5ai MCUboot seeds as the compatibility
source.  AIDK is a product binding, not another frozen ``configs/`` tree, so
this adapter copies only the seed inputs into a caller-owned temporary root,
selects the AIDK board, and strips the t5ai microphone binding.
"""

from __future__ import annotations

import argparse
from pathlib import Path


PAIR = {
    "cp": "t5ai_core_cp_mcuboot",
    "ap": "t5ai_core_ap_mcuboot",
}
COMPAT = "aidk_ai_toy_mcuboot_ab_v1"
FORBIDDEN = {
    "CONFIG_BK7258_MIC=y",
    "CONFIG_BK7258_AUD=y",
    "CONFIG_BK7258_LCD=y",
    "CONFIG_BK7258_DVP=y",
    "CONFIG_BK7258_T5_BOARD_CAMERA=y",
    "CONFIG_BK7258_T5_BOARD_TF_SLOT=y",
}

CP_CONTRACT = (
    "CONFIG_BK7258_CONSOLE_UART0=y",
    "CONFIG_BK7258_UART0=y",
    "CONFIG_BK7258_UART0_BAUD=115200",
    "CONFIG_BK7258_UART0_DATA_BITS=8",
    "CONFIG_BK7258_UART0_PARITY=0",
    "CONFIG_BK7258_UART0_STOP_BITS=1",
    "# CONFIG_BK7258_UART0_FLOW_CONTROL is not set",
    "# CONFIG_BK7258_SWD_DEBUG is not set",
    "# CONFIG_BK7258_SWD_BOOT_HOLD is not set",
)


def _rewrite_profile(text: str, role: str) -> str:
    lines = text.splitlines()
    fields = {
        "BK7258_PROFILE_BOARD": "aidk_ai_toy",
        "BK7258_PROFILE_ROLE": role,
        "BK7258_PROFILE_BOOT": "mcuboot",
        "BK7258_PROFILE_CLASS": "runnable",
        "BK7258_PROFILE_COMPAT": COMPAT,
    }
    seen: set[str] = set()
    output: list[str] = []
    for line in lines:
        key = line.split("=", 1)[0] if "=" in line else ""
        if key in fields:
            output.append(f"{key}={fields[key]}")
            seen.add(key)
        else:
            output.append(line)
    missing = set(fields) - seen
    if missing:
        raise ValueError(f"seed profile is missing metadata: {sorted(missing)}")
    return "\n".join(output) + "\n"


def _rewrite_defconfig(text: str, role: str) -> str:
    if role not in PAIR:
        raise ValueError(f"unsupported AIDK role: {role}")

    lines = []
    for line in text.splitlines():
        if line in FORBIDDEN:
            continue
        if line in {
            "CONFIG_BK7258_BOARD_AIDK_AI_TOY=y",
            "CONFIG_BK7258_BOARD_T5_BOARD=y",
            "CONFIG_BK7258_BOARD_T5AI_CORE=y",
        }:
            continue
        lines.append(line)

    # Keep the source seed's role and MCUboot settings, but make the physical
    # board choice explicit.  This prevents Kconfig's T5AI default from being
    # inherited by a product that has no verified T5AI peripherals.
    if "CONFIG_BK7258_MCUBOOT_IMAGE=y" not in lines:
        raise ValueError("MCUboot seed does not enable CONFIG_BK7258_MCUBOOT_IMAGE")
    lines.append("CONFIG_BK7258_BOARD_AIDK_AI_TOY=y")
    if role == "cp":
        existing_keys = {
            line.lstrip("# ").split("=", 1)[0].split(" is not set", 1)[0]
            for line in lines
        }
        for setting in CP_CONTRACT:
            key = setting.lstrip("# ").split("=", 1)[0].split(
                " is not set", 1)[0]
            if key not in existing_keys:
                lines.append(setting)
    return "\n".join(lines) + "\n"


def materialize(seed_root: Path, output_root: Path, make_defs: Path) -> Path:
    make_defs = make_defs.resolve()
    if not make_defs.is_file():
        raise ValueError(f"missing canonical board Make.defs: {make_defs}")

    if output_root.exists():
        if any(output_root.iterdir()):
            raise ValueError(f"output root is not empty: {output_root}")
    else:
        output_root.mkdir(parents=True)

    # NuttX configure.sh resolves a custom profile's board Make.defs through
    # <board-root>/configs/<profile>/../../scripts/Make.defs.  Reproduce that
    # standard logical-board layout in the caller-owned temporary directory
    # instead of adding another persistent configs/ tree.
    scripts_root = output_root.parent / "scripts"
    scripts_root.mkdir()
    (scripts_root / "Make.defs").symlink_to(make_defs)

    for role, seed_name in PAIR.items():
        source = seed_root / seed_name
        if not (source / "defconfig").is_file() or not (source / "profile.conf").is_file():
            raise ValueError(f"missing reviewed MCUboot seed: {source}")
        target = output_root / f"aidk_ai_toy_{role}_mcuboot"
        target.mkdir()
        (target / "profile.conf").write_text(
            _rewrite_profile((source / "profile.conf").read_text(encoding="utf-8"), role),
            encoding="utf-8",
        )
        (target / "defconfig").write_text(
            _rewrite_defconfig((source / "defconfig").read_text(encoding="utf-8"), role),
            encoding="utf-8",
        )
    return output_root


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seed-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--make-defs", type=Path, required=True)
    args = parser.parse_args()
    try:
        materialize(
            args.seed_root.resolve(),
            args.output.resolve(),
            args.make_defs.resolve(),
        )
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(f"AIDK MCUboot profiles materialized under {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
