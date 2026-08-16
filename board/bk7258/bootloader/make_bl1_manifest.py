#!/usr/bin/env python3
"""Create the board-owned, signed BL1 Manifest that authorizes one BL2.

This is intentionally a board packer, not an SDK Secure-Boot tool and not a
replacement for NuttX MCUboot imgtool.  imgtool signs CP/AP images; this tool
only signs the fixed record that BL1 consumes before it starts BL2.

The optional ``beken-candidate-v1`` output is the repository's compatibility
name for the official release/v2.0.1 ``secure_boot_tool`` one-image record.
For identical BL2 bytes, addresses, version and P-256 key, bytes 0x00..0x94
match the official tool exactly; bytes 0x95..0xd4 are the randomized ECDSA
signature.  This establishes tool-format compatibility, but it is not an
assertion that an unprovisioned BK7258 BootROM has consumed the record.
"""

from __future__ import annotations

import argparse
import hashlib
import subprocess
import sys
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR.parent / "scripts"))

from gen_bk7258_partitions import load_layout


MANIFEST_SIZE = 0x100
BL2_LOGICAL_CAPACITY = 0x20000
SIGNED_SIZE = 0xB0
SIGNATURE_OFFSET = 0xB0
SIGNATURE_SIZE = 0x40
MAGIC = b"BKBL1M2\0"
FORMAT = 2
SIGNATURE_ALGORITHM = 1  # ECDSA-P256
DIGEST_ALGORITHM = 1  # SHA-256
KEY_ID = 1
MIN_IMAGE_VERSION = 1
DEFAULT_MANIFEST_VERSION = 5
DEFAULT_BL2_XIP = 0x024D0000
CPU_VECTOR_ALIGNMENT = 512

BEKEN_MANIFEST_SIZE = 0x100
BEKEN_LAYOUT_VERSION = 0x00010001
BEKEN_FLAG_EC256_SHA256 = 0x00030619
BEKEN_TOTAL_SIZE = 0xD5

# These are the hashes of the board-owned development root compiled into
# boot_bl1_manifest_key.c.  They are deliberately checked at packaging time:
# a random signing key would produce a perfectly well-formed record that the
# BL1 must (and does) reject.  This is a software development root only; it is
# not the undisclosed BK7258 OTP/BootROM root.
SOFTWARE_ROOT_XY_SHA256 = bytes.fromhex(
    "e31631bcfcef65c7bb2d122abf5326bf727d45fca6ca6c43fbcd4ce75e823432"
)
SOFTWARE_ROOT_SEC1_SHA256 = bytes.fromhex(
    "53765e4353e2a61f76e01801dcb8ab63f2f0b6f64e6c8caf03ea43a466c90b57"
)


def c_byte_array(name: str, data: bytes) -> str:
    rows = []
    for offset in range(0, len(data), 8):
        row = ", ".join(f"0x{value:02x}" for value in data[offset:offset + 8])
        rows.append(f"  {row}")
    return (
        f"const uint8_t {name}[{len(data)}] =\n"
        "{\n"
        + ",\n".join(rows)
        + "\n};\n"
    )


def write_root_public_key_c(path: Path, public_key: bytes) -> None:
    """Emit only the public half of the externally supplied development root."""

    source = (
        "/* Generated from an external P-256 BL1 Manifest signing key.\n"
        " * This file contains public material only; the private key is never\n"
        " * linked into the firmware or copied into the image package. */\n"
        "#include <stdint.h>\n\n"
        + c_byte_array(
            "bk7258_bl1_manifest_root_public_key", public_key)
        + "\n"
        + c_byte_array(
            "bk7258_bl1_manifest_root_public_key_hash",
            hashlib.sha256(public_key).digest())
        + "\n"
        + c_byte_array(
            "bk7258_beken_manifest_root_public_key_hash",
            hashlib.sha256(b"\x04" + public_key).digest())
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(source, encoding="utf-8")


def positive_int(value: str) -> int:
    parsed = int(value, 0)
    if parsed < 0 or parsed > 0xffffffff:
        raise argparse.ArgumentTypeError(f"not a uint32: {value}")
    return parsed


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) // alignment * alignment


def physical_to_virtual(address: int, physical_block: int,
                        data_block: int) -> int:
    """Match Beken ``phy2virtual`` for the 32+2 Flash mapping."""

    return (address % physical_block) + (
        address // physical_block * data_block
    )


def bl2_contract_from_layout(
    partition_csv: Path,
    slot: str,
    expected_layout_id: str | None = None,
    expected_layout_sha256: str | None = None,
) -> tuple[int, int]:
    """Derive one BL2 XIP address and capacity from a verified layout.

    The active A/B layout owns one physical ``bl2`` row and derives its
    secondary copy from the following reserved gap.  The host-reference
    secureboot layout instead names two explicit BL2 rows.  Supporting both
    forms here keeps the signing decision bound to the layout object that was
    just identity-checked.
    """

    layout = load_layout(partition_csv)
    if (expected_layout_id is None) != (expected_layout_sha256 is None):
        raise ValueError(
            "--expect-layout-id and --expect-layout-sha256 must be supplied together"
        )
    if expected_layout_id is not None:
        if (layout.layout_id != expected_layout_id or
                layout.layout_sha256 != expected_layout_sha256):
            raise ValueError(
                "partition layout identity mismatch: "
                f"expected {expected_layout_id}/{expected_layout_sha256}, "
                f"got {layout.layout_id}/{layout.layout_sha256}"
            )
    roles = {partition.role for partition in layout.partitions}
    has_active = "bl2" in roles
    has_staging = {
        "bl1_primary_bl2", "bl1_secondary_bl2"
    }.issubset(roles)
    if has_active == has_staging:
        raise ValueError(
            "partition layout must define exactly one supported BL2 slot model"
        )

    if has_active:
        partition = layout.by_role("bl2")
        capacity = layout.logical_size(partition)
        primary_xip = layout.xip_base + layout.logical_offset(partition)
        if primary_xip % CPU_VECTOR_ALIGNMENT:
            raise ValueError("active BL2 XIP address is not vector-aligned")
        if slot == "primary":
            return primary_xip, capacity

        littlefs = layout.by_role("littlefs")
        secondary_offset = partition.end
        if secondary_offset + partition.size > littlefs.offset:
            raise ValueError("derived secondary BL2 overlaps LittleFS")
        return primary_xip + capacity, capacity

    partition = layout.by_role(f"bl1_{slot}_bl2")
    aligned_physical = align_up(partition.offset, layout.crc_total_size)
    virtual_partition = physical_to_virtual(
        aligned_physical, layout.crc_total_size, layout.crc_data_size
    )
    virtual_code = align_up(virtual_partition, CPU_VECTOR_ALIGNMENT)
    xip = layout.xip_base + virtual_code
    if xip >= layout.xip_base + physical_to_virtual(
        partition.end, layout.crc_total_size, layout.crc_data_size
    ):
        raise ValueError(f"{partition.name} has no vector-aligned code window")
    virtual_end = physical_to_virtual(
        partition.end, layout.crc_total_size, layout.crc_data_size
    )
    capacity = (
        (virtual_end - virtual_code) // layout.crc_data_size
        * layout.crc_data_size
    )
    if capacity <= 0:
        raise ValueError(f"{partition.name} has no usable BL2 code capacity")
    return xip, capacity


def der_length(data: bytes, offset: int) -> tuple[int, int]:
    if offset >= len(data):
        raise ValueError("truncated DER length")
    first = data[offset]
    offset += 1
    if first < 0x80:
        return first, offset
    count = first & 0x7f
    if count == 0 or count > 2 or offset + count > len(data):
        raise ValueError("unsupported DER length")
    length = int.from_bytes(data[offset:offset + count], "big")
    return length, offset + count


def der_integer(data: bytes, offset: int) -> tuple[bytes, int]:
    if offset >= len(data) or data[offset] != 0x02:
        raise ValueError("expected DER INTEGER")
    length, offset = der_length(data, offset + 1)
    value = data[offset:offset + length]
    if len(value) != length or not value:
        raise ValueError("truncated DER INTEGER")
    if value[0] & 0x80:
        raise ValueError("negative DER INTEGER")
    value = value.lstrip(b"\0") or b"\0"
    if len(value) > 32:
        raise ValueError("P-256 integer too wide")
    return value.rjust(32, b"\0"), offset + length


def p256_der_to_raw(signature: bytes) -> bytes:
    if not signature or signature[0] != 0x30:
        raise ValueError("expected DER ECDSA sequence")
    length, offset = der_length(signature, 1)
    if offset + length != len(signature):
        raise ValueError("DER ECDSA sequence length mismatch")
    r, offset = der_integer(signature, offset)
    s, offset = der_integer(signature, offset)
    if offset != len(signature):
        raise ValueError("trailing DER ECDSA data")
    return r + s


def sign(private_key: Path, signed: bytes) -> bytes:
    result = subprocess.run(
        ["openssl", "dgst", "-sha256", "-sign", str(private_key)],
        input=signed, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        check=False)
    if result.returncode != 0:
        raise RuntimeError("openssl failed to sign BL1 Manifest")
    return p256_der_to_raw(result.stdout)


def public_key_from_private(private_key: Path) -> bytes:
    result = subprocess.run(
        ["openssl", "ec", "-in", str(private_key), "-pubout",
         "-conv_form", "uncompressed", "-outform", "DER"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if result.returncode != 0 or len(result.stdout) < 65:
        raise RuntimeError("openssl failed to derive the BL1 public key")
    public_key = result.stdout[-65:]
    if public_key[0] != 4:
        raise ValueError("derived key is not an uncompressed P-256 point")
    return public_key[1:]


def require_software_root(public_key: bytes, manifest_format: str) -> None:
    """Reject a manifest key that the board BL1 cannot anchor."""

    if manifest_format == "beken-candidate-v1":
        digest = hashlib.sha256(b"\x04" + public_key).digest()
        expected = SOFTWARE_ROOT_SEC1_SHA256
    else:
        digest = hashlib.sha256(public_key).digest()
        expected = SOFTWARE_ROOT_XY_SHA256

    if digest != expected:
        raise ValueError(
            "BL1 Manifest private key is not the board-owned development "
            "root; refusing to emit an image that BL1 will reject"
        )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bl2", type=Path, required=True,
                        help="flat logical BL2 binary (it is ff-padded)")
    parser.add_argument("--private-key", type=Path, required=True,
                        help="P-256 private key; never place it in the repository")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument(
        "--generated-root-c", type=Path,
        help=(
            "emit the matching public-only BL1 root C source; supplying this "
            "option deliberately replaces the repository development root"
        ),
    )
    parser.add_argument(
        "--bl2-xip",
        type=positive_int,
        help=(
            f"authorized BL2 XIP address (legacy default 0x{DEFAULT_BL2_XIP:x}); "
            "derived when --partition-csv is supplied"
        ),
    )
    parser.add_argument(
        "--partition-csv",
        type=Path,
        help="derive the BL2 XIP address from a project-owned partition layout",
    )
    parser.add_argument(
        "--expect-layout-id",
        help="expected canonical identity of --partition-csv",
    )
    parser.add_argument(
        "--expect-layout-sha256",
        help="expected canonical SHA-256 of --partition-csv",
    )
    parser.add_argument(
        "--bl2-slot",
        choices=("primary", "secondary"),
        default="primary",
        help="BL2 slot selected when --partition-csv is used",
    )
    parser.add_argument("--bl2-size", type=positive_int, default=0x3000)
    parser.add_argument(
        "--bl2-capacity",
        type=positive_int,
        default=BL2_LOGICAL_CAPACITY,
        help="resolved usable logical capacity of the selected BL2 slot",
    )
    parser.add_argument("--bl2-load", type=positive_int, default=0x28020000)
    parser.add_argument(
        "--manifest-version", "--image-version", dest="manifest_version",
        type=positive_int, default=DEFAULT_MANIFEST_VERSION,
        help=(
            "Beken/IPSS Manifest version/security counter; the "
            "official bl1_sign.py uses 5 (legacy alias: --image-version)"
        ),
    )
    parser.add_argument(
        "--container-size",
        type=positive_int,
        default=MANIFEST_SIZE,
        help=(
            "total erased container size (default 0x100); use 0x1000 "
            "for an official-shaped XIP manifest partition"
        ),
    )
    parser.add_argument(
        "--format", choices=("custom-v2", "beken-candidate-v1"),
        default="custom-v2",
        help=(
            "record format (beken-candidate-v1 is retained as the stable "
            "CLI name for the official-tool-compatible record)"
        ),
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if args.partition_csv is None and (
            args.expect_layout_id is not None or
            args.expect_layout_sha256 is not None):
        raise ValueError(
            "layout identity expectations require --partition-csv"
        )
    if args.partition_csv is not None:
        derived_bl2_xip, derived_bl2_capacity = bl2_contract_from_layout(
            args.partition_csv,
            args.bl2_slot,
            args.expect_layout_id,
            args.expect_layout_sha256,
        )
        if args.bl2_xip is not None and args.bl2_xip != derived_bl2_xip:
            raise ValueError(
                f"--bl2-xip 0x{args.bl2_xip:08x} disagrees with "
                f"{args.bl2_slot} layout address 0x{derived_bl2_xip:08x}"
            )
        if args.bl2_capacity != derived_bl2_capacity:
            raise ValueError(
                f"--bl2-capacity 0x{args.bl2_capacity:x} disagrees with "
                f"{args.bl2_slot} layout capacity 0x{derived_bl2_capacity:x}"
            )
        args.bl2_xip = derived_bl2_xip
    elif args.bl2_xip is None:
        args.bl2_xip = DEFAULT_BL2_XIP
    minimum_container_size = (
        BEKEN_TOTAL_SIZE if args.format == "beken-candidate-v1" else MANIFEST_SIZE
    )
    if args.container_size < minimum_container_size:
        raise ValueError(
            f"container size 0x{args.container_size:x} is smaller than the "
            f"{args.format} record (0x{minimum_container_size:x})"
        )
    if args.bl2_size == 0 or args.bl2_size % 32:
        raise ValueError("BL2 size must be nonzero and CRC-block aligned")
    if args.bl2_capacity == 0 or args.bl2_capacity % 32:
        raise ValueError("BL2 capacity must be nonzero and CRC-block aligned")
    if args.bl2_size > args.bl2_capacity:
        raise ValueError("BL2 size exceeds the resolved logical capacity")
    image = args.bl2.read_bytes()
    raw_image = image
    if not raw_image:
        raise ValueError("BL2 binary must not be empty")
    if len(image) > args.bl2_size:
        raise ValueError("BL2 binary exceeds the authorized logical size")
    image = image.ljust(args.bl2_size, b"\xff")

    public_key = public_key_from_private(args.private_key)
    if args.generated_root_c is None:
        require_software_root(public_key, args.format)
    else:
        write_root_public_key_c(args.generated_root_c, public_key)
    if args.format == "beken-candidate-v1":
        manifest = bytearray(b"\xff" * args.container_size)
        sec1_public_key = b"\x04" + public_key
        manifest[0x00:0x04] = (0xA1BC2FD8).to_bytes(4, "little")
        manifest[0x04:0x08] = BEKEN_LAYOUT_VERSION.to_bytes(4, "little")
        manifest[0x08:0x0C] = args.manifest_version.to_bytes(4, "little")
        manifest[0x0C:0x10] = BEKEN_TOTAL_SIZE.to_bytes(4, "little")
        manifest[0x10:0x14] = BEKEN_FLAG_EC256_SHA256.to_bytes(4, "little")
        manifest[0x14:0x18] = (1).to_bytes(4, "little")
        manifest[0x18:0x1C] = (0).to_bytes(4, "little")
        manifest[0x1C:0x20] = (0).to_bytes(4, "little")
        manifest[0x20:0x24] = args.bl2_xip.to_bytes(4, "little")
        manifest[0x24:0x28] = args.bl2_load.to_bytes(4, "little")
        # The official IPSS tool records the source image length and hashes
        # that exact byte stream.  BL1 still copies the separately supplied
        # logical window; its unused tail must be erased (0xff).
        manifest[0x28:0x2C] = len(raw_image).to_bytes(4, "little")
        manifest[0x2C:0x30] = args.bl2_load.to_bytes(4, "little")
        manifest[0x30:0x50] = hashlib.sha256(raw_image).digest()
        manifest[0x50:0x54] = (0).to_bytes(4, "little")
        manifest[0x54:0x54 + len(sec1_public_key)] = sec1_public_key
        manifest[0x95:0xD5] = sign(
            args.private_key, bytes(manifest[:0x95]))
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_bytes(manifest)
        return

    manifest = bytearray(b"\xff" * args.container_size)
    manifest[0:8] = MAGIC
    manifest[8:12] = FORMAT.to_bytes(4, "little")
    manifest[12:16] = SIGNATURE_ALGORITHM.to_bytes(4, "little")
    manifest[16:20] = DIGEST_ALGORITHM.to_bytes(4, "little")
    manifest[20:24] = KEY_ID.to_bytes(4, "little")
    manifest[24:28] = args.manifest_version.to_bytes(4, "little")
    manifest[28:32] = (0).to_bytes(4, "little")
    manifest[32:36] = args.bl2_xip.to_bytes(4, "little")
    manifest[36:40] = args.bl2_size.to_bytes(4, "little")
    manifest[40:44] = args.bl2_load.to_bytes(4, "little")
    manifest[44:48] = (0).to_bytes(4, "little")
    manifest[0x30:0x50] = hashlib.sha256(image).digest()
    manifest[0x50:0x70] = hashlib.sha256(public_key).digest()
    manifest[0x70:0xb0] = public_key
    manifest[SIGNATURE_OFFSET:SIGNATURE_OFFSET + SIGNATURE_SIZE] = sign(
        args.private_key, bytes(manifest[:SIGNED_SIZE]))

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(manifest)


if __name__ == "__main__":
    main()
