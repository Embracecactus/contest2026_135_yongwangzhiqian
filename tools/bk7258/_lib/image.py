# SPDX-License-Identifier: Apache-2.0

"""Pure BK7258 image codecs, placement, and materialization."""

from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Mapping

from _lib.layout import Layout, Partition


class ImageError(ValueError):
    """Image bytes do not satisfy the selected layout or format."""


CRC_POLYNOMIAL = 0x8005
CRC_INITIAL = 0xFFFFFFFF
ERASE_BYTE = 0xFF


@dataclass(frozen=True)
class Segment:
    artifact: str
    partition: str
    offset: int
    data: bytes

    @property
    def end(self) -> int:
        return self.offset + len(self.data)


@dataclass(frozen=True)
class EraseRange:
    partition: str
    offset: int
    size: int

    @property
    def end(self) -> int:
        return self.offset + self.size


@dataclass(frozen=True)
class ImageSet:
    layout: Layout
    writes: tuple[Segment, ...]
    erases: tuple[EraseRange, ...]
    preserved_external: tuple[str, ...]


def crc16(data: bytes) -> int:
    value = CRC_INITIAL
    for byte in data:
        value ^= byte << 8
        for _ in range(8):
            value = (value << 1) ^ CRC_POLYNOMIAL if value & 0x8000 else value << 1
    return value & 0xFFFF


def crc_encode(data: bytes, data_size: int = 32, total_size: int = 34) -> bytes:
    if data_size <= 0 or total_size != data_size + 2:
        raise ImageError("CRC packet geometry must be N data bytes plus two CRC bytes")
    padded = data + bytes([ERASE_BYTE]) * ((-len(data)) % data_size)
    output = bytearray()
    for offset in range(0, len(padded), data_size):
        block = padded[offset:offset + data_size]
        output.extend(block)
        output.extend(crc16(block).to_bytes(2, "big"))
    return bytes(output)


def crc_decode(data: bytes, data_size: int = 32, total_size: int = 34) -> bytes:
    if data_size <= 0 or total_size != data_size + 2 or len(data) % total_size:
        raise ImageError("encoded image does not match CRC packet geometry")
    output = bytearray()
    for offset in range(0, len(data), total_size):
        block = data[offset:offset + data_size]
        expected = int.from_bytes(data[offset + data_size:offset + total_size], "big")
        observed = crc16(block)
        if observed != expected:
            raise ImageError(
                f"CRC mismatch at physical offset 0x{offset:x}: "
                f"expected=0x{expected:04x} observed=0x{observed:04x}"
            )
        output.extend(block)
    return bytes(output)


def logical_to_physical(offset: int, data_size: int = 32,
                        total_size: int = 34) -> int:
    if offset < 0 or data_size <= 0 or total_size < data_size:
        raise ImageError("invalid logical-to-physical conversion")
    return offset // data_size * total_size + offset % data_size


def inspect_vectors(data: bytes, partition: Partition, layout: Layout, *,
                    sram_start: int, sram_end: int) -> dict[str, int]:
    if len(data) < 8:
        raise ImageError("raw image is too small for a vector table")
    msp, reset = struct.unpack_from("<II", data)
    if not sram_start <= msp < sram_end:
        raise ImageError(f"initial MSP is outside the explicit SRAM range: 0x{msp:08x}")
    if not reset & 1:
        raise ImageError(f"reset vector is not Thumb: 0x{reset:08x}")
    reset_address = reset & ~1
    start = layout.xip_base + layout.logical_offset(partition)
    if not start <= reset_address < start + len(data):
        raise ImageError(
            f"reset vector 0x{reset:08x} is outside image execution range "
            f"0x{start:08x}..0x{start + len(data):08x}"
        )
    return {"msp": msp, "reset": reset, "execution_start": start}


def encode_for_partition(data: bytes, partition: Partition, layout: Layout) -> bytes:
    if not data:
        raise ImageError(f"artifact is empty: {partition.artifact}")
    if partition.executable and layout.crc_total_size > layout.crc_data_size:
        encoded = crc_encode(data, layout.crc_data_size, layout.crc_total_size)
    else:
        encoded = data
    if len(encoded) > partition.size:
        raise ImageError(
            f"artifact {partition.artifact} exceeds partition {partition.name}: "
            f"0x{len(encoded):x} > 0x{partition.size:x}"
        )
    return encoded


def _selection(layout: Layout, artifacts: Mapping[str, bytes],
               preserved_external: tuple[str, ...]) -> dict[str, Partition]:
    required = {
        item.artifact: item
        for item in layout.partitions
        if item.policy == "image" and item.artifact is not None
    }
    optional = {
        item.artifact: item
        for item in layout.partitions
        if item.policy == "external" and item.artifact is not None
    }
    preserved = set(preserved_external)
    if len(preserved) != len(preserved_external) or not preserved.issubset(optional):
        raise ImageError("preserved external artifacts are invalid or duplicated")
    if preserved.intersection(artifacts):
        raise ImageError("an external artifact cannot be both supplied and preserved")
    expected_names = set(required) | (set(optional) - preserved)
    if set(artifacts) != expected_names:
        missing = sorted(expected_names - set(artifacts))
        extra = sorted(set(artifacts) - expected_names)
        raise ImageError(f"artifact set mismatch: missing={missing} extra={extra}")
    return {**required, **{name: optional[name] for name in optional if name not in preserved}}


def finalize(layout: Layout, artifacts: Mapping[str, bytes], *,
             preserved_external: tuple[str, ...] = ()) -> ImageSet:
    """Convert raw build artifacts into finalized sparse Flash operations."""

    expected = _selection(layout, artifacts, preserved_external)
    writes = tuple(
        Segment(name, item.name, item.offset, encode_for_partition(artifacts[name], item, layout))
        for name, item in sorted(expected.items(), key=lambda row: row[1].offset)
    )
    erases = tuple(
        EraseRange(item.name, item.offset, item.size)
        for item in layout.partitions if item.policy == "clear"
    )
    _validate_operations(layout, writes, erases)
    return ImageSet(layout, writes, erases, tuple(sorted(preserved_external)))


def finalized(layout: Layout, artifacts: Mapping[str, bytes], *,
              preserved_external: tuple[str, ...] = ()) -> ImageSet:
    """Verify already-finalized bytes without changing a single byte."""

    expected = _selection(layout, artifacts, preserved_external)
    writes = []
    for name, item in sorted(expected.items(), key=lambda row: row[1].offset):
        data = artifacts[name]
        if not data or len(data) > item.size:
            raise ImageError(
                f"final artifact {name} has invalid size: 0x{len(data):x}"
            )
        if item.executable and layout.crc_total_size > layout.crc_data_size:
            crc_decode(data, layout.crc_data_size, layout.crc_total_size)
        writes.append(Segment(name, item.name, item.offset, data))
    by_artifact = {row.artifact: row for row in writes}
    if {"cp", "ap", "pair"}.issubset(by_artifact):
        cp_partition = layout.artifact("cp")
        ap_partition = layout.artifact("ap")
        expected_pair = (
            by_artifact["cp"].data.ljust(cp_partition.size, bytes([ERASE_BYTE]))
            + by_artifact["ap"].data.ljust(ap_partition.size, bytes([ERASE_BYTE]))
        )
        if by_artifact["pair"].data != expected_pair:
            raise ImageError("final pair artifact does not match finalized CP/AP bytes")
    erases = tuple(
        EraseRange(item.name, item.offset, item.size)
        for item in layout.partitions if item.policy == "clear"
    )
    result = ImageSet(
        layout, tuple(writes), erases, tuple(sorted(preserved_external))
    )
    _validate_operations(layout, result.writes, result.erases)
    return result


def pair(layout: Layout, cp: bytes, ap: bytes) -> bytes:
    """Build the secondary CP+AP pair from raw primary artifacts."""

    cp_partition = layout.artifact("cp")
    ap_partition = layout.artifact("ap")
    pair_partition = layout.artifact("pair")
    cp_encoded = encode_for_partition(cp, cp_partition, layout)
    ap_encoded = encode_for_partition(ap, ap_partition, layout)
    result = (
        cp_encoded.ljust(cp_partition.size, bytes([ERASE_BYTE]))
        + ap_encoded.ljust(ap_partition.size, bytes([ERASE_BYTE]))
    )
    if len(result) != pair_partition.size:
        raise ImageError(
            "pair partition size must equal the physical CP+AP partition span"
        )
    return result


def _validate_operations(layout: Layout, writes: tuple[Segment, ...],
                         erases: tuple[EraseRange, ...]) -> None:
    ranges: list[tuple[int, int, str]] = []
    forbidden = [
        item for item in layout.partitions if item.policy in {"preserve", "immutable"}
    ]
    for row in (*writes, *erases):
        start, end = row.offset, row.end
        if start < 0 or end > layout.flash_size or start >= end:
            raise ImageError(f"Flash operation is outside the selected layout: {row}")
        for item in forbidden:
            if start < item.end and item.offset < end:
                raise ImageError(f"Flash operation touches {item.policy} partition: {item.name}")
        ranges.append((start, end, row.partition))
    ranges.sort()
    for left, right in zip(ranges, ranges[1:]):
        if left[1] > right[0]:
            raise ImageError(f"overlapping Flash operations: {left[2]} and {right[2]}")


def materialize(image_set: ImageSet, start: int, end: int) -> bytes:
    """Explicitly materialize one safe dense interval from sparse writes."""

    if start < 0 or end <= start or end > image_set.layout.flash_size:
        raise ImageError("dense image interval is outside Flash")
    for item in image_set.layout.partitions:
        if item.policy in {"preserve", "immutable"} and start < item.end and item.offset < end:
            raise ImageError(f"dense image interval crosses {item.policy}: {item.name}")
        if item.artifact in image_set.preserved_external \
                and start < item.end and item.offset < end:
            raise ImageError(f"dense image interval crosses preserved external: {item.name}")
    output = bytearray([ERASE_BYTE]) * (end - start)
    for segment in image_set.writes:
        if segment.offset < start or segment.end > end:
            raise ImageError(f"segment is outside dense interval: {segment.artifact}")
        begin = segment.offset - start
        output[begin:begin + len(segment.data)] = segment.data
    for erase in image_set.erases:
        if erase.offset < start or erase.end > end:
            raise ImageError(f"erase range is outside dense interval: {erase.partition}")
    return bytes(output)


def read_artifacts(paths: Mapping[str, Path]) -> dict[str, bytes]:
    result: dict[str, bytes] = {}
    for name, path in paths.items():
        path = path.absolute()
        if path.is_symlink() or not path.is_file():
            raise ImageError(f"artifact must be a regular non-symlink file: {path}")
        result[name] = path.read_bytes()
    return result
