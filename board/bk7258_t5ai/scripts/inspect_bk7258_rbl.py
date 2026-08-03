#!/usr/bin/env python3
"""Inspect and fail-closed verify a Beken v3.1.1.9 96-byte RBL image."""

from __future__ import annotations

import argparse
import binascii
import json
import struct
from dataclasses import asdict, dataclass
from pathlib import Path


HEADER = struct.Struct("<4sH6s16s24s24sIIIII")
HEADER_WITHOUT_CRC_SIZE = HEADER.size - 4
AB_HEADER_TAIL_DISTANCE = 0x1000
RBL_MAGIC = b"RBL\0"
FNV1A_OFFSET_BASIS = 0x811C9DC5
FNV1A_PRIME = 0x01000193


class VerificationError(RuntimeError):
    """Raised when an RBL image violates the pinned v3.1.1.9 contract."""


@dataclass(frozen=True)
class RblHeader:
    magic: bytes
    algorithm: int
    timestamp_raw: bytes
    app_partition: bytes
    download_version: bytes
    current_version: bytes
    body_crc32: int
    body_fnv1a: int
    raw_size: int
    body_size: int
    header_crc32: int

    @staticmethod
    def from_bytes(data: bytes) -> "RblHeader":
        if len(data) != HEADER.size:
            raise VerificationError(
                f"RBL header must be {HEADER.size} bytes, got {len(data)}"
            )
        return RblHeader(*HEADER.unpack(data))


def c_string(value: bytes) -> str:
    return value.split(b"\0", 1)[0].decode("ascii", errors="strict")


def fnv1a(data: bytes) -> int:
    value = FNV1A_OFFSET_BASIS
    for byte in data:
        value = ((value ^ byte) * FNV1A_PRIME) & 0xFFFFFFFF
    return value


def locate_header(data: bytes, mode: str) -> tuple[str, int, int]:
    prefix = len(data) >= HEADER.size and data[:4] == RBL_MAGIC
    ab_offset = len(data) - AB_HEADER_TAIL_DISTANCE
    ab = ab_offset >= 0 and data[ab_offset : ab_offset + 4] == RBL_MAGIC
    if mode == "prefix":
        if not prefix:
            raise VerificationError("prefix RBL magic is missing")
        return mode, 0, HEADER.size
    if mode == "ab":
        if not ab:
            raise VerificationError("AB-tail RBL magic is missing")
        return mode, ab_offset, 0
    if prefix:
        return "prefix", 0, HEADER.size
    if ab:
        return "ab", ab_offset, 0
    raise VerificationError("no v3.1.1.9 RBL header found")


def inspect(data: bytes, mode: str, allow_encoded: bool) -> dict[str, object]:
    detected_mode, header_offset, body_offset = locate_header(data, mode)
    header_bytes = data[header_offset : header_offset + HEADER.size]
    header = RblHeader.from_bytes(header_bytes)
    if header.magic != RBL_MAGIC:
        raise VerificationError(f"bad RBL magic: {header.magic!r}")
    if header.algorithm != 0 and not allow_encoded:
        raise VerificationError(
            f"algorithm 0x{header.algorithm:04x} is encoded; "
            "N15 first release accepts plain algorithm 0 only"
        )
    body_end = body_offset + header.body_size
    if body_end > len(data):
        raise VerificationError("RBL body extends past the input")
    if detected_mode == "ab" and body_end > header_offset:
        raise VerificationError("AB RBL body overlaps its tail header")
    body = data[body_offset:body_end]

    observed_header_crc = (
        binascii.crc32(header_bytes[:HEADER_WITHOUT_CRC_SIZE]) & 0xFFFFFFFF
    )
    if observed_header_crc != header.header_crc32:
        raise VerificationError(
            f"header CRC32 mismatch: expected 0x{header.header_crc32:08x}, "
            f"got 0x{observed_header_crc:08x}"
        )
    observed_body_crc = binascii.crc32(body) & 0xFFFFFFFF
    if observed_body_crc != header.body_crc32:
        raise VerificationError(
            f"body CRC32 mismatch: expected 0x{header.body_crc32:08x}, "
            f"got 0x{observed_body_crc:08x}"
        )

    hash_verified = False
    observed_hash: int | None = None
    if header.algorithm == 0:
        if header.raw_size != header.body_size:
            raise VerificationError("plain RBL raw/body sizes differ")
        observed_hash = fnv1a(body)
        if observed_hash != header.body_fnv1a:
            raise VerificationError(
                f"body FNV-1a mismatch: expected 0x{header.body_fnv1a:08x}, "
                f"got 0x{observed_hash:08x}"
            )
        hash_verified = True

    if detected_mode == "ab":
        padding = data[body_end:header_offset]
        if any(byte != 0xFF for byte in padding):
            raise VerificationError("AB padding before the tail header is not erased")

    result = asdict(header)
    for name in (
        "magic",
        "timestamp_raw",
        "app_partition",
        "download_version",
        "current_version",
    ):
        result[name] = getattr(header, name).hex()
    result.update(
        {
            "format": "beken-rbl-v3.1.1.9",
            "mode": detected_mode,
            "header_size": HEADER.size,
            "header_offset": header_offset,
            "body_offset": body_offset,
            "app_partition_text": c_string(header.app_partition),
            "download_version_text": c_string(header.download_version),
            "current_version_text": c_string(header.current_version),
            "observed_header_crc32": observed_header_crc,
            "observed_body_crc32": observed_body_crc,
            "observed_body_fnv1a": observed_hash,
            "body_fnv1a_verified": hash_verified,
            "authenticated": False,
            "security_note": (
                "CRC32 and FNV-1a detect accidental corruption but provide no "
                "publisher authenticity or rollback protection"
            ),
        }
    )
    return result


def make_plain_rbl(body: bytes, mode: str) -> bytes:
    prefix = HEADER.pack(
        RBL_MAGIC,
        0,
        b"\0" * 6,
        b"app".ljust(16, b"\0"),
        b"n15-test".ljust(24, b"\0"),
        b"current".ljust(24, b"\0"),
        binascii.crc32(body) & 0xFFFFFFFF,
        fnv1a(body),
        len(body),
        len(body),
        0,
    )
    header_crc = binascii.crc32(prefix[:HEADER_WITHOUT_CRC_SIZE]) & 0xFFFFFFFF
    header = prefix[:-4] + struct.pack("<I", header_crc)
    if mode == "prefix":
        return header + body
    container_size = (
        (len(body) + AB_HEADER_TAIL_DISTANCE * 2 - 1)
        // AB_HEADER_TAIL_DISTANCE
        * AB_HEADER_TAIL_DISTANCE
    )
    header_offset = container_size - AB_HEADER_TAIL_DISTANCE
    image = bytearray(b"\xff" * container_size)
    image[: len(body)] = body
    image[header_offset : header_offset + HEADER.size] = header
    return bytes(image)


def self_test() -> None:
    body = bytes(range(251)) * 3
    for mode in ("prefix", "ab"):
        image = make_plain_rbl(body, mode)
        result = inspect(image, mode, allow_encoded=False)
        if not result["body_fnv1a_verified"]:
            raise VerificationError(f"{mode} self-test did not verify FNV-1a")
        corrupted = bytearray(image)
        corrupted[0 if mode == "ab" else HEADER.size] ^= 1
        try:
            inspect(bytes(corrupted), mode, allow_encoded=False)
        except VerificationError:
            pass
        else:
            raise VerificationError(f"{mode} corruption negative test passed")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, nargs="?")
    parser.add_argument("--mode", choices=("auto", "prefix", "ab"), default="auto")
    parser.add_argument(
        "--allow-encoded",
        action="store_true",
        help="inspect encoded payloads without claiming the raw FNV hash was verified",
    )
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--json", action="store_true", help="emit JSON only")
    args = parser.parse_args()

    try:
        if args.self_test:
            self_test()
            print("BK7258 RBL inspector self-test PASS")
            return 0
        if args.image is None:
            parser.error("IMAGE is required unless --self-test is used")
        result = inspect(args.image.read_bytes(), args.mode, args.allow_encoded)
    except (OSError, UnicodeError, VerificationError) as error:
        print(f"BK7258 RBL verification FAIL: {error}")
        return 1

    encoded = json.dumps(result, indent=2, sort_keys=True)
    if args.json:
        print(encoded)
    else:
        print(
            "BK7258 RBL verification PASS: CRC32/FNV-1a integrity only; "
            "authenticated=false"
        )
        print(encoded)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
