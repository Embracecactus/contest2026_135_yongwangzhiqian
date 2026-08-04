#!/usr/bin/env python3
"""Build the deterministic, initially-pending N15-C boot metadata sector."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import zlib
from pathlib import Path

from bk7258_ab_layout import (
    AP_A_SIZE,
    AP_XIP_SIZE,
    AP_XIP_START,
    CP_A_START,
    CP_A_SIZE,
    CP_XIP_SIZE,
    CP_XIP_START,
    OTA_METADATA_SIZE,
    OTA_METADATA_START,
    PAIR_B_SIZE,
    PAIR_B_START,
)
from bk7258_crc_expand import APP_MAGIC, ExpansionError, PACKET_TOTAL, decode
from pack_bk7258_ota_pair import parse_int
from pack_bk7258_ota_stage import (
    STAGE_DESCRIPTOR_SIZE,
    build_stage_descriptor,
    encode_c_string,
)


BOOT_METADATA_MAGIC = b"BKOTA15C"
BOOT_METADATA_FORMAT = 1
BOOT_METADATA_RECORD_SIZE = 512
BOOT_METADATA_RECORD_COUNT = OTA_METADATA_SIZE // BOOT_METADATA_RECORD_SIZE
BOOT_METADATA_FILE = "bk7258-ota-metadata.bin"
BOOT_METADATA_REPORT_FILE = "bk7258-ota-metadata.json"

META_PENDING_B = 1
META_TRIAL_STARTED = 2
META_CONFIRMED_B = 3
META_ROLLBACK_A = 4

# Explicit little-endian wire format.  The embedded N15-B descriptor starts
# at byte 124 and the record CRC32 covers bytes 0..507.
BOOT_METADATA_RECORD = struct.Struct("<8sHHIQQIII24s24s32s384sI")
BOOT_METADATA_DESCRIPTOR_OFFSET = struct.calcsize("<8sHHIQQIII24s24s32s")
assert BOOT_METADATA_RECORD.size == BOOT_METADATA_RECORD_SIZE
assert BOOT_METADATA_RECORD_COUNT == 8
assert BOOT_METADATA_DESCRIPTOR_OFFSET == 124


class BootMetadataError(RuntimeError):
    """Raised when primary or candidate inputs cannot form N15-C metadata."""


def sha256(payload: bytes) -> bytes:
    return hashlib.sha256(payload).digest()


def validate_encoded_component(
    encoded: bytes,
    *,
    role: str,
    physical_capacity: int,
    logical_capacity: int,
    xip_start: int,
    require_magic: bool,
) -> bytes:
    if not encoded or len(encoded) % PACKET_TOTAL:
        raise BootMetadataError(
            f"primary {role} physical length must be a non-zero multiple of "
            f"{PACKET_TOTAL}"
        )
    if len(encoded) > physical_capacity:
        raise BootMetadataError(f"primary {role} exceeds its physical slot")

    try:
        logical = decode(encoded)
    except ExpansionError as error:
        raise BootMetadataError(f"primary {role}: {error}") from error
    if len(logical) > logical_capacity or len(logical) < (0x108 if require_magic else 8):
        raise BootMetadataError(f"primary {role} logical length is outside its slot")

    msp, reset = struct.unpack_from("<II", logical)
    reset_address = reset & ~1
    if not 0x28000000 <= msp < 0x280A0000:
        raise BootMetadataError(f"primary {role} MSP is outside BK7258 SRAM")
    if (reset & 1) == 0:
        raise BootMetadataError(f"primary {role} reset vector is not Thumb")
    if not xip_start <= reset_address < xip_start + len(logical):
        raise BootMetadataError(f"primary {role} reset vector is outside its image")
    if require_magic and logical[0x100:0x108] != APP_MAGIC:
        raise BootMetadataError("primary CP image is missing BK7236 magic")
    return logical


def build_primary_pair(cp_encoded: bytes, ap_encoded: bytes) -> bytes:
    """Validate the actual encoded A images and reproduce their Flash slots."""

    validate_encoded_component(
        cp_encoded,
        role="CP",
        physical_capacity=CP_A_SIZE,
        logical_capacity=CP_XIP_SIZE,
        xip_start=CP_XIP_START,
        require_magic=True,
    )
    validate_encoded_component(
        ap_encoded,
        role="AP",
        physical_capacity=AP_A_SIZE,
        logical_capacity=AP_XIP_SIZE,
        xip_start=AP_XIP_START,
        require_magic=False,
    )

    primary = bytearray(b"\xff" * PAIR_B_SIZE)
    primary[: len(cp_encoded)] = cp_encoded
    primary[CP_A_SIZE : CP_A_SIZE + len(ap_encoded)] = ap_encoded
    return bytes(primary)


def build_record(
    *,
    state: int,
    sequence: int,
    generation: int,
    timestamp: int,
    cp_physical_length: int,
    ap_physical_length: int,
    version: str,
    base_version: str,
    primary_sha256: bytes,
    descriptor: bytes,
) -> bytes:
    """Build one append-only state record; exposed for the N15-D verifier."""

    if state not in {
        META_PENDING_B,
        META_TRIAL_STARTED,
        META_CONFIRMED_B,
        META_ROLLBACK_A,
    }:
        raise BootMetadataError("invalid metadata state")
    if sequence <= 0 or sequence > 0xFFFFFFFFFFFFFFFF:
        raise BootMetadataError("metadata sequence is outside uint64")
    if generation <= 0 or generation > 0xFFFFFFFFFFFFFFFF:
        raise BootMetadataError("metadata generation is outside uint64")
    if timestamp < 0 or timestamp > 0xFFFFFFFF:
        raise BootMetadataError("metadata timestamp is outside uint32")
    if (
        cp_physical_length < 9 * PACKET_TOTAL
        or cp_physical_length > CP_A_SIZE
        or cp_physical_length % PACKET_TOTAL
    ):
        raise BootMetadataError("invalid primary CP physical length")
    if (
        ap_physical_length < PACKET_TOTAL
        or ap_physical_length > AP_A_SIZE
        or ap_physical_length % PACKET_TOTAL
    ):
        raise BootMetadataError("invalid primary AP physical length")
    if len(primary_sha256) != 32 or not any(primary_sha256):
        raise BootMetadataError("primary pair SHA-256 is invalid")
    if len(descriptor) != STAGE_DESCRIPTOR_SIZE:
        raise BootMetadataError("N15-B descriptor must be exactly 384 bytes")
    if version == base_version:
        raise BootMetadataError("candidate and base versions must differ")

    record = BOOT_METADATA_RECORD.pack(
        BOOT_METADATA_MAGIC,
        BOOT_METADATA_FORMAT,
        BOOT_METADATA_RECORD_SIZE,
        state,
        sequence,
        generation,
        timestamp,
        cp_physical_length,
        ap_physical_length,
        encode_c_string(version, 24, "version"),
        encode_c_string(base_version, 24, "base_version"),
        primary_sha256,
        descriptor,
        0,
    )
    return record[:-4] + struct.pack("<I", zlib.crc32(record[:-4]) & 0xFFFFFFFF)


def append_record(metadata: bytes, record: bytes) -> bytes:
    """Append to the first erased record without accepting dirty gaps."""

    if len(metadata) != OTA_METADATA_SIZE or len(record) != BOOT_METADATA_RECORD_SIZE:
        raise BootMetadataError("metadata/record size mismatch")
    output = bytearray(metadata)
    erased = b"\xff" * BOOT_METADATA_RECORD_SIZE
    for index in range(BOOT_METADATA_RECORD_COUNT):
        start = index * BOOT_METADATA_RECORD_SIZE
        if output[start : start + BOOT_METADATA_RECORD_SIZE] == erased:
            if output[start:] != b"\xff" * (OTA_METADATA_SIZE - start):
                raise BootMetadataError("metadata contains a dirty record gap")
            output[start : start + BOOT_METADATA_RECORD_SIZE] = record
            return bytes(output)
    raise BootMetadataError("metadata sector has no erased record")


def build_boot_metadata(
    bundle: Path,
    cp_crc: Path,
    ap_crc: Path,
    *,
    generation: int,
    version: str,
    base_version: str,
    timestamp: int,
    sdk_source: Path | None = None,
) -> tuple[bytes, bytes, dict[str, object]]:
    descriptor, stage_report = build_stage_descriptor(
        bundle,
        expected_generation=generation,
        expected_version=version,
        expected_base_version=base_version,
        expected_timestamp=timestamp,
        sdk_source=sdk_source,
    )
    cp_encoded = cp_crc.read_bytes()
    ap_encoded = ap_crc.read_bytes()
    primary = build_primary_pair(cp_encoded, ap_encoded)
    primary_digest = sha256(primary)
    record = build_record(
        state=META_PENDING_B,
        sequence=1,
        generation=generation,
        timestamp=timestamp,
        cp_physical_length=len(cp_encoded),
        ap_physical_length=len(ap_encoded),
        version=version,
        base_version=base_version,
        primary_sha256=primary_digest,
        descriptor=descriptor,
    )
    metadata = record + b"\xff" * (OTA_METADATA_SIZE - len(record))
    report: dict[str, object] = {
        "format": BOOT_METADATA_FORMAT,
        "status": "pass",
        "metadata_offset": OTA_METADATA_START,
        "metadata_size": len(metadata),
        "metadata_sha256": hashlib.sha256(metadata).hexdigest(),
        "record_size": BOOT_METADATA_RECORD_SIZE,
        "record_count": BOOT_METADATA_RECORD_COUNT,
        "valid_records": 1,
        "state": "pending_b",
        "sequence": 1,
        "generation": generation,
        "timestamp": timestamp,
        "version": version,
        "base_version": base_version,
        "primary": {
            "physical_offset": CP_A_START,
            "physical_size": len(primary),
            "cp_physical_length": len(cp_encoded),
            "ap_physical_length": len(ap_encoded),
            "sha256": primary_digest.hex(),
        },
        "secondary": {
            "physical_offset": PAIR_B_START,
            "physical_size": PAIR_B_SIZE,
            "descriptor_sha256": hashlib.sha256(descriptor).hexdigest(),
            "candidate_sha256": stage_report["physical_sha256"],
        },
        "gates": {
            "compile_selection_enabled": False,
            "runtime_selection_enabled": False,
            "compile_remap_enabled": False,
            "runtime_remap_enabled": False,
            "trial_metadata_mutation_enabled": False,
            "board_write_authorized": False,
        },
        "security": {
            "integrity_only": True,
            "publisher_authenticated": False,
            "anti_rollback": False,
        },
    }
    return metadata, primary, report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--cp-crc", type=Path, required=True)
    parser.add_argument("--ap-crc", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--record-output", type=Path)
    parser.add_argument("--descriptor-output", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--expected-generation", type=parse_int, required=True)
    parser.add_argument("--expected-version", required=True)
    parser.add_argument("--expected-base-version", required=True)
    parser.add_argument("--expected-timestamp", type=parse_int, required=True)
    parser.add_argument("--sdk-source", type=Path)
    parser.add_argument("--validation-profile", action="store_true")
    args = parser.parse_args()

    try:
        metadata, _, report = build_boot_metadata(
            args.bundle,
            args.cp_crc,
            args.ap_crc,
            generation=args.expected_generation,
            version=args.expected_version,
            base_version=args.expected_base_version,
            timestamp=args.expected_timestamp,
            sdk_source=args.sdk_source,
        )
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(metadata)
        record_output = args.record_output or args.output.with_name(
            "bk7258-ota-pending-record.bin"
        )
        record_output.parent.mkdir(parents=True, exist_ok=True)
        record = metadata[:BOOT_METADATA_RECORD_SIZE]
        record_output.write_bytes(record)
        descriptor_output = args.descriptor_output or args.output.with_name(
            "bk7258-ota-stage.bin"
        )
        descriptor_output.parent.mkdir(parents=True, exist_ok=True)
        descriptor = record[
            BOOT_METADATA_DESCRIPTOR_OFFSET :
            BOOT_METADATA_DESCRIPTOR_OFFSET + STAGE_DESCRIPTOR_SIZE
        ]
        descriptor_output.write_bytes(descriptor)
        report["pending_record"] = {
            "path": str(record_output),
            "size": len(record),
            "sha256": hashlib.sha256(record).hexdigest(),
        }
        report["stage_descriptor"] = {
            "path": str(descriptor_output),
            "size": len(descriptor),
            "sha256": hashlib.sha256(descriptor).hexdigest(),
        }
        if args.validation_profile:
            report["gates"].update(
                {
                    "compile_selection_enabled": True,
                    "runtime_selection_enabled": True,
                    "compile_remap_enabled": True,
                    "runtime_remap_enabled": True,
                    "trial_metadata_mutation_enabled": True,
                }
            )
        report["validation_profile"] = args.validation_profile
        report_path = args.report or args.output.with_name(BOOT_METADATA_REPORT_FILE)
        report_path.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
    except (OSError, KeyError, TypeError, ValueError, BootMetadataError) as error:
        print(f"BK7258 N15-C boot metadata FAIL: {error}")
        return 1

    print(
        "BK7258 N15-C boot metadata PASS: "
        f"generation={report['generation']} state=pending_b "
        f"validation_profile={str(args.validation_profile).lower()}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
