#!/usr/bin/env python3
"""Generate all BK7258 partition consumers from one SDK-compatible CSV.

The input deliberately keeps the first six columns compatible with the exact
Beken v3.1.1.9 ``auto_partitions.csv`` parser.  The seventh ``Role`` column is
project-owned semantic metadata; the pinned SDK parser ignores extra columns.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


SCRIPT_DIR = Path(__file__).resolve().parent
# 形态无关地解析仓库根 / 板级目录（P1 路径层），
# 替代 ``SCRIPT_DIR.parent`` 的脆弱推导（迁移到 tools/bk7258 后语义已变）。
try:
    from bk7258_paths import Bk7258Layout
except ModuleNotFoundError:
    sys.path.insert(0, str(SCRIPT_DIR.parent.parent.parent / "tools" / "bk7258"))
    from bk7258_paths import Bk7258Layout
_LAYOUT = Bk7258Layout()
BOARD_DIR = _LAYOUT.board_dir
REPOSITORY_ROOT = _LAYOUT.contest_root
LEGACY_INPUT = BOARD_DIR / "partitions/bk7258/auto_partitions.csv"


def _environment_input() -> Path:
    """Resolve the active input exported by the product build adapter.

    The absent-variable path intentionally preserves the historical standalone
    CLI behavior.  Product builds export an absolute path; accepting a
    repository-relative value as well keeps hand-written host invocations
    deterministic regardless of their current directory.
    """

    value = os.environ.get("BK7258_PARTITION_LAYOUT_SOURCE")
    if not value:
        return LEGACY_INPUT
    path = Path(value)
    return path if path.is_absolute() else REPOSITORY_ROOT / path


DEFAULT_INPUT = _environment_input()
DEFAULT_OUTPUT_DIR = BOARD_DIR / "partitions/generated"
DEFAULT_HEADER = BOARD_DIR / "include/bk7258_partition_layout.h"
SDK_RELEASE = "v3.1.1.9"
SDK_DIRECTORY = "bk_avdk_smp-release-v3.1.1.9"
SDK_REFERENCE_CSV = Path("projects/app_ab/partitions/bk7258/auto_partitions.csv")
SDK_POSITION_CSV = Path(
    "projects/app_ab/partitions/bk7258/ab_position_independent.csv"
)
SDK_PARTITION_SETTING = Path(
    "tools/build_tools/build_process/bk_sdk/smp_flash_partitions_setting.json"
)

# v3.1.1.9 keeps these IDs stable even when a project CSV omits one of the
# reserved rows.  The shipped CP archives were compiled against this ABI.
# Project-specific rows are assigned monotonically after the reserved range,
# exactly as bk_partitions_table.sort_partitions() does in the official SDK.
SDK_INTERNAL_PARTITIONS = (
    "primary_bootloader",
    "primary_cp_app",
    "primary_ap_app",
    "sys_rf",
    "sys_net",
    "ota",
    "usr_config",
    "easyflash",
    "easyflash_ap",
)
SDK_INTERNAL_IDS = {
    name: index for index, name in enumerate(SDK_INTERNAL_PARTITIONS)
}

REQUIRED_ROLES = frozenset(
    {
        "boot",
        "slot_a_cp",
        "slot_a_ap",
        "slot_b_pair",
        "vendor_config",
        "bl1_primary_manifest",
        "bl1_secondary_manifest",
        "bl2",
        "littlefs",
        "easyflash_cp",
        "easyflash_ap",
        "calibration_rf",
        "calibration_net",
    }
)
SECUREBOOT_XIP_LAYOUT = "bk7258-secureboot-xip-cp-ap"
SECUREBOOT_XIP_REQUIRED_ROLES = frozenset(
    {
        "bl1_control",
        "bl1_primary_manifest",
        "bl1_secondary_manifest",
        "bl1_primary_bl2",
        "bl1_secondary_bl2",
        "primary_cp_app",
        "primary_ap_app",
        "secondary_cp_app",
        "secondary_ap_app",
        "vendor_config",
        "ota_scratch",
        "littlefs",
        "easyflash_cp",
        "easyflash_ap",
        "calibration_rf",
        "calibration_net",
    }
)
OFFICIAL_REFERENCE_NAMES = (
    "primary_bootloader",
    "primary_cp_app",
    "primary_ap_app",
    "s_app",
    "usr_config",
    "easyflash",
    "easyflash_ap",
    "sys_rf",
    "sys_net",
)


class PartitionLayoutError(RuntimeError):
    """Raised when the CSV or one of its generated artifacts is unsafe."""


def parse_size(value: str) -> int:
    """Parse the size syntax accepted by the pinned SDK (hex, K, or M)."""

    text = value.strip().lower()
    match = re.fullmatch(r"(0x[0-9a-f]+|[0-9]+)\s*([kmg](?:i?b)?|b)?", text)
    if match is None:
        raise PartitionLayoutError(f"invalid size: {value!r}")
    number = int(match.group(1), 0)
    suffix = match.group(2) or "b"
    multiplier = {
        "b": 1,
        "k": 1024,
        "kb": 1024,
        "kib": 1024,
        "m": 1024 * 1024,
        "mb": 1024 * 1024,
        "mib": 1024 * 1024,
        "g": 1024 * 1024 * 1024,
        "gb": 1024 * 1024 * 1024,
        "gib": 1024 * 1024 * 1024,
    }[suffix]
    return number * multiplier


def parse_bool(value: str, label: str) -> bool:
    normalized = value.strip().upper()
    if normalized not in {"TRUE", "FALSE"}:
        raise PartitionLayoutError(f"{label} must be TRUE or FALSE")
    return normalized == "TRUE"


def macro_name(value: str) -> str:
    return re.sub(r"[^A-Z0-9]+", "_", value.upper()).strip("_")


def format_sdk_size(value: int) -> str:
    """Use the decimal/K/M grammar implemented by SDK v3.1.1.9."""

    for unit, suffix in ((1024 * 1024, "M"), (1024, "K")):
        if value % unit == 0:
            return f"{value // unit}{suffix}"
    return str(value)


@dataclass(frozen=True)
class Partition:
    index: int
    name: str
    offset: int
    size: int
    kind: str
    readable: bool
    writable: bool
    role: str
    offset_is_auto: bool

    @property
    def end(self) -> int:
        return self.offset + self.size

    @property
    def executable(self) -> bool:
        return self.kind == "code"

    def canonical(self) -> dict[str, object]:
        return {
            "index": self.index,
            "name": self.name,
            "offset": self.offset,
            "size": self.size,
            "type": self.kind,
            "read": self.readable,
            "write": self.writable,
            "role": self.role,
        }

    def sdk_row(self) -> str:
        return ",".join(
            (
                self.name,
                f"0x{self.offset:x}",
                format_sdk_size(self.size),
                self.kind,
                str(self.readable).upper(),
                str(self.writable).upper(),
            )
        )


@dataclass(frozen=True)
class Gap:
    offset: int
    size: int

    @property
    def end(self) -> int:
        return self.offset + self.size


@dataclass(frozen=True)
class SdkPartition:
    """One partition after the official v3.1.1.9 ABI adapter."""

    partition: Partition
    sdk_id: int
    sdk_name: str


@dataclass(frozen=True)
class PartitionLayout:
    source: Path
    sdk_release: str
    layout_name: str
    flash_size: int
    erase_size: int
    crc_data_size: int
    crc_total_size: int
    xip_base: int
    partitions: tuple[Partition, ...]
    gaps: tuple[Gap, ...]
    layout_sha256: str
    layout_id: str

    @property
    def uses_sdk_crc_translation(self) -> bool:
        """Whether this profile uses the legacy v3 application CRC mapping.

        The normal project image is linked and packed as 32 bytes of payload
        followed by two CRC bytes.  A Beken secureboot image is placed by the
        official BL1/BL2 packer, which owns AES, CRC and padding for every
        image in the verified group.  Keep it out of the legacy address
        calculation until that packer is integrated.
        """

        return self.layout_name != SECUREBOOT_XIP_LAYOUT

    def by_name(self, name: str) -> Partition:
        for partition in self.partitions:
            if partition.name == name:
                return partition
        raise PartitionLayoutError(f"partition name is absent: {name}")

    def by_role(self, role: str) -> Partition:
        for partition in self.partitions:
            if partition.role == role:
                return partition
        raise PartitionLayoutError(f"partition role is absent: {role}")

    def sdk_partitions(self) -> tuple[SdkPartition, ...]:
        user_id = len(SDK_INTERNAL_PARTITIONS)
        application_index = 0
        result: list[SdkPartition] = []
        for partition in self.partitions:
            sdk_id = SDK_INTERNAL_IDS.get(partition.name)
            if sdk_id is None:
                sdk_id = user_id
                user_id += 1

            if partition.executable:
                if "bootloader" in partition.name:
                    sdk_name = "bootloader"
                else:
                    sdk_name = "application" + (
                        str(application_index) if application_index else ""
                    )
                    application_index += 1
            else:
                sdk_name = partition.name
            result.append(SdkPartition(partition, sdk_id, sdk_name))

        return tuple(sorted(result, key=lambda item: item.sdk_id))

    def sdk_partition(self, partition: Partition) -> SdkPartition:
        for sdk_partition in self.sdk_partitions():
            if sdk_partition.partition is partition:
                return sdk_partition
        raise PartitionLayoutError(f"SDK partition mapping is absent: {partition.name}")

    def logical_offset(self, partition: Partition) -> int:
        if not partition.executable:
            raise PartitionLayoutError(f"{partition.name} is not executable")
        return partition.offset // self.crc_total_size * self.crc_data_size

    def logical_size(self, partition: Partition) -> int:
        if not partition.executable:
            raise PartitionLayoutError(f"{partition.name} is not executable")
        return partition.size // self.crc_total_size * self.crc_data_size

    def partition_report(self, partition: Partition) -> dict[str, object]:
        sdk_partition = self.sdk_partition(partition)
        result = partition.canonical()
        result.update(
            {
                "offset_hex": f"0x{partition.offset:06x}",
                "end": partition.end,
                "end_hex": f"0x{partition.end:06x}",
                "size_hex": f"0x{partition.size:x}",
                "offset_source": "auto" if partition.offset_is_auto else "explicit",
                "sdk_id": sdk_partition.sdk_id,
                "sdk_name": sdk_partition.sdk_name,
            }
        )
        if partition.executable and self.uses_sdk_crc_translation:
            logical_offset = self.logical_offset(partition)
            logical_size = self.logical_size(partition)
            result.update(
                {
                    "logical_offset": logical_offset,
                    "logical_size": logical_size,
                    "xip_start": self.xip_base + logical_offset,
                    "xip_end": self.xip_base + logical_offset + logical_size,
                }
            )
        return result

    def report(self) -> dict[str, object]:
        try:
            source_label = self.source.relative_to(REPOSITORY_ROOT).as_posix()
        except ValueError:
            source_label = str(self.source)
        return {
            "format": 1,
            "source": source_label,
            "sdk_release": self.sdk_release,
            "layout_name": self.layout_name,
            "layout_id": self.layout_id,
            "layout_sha256": self.layout_sha256,
            "flash_size": self.flash_size,
            "erase_size": self.erase_size,
            "crc": {
                "data_size": self.crc_data_size,
                "physical_size": self.crc_total_size,
            },
            "xip_base": self.xip_base,
            "partitions": [self.partition_report(part) for part in self.partitions],
            "gaps": [
                {
                    "offset": gap.offset,
                    "offset_hex": f"0x{gap.offset:06x}",
                    "size": gap.size,
                    "size_hex": f"0x{gap.size:x}",
                    "end": gap.end,
                    "end_hex": f"0x{gap.end:06x}",
                }
                for gap in self.gaps
            ],
        }


def _read_directives_and_rows(path: Path) -> tuple[dict[str, str], list[list[str]]]:
    directives: dict[str, str] = {}
    rows: list[list[str]] = []
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise PartitionLayoutError(f"cannot read partition CSV: {error}") from error

    for line_number, raw_line in enumerate(lines, start=1):
        line = raw_line.strip()
        if not line:
            continue
        if line.startswith("#"):
            match = re.fullmatch(r"#\s*([A-Z][A-Z0-9_]*)=(.+)", line)
            if match is not None:
                directives[match.group(1)] = match.group(2).strip()
            continue
        if line.startswith("FLASH_CAPACITY="):
            directives["FLASH_CAPACITY"] = line.split("=", 1)[1].strip()
            continue
        fields = next(csv.reader([raw_line]))
        if len(fields) != 7:
            raise PartitionLayoutError(
                f"{path}:{line_number}: expected seven partition fields, got {len(fields)}"
            )
        rows.append([field.strip() for field in fields])
    return directives, rows


def _required_directive(directives: dict[str, str], name: str) -> str:
    try:
        return directives[name]
    except KeyError as error:
        raise PartitionLayoutError(f"missing CSV directive: {name}") from error


def _build_gaps(partitions: Iterable[Partition], flash_size: int) -> tuple[Gap, ...]:
    gaps: list[Gap] = []
    cursor = 0
    for partition in sorted(partitions, key=lambda item: item.offset):
        if partition.offset > cursor:
            gaps.append(Gap(cursor, partition.offset - cursor))
        cursor = partition.end
    if cursor < flash_size:
        gaps.append(Gap(cursor, flash_size - cursor))
    return tuple(gaps)


def _validate_layout(layout: PartitionLayout) -> None:
    if layout.sdk_release != SDK_RELEASE:
        raise PartitionLayoutError(
            f"only official SDK {SDK_RELEASE} is allowed, got {layout.sdk_release}"
        )
    if layout.flash_size != 0x800000:
        raise PartitionLayoutError("BK7258 project policy requires an 8 MiB Flash")
    if layout.erase_size <= 0 or layout.erase_size & (layout.erase_size - 1):
        raise PartitionLayoutError("erase size must be a positive power of two")
    if layout.crc_data_size != 32 or layout.crc_total_size != 34:
        raise PartitionLayoutError("BK7258 executable Flash must use the 32+2 CRC format")

    names: set[str] = set()
    roles: set[str] = set()
    previous_end = 0
    executable_alignment = 1024 * layout.crc_total_size
    for partition in layout.partitions:
        if partition.name in names:
            raise PartitionLayoutError(f"duplicate partition name: {partition.name}")
        if partition.role in roles:
            raise PartitionLayoutError(f"duplicate partition role: {partition.role}")
        names.add(partition.name)
        roles.add(partition.role)
        if partition.offset < previous_end:
            raise PartitionLayoutError(f"partition overlap at {partition.name}")
        if partition.offset % layout.erase_size or partition.size % layout.erase_size:
            raise PartitionLayoutError(
                f"{partition.name} is not {layout.erase_size:#x}-aligned"
            )
        if partition.size <= 0 or partition.end > layout.flash_size:
            raise PartitionLayoutError(f"{partition.name} is outside Flash")
        if layout.uses_sdk_crc_translation and partition.executable and (
            partition.offset % executable_alignment
            or partition.size % executable_alignment
        ):
            raise PartitionLayoutError(
                f"{partition.name} violates the SDK 34 KiB executable alignment"
            )
        previous_end = partition.end

    if layout.layout_name == SECUREBOOT_XIP_LAYOUT:
        _validate_secureboot_xip_layout(layout)
        return

    missing_roles = REQUIRED_ROLES - roles
    if missing_roles:
        raise PartitionLayoutError(
            "missing required partition roles: " + ", ".join(sorted(missing_roles))
        )

    boot = layout.by_role("boot")
    cp = layout.by_role("slot_a_cp")
    ap = layout.by_role("slot_a_ap")
    slot_b = layout.by_role("slot_b_pair")
    usr_config = layout.by_role("vendor_config")
    manifest_a = layout.by_role("bl1_primary_manifest")
    manifest_b = layout.by_role("bl1_secondary_manifest")
    bl2 = layout.by_role("bl2")
    littlefs = layout.by_role("littlefs")
    easyflash = layout.by_role("easyflash_cp")
    easyflash_ap = layout.by_role("easyflash_ap")
    sys_rf = layout.by_role("calibration_rf")
    sys_net = layout.by_role("calibration_net")

    chain = (
        boot,
        cp,
        ap,
        slot_b,
    )
    if boot.offset != 0:
        raise PartitionLayoutError("boot partition must start at raw offset zero")
    for left, right in zip(chain, chain[1:]):
        if left.end != right.offset:
            raise PartitionLayoutError(
                f"required contiguous boundary drift: {left.name} -> {right.name}"
            )
    if cp.size + ap.size != slot_b.size:
        raise PartitionLayoutError("slot B must equal the combined CP/AP slot A span")
    if (
        manifest_a.size != layout.erase_size
        or manifest_b.size != layout.erase_size
    ):
        raise PartitionLayoutError("each signed Manifest must occupy exactly one sector")
    if (
        not manifest_a.readable
        or manifest_a.writable
        or not manifest_b.readable
        or manifest_b.writable
    ):
        raise PartitionLayoutError(
            "BL1 Manifest sectors must be readable and runtime read-only"
        )
    if usr_config.offset != 0x4FC000 or usr_config.size != 56 * 1024:
        raise PartitionLayoutError("vendor usr_config envelope changed")
    if manifest_a.offset != 0x50B000 or manifest_b.offset != manifest_a.end:
        raise PartitionLayoutError("BL1 Manifest sector placement changed")
    if bl2.offset < manifest_b.end or bl2.end > littlefs.offset:
        raise PartitionLayoutError("BL2 must occupy the pre-LittleFS spare gap")
    if layout.logical_size(bl2) < 0x20000:
        raise PartitionLayoutError("BL2 requires at least 128 KiB logical space")
    if littlefs.end > easyflash.offset:
        raise PartitionLayoutError("LittleFS reaches the immutable vendor tail")
    if easyflash.offset != 0x7FA000:
        raise PartitionLayoutError("official calibration tail must start at 0x7fa000")
    tail = (easyflash, easyflash_ap, sys_rf, sys_net)
    for left, right in zip(tail, tail[1:]):
        if left.end != right.offset:
            raise PartitionLayoutError(
                f"official tail is not contiguous: {left.name} -> {right.name}"
            )
    if sys_net.end != layout.flash_size:
        raise PartitionLayoutError("official calibration tail must end at Flash end")

    sdk_partitions = layout.sdk_partitions()
    sdk_ids = [partition.sdk_id for partition in sdk_partitions]
    sdk_names = [partition.sdk_name for partition in sdk_partitions]
    if len(sdk_ids) != len(set(sdk_ids)):
        raise PartitionLayoutError("SDK partition IDs are not unique")
    if len(sdk_names) != len(set(sdk_names)):
        raise PartitionLayoutError("SDK partition names are not unique")
    if not sdk_ids or sdk_ids[-1] >= 32:
        raise PartitionLayoutError("SDK partition wrapper supports IDs 0..31")


def _validate_secureboot_xip_layout(layout: PartitionLayout) -> None:
    """Validate the project-owned BK7258 secureboot XIP staging profile.

    The ordinary NuttX build remains on ``bk7258-contiguous-ab``.  This
    profile instead follows Beken's BL1 -> BL2 (MCUboot) ->
    primary_all/secondary_all convention.  The official secure packer is the
    sole authority for final AES, CRC, padding and signed-image placement.
    """

    roles = {partition.role for partition in layout.partitions}
    missing_roles = SECUREBOOT_XIP_REQUIRED_ROLES - roles
    if missing_roles:
        raise PartitionLayoutError(
            "missing secureboot XIP roles: " + ", ".join(sorted(missing_roles))
        )

    ordered = (
        "bl1_control",
        "bl1_primary_manifest",
        "bl1_secondary_manifest",
        "bl1_primary_bl2",
        "bl1_secondary_bl2",
        "primary_cp_app",
        "primary_ap_app",
        "secondary_cp_app",
        "secondary_ap_app",
    )
    chain = tuple(layout.by_role(role) for role in ordered)
    if chain[0].offset != 0:
        raise PartitionLayoutError("secureboot bl1_control must start at raw offset zero")
    for left, right in zip(chain, chain[1:]):
        if left.end != right.offset:
            raise PartitionLayoutError(
                f"secureboot required contiguous boundary drift: {left.name} -> {right.name}"
            )

    control = layout.by_role("bl1_control")
    primary_manifest = layout.by_role("bl1_primary_manifest")
    secondary_manifest = layout.by_role("bl1_secondary_manifest")
    primary_bl2 = layout.by_role("bl1_primary_bl2")
    secondary_bl2 = layout.by_role("bl1_secondary_bl2")
    if control.name != "bl1_control":
        raise PartitionLayoutError(
            "secureboot BL1 control partition must use the official bl1_control name"
        )
    if control.size != 12 * 1024:
        raise PartitionLayoutError(
            "secureboot bl1_control must contain the three official 4 KiB control pages"
        )
    if primary_manifest.name != "primary_manifest":
        raise PartitionLayoutError(
            "secureboot primary manifest must use the official primary_manifest name"
        )
    if secondary_manifest.name != "secondary_manifest":
        raise PartitionLayoutError(
            "secureboot secondary manifest must use the official secondary_manifest name"
        )
    if primary_bl2.name != "primary_bl2" or secondary_bl2.name != "secondary_bl2":
        raise PartitionLayoutError(
            "secureboot BL2 slots must use the official primary_bl2/secondary_bl2 names"
        )
    if primary_manifest.size != 4 * 1024 or secondary_manifest.size != 4 * 1024:
        raise PartitionLayoutError("secureboot Manifest partitions must be exactly 4 KiB")
    if primary_bl2.size != secondary_bl2.size:
        raise PartitionLayoutError("secureboot primary/secondary BL2 sizes differ")
    if not primary_bl2.executable or not secondary_bl2.executable:
        raise PartitionLayoutError("secureboot BL2 slots must be executable")

    primary = (
        layout.by_role("primary_cp_app"),
        layout.by_role("primary_ap_app"),
    )
    secondary = (
        layout.by_role("secondary_cp_app"),
        layout.by_role("secondary_ap_app"),
    )
    for left, right in zip(primary, secondary):
        if left.size != right.size:
            raise PartitionLayoutError(
                f"secureboot paired image size drift: {left.name} != {right.name}"
            )
    if any(not partition.executable for partition in (*primary, *secondary)):
        raise PartitionLayoutError("secureboot primary/secondary image members must be code")

    littlefs = layout.by_role("littlefs")
    ota_scratch = layout.by_role("ota_scratch")
    if chain[-1].end > littlefs.offset:
        raise PartitionLayoutError("secureboot image groups overlap LittleFS")
    if ota_scratch.end > littlefs.offset:
        raise PartitionLayoutError("secureboot OTA scratch overlaps LittleFS")
    tail = (
        layout.by_role("easyflash_cp"),
        layout.by_role("easyflash_ap"),
        layout.by_role("calibration_rf"),
        layout.by_role("calibration_net"),
    )
    if littlefs.end > tail[0].offset:
        raise PartitionLayoutError("LittleFS reaches the immutable vendor tail")
    if tail[0].offset != 0x7FA000:
        raise PartitionLayoutError("official calibration tail must start at 0x7fa000")
    for left, right in zip(tail, tail[1:]):
        if left.end != right.offset:
            raise PartitionLayoutError(
                f"official tail is not contiguous: {left.name} -> {right.name}"
            )
    if tail[-1].end != layout.flash_size:
        raise PartitionLayoutError("official calibration tail must end at Flash end")


def load_layout(path: Path = DEFAULT_INPUT) -> PartitionLayout:
    directives, rows = _read_directives_and_rows(path)
    sdk_release = _required_directive(directives, "SDK_RELEASE")
    layout_name = _required_directive(directives, "LAYOUT_NAME")
    flash_size = parse_size(_required_directive(directives, "FLASH_CAPACITY"))
    erase_size = parse_size(_required_directive(directives, "ERASE_SIZE"))
    crc_data_size = parse_size(_required_directive(directives, "CRC_DATA_SIZE"))
    crc_total_size = parse_size(_required_directive(directives, "CRC_TOTAL_SIZE"))
    xip_base = int(_required_directive(directives, "XIP_BASE"), 0)

    partitions: list[Partition] = []
    cursor = 0
    valid_name = re.compile(r"[a-z][a-z0-9_]*\Z")
    for index, fields in enumerate(rows):
        name, offset_text, size_text, kind_text, read_text, write_text, role = fields
        if valid_name.fullmatch(name) is None:
            raise PartitionLayoutError(f"invalid partition name: {name!r}")
        if valid_name.fullmatch(role) is None:
            raise PartitionLayoutError(f"invalid partition role: {role!r}")
        kind = kind_text.lower()
        if kind not in {"code", "data"}:
            raise PartitionLayoutError(f"unsupported partition type: {kind_text!r}")
        offset_is_auto = offset_text == ""
        offset = cursor if offset_is_auto else int(offset_text, 0)
        size = parse_size(size_text)
        partition = Partition(
            index=index,
            name=name,
            offset=offset,
            size=size,
            kind=kind,
            readable=parse_bool(read_text, f"{name}.Read"),
            writable=parse_bool(write_text, f"{name}.Write"),
            role=role,
            offset_is_auto=offset_is_auto,
        )
        partitions.append(partition)
        cursor = partition.end

    canonical = {
        "format": 1,
        "sdk_release": sdk_release,
        "layout_name": layout_name,
        "flash_size": flash_size,
        "erase_size": erase_size,
        "crc_data_size": crc_data_size,
        "crc_total_size": crc_total_size,
        "xip_base": xip_base,
        "partitions": [partition.canonical() for partition in partitions],
    }
    canonical_bytes = json.dumps(
        canonical, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    digest = hashlib.sha256(canonical_bytes).hexdigest()
    layout = PartitionLayout(
        source=path.resolve(),
        sdk_release=sdk_release,
        layout_name=layout_name,
        flash_size=flash_size,
        erase_size=erase_size,
        crc_data_size=crc_data_size,
        crc_total_size=crc_total_size,
        xip_base=xip_base,
        partitions=tuple(partitions),
        gaps=_build_gaps(partitions, flash_size),
        layout_sha256=digest,
        layout_id=(
            f"bk7258-v3119-"
            f"{'secureboot-xip' if layout_name == SECUREBOOT_XIP_LAYOUT else 'ab'}-"
            f"{digest[:16]}"
        ),
    )
    _validate_layout(layout)
    return layout


def render_sdk_csv(layout: PartitionLayout) -> str:
    lines = [
        "# Generated from the repository-owned BK7258 partition source.",
        "# Name,Offset,Size,Type,Read,Write",
        f"FLASH_CAPACITY={layout.flash_size // (1024 * 1024)}M",
    ]
    lines.extend(partition.sdk_row() for partition in layout.partitions)
    return "\n".join(lines) + "\n"


def render_header(layout: PartitionLayout) -> str:
    sdk_partitions = layout.sdk_partitions()
    sdk_table_size = sdk_partitions[-1].sdk_id + 1
    sdk_valid_mask = sum(1 << partition.sdk_id for partition in sdk_partitions)
    layout_digest = bytes.fromhex(layout.layout_sha256)
    layout_digest_bytes = ", ".join(f"0x{value:02x}" for value in layout_digest)
    lines = [
        "/* Auto-generated by gen_bk7258_partitions.py. Do not edit. */",
        "#ifndef __BK7258_PARTITION_LAYOUT_H",
        "#define __BK7258_PARTITION_LAYOUT_H",
        "",
        "#ifndef BK7258_FLASH_XIP_BASE",
        '#  error "Include the BK7258 SoC memory map before the partition contract"',
        "#endif",
        "",
        f"#if BK7258_FLASH_CRC_DATA_SIZE != {layout.crc_data_size}u",
        '#  error "BK7258 partition CRC data geometry does not match the SoC"',
        "#endif",
        f"#if BK7258_FLASH_CRC_TOTAL_SIZE != {layout.crc_total_size}u",
        '#  error "BK7258 partition CRC total geometry does not match the SoC"',
        "#endif",
        f"#if BK7258_FLASH_XIP_BASE != 0x{layout.xip_base:08x}u",
        '#  error "BK7258 partition XIP base does not match the SoC"',
        "#endif",
        "",
        f'#define BK7258_PARTITION_LAYOUT_ID "{layout.layout_id}"',
        f'#define BK7258_PARTITION_LAYOUT_SHA256 "{layout.layout_sha256}"',
        f"#define BK7258_PARTITION_LAYOUT_SHA256_BYTES {{{layout_digest_bytes}}}",
        f"#define BK7258_FLASH_SIZE 0x{layout.flash_size:08x}",
        f"#define BK7258_FLASH_ERASE_SIZE 0x{layout.erase_size:08x}",
        f"#define BK7258_PARTITION_COUNT {len(layout.partitions)}",
        f"#define BK7258_SDK_PARTITIONS_TABLE_SIZE {sdk_table_size}",
        f"#define BK7258_SDK_PARTITION_VALID_MASK 0x{sdk_valid_mask:08x}",
        "",
    ]
    for partition in layout.partitions:
        name = macro_name(partition.name)
        role = macro_name(partition.role)
        sdk_partition = layout.sdk_partition(partition)
        lines.extend(
            (
                f"#define BK7258_PARTITION_{name}_SDK_ID {sdk_partition.sdk_id}",
                f"#define BK7258_ROLE_{role}_SDK_ID {sdk_partition.sdk_id}",
                f"#define BK7258_PARTITION_{name}_EXECUTABLE {int(partition.executable)}",
                f"#define BK7258_PARTITION_{name}_READABLE {int(partition.readable)}",
                f"#define BK7258_PARTITION_{name}_WRITABLE {int(partition.writable)}",
            )
        )
        values = (
            ("OFFSET", partition.offset),
            ("SIZE", partition.size),
            ("END", partition.end),
        )
        for suffix, value in values:
            lines.append(
                f"#define BK7258_PARTITION_{name}_{suffix} 0x{value:08x}"
            )
            lines.append(f"#define BK7258_ROLE_{role}_{suffix} 0x{value:08x}")
        if partition.executable and layout.uses_sdk_crc_translation:
            logical_offset = layout.logical_offset(partition)
            logical_size = layout.logical_size(partition)
            lines.extend(
                (
                    f"#define BK7258_ROLE_{role}_LOGICAL_OFFSET 0x{logical_offset:08x}",
                    f"#define BK7258_ROLE_{role}_LOGICAL_SIZE 0x{logical_size:08x}",
                    f"#define BK7258_ROLE_{role}_XIP_START 0x{layout.xip_base + logical_offset:08x}",
                    f"#define BK7258_ROLE_{role}_XIP_END 0x{layout.xip_base + logical_offset + logical_size:08x}",
                )
            )
        lines.append("")

    lines.append("#define BK7258_SDK_PARTITION_FOREACH(_) \\")
    for index, sdk_partition in enumerate(sdk_partitions):
        partition = sdk_partition.partition
        continuation = " \\" if index + 1 < len(sdk_partitions) else ""
        lines.append(
            "  _("
            f"{sdk_partition.sdk_id}, \"{sdk_partition.sdk_name}\", "
            f"0x{partition.offset:08x}, 0x{partition.size:08x}, "
            f"{int(partition.executable)}, {int(partition.readable)}, "
            f"{int(partition.writable)}){continuation}"
        )
    lines.append("")
    lines.extend(("#endif /* __BK7258_PARTITION_LAYOUT_H */", ""))
    return "\n".join(lines)


def render_text(layout: PartitionLayout) -> str:
    lines = [
        f"layout_id: {layout.layout_id}",
        f"layout_sha256: {layout.layout_sha256}",
        f"sdk_release: {layout.sdk_release}",
        "",
        "kind       offset     end        size       name / role",
        "---------- ---------- ---------- ---------- ------------------------------",
    ]
    gaps = {gap.offset: gap for gap in layout.gaps}
    partition_by_offset = {part.offset: part for part in layout.partitions}
    for offset in sorted(set(gaps) | set(partition_by_offset)):
        if offset in gaps:
            gap = gaps[offset]
            lines.append(
                f"unused     0x{gap.offset:06x}   0x{gap.end:06x}   0x{gap.size:06x}   -"
            )
        if offset in partition_by_offset:
            part = partition_by_offset[offset]
            lines.append(
                f"{part.kind:<10} 0x{part.offset:06x}   0x{part.end:06x}   "
                f"0x{part.size:06x}   {part.name} / {part.role}"
            )
    return "\n".join(lines) + "\n"


def generated_contents(layout: PartitionLayout) -> dict[str, str]:
    return {
        "auto_partitions.sdk.csv": render_sdk_csv(layout),
        "bk7258_partition_layout.h": render_header(layout),
        "bk7258_partition_layout.json": json.dumps(
            layout.report(), indent=2, sort_keys=True
        )
        + "\n",
        "bk7258_partition_layout.txt": render_text(layout),
    }


def query_layout(layout: PartitionLayout, query: str) -> str:
    """Return one stable scalar for shell/build wrappers."""

    if query == "layout_id":
        return layout.layout_id
    if query == "layout_sha256":
        return layout.layout_sha256
    if query == "flash_size":
        return f"0x{layout.flash_size:x}"
    if query == "erase_size":
        return f"0x{layout.erase_size:x}"

    try:
        role, field = query.split(".", 1)
    except ValueError as error:
        raise PartitionLayoutError(f"invalid layout query: {query!r}") from error
    partition = layout.by_role(role)
    values = {
        "offset": partition.offset,
        "size": partition.size,
        "end": partition.end,
    }
    if partition.executable and layout.uses_sdk_crc_translation:
        logical_offset = layout.logical_offset(partition)
        logical_size = layout.logical_size(partition)
        values.update(
            {
                "logical_offset": logical_offset,
                "logical_size": logical_size,
                "xip_start": layout.xip_base + logical_offset,
                "xip_end": layout.xip_base + logical_offset + logical_size,
            }
        )
    if field not in values:
        raise PartitionLayoutError(f"unsupported layout query: {query!r}")
    return f"0x{values[field]:x}"


def sync_generated(
    layout: PartitionLayout, output_dir: Path, check_only: bool,
    header_path: Path = DEFAULT_HEADER,
) -> tuple[str, ...]:
    expected = generated_contents(layout)
    drift: list[str] = []
    for name, content in expected.items():
        path = header_path if name == "bk7258_partition_layout.h" else output_dir / name
        if check_only:
            try:
                observed = path.read_text(encoding="utf-8")
            except OSError:
                drift.append(name)
                continue
            if observed != content:
                drift.append(name)
        else:
            # The canonical header may live outside output_dir.  Product and
            # role builds deliberately put it under a private include root,
            # so create the parent of every concrete artifact rather than
            # assuming the repository-owned default tree.  Preserve mtime for
            # identical content so a standard Make/CMake adapter can resolve
            # the contract repeatedly without forcing an incremental relink.
            path.parent.mkdir(parents=True, exist_ok=True)
            try:
                if path.read_text(encoding="utf-8") == content:
                    continue
            except OSError:
                pass

            temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
            try:
                temporary.write_text(content, encoding="utf-8", newline="\n")
                os.replace(temporary, path)
            finally:
                temporary.unlink(missing_ok=True)
    return tuple(drift)


def _parse_reference_csv(path: Path) -> dict[str, tuple[int, int]]:
    cursor = 0
    result: dict[str, tuple[int, int]] = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        fields = [field.strip() for field in next(csv.reader([raw_line]))]
        if len(fields) < 6:
            raise PartitionLayoutError(f"invalid SDK reference row: {raw_line}")
        offset = cursor if not fields[1] else int(fields[1], 0)
        size = parse_size(fields[2])
        result[fields[0]] = (offset, size)
        cursor = offset + size
    return result


def verify_sdk_compatibility(
    layout: PartitionLayout, sdk_source: Path
) -> dict[str, object]:
    source = sdk_source.resolve()
    if source.name != SDK_DIRECTORY:
        raise PartitionLayoutError(
            f"SDK source must be the exact {SDK_DIRECTORY} directory"
        )
    reference_path = source / SDK_REFERENCE_CSV
    position_path = source / SDK_POSITION_CSV
    setting_path = source / SDK_PARTITION_SETTING
    if (
        not reference_path.is_file()
        or not position_path.is_file()
        or not setting_path.is_file()
    ):
        raise PartitionLayoutError("official app_ab partition inputs are missing")
    if "pos_independent,TRUE" not in position_path.read_text(
        encoding="utf-8"
    ).replace("\r", ""):
        raise PartitionLayoutError("official position-independent AB mode is disabled")

    reference = _parse_reference_csv(reference_path)
    current = {
        partition.name: (partition.offset, partition.size)
        for partition in layout.partitions
    }
    geometry_differences = {
        name: {"official": reference[name], "project": current.get(name)}
        for name in OFFICIAL_REFERENCE_NAMES
        if current.get(name) != reference.get(name)
    }

    setting = json.loads(setting_path.read_text(encoding="utf-8"))
    if tuple(setting.get("internel_partitions", ())) != SDK_INTERNAL_PARTITIONS:
        raise PartitionLayoutError("official SDK partition ID reservation changed")

    sdk_python = source / "tools/env_tools/bk_py_libs"
    sdk_build_process = source / "tools/build_tools/build_process"
    old_path = list(sys.path)
    try:
        sys.path.insert(0, str(sdk_python))
        sys.path.insert(0, str(sdk_build_process))
        from bk_auto_partition import (  # type: ignore
            bk_partitions_table,
            partition_limit,
        )
        from bk_flash_partiton import bk_flash_partition  # type: ignore
        from bk_sdk.bk_flash_partitions_generator import (  # type: ignore
            bk_flash_denpendecny_generator,
        )

        with tempfile.TemporaryDirectory(prefix="bk7258-partitions-") as temp_dir:
            temp_path = Path(temp_dir)
            sdk_csv = temp_path / "auto_partitions.csv"
            sdk_csv.write_text(render_sdk_csv(layout), encoding="utf-8")
            table = bk_partitions_table(sdk_csv, "8M", True)
            observed = [
                (part.Name, part.Offset, part.Size) for part in table.partitions
            ]
            table.set_default_setting(
                [partition_limit(**item) for item in setting["patitions_limit"]]
            )
            table.sort_partitions(setting["internel_partitions"])
            partitions_json = temp_path / "partitions.json"
            partitions_header = temp_path / "partitions_gen.h"
            table.gen_partition_json(partitions_json)
            flash_partitions = bk_flash_partition(
                partitions_json, bk_flash_denpendecny_generator()
            )
            flash_partitions.gen_partitions_layout_hdr(partitions_header)
            official_header = partitions_header.read_text(encoding="utf-8")
            observed_abi = [
                (
                    part.Id,
                    part.Name,
                    part.Offset,
                    part.Size,
                    part.Execute,
                    part.Read,
                    part.Write,
                )
                for part in flash_partitions.part_info
            ]
    except (ImportError, OSError, RuntimeError, ValueError) as error:
        raise PartitionLayoutError(
            f"official SDK partition generator rejected project CSV: {error}"
        ) from error
    finally:
        sys.path[:] = old_path

    expected = [
        (part.name, part.offset, part.size) for part in layout.partitions
    ]
    if observed != expected:
        raise PartitionLayoutError(
            f"official SDK parser changed project geometry: {observed!r}"
        )
    expected_abi = [
        (
            sdk_partition.sdk_id,
            sdk_partition.sdk_name,
            sdk_partition.partition.offset,
            sdk_partition.partition.size,
            sdk_partition.partition.executable,
            sdk_partition.partition.readable,
            sdk_partition.partition.writable,
        )
        for sdk_partition in layout.sdk_partitions()
    ]
    if observed_abi != expected_abi:
        raise PartitionLayoutError(
            "project SDK ABI model differs from the official generator: "
            f"expected={expected_abi!r} observed={observed_abi!r}"
        )

    table_size_match = re.search(
        r"^#define\s+BK_PARTITIONS_TABLE_SIZE\s+(\d+)\s*$",
        official_header,
        re.MULTILINE,
    )
    expected_table_size = layout.sdk_partitions()[-1].sdk_id + 1
    if table_size_match is None or int(table_size_match.group(1)) != expected_table_size:
        raise PartitionLayoutError("official generated SDK table size differs")

    return {
        "release": SDK_RELEASE,
        "source": str(source),
        "reference_csv": str(reference_path),
        "reference_csv_sha256": hashlib.sha256(reference_path.read_bytes()).hexdigest(),
        "position_csv_sha256": hashlib.sha256(position_path.read_bytes()).hexdigest(),
        "partition_setting_sha256": hashlib.sha256(
            setting_path.read_bytes()
        ).hexdigest(),
        "position_independent": True,
        "official_reference_geometry_match": not geometry_differences,
        "geometry_differences": geometry_differences,
        "project_csv_accepted_by_sdk_parser": True,
        "project_csv_accepted_by_sdk_generator": True,
        "sdk_generated_header_sha256": hashlib.sha256(
            official_header.encode("utf-8")
        ).hexdigest(),
        "sdk_table_size": expected_table_size,
        "sdk_partition_ids": {
            partition.sdk_name: partition.sdk_id
            for partition in layout.sdk_partitions()
        },
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=DEFAULT_INPUT)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR)
    parser.add_argument("--header", type=Path, default=DEFAULT_HEADER)
    parser.add_argument("--sdk-source", type=Path)
    parser.add_argument("--expect-layout-id")
    parser.add_argument("--expect-layout-sha256")
    parser.add_argument(
        "--get",
        metavar="ROLE.FIELD",
        help="print one scalar without generating files (for build wrappers)",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="fail if generated artifacts are absent or stale; do not write",
    )
    parser.add_argument("--json", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        layout = load_layout(args.input)
        if (args.expect_layout_id is None) != (
            args.expect_layout_sha256 is None
        ):
            raise PartitionLayoutError(
                "--expect-layout-id and --expect-layout-sha256 must be supplied together"
            )
        if args.expect_layout_id is not None and (
            layout.layout_id != args.expect_layout_id
            or layout.layout_sha256 != args.expect_layout_sha256
        ):
            raise PartitionLayoutError(
                "resolved partition layout identity mismatch: "
                f"expected={args.expect_layout_id}/{args.expect_layout_sha256} "
                f"observed={layout.layout_id}/{layout.layout_sha256}"
            )
        if args.get is not None:
            print(query_layout(layout, args.get))
            return 0
        drift = sync_generated(layout, args.output_dir, args.check, args.header)
        if drift:
            raise PartitionLayoutError(
                "generated partition artifacts are stale: " + ", ".join(drift)
            )
        result = layout.report()
        if args.sdk_source is not None:
            result["official_sdk"] = verify_sdk_compatibility(
                layout, args.sdk_source
            )
    except (PartitionLayoutError, OSError, ValueError) as error:
        print(f"BK7258 partition generation FAIL: {error}", file=sys.stderr)
        return 1

    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        mode = "check" if args.check else "generate"
        print(
            "BK7258 partition generation PASS: "
            f"mode={mode} layout_id={layout.layout_id} partitions={len(layout.partitions)}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
