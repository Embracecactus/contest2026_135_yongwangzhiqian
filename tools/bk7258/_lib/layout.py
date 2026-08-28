# SPDX-License-Identifier: Apache-2.0

"""Data-driven BK7258 partition layout and official CSV adapter."""

from __future__ import annotations

import csv
import hashlib
import os
import re
import stat
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Mapping


class LayoutError(ValueError):
    """The selected partition CSV is malformed or unsafe."""


POLICIES = frozenset({"image", "external", "clear", "preserve", "immutable"})
STORAGE_TOPOLOGIES = frozenset(
    {"onchip-persistent", "removable-block", "fixed-block"}
)
DIRECTIVES = frozenset(
    {
        "LAYOUT_NAME",
        "STORAGE_TOPOLOGY",
        "ERASE_SIZE",
        "CRC_DATA_SIZE",
        "CRC_TOTAL_SIZE",
        "XIP_BASE",
    }
)
SIZE_RE = re.compile(r"^(0[xX][0-9a-fA-F]+|[0-9]+)([kKmM]?)$")
TOKEN_RE = re.compile(r"^[a-z][a-z0-9_-]*$")


def _regular(path: Path, label: str) -> None:
    try:
        mode = path.lstat().st_mode
    except OSError as error:
        raise LayoutError(f"missing {label}: {path}") from error
    if stat.S_ISLNK(mode) or not stat.S_ISREG(mode):
        raise LayoutError(f"{label} must be a regular non-symlink file: {path}")


def parse_size(value: str, label: str) -> int:
    match = SIZE_RE.fullmatch(value.strip())
    if match is None:
        raise LayoutError(f"invalid {label}: {value!r}")
    number, suffix = match.groups()
    result = int(number, 0)
    if suffix.lower() == "k":
        result *= 1024
    elif suffix.lower() == "m":
        result *= 1024 * 1024
    if result <= 0:
        raise LayoutError(f"{label} must be positive")
    return result


def parse_bool(value: str, label: str) -> bool:
    normalized = value.strip().lower()
    if normalized == "true":
        return True
    if normalized == "false":
        return False
    raise LayoutError(f"{label} must be TRUE or FALSE")


def macro(value: str) -> str:
    result = re.sub(r"[^A-Za-z0-9]+", "_", value).strip("_").upper()
    if not result or result[0].isdigit():
        raise LayoutError(f"cannot form a C identifier from {value!r}")
    return result


@dataclass(frozen=True)
class Partition:
    name: str
    offset: int
    size: int
    kind: str
    readable: bool
    writable: bool
    artifact: str | None
    policy: str

    @property
    def end(self) -> int:
        return self.offset + self.size

    @property
    def executable(self) -> bool:
        return self.kind == "code"


@dataclass(frozen=True)
class Layout:
    source: Path
    name: str
    storage_topology: str
    flash_size: int
    erase_size: int
    crc_data_size: int
    crc_total_size: int
    xip_base: int
    partitions: tuple[Partition, ...]
    sha256: str

    @property
    def identity(self) -> str:
        return f"bk7258-{self.sha256[:16]}"

    def artifact(self, name: str) -> Partition:
        matches = [item for item in self.partitions if item.artifact == name]
        if len(matches) != 1:
            raise LayoutError(f"layout must map artifact exactly once: {name}")
        return matches[0]

    def logical_offset(self, item: Partition) -> int:
        if not item.executable:
            raise LayoutError(f"partition is not executable: {item.name}")
        return item.offset // self.crc_total_size * self.crc_data_size

    def logical_size(self, item: Partition) -> int:
        if not item.executable:
            raise LayoutError(f"partition is not executable: {item.name}")
        return item.size // self.crc_total_size * self.crc_data_size


@dataclass(frozen=True)
class Placement:
    artifact: str
    partition: Partition
    source: Path


@dataclass(frozen=True)
class GeneratedLayout:
    root: Path
    sdk_csv: Path
    header: Path
    linker: Path


def identity_sha256(*, name: str, storage_topology: str, flash_size: int,
                    erase_size: int,
                    crc_data_size: int, crc_total_size: int, xip_base: int,
                    partitions: tuple[Partition, ...]) -> str:
    """Hash normalized layout facts rather than their CSV spelling."""

    values = [
        "schema=bk7258.layout/1",
        f"name={name}",
        f"storage_topology={storage_topology}",
        f"flash_size={flash_size}",
        f"erase_size={erase_size}",
        f"crc_data_size={crc_data_size}",
        f"crc_total_size={crc_total_size}",
        f"xip_base={xip_base}",
    ]
    for item in partitions:
        values.append(
            "\0".join(
                (
                    item.name,
                    str(item.offset),
                    str(item.size),
                    item.kind,
                    "1" if item.readable else "0",
                    "1" if item.writable else "0",
                    item.artifact or "",
                    item.policy,
                )
            )
        )
    return hashlib.sha256(("\n".join(values) + "\n").encode()).hexdigest()


def load(path: Path) -> Layout:
    """Load one explicit eight-column project CSV."""

    path = path.absolute()
    _regular(path, "partition CSV")
    directives: dict[str, str] = {}
    source_rows: list[tuple[int, list[str]]] = []
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line:
            continue
        if line.startswith("#"):
            match = re.fullmatch(r"#\s*([A-Z][A-Z0-9_]*)=(.+)", line)
            if match is not None:
                key, value = match.groups()
                if key in directives:
                    raise LayoutError(f"duplicate directive {key}: {path}:{number}")
                directives[key] = value.strip()
            continue
        if line.startswith("FLASH_CAPACITY="):
            if "FLASH_CAPACITY" in directives:
                raise LayoutError("duplicate FLASH_CAPACITY")
            directives["FLASH_CAPACITY"] = line.split("=", 1)[1].strip()
            continue
        fields = [value.strip() for value in next(csv.reader([raw]))]
        if len(fields) != 8:
            raise LayoutError(
                f"{path}:{number}: expected Name,Offset,Size,Type,Read,Write,Artifact,Policy"
            )
        source_rows.append((number, fields))

    missing = (DIRECTIVES | {"FLASH_CAPACITY"}) - directives.keys()
    if missing:
        raise LayoutError("missing directives: " + ", ".join(sorted(missing)))
    if not source_rows:
        raise LayoutError("partition CSV contains no rows")

    erase_size = parse_size(directives["ERASE_SIZE"], "ERASE_SIZE")
    flash_size = parse_size(directives["FLASH_CAPACITY"], "FLASH_CAPACITY")
    crc_data_size = parse_size(directives["CRC_DATA_SIZE"], "CRC_DATA_SIZE")
    crc_total_size = parse_size(directives["CRC_TOTAL_SIZE"], "CRC_TOTAL_SIZE")
    xip_base = int(directives["XIP_BASE"], 0)
    storage_topology = directives["STORAGE_TOPOLOGY"]
    if storage_topology not in STORAGE_TOPOLOGIES:
        raise LayoutError(f"unsupported STORAGE_TOPOLOGY: {storage_topology}")
    if erase_size & (erase_size - 1):
        raise LayoutError("ERASE_SIZE must be a power of two")
    if crc_total_size < crc_data_size:
        raise LayoutError("CRC_TOTAL_SIZE must not be smaller than CRC_DATA_SIZE")

    names: set[str] = set()
    artifacts: set[str] = set()
    partitions: list[Partition] = []
    cursor = 0
    for number, fields in source_rows:
        name, offset_text, size_text, kind, read, write, artifact, policy = fields
        if not TOKEN_RE.fullmatch(name) or name in names:
            raise LayoutError(f"invalid or duplicate partition name: {path}:{number}:{name}")
        names.add(name)
        if kind not in {"code", "data"}:
            raise LayoutError(f"partition type must be code or data: {name}")
        if policy not in POLICIES:
            raise LayoutError(f"invalid partition policy: {name}:{policy}")
        artifact_value = artifact or None
        if artifact_value is not None:
            if not TOKEN_RE.fullmatch(artifact_value) or artifact_value in artifacts:
                raise LayoutError(f"invalid or duplicate artifact: {artifact_value}")
            artifacts.add(artifact_value)
        if policy in {"image", "external"} and artifact_value is None:
            raise LayoutError(f"{policy} partition requires an artifact: {name}")
        if policy in {"clear", "preserve", "immutable"} and artifact_value is not None:
            raise LayoutError(f"{policy} partition must not map an artifact: {name}")

        offset = cursor if not offset_text else int(offset_text, 0)
        size = parse_size(size_text, f"partition size {name}")
        if offset % erase_size or size % erase_size:
            raise LayoutError(f"partition is not erase-aligned: {name}")
        if offset < cursor:
            raise LayoutError(f"partition overlap at {name}")
        if offset + size > flash_size:
            raise LayoutError(f"partition exceeds Flash: {name}")
        if kind == "code" and crc_total_size > crc_data_size:
            alignment = 1024 * crc_total_size
            if offset % alignment or size % alignment:
                raise LayoutError(f"executable partition violates CRC alignment: {name}")
        item = Partition(
            name,
            offset,
            size,
            kind,
            parse_bool(read, f"Read for {name}"),
            parse_bool(write, f"Write for {name}"),
            artifact_value,
            policy,
        )
        partitions.append(item)
        cursor = item.end

    rows = tuple(partitions)
    return Layout(
        path.resolve(strict=True),
        directives["LAYOUT_NAME"],
        storage_topology,
        flash_size,
        erase_size,
        crc_data_size,
        crc_total_size,
        xip_base,
        rows,
        identity_sha256(
            name=directives["LAYOUT_NAME"],
            storage_topology=storage_topology,
            flash_size=flash_size,
            erase_size=erase_size,
            crc_data_size=crc_data_size,
            crc_total_size=crc_total_size,
            xip_base=xip_base,
            partitions=rows,
        ),
    )


def bind(layout: Layout, artifacts: Mapping[str, Path], *,
         policies: frozenset[str] = frozenset({"image", "external"})) -> tuple[Placement, ...]:
    """Bind explicit artifact paths without naming partitions in code."""

    expected = {
        item.artifact: item
        for item in layout.partitions
        if item.policy in policies and item.artifact is not None
    }
    if set(artifacts) != set(expected):
        missing = sorted(set(expected) - set(artifacts))
        extra = sorted(set(artifacts) - set(expected))
        raise LayoutError(f"artifact set mismatch: missing={missing} extra={extra}")
    result: list[Placement] = []
    for name, partition in expected.items():
        source = artifacts[name].absolute()
        _regular(source, f"artifact {name}")
        result.append(Placement(name, partition, source.resolve(strict=True)))
    return tuple(sorted(result, key=lambda row: row.partition.offset))


def _sdk_csv(layout: Layout) -> str:
    lines = [
        "# Generated from the selected BK7258 project partition contract.",
        "# Name,Offset,Size,Type,Read,Write",
        f"FLASH_CAPACITY=0x{layout.flash_size:x}",
    ]
    for item in layout.partitions:
        lines.append(
            f"{item.name},0x{item.offset:x},0x{item.size:x},{item.kind},"
            f"{'TRUE' if item.readable else 'FALSE'},"
            f"{'TRUE' if item.writable else 'FALSE'}"
        )
    return "\n".join(lines) + "\n"


def _header(layout: Layout) -> str:
    digest = ", ".join(
        f"0x{layout.sha256[index:index + 2]}" for index in range(0, 64, 2)
    )
    lines = [
        "/* Generated from the selected BK7258 partition CSV. */",
        "#ifndef __BK7258_GENERATED_PARTITIONS_H",
        "#define __BK7258_GENERATED_PARTITIONS_H",
        "",
        f'#define BK7258_LAYOUT_ID "{layout.identity}"',
        f'#define BK7258_LAYOUT_SHA256 "{layout.sha256}"',
        f'#define BK7258_STORAGE_TOPOLOGY "{layout.storage_topology}"',
        f"#define BK7258_STORAGE_TOPOLOGY_{macro(layout.storage_topology)} 1",
        f"#define BK7258_LAYOUT_SHA256_BYTES {{{digest}}}",
        f"#define BK7258_FLASH_SIZE 0x{layout.flash_size:08x}",
        f"#define BK7258_FLASH_ERASE_SIZE 0x{layout.erase_size:08x}",
        f"#define BK7258_FLASH_XIP_BASE 0x{layout.xip_base:08x}",
        f"#define BK7258_FLASH_CRC_DATA_SIZE {layout.crc_data_size}",
        f"#define BK7258_FLASH_CRC_TOTAL_SIZE {layout.crc_total_size}",
        f"#define BK7258_PARTITION_COUNT {len(layout.partitions)}",
        f"#define BK7258_PARTITION_VALID_MASK 0x{(1 << len(layout.partitions)) - 1:08x}",
        "",
    ]
    for index, item in enumerate(layout.partitions):
        prefix = f"BK7258_PARTITION_{macro(item.name)}"
        lines.extend(
            (
                f"#define {prefix}_INDEX {index}",
                f"#define {prefix}_OFFSET 0x{item.offset:08x}",
                f"#define {prefix}_SIZE 0x{item.size:08x}",
                f"#define {prefix}_END 0x{item.end:08x}",
            )
        )
        if item.artifact is not None:
            artifact = f"BK7258_ARTIFACT_{macro(item.artifact)}"
            lines.extend(
                (
                    f"#define {artifact}_PARTITION_INDEX {index}",
                    f"#define {artifact}_OFFSET 0x{item.offset:08x}",
                    f"#define {artifact}_SIZE 0x{item.size:08x}",
                    f"#define {artifact}_END 0x{item.end:08x}",
                )
            )
            if item.executable:
                logical_offset = layout.logical_offset(item)
                logical_size = layout.logical_size(item)
                lines.extend(
                    (
                        f"#define {artifact}_LOGICAL_OFFSET 0x{logical_offset:08x}",
                        f"#define {artifact}_LOGICAL_SIZE 0x{logical_size:08x}",
                        f"#define {artifact}_XIP_START 0x{layout.xip_base + logical_offset:08x}",
                        f"#define {artifact}_XIP_END 0x{layout.xip_base + logical_offset + logical_size:08x}",
                    )
                )
        lines.append("")
    lines.append("#define BK7258_PARTITION_FOREACH(_) \\")
    for index, item in enumerate(layout.partitions):
        suffix = " \\" if index + 1 < len(layout.partitions) else ""
        lines.append(
            f'  _({index}, "{item.name}", 0x{item.offset:08x}, '
            f'0x{item.size:08x}, {1 if item.executable else 0}, '
            f'{1 if item.readable else 0}, {1 if item.writable else 0}){suffix}'
        )
    lines.append("")
    lines.extend(("#endif /* __BK7258_GENERATED_PARTITIONS_H */", ""))
    return "\n".join(lines)


def _linker(layout: Layout) -> str:
    lines = [
        "/* Generated from the selected BK7258 partition CSV. */",
        f"BK7258_LAYOUT_FLASH_SIZE = 0x{layout.flash_size:08x};",
        f"BK7258_LAYOUT_XIP_BASE = 0x{layout.xip_base:08x};",
    ]
    for item in layout.partitions:
        prefix = f"BK7258_PARTITION_{macro(item.name)}"
        lines.extend(
            (
                f"{prefix}_OFFSET = 0x{item.offset:08x};",
                f"{prefix}_SIZE = 0x{item.size:08x};",
            )
        )
        if item.artifact is not None:
            artifact = f"BK7258_ARTIFACT_{macro(item.artifact)}"
            lines.extend(
                (
                    f"{artifact}_OFFSET = 0x{item.offset:08x};",
                    f"{artifact}_SIZE = 0x{item.size:08x};",
                )
            )
    return "\n".join(lines) + "\n"


def _atomic_text(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as stream:
            stream.write(content)
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def emit(layout: Layout, output_dir: Path) -> GeneratedLayout:
    """Write build-scoped derivatives; never mutate the source tree."""

    output_dir = output_dir.absolute()
    if output_dir.is_symlink():
        raise LayoutError(f"generated layout root must not be a symlink: {output_dir}")
    output_dir.mkdir(parents=True, exist_ok=True)
    sdk_csv = output_dir / "sdk_partitions.csv"
    header = output_dir / "bk7258_partitions.h"
    linker = output_dir / "bk7258_partitions.ld"
    _atomic_text(sdk_csv, _sdk_csv(layout))
    _atomic_text(header, _header(layout))
    _atomic_text(linker, _linker(layout))
    return GeneratedLayout(output_dir, sdk_csv, header, linker)


def report(layout: Layout) -> dict[str, object]:
    return {
        "identity": layout.identity,
        "sha256": layout.sha256,
        "source": str(layout.source),
        "storage_topology": layout.storage_topology,
        "flash_size": layout.flash_size,
        "partitions": [
            {
                "name": item.name,
                "offset": item.offset,
                "size": item.size,
                "artifact": item.artifact,
                "policy": item.policy,
            }
            for item in layout.partitions
        ],
    }
