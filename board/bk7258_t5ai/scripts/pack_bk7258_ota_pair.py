#!/usr/bin/env python3
"""Build a deterministic, non-selectable BK7258 CP/AP N15-A pair bundle."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

from bk7258_ab_layout import (
    AP_XIP_SIZE,
    AP_XIP_START,
    CP_XIP_SIZE,
    CP_XIP_START,
    LAYOUT_ID,
    PAIR_B_SIZE,
    PAIR_B_START,
    crc_physical_size,
    report as layout_report,
)
from bk7258_crc_expand import APP_MAGIC, expand
from inspect_bk7258_rbl import (
    AB_HEADER_TAIL_DISTANCE,
    HEADER,
    VerificationError as RblVerificationError,
    inspect as inspect_rbl,
    make_plain_rbl,
)


PAIR_SCHEMA = "bk7258-cp-ap-pair-v1"
PAIR_FORMAT = 1
SDK_RELEASE = "v3.1.1.9"
RBL_FORMAT = "beken-rbl-v3.1.1.9"
RBL_APP_PARTITION = "app"
RBL_CURRENT_VERSION = "00010203040506070809"
PAIR_LOGICAL_SIZE = CP_XIP_SIZE + AP_XIP_SIZE
PAIR_AP_LOGICAL_OFFSET = CP_XIP_SIZE
RBL_HEADER_OFFSET = PAIR_LOGICAL_SIZE - AB_HEADER_TAIL_DISTANCE
BODY_ALIGNMENT = 64
AP_MAX_BODY_SIZE = RBL_HEADER_OFFSET - PAIR_AP_LOGICAL_OFFSET - BODY_ALIGNMENT
SRAM_START = 0x28000000
SRAM_END = 0x280A0000

CP_FILE = "cp-app.bin"
AP_FILE = "ap-app.bin"
BODY_FILE = "pair-body.bin"
RBL_FILE = "pair.rbl"
S_APP_FILE = "s_app-candidate.bin"
MANIFEST_FILE = "bk7258-ota-pair.json"

VERSION_PATTERN = re.compile(r"[A-Za-z0-9][A-Za-z0-9._+-]*\Z")

# These hashes pin only the official v3.1.1.9 format/packing inputs.  The
# proprietary source remains external and read-only.
OFFICIAL_SOURCE_HASHES = {
    "tools/env_tools/rtt_ota/ota-rbl/ota_packager_python.py": (
        "940e36c11168de0ee8bc2840db1e4f3c0164271ba0173fe9180d9b94fc25e911"
    ),
    "tools/env_tools/bk_py_libs/bk_crc/bk_crc16.py": (
        "34bbb32004e79c925c5aea59d0932cae0541b6e4b32b181c2f2f8d8ff2d46b0f"
    ),
    "tools/build_tools/build_process/bk_sdk/bk_ota_pack.py": (
        "7c0226b32dbe79229838fc4254dae0150690e55ee85ba6d02cc8573012f96ea0"
    ),
    "projects/app_ab/partitions/bk7258/ota_rbl.config": (
        "9fdb7992de95428e0e7749733eb34ac7a8196973612d0aaf491fcba90b63b062"
    ),
}


class PairError(RuntimeError):
    """Raised when a candidate pair violates the frozen N15-A contract."""


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def parse_int(value: str) -> int:
    return int(value, 0)


def validate_generation(generation: int) -> int:
    if isinstance(generation, bool) or not isinstance(generation, int):
        raise PairError("generation must be an integer")
    if not 1 <= generation <= 0xFFFFFFFFFFFFFFFF:
        raise PairError("generation must be in uint64 range 1..2^64-1")
    return generation


def validate_timestamp(timestamp: int) -> int:
    if isinstance(timestamp, bool) or not isinstance(timestamp, int):
        raise PairError("timestamp must be an integer")
    if not 0 <= timestamp <= 0xFFFFFFFF:
        raise PairError("timestamp must fit the official uint32 RBL field")
    return timestamp


def validate_version(value: str, field: str) -> str:
    if not isinstance(value, str) or not VERSION_PATTERN.fullmatch(value):
        raise PairError(
            f"{field} must match [A-Za-z0-9][A-Za-z0-9._+-]*"
        )
    if len(value.encode("ascii")) > 23:
        raise PairError(f"{field} must fit a NUL-terminated 24-byte RBL field")
    return value


def validate_component(
    payload: bytes,
    *,
    role: str,
    xip_start: int,
    maximum_size: int,
    require_magic: bool,
) -> tuple[int, int]:
    if len(payload) < 8:
        raise PairError(f"{role} image is too small to contain vectors")
    if len(payload) > maximum_size:
        raise PairError(
            f"{role} image size 0x{len(payload):x} exceeds 0x{maximum_size:x}"
        )
    msp, reset = struct.unpack_from("<II", payload)
    reset_address = reset & ~1
    if not SRAM_START <= msp < SRAM_END:
        raise PairError(f"{role} MSP 0x{msp:08x} is outside BK7258 SRAM")
    if (reset & 1) == 0:
        raise PairError(f"{role} reset vector 0x{reset:08x} is not Thumb")
    if not xip_start <= reset_address < xip_start + len(payload):
        raise PairError(
            f"{role} reset vector 0x{reset:08x} is outside its image range"
        )
    if require_magic:
        if len(payload) < 0x108 or payload[0x100:0x108] != APP_MAGIC:
            raise PairError("CP image is missing BK7236 magic at raw offset 0x100")
    return msp, reset


def build_pair_body(cp: bytes, ap: bytes) -> bytes:
    validate_component(
        cp,
        role="CP",
        xip_start=CP_XIP_START,
        maximum_size=CP_XIP_SIZE,
        require_magic=True,
    )
    validate_component(
        ap,
        role="AP",
        xip_start=AP_XIP_START,
        maximum_size=AP_MAX_BODY_SIZE,
        require_magic=False,
    )
    body_size = align_up(PAIR_AP_LOGICAL_OFFSET + len(ap), BODY_ALIGNMENT)
    if body_size >= RBL_HEADER_OFFSET:
        raise PairError("AP body leaves no erased space before the RBL tail header")
    body = bytearray(b"\xff" * body_size)
    body[: len(cp)] = cp
    body[PAIR_AP_LOGICAL_OFFSET : PAIR_AP_LOGICAL_OFFSET + len(ap)] = ap
    return bytes(body)


def artifact(file_name: str, payload: bytes) -> dict[str, object]:
    return {
        "file": file_name,
        "length": len(payload),
        "sha256": sha256_bytes(payload),
    }


def component_entry(
    *,
    role: str,
    file_name: str,
    payload: bytes,
    logical_offset: int,
    logical_capacity: int,
    xip_start: int,
    generation: int,
    version: str,
) -> dict[str, object]:
    msp, reset = validate_component(
        payload,
        role=role,
        xip_start=xip_start,
        maximum_size=(CP_XIP_SIZE if role == "CP" else AP_MAX_BODY_SIZE),
        require_magic=role == "CP",
    )
    return {
        "role": role.lower(),
        "file": file_name,
        "generation": generation,
        "version": version,
        "logical_offset": logical_offset,
        "logical_capacity": logical_capacity,
        "xip_start": xip_start,
        "length": len(payload),
        "sha256": sha256_bytes(payload),
        "msp": msp,
        "reset": reset,
    }


def build_bundle(
    cp: bytes,
    ap: bytes,
    *,
    generation: int,
    version: str,
    base_version: str,
    timestamp: int,
) -> tuple[dict[str, bytes], dict[str, object]]:
    """Return canonical bundle files and their deterministic manifest."""

    layout_report()
    generation = validate_generation(generation)
    version = validate_version(version, "version")
    base_version = validate_version(base_version, "base_version")
    timestamp = validate_timestamp(timestamp)
    if version == base_version:
        raise PairError("candidate version must differ from base_version")
    if PAIR_LOGICAL_SIZE % 32:
        raise PairError("logical pair size is not CRC packet aligned")
    if crc_physical_size(PAIR_LOGICAL_SIZE) != PAIR_B_SIZE:
        raise PairError("logical/physical pair size conversion drift")

    body = build_pair_body(cp, ap)
    try:
        logical_rbl = make_plain_rbl(
            body,
            "ab",
            container_size=PAIR_LOGICAL_SIZE,
            app_partition=RBL_APP_PARTITION,
            download_version=version,
            current_version=RBL_CURRENT_VERSION,
            timestamp=timestamp,
        )
        rbl_report = inspect_rbl(logical_rbl, "ab", allow_encoded=False)
    except RblVerificationError as error:
        raise PairError(str(error)) from error

    physical_rbl = expand(logical_rbl)
    if len(physical_rbl) != PAIR_B_SIZE:
        raise PairError("encoded s_app candidate does not fill its exact partition")

    cp_entry = component_entry(
        role="CP",
        file_name=CP_FILE,
        payload=cp,
        logical_offset=0,
        logical_capacity=CP_XIP_SIZE,
        xip_start=CP_XIP_START,
        generation=generation,
        version=version,
    )
    ap_entry = component_entry(
        role="AP",
        file_name=AP_FILE,
        payload=ap,
        logical_offset=PAIR_AP_LOGICAL_OFFSET,
        logical_capacity=AP_XIP_SIZE,
        xip_start=AP_XIP_START,
        generation=generation,
        version=version,
    )

    source_hashes = [
        {"path": path, "sha256": digest}
        for path, digest in sorted(OFFICIAL_SOURCE_HASHES.items())
    ]
    manifest: dict[str, object] = {
        "schema": PAIR_SCHEMA,
        "format": PAIR_FORMAT,
        "sdk_contract": {
            "release": SDK_RELEASE,
            "source_hashes": source_hashes,
        },
        "layout_id": LAYOUT_ID,
        "generation": generation,
        "version": version,
        "base_version": base_version,
        "timestamp": timestamp,
        "components": [cp_entry, ap_entry],
        "pair": {
            "body": artifact(BODY_FILE, body),
            "logical_size": PAIR_LOGICAL_SIZE,
            "ap_logical_offset": PAIR_AP_LOGICAL_OFFSET,
            "body_alignment": BODY_ALIGNMENT,
            "rbl_header_offset": RBL_HEADER_OFFSET,
            "rbl_header_physical_offset": crc_physical_size(RBL_HEADER_OFFSET),
            "physical_offset": PAIR_B_START,
            "physical_size": PAIR_B_SIZE,
            "physical_end": PAIR_B_START + PAIR_B_SIZE,
        },
        "rbl": {
            "format": RBL_FORMAT,
            "mode": "ab",
            "algorithm": 0,
            "app_partition": RBL_APP_PARTITION,
            "download_version": version,
            "current_version": RBL_CURRENT_VERSION,
            "header_size": HEADER.size,
            "header_offset": RBL_HEADER_OFFSET,
            "body_size": len(body),
            "raw_size": len(body),
            "header_crc32": rbl_report["header_crc32"],
            "body_crc32": rbl_report["body_crc32"],
            "body_fnv1a": rbl_report["body_fnv1a"],
            "logical_container": artifact(RBL_FILE, logical_rbl),
            "physical_container": {
                **artifact(S_APP_FILE, physical_rbl),
                "physical_offset": PAIR_B_START,
                "physical_end": PAIR_B_START + len(physical_rbl),
            },
        },
        "gates": {
            "boot_selectable": False,
            "staging_writes_enabled": False,
            "remap_enabled": False,
            "trial_metadata_mutation_enabled": False,
            "board_write_authorized": False,
        },
        "security": {
            "integrity_only": True,
            "publisher_authenticated": False,
            "anti_rollback": False,
            "note": "CRC32/FNV-1a/SHA-256 are integrity checks, not signatures",
        },
    }

    manifest_bytes = (
        json.dumps(manifest, indent=2, sort_keys=True, ensure_ascii=True) + "\n"
    ).encode("utf-8")
    files = {
        CP_FILE: cp,
        AP_FILE: ap,
        BODY_FILE: body,
        RBL_FILE: logical_rbl,
        S_APP_FILE: physical_rbl,
        MANIFEST_FILE: manifest_bytes,
    }
    return files, manifest


def write_bundle(output: Path, files: dict[str, bytes]) -> None:
    output.mkdir(parents=True, exist_ok=True)
    for name, payload in files.items():
        (output / name).write_bytes(payload)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cp-raw", type=Path, required=True)
    parser.add_argument("--ap-raw", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--generation", type=parse_int, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--base-version", required=True)
    parser.add_argument("--timestamp", type=parse_int, required=True)
    args = parser.parse_args()

    try:
        files, manifest = build_bundle(
            args.cp_raw.read_bytes(),
            args.ap_raw.read_bytes(),
            generation=args.generation,
            version=args.version,
            base_version=args.base_version,
            timestamp=args.timestamp,
        )
        write_bundle(args.output, files)
    except (OSError, PairError, RblVerificationError, ValueError) as error:
        print(f"BK7258 N15-A pair pack FAIL: {error}")
        return 1

    print(
        "BK7258 N15-A pair pack PASS: "
        f"generation={manifest['generation']} version={manifest['version']} "
        "writes_enabled=false"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
