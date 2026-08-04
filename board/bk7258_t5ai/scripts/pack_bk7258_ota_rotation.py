#!/usr/bin/env python3
"""Build one deterministic format-2 symmetric OTA metadata bank."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import zlib
from pathlib import Path

from bk7258_ab_layout import (
    CP_A_SIZE,
    OTA_METADATA_MIRROR_START,
    OTA_METADATA_SIZE,
    OTA_METADATA_START,
)
from pack_bk7258_ota_metadata import (
    BOOT_METADATA_RECORD_SIZE,
    BootMetadataError,
    build_primary_pair,
)
from pack_bk7258_ota_pair import parse_int
from pack_bk7258_ota_stage import (
    STAGE_DESCRIPTOR_SIZE,
    StageDescriptorError,
    build_stage_descriptor,
    encode_c_string,
)


ROTATION_MAGIC = b"BKOTA15R"
ROTATION_FORMAT = 2
ROTATION_RECORD = struct.Struct("<8sHHIQQIII24s24s32s384sI")
ROTATION_RECORD_COUNT = OTA_METADATA_SIZE // BOOT_METADATA_RECORD_SIZE
ROTATION_DESCRIPTOR_OFFSET = struct.calcsize("<8sHHIQQIII24s24s32s")

PENDING_B = 1
PENDING_A = 5

assert ROTATION_RECORD.size == BOOT_METADATA_RECORD_SIZE
assert ROTATION_RECORD_COUNT == 8
assert ROTATION_DESCRIPTOR_OFFSET == 124


class RotationPackError(RuntimeError):
    """Raised when a slot-neutral pending bank cannot be constructed."""


def build_rotation_bank(
    bundle: Path,
    base_cp_crc: Path,
    base_ap_crc: Path,
    *,
    target_slot: str,
    bank: int,
    generation: int,
    version: str,
    base_version: str,
    timestamp: int,
    sdk_source: Path | None = None,
    base_pair_path: Path | None = None,
) -> tuple[bytes, bytes, bytes, dict[str, object]]:
    if target_slot not in {"a", "b"}:
        raise RotationPackError("target_slot must be exactly 'a' or 'b'")
    if bank not in {0, 1}:
        raise RotationPackError("bank must be exactly 0 or 1")
    if generation <= 0 or generation > 0xFFFFFFFFFFFFFFFF:
        raise RotationPackError("generation must fit uint64 and be non-zero")
    if timestamp < 0 or timestamp > 0xFFFFFFFF:
        raise RotationPackError("timestamp must fit uint32")
    if version == base_version:
        raise RotationPackError("candidate and base versions must differ")

    descriptor, stage_report = build_stage_descriptor(
        bundle,
        expected_generation=generation,
        expected_version=version,
        expected_base_version=base_version,
        expected_timestamp=timestamp,
        sdk_source=sdk_source,
        target_slot=target_slot,
    )
    cp_encoded = base_cp_crc.read_bytes()
    ap_encoded = base_ap_crc.read_bytes()
    reconstructed_base = build_primary_pair(cp_encoded, ap_encoded)
    if base_pair_path is None:
        if target_slot == "a":
            raise RotationPackError(
                "target A requires --base-pair for the stable B CRC container"
            )
        base_pair = reconstructed_base
        base_representation = "factory-split-pair"
    else:
        base_pair = base_pair_path.read_bytes()
        if len(base_pair) != len(reconstructed_base):
            raise RotationPackError("base pair must fill the exact A/B pair size")
        if base_pair[: len(cp_encoded)] != cp_encoded:
            raise RotationPackError("base pair CP bytes do not match base CP image")
        if base_pair[CP_A_SIZE : CP_A_SIZE + len(ap_encoded)] != ap_encoded:
            raise RotationPackError("base pair AP bytes do not match base AP image")
        base_representation = "crc-container"
    base_digest = hashlib.sha256(base_pair).digest()
    state = PENDING_A if target_slot == "a" else PENDING_B
    record = ROTATION_RECORD.pack(
        ROTATION_MAGIC,
        ROTATION_FORMAT,
        BOOT_METADATA_RECORD_SIZE,
        state,
        1,
        generation,
        timestamp,
        len(cp_encoded),
        len(ap_encoded),
        encode_c_string(version, 24, "version"),
        encode_c_string(base_version, 24, "base_version"),
        base_digest,
        descriptor,
        0,
    )
    record = record[:-4] + struct.pack(
        "<I", zlib.crc32(record[:-4]) & 0xFFFFFFFF
    )
    bank_image = record + b"\xff" * (OTA_METADATA_SIZE - len(record))
    bank_start = OTA_METADATA_START if bank == 0 else OTA_METADATA_MIRROR_START
    report: dict[str, object] = {
        "format": ROTATION_FORMAT,
        "status": "pass",
        "state": "pending_a" if target_slot == "a" else "pending_b",
        "target_slot": target_slot,
        "base_slot": "b" if target_slot == "a" else "a",
        "bank": bank,
        "bank_offset": bank_start,
        "bank_size": len(bank_image),
        "bank_sha256": hashlib.sha256(bank_image).hexdigest(),
        "record_size": len(record),
        "record_sha256": hashlib.sha256(record).hexdigest(),
        "generation": generation,
        "sequence": 1,
        "timestamp": timestamp,
        "version": version,
        "base_version": base_version,
        "base_cp_physical_length": len(cp_encoded),
        "base_ap_physical_length": len(ap_encoded),
        "base_pair_sha256": base_digest.hex(),
        "base_pair_representation": base_representation,
        "descriptor_sha256": hashlib.sha256(descriptor).hexdigest(),
        "descriptor": stage_report,
        "gates": {
            "metadata_publish_enabled": False,
            "slot_write_enabled": False,
            "board_write_authorized": False,
        },
    }
    return bank_image, record, descriptor, report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--base-cp-crc", type=Path, required=True)
    parser.add_argument("--base-ap-crc", type=Path, required=True)
    parser.add_argument("--base-pair", type=Path)
    parser.add_argument("--target-slot", choices=("a", "b"), required=True)
    parser.add_argument("--bank", type=int, choices=(0, 1), required=True)
    parser.add_argument("--generation", type=parse_int, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--base-version", required=True)
    parser.add_argument("--timestamp", type=parse_int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--record-output", type=Path)
    parser.add_argument("--descriptor-output", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--sdk-source", type=Path)
    args = parser.parse_args()

    try:
        bank_image, record, descriptor, report = build_rotation_bank(
            args.bundle,
            args.base_cp_crc,
            args.base_ap_crc,
            target_slot=args.target_slot,
            bank=args.bank,
            generation=args.generation,
            version=args.version,
            base_version=args.base_version,
            timestamp=args.timestamp,
            sdk_source=args.sdk_source,
            base_pair_path=args.base_pair,
        )
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(bank_image)
        record_output = args.record_output or args.output.with_name(
            "bk7258-ota-rotation-pending.bin"
        )
        descriptor_output = args.descriptor_output or args.output.with_name(
            "bk7258-ota-stage.bin"
        )
        report_path = args.report or args.output.with_suffix(".json")
        record_output.write_bytes(record)
        descriptor_output.write_bytes(descriptor)
        report["record_path"] = str(record_output)
        report["descriptor_path"] = str(descriptor_output)
        report_path.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
    except (
        BootMetadataError,
        RotationPackError,
        StageDescriptorError,
        OSError,
        KeyError,
        TypeError,
        ValueError,
    ) as error:
        print(f"BK7258 format-2 rotation pack FAIL: {error}")
        return 1

    print(
        "BK7258 format-2 rotation pack PASS: "
        f"bank={args.bank} target={args.target_slot} "
        f"generation={args.generation} writes_enabled=false"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
