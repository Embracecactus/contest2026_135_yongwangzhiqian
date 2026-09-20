# SPDX-License-Identifier: Apache-2.0

"""Pinned source and configuration applicability for BK7258 kernel wrappers."""

from __future__ import annotations

import hashlib
import json
import re
import stat
import subprocess
from dataclasses import dataclass
from pathlib import Path, PurePosixPath


CONTRACT = Path("chips/bk7258/kernel_compat.json")
HEX = re.compile(r"[0-9a-f]{64}")
FILES = frozenset(
    {
        "arch/arm/src/arm_m/arm_exception.S",
        "arch/arm/src/armv8-m/arm_doirq.c",
        "arch/arm/src/armv8-m/arm_initialstate.c",
        "arch/arm/include/arm_m/irq.h",
        "arch/arm/include/irq.h",
        "sched/sched/sched_switchcontext.c",
        "sched/init/nx_start.c",
        "sched/init/nx_bringup.c",
        "sched/init/nx_smpstart.c",
        "wireless/bluetooth/bt_hcicore.c",
        "wireless/bluetooth/bt_conn.c",
        "wireless/bluetooth/bt_l2cap.c",
        "wireless/bluetooth/bt_att.c",
        "wireless/bluetooth/bt_att.h",
    }
)
WRAPPERS = {
    "__wrap_arm_doirq": ("ap", "cp"),
    "__wrap_nxsched_resume_scheduler": ("ap", "cp"),
    "__wrap_nx_bringup": ("ap",),
    "__wrap_bt_conn_receive": ("ap",),
    "__wrap_bt_l2cap_receive": ("ap",),
}

# Physical validation is a test-state progression, not a source-code fact:
# "pending" records an unverified wrapper and "passed" moves it forward while
# citing its acceptance evidence. Source identity stays protected separately
# by the pinned NuttX commit and the per-file sha256 digests in the contract.
PHYSICAL_VALIDATION_STATES = frozenset({"pending", "passed"})


class KernelCompatError(RuntimeError):
    """The pinned kernel implementation or its applicable config drifted."""


@dataclass(frozen=True)
class Contract:
    commit: str
    files: dict[str, str]


def _digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def _regular(path: Path, label: str) -> Path:
    try:
        mode = path.lstat().st_mode
    except OSError as error:
        raise KernelCompatError(f"missing {label}: {path}") from error
    if stat.S_ISLNK(mode) or not stat.S_ISREG(mode):
        raise KernelCompatError(f"{label} must be a regular non-symlink file: {path}")
    return path.resolve(strict=True)


def load(repository: Path) -> Contract:
    path = _regular(repository / CONTRACT, "kernel compatibility contract")
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (UnicodeError, json.JSONDecodeError) as error:
        raise KernelCompatError(
            f"invalid kernel compatibility contract: {error}"
        ) from error
    if (
        not isinstance(document, dict)
        or set(document) != {"schema", "nuttx", "source_files", "wrappers"}
        or document["schema"] != 1
    ):
        raise KernelCompatError("invalid kernel compatibility contract schema")
    nuttx = document["nuttx"]
    if (
        not isinstance(nuttx, dict)
        or set(nuttx) != {"commit", "provenance"}
        or not isinstance(nuttx["commit"], str)
        or not re.fullmatch(r"[0-9a-f]{40}", nuttx["commit"])
        or not isinstance(nuttx["provenance"], str)
        or not nuttx["provenance"].strip()
    ):
        raise KernelCompatError(
            "invalid NuttX provenance in kernel compatibility contract"
        )
    rows = document["source_files"]
    files: dict[str, str] = {}
    if not isinstance(rows, list):
        raise KernelCompatError("kernel compatibility source_files must be a list")
    for row in rows:
        if not isinstance(row, dict) or set(row) != {"path", "sha256"}:
            raise KernelCompatError("invalid kernel compatibility source file row")
        name, digest = row["path"], row["sha256"]
        pure = PurePosixPath(name) if isinstance(name, str) else None
        if (
            not isinstance(name, str)
            or pure.is_absolute()
            or not pure.parts
            or ".." in pure.parts
            or "." in pure.parts
            or "\\" in name
            or not isinstance(digest, str)
            or HEX.fullmatch(digest) is None
            or name in files
        ):
            raise KernelCompatError(
                "unsafe or malformed kernel compatibility source path"
            )
        files[name] = digest
    if set(files) != FILES:
        raise KernelCompatError(
            "kernel compatibility source file set is not the reviewed set"
        )
    wrappers = document["wrappers"]
    if not isinstance(wrappers, dict) or set(wrappers) != set(WRAPPERS):
        raise KernelCompatError(
            "kernel compatibility wrapper set is not the reviewed set"
        )
    for symbol, roles in WRAPPERS.items():
        row = wrappers[symbol]
        fields = {"disposition", "physical_validation", "roles"}
        if not isinstance(row, dict) or not fields <= set(row):
            raise KernelCompatError(f"invalid reviewed disposition for {symbol}")
        if (
            row["disposition"] != "C"
            or row["physical_validation"] not in PHYSICAL_VALIDATION_STATES
            or row["roles"] != list(roles)
        ):
            raise KernelCompatError(f"invalid reviewed disposition for {symbol}")
        if row["physical_validation"] == "passed":
            evidence = row.get("evidence")
            if (
                not isinstance(evidence, str)
                or not evidence.strip()
                or set(row) - fields - {"evidence"}
            ):
                raise KernelCompatError(
                    f"physical validation passed without evidence for {symbol}"
                )
        elif set(row) != fields:
            raise KernelCompatError(f"invalid reviewed disposition for {symbol}")
    return Contract(nuttx["commit"], files)


def verify_sources(
    repository: Path, nuttx: Path, *, provenance_root: Path | None = None
) -> Contract:
    """Verify copied build sources and their canonical Git provenance separately."""

    contract = load(repository)
    source_root = nuttx.resolve(strict=True)
    provenance = (
        source_root if provenance_root is None else provenance_root.resolve(strict=True)
    )
    try:
        actual_commit = subprocess.run(
            ["git", "-C", str(provenance), "rev-parse", "HEAD"],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError) as error:
        raise KernelCompatError(
            f"cannot read NuttX Git provenance: {provenance}"
        ) from error
    if actual_commit != contract.commit:
        raise KernelCompatError(
            f"NuttX compatibility drift: commit {actual_commit} does not match "
            f"reviewed {contract.commit}; review and update {CONTRACT} explicitly"
        )
    roots = (("workspace build source", source_root),)
    if provenance != source_root:
        roots += (("canonical provenance source", provenance),)
    for label, root in roots:
        for relative, expected in sorted(contract.files.items()):
            source = root.joinpath(*PurePosixPath(relative).parts)
            source = _regular(source, f"{label} {relative}")
            try:
                source.relative_to(root)
            except (OSError, ValueError) as error:
                raise KernelCompatError(f"{label} escapes NuttX: {relative}") from error
            actual = _digest(source)
            if actual != expected:
                raise KernelCompatError(
                    f"NuttX compatibility drift in {label}: {relative} sha256={actual}, "
                    f"expected {expected}; review and update {CONTRACT} explicitly"
                )
    return contract


def _config(path: Path) -> dict[str, str | None]:
    values: dict[str, str | None] = {}
    for line in (
        _regular(path, "resolved NuttX config").read_text(encoding="utf-8").splitlines()
    ):
        if line.startswith("CONFIG_") and "=" in line:
            key, value = line.split("=", 1)
            values[key] = value
        else:
            match = re.fullmatch(r"# (CONFIG_[A-Z0-9_]+) is not set", line)
            if match:
                values[match.group(1)] = None
    return values


def verify_role_config(role: str, dotconfig: Path) -> None:
    """Check only configurations that compile one of the retained wrappers."""

    if role not in {"ap", "cp"}:
        raise KernelCompatError(f"invalid kernel compatibility role: {role}")
    values = _config(dotconfig)
    enabled = lambda name: values.get(name) == "y"
    smp_wrapper = enabled("CONFIG_BK7258_AP_SMP_SCHED_ONLINE")
    bt_wrapper = any(
        enabled(name)
        for name in (
            "CONFIG_BK7258_BT_CONN_RX_REF_COMPAT",
            "CONFIG_BK7258_BT_ATT_MTU_COMPAT",
        )
    )
    wrappers_active = role == "cp" or smp_wrapper or bt_wrapper
    if not wrappers_active:
        return
    missing = [
        name
        for name in (
            "CONFIG_BUILD_FLAT",
            "CONFIG_ARCH_ARMV8M",
            "CONFIG_ARCH_CHIP_BK7258",
            "CONFIG_LTO_NONE",
        )
        if not enabled(name)
    ]
    if missing:
        raise KernelCompatError(
            "kernel wrappers require resolved config: " + ", ".join(missing)
        )
    if enabled("CONFIG_LTO_FULL"):
        raise KernelCompatError("kernel wrappers require CONFIG_LTO_FULL disabled")
    if enabled("CONFIG_ARCH_HIPRI_INTERRUPT"):
        raise KernelCompatError(
            "kernel wrappers require CONFIG_ARCH_HIPRI_INTERRUPT disabled"
        )
    if role == "cp" and enabled("CONFIG_SMP"):
        raise KernelCompatError("CP kernel wrappers require non-SMP resolved config")
    if role == "ap" and smp_wrapper and not enabled("CONFIG_SMP"):
        raise KernelCompatError("AP SMP_ONLINE wrappers require CONFIG_SMP=y")
