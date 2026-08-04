#!/usr/bin/env python3
"""Build the deterministic N15-B binary staging descriptor for an N15-A bundle."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import zlib
from pathlib import Path

from bk7258_ab_layout import CP_A_START, LAYOUT_ID, PAIR_B_START
from pack_bk7258_ota_pair import (
    AP_FILE,
    CP_FILE,
    MANIFEST_FILE,
    PAIR_SCHEMA,
    RBL_FILE,
    S_APP_FILE,
    parse_int,
)
from verify_bk7258_ota_pair import load_manifest, verify_bundle


STAGE_MAGIC = b"BKOTA15B"
STAGE_FORMAT = 1
STAGE_FLAGS = 0
STAGE_DESCRIPTOR_SIZE = 384
STAGE_DESCRIPTOR_FILE = "bk7258-ota-stage.bin"
STAGE_REPORT_FILE = "bk7258-ota-stage.json"

# Explicit little-endian wire format; the target never casts it to a struct.
STAGE_HEADER = struct.Struct(
    "<8sHHI32s48sQ14I24s24s32s32s32s32s32s12sI"
)
assert STAGE_HEADER.size == STAGE_DESCRIPTOR_SIZE


class StageDescriptorError(RuntimeError):
    """Raised when an N15-A bundle cannot become a canonical descriptor."""


def encode_c_string(value: str, size: int, field: str) -> bytes:
    try:
        encoded = value.encode("ascii")
    except UnicodeEncodeError as error:
        raise StageDescriptorError(f"{field} must be ASCII") from error
    if not encoded or len(encoded) >= size or b"\0" in encoded:
        raise StageDescriptorError(
            f"{field} must be non-empty and fit a NUL-terminated {size}-byte field"
        )
    return encoded.ljust(size, b"\0")


def sha256(payload: bytes) -> bytes:
    return hashlib.sha256(payload).digest()


def component(manifest: dict[str, object], role: str) -> dict[str, object]:
    entries = manifest.get("components")
    if not isinstance(entries, list):
        raise StageDescriptorError("pair components are missing")
    matches = [entry for entry in entries if isinstance(entry, dict) and entry.get("role") == role]
    if len(matches) != 1:
        raise StageDescriptorError(f"pair must contain exactly one {role} component")
    return matches[0]


def build_stage_descriptor(
    bundle: Path,
    *,
    expected_generation: int,
    expected_version: str,
    expected_base_version: str,
    expected_timestamp: int,
    sdk_source: Path | None = None,
    target_slot: str = "b",
) -> tuple[bytes, dict[str, object]]:
    """Verify the canonical bundle, then bind it into the fixed target ABI."""

    verified = verify_bundle(
        bundle,
        expected_generation=expected_generation,
        expected_version=expected_version,
        expected_base_version=expected_base_version,
        expected_timestamp=expected_timestamp,
        sdk_source=sdk_source,
    )
    manifest_raw, manifest = load_manifest(bundle / MANIFEST_FILE)
    cp = (bundle / CP_FILE).read_bytes()
    ap = (bundle / AP_FILE).read_bytes()
    logical = (bundle / RBL_FILE).read_bytes()
    physical = (bundle / S_APP_FILE).read_bytes()
    cp_entry = component(manifest, "cp")
    ap_entry = component(manifest, "ap")
    pair = manifest.get("pair")
    if not isinstance(pair, dict):
        raise StageDescriptorError("pair geometry is missing")

    if target_slot not in {"a", "b"}:
        raise StageDescriptorError("target_slot must be exactly 'a' or 'b'")
    target_physical_offset = CP_A_START if target_slot == "a" else PAIR_B_START

    values: tuple[object, ...] = (
        STAGE_MAGIC,
        STAGE_FORMAT,
        STAGE_DESCRIPTOR_SIZE,
        STAGE_FLAGS,
        encode_c_string(PAIR_SCHEMA, 32, "schema"),
        encode_c_string(LAYOUT_ID, 48, "layout_id"),
        expected_generation,
        expected_timestamp,
        target_physical_offset,
        pair["physical_size"],
        pair["logical_size"],
        pair["rbl_header_offset"],
        pair["rbl_header_physical_offset"],
        cp_entry["logical_offset"],
        cp_entry["logical_capacity"],
        len(cp),
        cp_entry["xip_start"],
        ap_entry["logical_offset"],
        ap_entry["logical_capacity"],
        len(ap),
        ap_entry["xip_start"],
        encode_c_string(expected_version, 24, "version"),
        encode_c_string(expected_base_version, 24, "base_version"),
        sha256(physical),
        sha256(logical),
        sha256(cp),
        sha256(ap),
        sha256(manifest_raw),
        bytes(12),
        0,
    )
    descriptor = STAGE_HEADER.pack(*values)
    header_crc32 = zlib.crc32(descriptor[:-4]) & 0xFFFFFFFF
    descriptor = descriptor[:-4] + struct.pack("<I", header_crc32)

    report: dict[str, object] = {
        "format": STAGE_FORMAT,
        "status": "pass",
        "descriptor_size": len(descriptor),
        "descriptor_sha256": hashlib.sha256(descriptor).hexdigest(),
        "descriptor_crc32": header_crc32,
        "schema": PAIR_SCHEMA,
        "layout_id": LAYOUT_ID,
        "generation": expected_generation,
        "version": expected_version,
        "base_version": expected_base_version,
        "timestamp": expected_timestamp,
        "target_slot": target_slot,
        "physical_offset": target_physical_offset,
        "physical_size": len(physical),
        "physical_sha256": hashlib.sha256(physical).hexdigest(),
        "logical_sha256": hashlib.sha256(logical).hexdigest(),
        "cp_sha256": hashlib.sha256(cp).hexdigest(),
        "ap_sha256": hashlib.sha256(ap).hexdigest(),
        "manifest_sha256": hashlib.sha256(manifest_raw).hexdigest(),
        "bundle_verification": verified,
        "gates": {
            "compile_write_enabled": False,
            "runtime_write_enabled": False,
            "remap_enabled": False,
            "trial_metadata_mutation_enabled": False,
            "board_write_authorized": False,
        },
        "security": {
            "integrity_only": True,
            "publisher_authenticated": False,
            "anti_rollback": False,
        },
    }
    return descriptor, report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--expected-generation", type=parse_int, required=True)
    parser.add_argument("--expected-version", required=True)
    parser.add_argument("--expected-base-version", required=True)
    parser.add_argument("--expected-timestamp", type=parse_int, required=True)
    parser.add_argument("--sdk-source", type=Path)
    parser.add_argument("--target-slot", choices=("a", "b"), default="b")
    args = parser.parse_args()

    try:
        descriptor, report = build_stage_descriptor(
            args.bundle,
            expected_generation=args.expected_generation,
            expected_version=args.expected_version,
            expected_base_version=args.expected_base_version,
            expected_timestamp=args.expected_timestamp,
            sdk_source=args.sdk_source,
            target_slot=args.target_slot,
        )
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(descriptor)
        report_path = args.report or args.output.with_name(STAGE_REPORT_FILE)
        report_path.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
    except (OSError, KeyError, TypeError, ValueError, StageDescriptorError) as error:
        print(f"BK7258 N15-B stage descriptor FAIL: {error}")
        return 1

    print(
        "BK7258 N15-B stage descriptor PASS: "
        f"generation={report['generation']} version={report['version']} "
        "writes_enabled=false"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
