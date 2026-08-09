#!/usr/bin/env python3
"""Encode and fail-closed verify the BK7258 N15 journal metadata ABI.

This module is host-only and performs no board or filesystem writes.  Its
formats mirror bootloader/boot_ota_abi.h exactly.  SHA-256 and CRC32 are
integrity checks, not a signature, publisher authentication, or anti-rollback
mechanism.
"""

from __future__ import annotations

import argparse
import binascii
import hashlib
import json
import struct
from dataclasses import dataclass
from typing import Any


FLASH_ID = 0x00C86517
ERASE_SIZE = 0x1000
WRITE_CHUNK_SIZE = 0x20
JOURNAL_COPY_SIZE = 0x13000
HEADER_SIZE = 0x100
CONTROL_SIZE = 0x100
PHASE_OFFSET = HEADER_SIZE + CONTROL_SIZE
PAIR_SECTORS = 0x30F
PHASES_PER_SECTOR = 3
PHASE_MARKERS = 0x92D
SCRATCH_START = 0x79B000

CP_ACTIVE_START = 0x011000
CP_STAGING_START = 0x440000
CP_SLOT_SIZE = 0x0EF000
AP_ACTIVE_START = 0x220000
AP_STAGING_START = 0x52F000
AP_SLOT_SIZE = 0x220000

HEADER_MAGIC = 0x314A4B42
MARKER_MAGIC = 0x314D4B42
FORMAT_VERSION = 1
FLAG_PAIRED = 1 << 0
FLAG_ONE_TRIAL = 1 << 1
FLAG_INTEGRITY_ONLY = 1 << 2
REQUIRED_FLAGS = FLAG_PAIRED | FLAG_ONE_TRIAL | FLAG_INTEGRITY_ONLY
IMAGE_ENCODING_CRC_PHYSICAL = 1

MARKER_ARM = 1
MARKER_DIRECTION_COMPLETE = 2
MARKER_TRIAL_STARTED = 3
MARKER_CONFIRMED = 4
MARKER_ROLLBACK_REQUESTED = 5
MARKER_RETIRED = 6
MARKER_SCRATCH_READY = 0x100
MARKER_ACTIVE_REPLACED = 0x101
MARKER_STAGING_REPLACED = 0x102
VALID_MARKER_KINDS = {
    MARKER_ARM,
    MARKER_DIRECTION_COMPLETE,
    MARKER_TRIAL_STARTED,
    MARKER_CONFIRMED,
    MARKER_ROLLBACK_REQUESTED,
    MARKER_RETIRED,
    MARKER_SCRATCH_READY,
    MARKER_ACTIVE_REPLACED,
    MARKER_STAGING_REPLACED,
}

UINT64_MAX = (1 << 64) - 1
ERASED_CHUNK = b"\xff" * WRITE_CHUNK_SIZE

# Header bytes [0x00, 0xfc), then CRC32 at 0xfc.  There are exactly eighteen
# uint32 fields between generation and the three digests.

HEADER_WITHOUT_CRC = struct.Struct("<IHHIIQQ18I32s32s32s52s")
HEADER = struct.Struct("<IHHIIQQ18I32s32s32s52sI")
MARKER_WITHOUT_CRC = struct.Struct("<IHHQQI")
MARKER = struct.Struct("<IHHQQII")


class MetadataError(RuntimeError):
    """Raised when persistent OTA metadata is not exact v1 data."""


def crc32(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


def require(condition: bool, message: str) -> None:
    if not condition:
        raise MetadataError(message)


def require_digest(name: str, value: bytes) -> None:
    require(len(value) == 32, f"{name} must be 32 bytes")
    require(value not in (b"\0" * 32, b"\xff" * 32), f"{name} is unset")


@dataclass(frozen=True)
class JournalHeaderV1:
    sequence: int
    generation: int
    cp_image_size: int
    ap_image_size: int
    flash_status_before: int
    pair_digest: bytes
    cp_slot_digest: bytes
    ap_slot_digest: bytes

    def encode(self) -> bytes:
        require(0 < self.sequence <= UINT64_MAX, "sequence is invalid")
        require(0 < self.generation <= UINT64_MAX, "generation is invalid")
        require(0 < self.cp_image_size <= CP_SLOT_SIZE, "CP image size is invalid")
        require(0 < self.ap_image_size <= AP_SLOT_SIZE, "AP image size is invalid")
        require(
            0 <= self.flash_status_before <= 0xFFFF,
            "Flash status snapshot is invalid",
        )
        require_digest("pair digest", self.pair_digest)
        require_digest("CP slot digest", self.cp_slot_digest)
        require_digest("AP slot digest", self.ap_slot_digest)

        prefix = HEADER_WITHOUT_CRC.pack(
            HEADER_MAGIC,
            FORMAT_VERSION,
            HEADER_SIZE,
            JOURNAL_COPY_SIZE,
            REQUIRED_FLAGS,
            self.sequence,
            self.generation,
            FLASH_ID,
            ERASE_SIZE,
            WRITE_CHUNK_SIZE,
            PHASE_OFFSET,
            PHASE_MARKERS,
            PAIR_SECTORS,
            PHASES_PER_SECTOR,
            SCRATCH_START,
            CP_ACTIVE_START,
            CP_STAGING_START,
            CP_SLOT_SIZE,
            self.cp_image_size,
            AP_ACTIVE_START,
            AP_STAGING_START,
            AP_SLOT_SIZE,
            self.ap_image_size,
            self.flash_status_before,
            IMAGE_ENCODING_CRC_PHYSICAL,
            self.pair_digest,
            self.cp_slot_digest,
            self.ap_slot_digest,
            b"\xff" * 52,
        )
        require(len(prefix) == HEADER_SIZE - 4, "header prefix size drifted")
        return prefix + struct.pack("<I", crc32(prefix))

    @staticmethod
    def decode(data: bytes) -> "JournalHeaderV1":
        require(len(data) == HEADER_SIZE, "journal header size is not 0x100")
        values = HEADER.unpack(data)
        (
            magic,
            version,
            header_size,
            journal_copy_size,
            flags,
            sequence,
            generation,
            flash_id,
            erase_size,
            write_chunk_size,
            phase_offset,
            phase_marker_count,
            pair_sector_count,
            phases_per_sector,
            scratch_start,
            cp_active_start,
            cp_staging_start,
            cp_slot_size,
            cp_image_size,
            ap_active_start,
            ap_staging_start,
            ap_slot_size,
            ap_image_size,
            flash_status_before,
            image_encoding,
            pair_digest,
            cp_slot_digest,
            ap_slot_digest,
            reserved,
            stored_crc,
        ) = values

        require(magic == HEADER_MAGIC, "journal header magic mismatch")
        require(version == FORMAT_VERSION, "journal format version mismatch")
        require(header_size == HEADER_SIZE, "journal header length drifted")
        require(
            journal_copy_size == JOURNAL_COPY_SIZE,
            "journal copy allocation drifted",
        )
        require(flags == REQUIRED_FLAGS, "journal flags are not exact v1 flags")
        require(flash_id == FLASH_ID, "journal Flash identity mismatch")
        require(erase_size == ERASE_SIZE, "journal erase size drifted")
        require(
            write_chunk_size == WRITE_CHUNK_SIZE,
            "journal write chunk size drifted",
        )
        require(phase_offset == PHASE_OFFSET, "journal phase offset drifted")
        require(
            phase_marker_count == PHASE_MARKERS,
            "journal phase marker count drifted",
        )
        require(
            pair_sector_count == PAIR_SECTORS,
            "journal pair sector count drifted",
        )
        require(
            phases_per_sector == PHASES_PER_SECTOR,
            "journal phase count drifted",
        )
        require(scratch_start == SCRATCH_START, "journal scratch address drifted")
        require(
            (cp_active_start, cp_staging_start, cp_slot_size)
            == (CP_ACTIVE_START, CP_STAGING_START, CP_SLOT_SIZE),
            "journal CP layout drifted",
        )
        require(
            (ap_active_start, ap_staging_start, ap_slot_size)
            == (AP_ACTIVE_START, AP_STAGING_START, AP_SLOT_SIZE),
            "journal AP layout drifted",
        )
        require(
            image_encoding == IMAGE_ENCODING_CRC_PHYSICAL,
            "journal image encoding drifted",
        )
        require(reserved == b"\xff" * 52, "journal reserved bytes are not erased")
        require(
            crc32(data[: HEADER_SIZE - 4]) == stored_crc,
            "journal header CRC32 mismatch",
        )

        header = JournalHeaderV1(
            sequence=sequence,
            generation=generation,
            cp_image_size=cp_image_size,
            ap_image_size=ap_image_size,
            flash_status_before=flash_status_before,
            pair_digest=pair_digest,
            cp_slot_digest=cp_slot_digest,
            ap_slot_digest=ap_slot_digest,
        )
        require(header.encode() == data, "journal header is not canonical v1")
        return header


@dataclass(frozen=True)
class JournalMarkerV1:
    kind: int
    sequence: int
    generation: int
    ordinal: int

    def encode(self) -> bytes:
        require(self.kind in VALID_MARKER_KINDS, "marker kind is invalid")
        require(0 < self.sequence <= UINT64_MAX, "marker sequence is invalid")
        require(0 < self.generation <= UINT64_MAX, "marker generation is invalid")
        require(0 <= self.ordinal <= 0xFFFFFFFF, "marker ordinal is invalid")
        if self.kind >= MARKER_SCRATCH_READY:
            require(self.ordinal < PAIR_SECTORS, "phase marker ordinal is invalid")
        else:
            require(self.ordinal == 0, "control marker ordinal must be zero")

        prefix = MARKER_WITHOUT_CRC.pack(
            MARKER_MAGIC,
            FORMAT_VERSION,
            self.kind,
            self.sequence,
            self.generation,
            self.ordinal,
        )
        return prefix + struct.pack("<I", crc32(prefix))

    @staticmethod
    def decode(data: bytes) -> "JournalMarkerV1":
        require(len(data) == WRITE_CHUNK_SIZE, "journal marker size is not 32")
        magic, version, kind, sequence, generation, ordinal, stored_crc = MARKER.unpack(
            data
        )
        require(magic == MARKER_MAGIC, "journal marker magic mismatch")
        require(version == FORMAT_VERSION, "journal marker version mismatch")
        require(crc32(data[:28]) == stored_crc, "journal marker CRC32 mismatch")
        marker = JournalMarkerV1(kind, sequence, generation, ordinal)
        require(marker.encode() == data, "journal marker is not canonical v1")
        return marker


def programmed_prefix(expected: bytes, length: int) -> bytes:
    require(0 <= length <= len(expected), "program prefix length is invalid")
    return expected[:length] + b"\xff" * (len(expected) - length)


def is_exact_marker(
    data: bytes, *, kind: int, sequence: int, generation: int, ordinal: int
) -> bool:
    try:
        marker = JournalMarkerV1.decode(data)
    except MetadataError:
        return False
    return marker == JournalMarkerV1(kind, sequence, generation, ordinal)


def sample_header(sequence: int = 7, generation: int = 42) -> JournalHeaderV1:
    cp_digest = hashlib.sha256(b"cp-slot-v1").digest()
    ap_digest = hashlib.sha256(b"ap-slot-v1").digest()
    pair_digest = hashlib.sha256(cp_digest + ap_digest).digest()
    return JournalHeaderV1(
        sequence=sequence,
        generation=generation,
        cp_image_size=0x0B0000,
        ap_image_size=0x02E000,
        flash_status_before=0x0200,
        pair_digest=pair_digest,
        cp_slot_digest=cp_digest,
        ap_slot_digest=ap_digest,
    )


def run_self_test() -> dict[str, Any]:
    require(HEADER_WITHOUT_CRC.size == 0xFC, "Python header prefix ABI drifted")
    require(HEADER.size == HEADER_SIZE, "Python header ABI drifted")
    require(MARKER_WITHOUT_CRC.size == 0x1C, "Python marker prefix ABI drifted")
    require(MARKER.size == WRITE_CHUNK_SIZE, "Python marker ABI drifted")

    header = sample_header()
    encoded_header = header.encode()
    require(
        JournalHeaderV1.decode(encoded_header) == header, "header round trip failed"
    )

    header_corruption_cases = 0
    for index in range(len(encoded_header)):
        corrupted = bytearray(encoded_header)
        corrupted[index] ^= 1
        try:
            JournalHeaderV1.decode(bytes(corrupted))
        except MetadataError:
            header_corruption_cases += 1
        else:
            raise MetadataError(f"header corruption at byte {index} was accepted")

    marker = JournalMarkerV1(
        MARKER_ACTIVE_REPLACED,
        header.sequence,
        header.generation,
        17,
    )
    encoded_marker = marker.encode()
    require(
        JournalMarkerV1.decode(encoded_marker) == marker, "marker round trip failed"
    )

    marker_corruption_cases = 0
    for index in range(len(encoded_marker)):
        corrupted = bytearray(encoded_marker)
        corrupted[index] ^= 1
        try:
            JournalMarkerV1.decode(bytes(corrupted))
        except MetadataError:
            marker_corruption_cases += 1
        else:
            raise MetadataError(f"marker corruption at byte {index} was accepted")

    torn_marker_cases = 0
    for length in range(len(encoded_marker)):
        candidate = programmed_prefix(encoded_marker, length)
        exact = candidate == encoded_marker
        accepted = is_exact_marker(
            candidate,
            kind=marker.kind,
            sequence=marker.sequence,
            generation=marker.generation,
            ordinal=marker.ordinal,
        )
        require(accepted == exact, f"torn marker prefix {length} was misclassified")
        torn_marker_cases += 1

    require(
        sample_header(sequence=8).encode() != encoded_header,
        "different transaction headers compared equal",
    )
    require(ERASED_CHUNK != encoded_marker, "committed marker looks erased")

    return {
        "status": "pass-read-only-metadata-abi",
        "writes_enabled": False,
        "format_version": FORMAT_VERSION,
        "header_size": HEADER_SIZE,
        "marker_size": WRITE_CHUNK_SIZE,
        "header_crc_offset": HEADER_SIZE - 4,
        "marker_crc_offset": WRITE_CHUNK_SIZE - 4,
        "header_corruption_cases": header_corruption_cases,
        "marker_corruption_cases": marker_corruption_cases,
        "torn_marker_prefix_cases": torn_marker_cases,
        "authenticated": False,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    if not args.self_test:
        parser.error("--self-test is required; no write-producing mode exists in R2")
    try:
        result = run_self_test()
    except MetadataError as error:
        print(f"BK7258 N15 OTA metadata ABI FAIL: {error}")
        return 1

    if not args.json:
        print("BK7258 N15 OTA metadata ABI PASS (read-only)")
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
