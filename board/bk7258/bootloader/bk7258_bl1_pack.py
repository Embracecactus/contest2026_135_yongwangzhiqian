#!/usr/bin/env python3
"""Single BK7258 BL1 packer entry: ``control`` / ``manifest`` / ``crc``.

This module converges the three historical board-owned Python entry points
into one chip-named CLI:

``bk7258_bl1_pack.py control  --bl2 bl.bin --out bl1_control.bin ...``
``bk7258_bl1_pack.py manifest --bl2 bl2.bin --private-key key.pem --out ...``
``bk7258_bl1_pack.py crc      --in bl.bin --out bl_crc.bin ...``

Each subcommand is a repository-owned, deterministic host mirror of the
corresponding official Beken/BKFIL packaging contract; outputs must remain
byte-exact with the vendor tools.  No subcommand performs device, OTP/eFuse,
signing-root installation, or Flash operations.

Official counterpart: the Windows-only Beken ``BEKEN_BKFIL`` / SDK packers.
This file is the CI/host deterministic mirror used in their place; when a
new SDK arrives, byte-level cross-checks (``diff_bk7258_packages.py`` /
``inspect_bk7258_rbl.py``) are the guard against format drift.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
import subprocess
import sys
from dataclasses import asdict, dataclass
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR.parent / "scripts"))

import bk7258_crc16  # noqa: E402
from gen_bk7258_partitions import DEFAULT_INPUT, load_layout  # noqa: E402


# ---------------------------------------------------------------------------
# control: 12 KiB Beken-compatible XIP ``bl1_control`` image
# ---------------------------------------------------------------------------
CONTROL_DOC = """Create the 12 KiB Beken-compatible XIP ``bl1_control`` image.

The public BK7258 partition documentation defines three consecutive pages:
the vector hand-off page, a debug boot-control page, and an OTP-simulation
page.  The official packer copies the first 64 bytes of ``bl2.bin`` into the
first page.  The recovered boot-control ABI occupies the first 0x28 bytes of
the second page.  This tool leaves the OTP-simulation page erased and keeps
secure boot and JTAG-disable controls off.

This is a repository-owned compatibility packer.  It has no device, OTP,
eFuse, signing, or Flash-writing capability.  Generating this file does not
enable the chip's irreversible secure-boot mode.
"""

PAGE_SIZE = 0x1000
CONTROL_SIZE = 3 * PAGE_SIZE
VECTOR_COPY_SIZE = 0x40
BOOT_CONTROL_OFFSET = PAGE_SIZE
BOOT_CONTROL_MAGIC = 0x4C725463
BOOT_FLAG_PRIMARY = 1
BOOT_FLAG_SECONDARY = 2
BOOT_FLAG_ERASED = 0xFFFFFFFF
DEFAULT_SRAM_START = 0x28000000
DEFAULT_SRAM_END = 0x280A0000
DEFAULT_PRIMARY_MANIFEST_ADDR = 0x02003000
DEFAULT_SECONDARY_MANIFEST_ADDR = 0x02004000


def uint32(value: str) -> int:
    """Parse a command-line uint32 in decimal or C-style hexadecimal form."""

    try:
        parsed = int(value, 0)
    except ValueError as error:
        raise argparse.ArgumentTypeError(f"not a uint32: {value}") from error
    if not 0 <= parsed <= 0xFFFFFFFF:
        raise argparse.ArgumentTypeError(f"not a uint32: {value}")
    return parsed


def read_vector(path: Path, sram_start: int, sram_end: int) -> bytes:
    image = path.read_bytes()
    if len(image) < VECTOR_COPY_SIZE:
        raise ValueError(
            f"BL2 is {len(image)} bytes; at least 0x{VECTOR_COPY_SIZE:x} bytes "
            "are required for the vector hand-off"
        )

    vector = image[:VECTOR_COPY_SIZE]
    msp, pc = struct.unpack_from("<II", vector)
    # Cortex-M requires an aligned initial stack.  The top of the SRAM window
    # is a valid initial MSP, hence the inclusive upper-bound check.
    if (msp & 0x7) != 0 or not sram_start <= msp <= sram_end:
        raise ValueError(
            f"BL2 initial MSP 0x{msp:08x} is outside the configured SRAM "
            f"window [0x{sram_start:08x}, 0x{sram_end:08x}]"
        )
    if pc in (0, 0xFFFFFFFF) or (pc & 1) == 0:
        raise ValueError(
            f"BL2 reset PC 0x{pc:08x} is not a valid Thumb entry point"
        )
    return vector


def build_control(
    vector: bytes,
    boot_flag: int,
    primary_manifest_addr: int,
    secondary_manifest_addr: int,
) -> bytes:
    control = bytearray(b"\xFF" * CONTROL_SIZE)
    control[:VECTOR_COPY_SIZE] = vector
    struct.pack_into(
        "<10I",
        control,
        BOOT_CONTROL_OFFSET,
        BOOT_CONTROL_MAGIC,
        boot_flag,
        primary_manifest_addr,
        secondary_manifest_addr,
        1,  # pll_ena: keep the documented early clock path available
        1,  # security_boot_supported: describes hardware capability only
        0,  # security_boot_ena: never enable from a generated debug image
        0,  # security_boot_print_dis: retain recovery diagnostics
        0,  # jtag_dis: retain SWD/JTAG recovery access
        0,  # sw_fih_delay_ena: disabled until the secure chain is proven
    )
    return bytes(control)


def _add_control_parser(subparsers: argparse._SubParsersAction) -> None:
    parser = subparsers.add_parser(
        "control",
        description=CONTROL_DOC,
        help="generate the 12 KiB bl1_control XIP image",
    )
    parser.add_argument(
        "--bl2",
        type=Path,
        required=True,
        help="flat BL2 binary whose first 64 bytes form the vector hand-off",
    )
    parser.add_argument(
        "--out",
        type=Path,
        required=True,
        help="output path for the exact 12288-byte bl1_control.bin",
    )
    parser.add_argument(
        "--boot-flag",
        choices=("primary", "secondary", "erased"),
        default="primary",
        help="initial preferred BL2; 'erased' reproduces the debug template",
    )
    parser.add_argument(
        "--primary-manifest-addr",
        type=uint32,
        default=DEFAULT_PRIMARY_MANIFEST_ADDR,
        help="primary Manifest XIP address from the staging layout",
    )
    parser.add_argument(
        "--secondary-manifest-addr",
        type=uint32,
        default=DEFAULT_SECONDARY_MANIFEST_ADDR,
        help="secondary Manifest XIP address from the staging layout",
    )
    parser.add_argument(
        "--sram-start",
        type=uint32,
        default=DEFAULT_SRAM_START,
        help=f"inclusive MSP SRAM lower bound (default 0x{DEFAULT_SRAM_START:x})",
    )
    parser.add_argument(
        "--sram-end",
        type=uint32,
        default=DEFAULT_SRAM_END,
        help=f"inclusive MSP SRAM upper bound (default 0x{DEFAULT_SRAM_END:x})",
    )


def run_control(args: argparse.Namespace) -> int:
    if args.sram_start > args.sram_end:
        print("bl1_control FAIL: SRAM bounds are reversed", file=sys.stderr)
        return 1
    try:
        vector = read_vector(args.bl2, args.sram_start, args.sram_end)
        if args.primary_manifest_addr == args.secondary_manifest_addr:
            raise ValueError("primary and secondary Manifest addresses overlap")
        if ((args.primary_manifest_addr | args.secondary_manifest_addr) & 3) != 0:
            raise ValueError("Manifest addresses must be 4-byte aligned")
        boot_flag = {
            "primary": BOOT_FLAG_PRIMARY,
            "secondary": BOOT_FLAG_SECONDARY,
            "erased": BOOT_FLAG_ERASED,
        }[args.boot_flag]
        control = build_control(
            vector,
            boot_flag,
            args.primary_manifest_addr,
            args.secondary_manifest_addr,
        )
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_bytes(control)
    except (OSError, ValueError) as error:
        print(f"BK7258 bl1_control FAIL: {error}", file=sys.stderr)
        return 1

    msp, pc = struct.unpack_from("<II", vector)
    print(
        f"BK7258 bl1_control PASS: {args.out} "
        f"size=0x{len(control):x} msp=0x{msp:08x} pc=0x{pc:08x} "
        f"boot_flag={args.boot_flag} "
        f"primary_manifest=0x{args.primary_manifest_addr:08x} "
        f"secondary_manifest=0x{args.secondary_manifest_addr:08x}"
    )
    return 0


# ---------------------------------------------------------------------------
# manifest: board-owned signed BL1 Manifest that authorizes one BL2
# ---------------------------------------------------------------------------
MANIFEST_DOC = """Create the board-owned, signed BL1 Manifest that authorizes one BL2.

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


def _add_manifest_parser(subparsers: argparse._SubParsersAction) -> None:
    parser = subparsers.add_parser(
        "manifest",
        description=MANIFEST_DOC,
        help="generate the signed BL1 Manifest record",
    )
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


def run_manifest(args: argparse.Namespace) -> None:
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


# ---------------------------------------------------------------------------
# crc: pack a minimal BK7236-compatible BL1 into 32+2 CRC flash format
# ---------------------------------------------------------------------------
CRC_DOC = """Pack a minimal BK7236-compatible bootloader into Beken 32-byte-data
+ 2-byte-CRC flash format.

The BK7258 BootROM consumes the BK7236-family flash contract: logical images
are encoded into 32-byte data packets followed by a 2-byte big-endian CRC16.
This subcommand is the host-side deterministic mirror of the official
packer; generated bytes must remain identical.
"""


@dataclass(frozen=True)
class _CrcContext:
    layout_id: str
    layout_sha256: str
    layout_source: str
    boot_partition: object
    magic: bytes
    flash_base: int
    bootloader_logical_size: int
    bl1_manifest_size: int
    bl1_manifest_tail_size: int
    bl1_manifest_primary_logical_offset: int
    bl1_manifest_secondary_logical_offset: int
    bootloader_physical_size: int


def _crc_context() -> _CrcContext:
    layout = load_layout(DEFAULT_INPUT)
    if layout.crc_data_size != bk7258_crc16.PACKET_DATA or (
        layout.crc_total_size != bk7258_crc16.PACKET_TOTAL
    ):
        raise RuntimeError(
            "partition CRC packet geometry disagrees with the shared codec: "
            f"layout={layout.crc_data_size}+{layout.crc_total_size} "
            f"codec={bk7258_crc16.PACKET_DATA}+{bk7258_crc16.PACKET_TOTAL}"
        )
    expected_layout_id = os.environ.get("BK7258_PARTITION_LAYOUT_ID") or None
    expected_layout_sha256 = (
        os.environ.get("BK7258_PARTITION_LAYOUT_SHA256") or None
    )
    if (expected_layout_id is None) != (expected_layout_sha256 is None):
        raise RuntimeError(
            "partition layout ID and SHA-256 must be exported together"
        )
    if expected_layout_id is not None and (
        layout.layout_id != expected_layout_id
        or layout.layout_sha256 != expected_layout_sha256
    ):
        raise RuntimeError(
            "bootloader partition identity mismatch: "
            f"expected={expected_layout_id}/{expected_layout_sha256} "
            f"observed={layout.layout_id}/{layout.layout_sha256}"
        )
    boot_partition = layout.by_role("boot")
    flash_base = layout.xip_base + layout.logical_offset(boot_partition)
    bootloader_logical_size = layout.logical_size(boot_partition)
    bl1_manifest_size = 0x100
    bl1_manifest_tail_size = 0x200
    return _CrcContext(
        layout_id=layout.layout_id,
        layout_sha256=layout.layout_sha256,
        layout_source=str(layout.report()["source"]),
        boot_partition=boot_partition,
        magic=b"BK7236\x10\x00",
        flash_base=flash_base,
        bootloader_logical_size=bootloader_logical_size,
        bl1_manifest_size=bl1_manifest_size,
        bl1_manifest_tail_size=bl1_manifest_tail_size,
        bl1_manifest_primary_logical_offset=(
            bootloader_logical_size - bl1_manifest_size
        ),
        bl1_manifest_secondary_logical_offset=(
            bootloader_logical_size - bl1_manifest_tail_size
        ),
        bootloader_physical_size=boot_partition.size,
    )


MAGIC_LOGICAL_OFFSET = 0x100


@dataclass
class BootloaderInfo:
    layout_id: str
    layout_sha256: str
    layout_source: str
    input_size: int
    logical_size: int
    physical_size: int
    link_address: int
    sp: int
    reset: int
    magic_logical_offset: int
    magic_physical_offset: int
    magic: str


def prepare_bootloader(
    data: bytearray,
    manifest_primary: bytes | None,
    manifest_secondary: bytes | None,
    ctx: _CrcContext,
) -> tuple[int, int]:
    min_size = MAGIC_LOGICAL_OFFSET + len(ctx.magic)
    if len(data) < min_size:
        data.extend(b"\xff" * (min_size - len(data)))

    sp, reset = struct.unpack_from("<II", data, 0)
    if not (0x28000000 <= sp <= 0x280A0000):
        raise ValueError(f"initial SP 0x{sp:08x} outside expected SRAM")
    if (reset & 1) == 0:
        raise ValueError(f"reset vector 0x{reset:08x} is not a Thumb address")
    if not (ctx.flash_base <= (reset & ~1) < ctx.flash_base + len(data)):
        raise ValueError(
            f"reset 0x{reset:08x} does not target 0x{ctx.flash_base:08x}.."
            f"0x{ctx.flash_base + len(data):08x}; rebuild bootloader for "
            f"0x{ctx.flash_base:08x}"
        )

    data[MAGIC_LOGICAL_OFFSET:MAGIC_LOGICAL_OFFSET + len(ctx.magic)] = ctx.magic

    if len(data) > ctx.bootloader_logical_size:
        raise ValueError(
            f"bootloader 0x{len(data):x} exceeds logical slot "
            f"0x{ctx.bootloader_logical_size:x}"
        )
    if manifest_primary is not None or manifest_secondary is not None:
        if len(data) > ctx.bl1_manifest_secondary_logical_offset:
            raise ValueError(
                f"bootloader 0x{len(data):x} leaves no reserved "
                "BL1 Manifest tail"
            )
    # The SDK-shaped experiment stores the same 256-byte signed record in a
    # separate 4 KiB raw data page.  The bootloader tail remains two 256-byte
    # records, so accept that page as input but embed only its first record.
    if manifest_primary is not None:
        if len(manifest_primary) == 0x1000:
            if any(byte != 0xff for byte in manifest_primary[0x100:]):
                raise ValueError("primary 4 KiB BL1 Manifest tail is not erased")
            manifest_primary = manifest_primary[:ctx.bl1_manifest_size]
        elif len(manifest_primary) != ctx.bl1_manifest_size:
            raise ValueError(
                f"primary BL1 Manifest must be "
                f"0x{ctx.bl1_manifest_size:x} or 0x1000 bytes"
            )
    if manifest_secondary is not None:
        if len(manifest_secondary) == 0x1000:
            if any(byte != 0xff for byte in manifest_secondary[0x100:]):
                raise ValueError("secondary 4 KiB BL1 Manifest tail is not erased")
            manifest_secondary = manifest_secondary[:ctx.bl1_manifest_size]
        elif len(manifest_secondary) != ctx.bl1_manifest_size:
            raise ValueError(
                f"secondary BL1 Manifest must be "
                f"0x{ctx.bl1_manifest_size:x} or 0x1000 bytes"
            )
    if manifest_primary is not None or manifest_secondary is not None:
        data.extend(b"\xff" * (ctx.bootloader_logical_size - len(data)))
        if manifest_primary is not None:
            data[ctx.bl1_manifest_primary_logical_offset:
                 ctx.bl1_manifest_primary_logical_offset
                 + ctx.bl1_manifest_size] = manifest_primary
        if manifest_secondary is not None:
            data[ctx.bl1_manifest_secondary_logical_offset:
                 ctx.bl1_manifest_secondary_logical_offset
                 + ctx.bl1_manifest_size] = manifest_secondary
        return sp, reset
    data.extend(b"\xff" * (ctx.bootloader_logical_size - len(data)))
    return sp, reset


def build_crc_image(
    args: argparse.Namespace,
    ctx: _CrcContext,
) -> BootloaderInfo:
    raw = bytearray(args.input.read_bytes())
    input_size = len(raw)
    manifest_primary = None
    manifest_secondary = None
    if args.manifest is not None:
        manifest_primary = args.manifest.read_bytes()
    if args.manifest_primary is not None:
        manifest_primary = args.manifest_primary.read_bytes()
    if args.manifest_secondary is not None:
        manifest_secondary = args.manifest_secondary.read_bytes()
    sp, reset = prepare_bootloader(
        raw, manifest_primary, manifest_secondary, ctx
    )
    encoded = bk7258_crc16.expand(raw)
    if len(encoded) != ctx.bootloader_physical_size:
        raise ValueError(
            f"encoded size 0x{len(encoded):x} != expected "
            f"0x{ctx.bootloader_physical_size:x}"
        )

    magic_physical_offset = bk7258_crc16.physical_offset_for_logical(
        MAGIC_LOGICAL_OFFSET
    )
    if encoded[magic_physical_offset:magic_physical_offset + len(ctx.magic)] \
            != ctx.magic:
        raise ValueError(
            f"physical magic verify failed at 0x{magic_physical_offset:x}"
        )

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(encoded)

    info = BootloaderInfo(
        layout_id=ctx.layout_id,
        layout_sha256=ctx.layout_sha256,
        layout_source=ctx.layout_source,
        input_size=input_size,
        logical_size=len(raw),
        physical_size=len(encoded),
        link_address=ctx.flash_base,
        sp=sp,
        reset=reset,
        magic_logical_offset=MAGIC_LOGICAL_OFFSET,
        magic_physical_offset=magic_physical_offset,
        magic=ctx.magic.hex(),
    )
    args.out.with_suffix(".json").write_text(
        json.dumps(asdict(info), indent=2) + "\n"
    )
    return info


def existing_path(value: str) -> Path:
    path = Path(value).expanduser().resolve()
    if not path.exists():
        raise argparse.ArgumentTypeError(f"path does not exist: {path}")
    return path


def _add_crc_parser(subparsers: argparse._SubParsersAction) -> None:
    parser = subparsers.add_parser(
        "crc",
        description=CRC_DOC,
        help="pack the BL1 raw image into Beken 32+2 CRC flash format",
    )
    parser.add_argument("--in", dest="input", type=existing_path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument(
        "--manifest",
        type=existing_path,
        help="compatibility alias for --manifest-primary",
    )
    parser.add_argument(
        "--manifest-primary",
        type=existing_path,
        help="optional 256-byte primary self-owned BL1 Manifest",
    )
    parser.add_argument(
        "--manifest-secondary",
        type=existing_path,
        help="optional 256-byte secondary self-owned BL1 Manifest",
    )


def run_crc(args: argparse.Namespace) -> None:
    ctx = _crc_context()
    args.out = args.out.expanduser().resolve()
    info = build_crc_image(args, ctx)
    print("Minimal BK7236 bootloader image generated:")
    for key, value in asdict(info).items():
        if isinstance(value, int):
            print(f"  {key}: 0x{value:x}")
        else:
            print(f"  {key}: {value}")
    print(f"  output: {args.out}")


# ---------------------------------------------------------------------------
# single-entry dispatcher
# ---------------------------------------------------------------------------
def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="bk7258_bl1_pack.py",
        description=__doc__,
    )
    subparsers = parser.add_subparsers(
        dest="command",
        required=True,
        metavar="{control,manifest,crc}",
    )
    _add_control_parser(subparsers)
    _add_manifest_parser(subparsers)
    _add_crc_parser(subparsers)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _build_parser().parse_args(argv)
    if args.command == "control":
        return run_control(args)
    if args.command == "manifest":
        run_manifest(args)
        return 0
    if args.command == "crc":
        run_crc(args)
        return 0
    raise AssertionError(f"unhandled BK7258 BL1 packer subcommand: {args.command}")


if __name__ == "__main__":
    raise SystemExit(main())
