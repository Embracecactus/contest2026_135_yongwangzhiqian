# SPDX-License-Identifier: Apache-2.0
"""Explicit factory derivative of an already trust-verified same-unit image.

This is NOT an OTA package. Firmware signatures stay unchanged; only declared
mutable factory ranges are replaced. Device-unique bytes remain target-bound.
"""
from __future__ import annotations

import hashlib
import os
import secrets
import struct
import zlib
from pathlib import Path

from .layout import Layout
from .package import PackageError


def create(layout: Layout, source: Path, source_sha256: str,
           output: Path, device_id: str) -> dict[str, object]:
    if not device_id or source.is_symlink() or not source.is_file():
        raise PackageError("factory input requires a regular same-unit image")
    original = source.read_bytes()
    if len(original) != layout.flash_size or hashlib.sha256(original).hexdigest() != source_sha256:
        raise PackageError("factory source hash/length mismatch")
    parts = {part.name: part for part in layout.partitions}
    required = ("persistent_data", "usr_config", "factory_state")
    if any(name not in parts for name in required):
        raise PackageError("layout does not declare factory initialization ranges")
    data, user, journal = (parts[name] for name in required)
    if journal.size != 8192 or any(
        p.executable or not p.writable or p.offset % layout.erase_size or
        p.size % layout.erase_size for p in (data, user, journal)
    ):
        raise PackageError("invalid factory initialization geometry")
    ranges = sorted((p.offset, p.end, p.name) for p in (data, user, journal))
    if any(a[1] > b[0] for a, b in zip(ranges, ranges[1:])):
        raise PackageError("overlapping factory ranges")
    transaction = secrets.token_bytes(16)
    record = bytearray(128)
    record[:24] = struct.pack(">4sIIIII", b"SFJ1", 1, 1, 1, data.offset, data.size)
    record[24:56] = bytes.fromhex(layout.sha256)
    record[56:72] = transaction
    struct.pack_into(">I", record, 124, zlib.crc32(record[:124]) & 0xffffffff)
    image = bytearray(original)
    for start, end, _ in ranges:
        image[start:end] = b"\xff" * (end - start)
    image[journal.offset:journal.offset + len(record)] = record
    # A reset marker can contain watchdog/OTA state. It is deliberately kept:
    # factory authorization has its own journal and must not alias that state.
    position = 0
    for start, end, _ in ranges:
        if image[position:start] != original[position:start]:
            raise PackageError("factory derivative changed an unauthorized range")
        position = end
    if image[position:] != original[position:]:
        raise PackageError("factory derivative changed the protected tail")
    descriptor = os.open(output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(image)
            stream.flush()
            os.fsync(stream.fileno())
    except BaseException:
        output.unlink(missing_ok=True)
        raise
    digest = hashlib.sha256(image).hexdigest()
    if hashlib.sha256(output.read_bytes()).hexdigest() != digest:
        raise PackageError("factory derivative readback mismatch")
    return {
        "format": "bk7258.factory-image/1", "device_id": device_id,
        "layout_sha256": layout.sha256, "source_sha256": source_sha256,
        "sha256": digest, "size": len(image), "transaction": transaction.hex(),
        "path": "flash/" + output.name,
        "data_impact": [{"partition": name, "offset": start, "size": end - start,
                         "operation": "initialize"} for start, end, name in ranges],
        "mode": "factory-only-not-ota", "device_unique_preserved": True,
        "warning": "Clears user configuration and identity on the designated unit; not a cross-device image.",
    }
