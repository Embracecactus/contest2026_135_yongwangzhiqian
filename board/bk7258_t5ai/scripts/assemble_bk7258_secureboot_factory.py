#!/usr/bin/env python3
"""Assemble a complete host-only BK7258 Secure-Boot staging image.

The output follows the project secureboot CSV and combines the already
generated BL1 control pages, Primary/Secondary Manifest records, two BL2
copies, and signed Primary/Secondary ALL CP/AP artifacts.  It never signs,
flashes, provisions OTP/eFuse, or enables Secure Boot.

The image is deliberately marked non-legacy-bootable: raw offset zero is the
documented BL1 control area, not the current 32+2 legacy boot envelope.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

from bk7258_crc_expand import decode, expand
from gen_bk7258_partitions import SECUREBOOT_XIP_LAYOUT, load_layout


SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_PARTITION_CSV = (
    SCRIPT_DIR.parent / "partitions/bk7258/secureboot_xip_cp_ap.csv"
)
CPU_VECTOR_ALIGNMENT = 512
DEFAULT_BL2_LOGICAL_SIZE = 0x3000
BL1_CONTROL_MAGIC = 0x4C725463
BEKEN_MANIFEST_MAGIC = 0xA1BC2FD8
MCUBOOT_IMAGE_MAGIC = 0x96F3B83D
XIP_STATUS_MAGIC = b"\xEF\xBE\xAD\xDE"


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) // alignment * alignment


def physical_to_virtual(value: int, physical_block: int,
                        data_block: int) -> int:
    return value % physical_block + value // physical_block * data_block


def virtual_to_physical(value: int, physical_block: int,
                        data_block: int) -> int:
    return value % data_block + value // data_block * physical_block


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_exact(path: Path, size: int, label: str) -> bytes:
    data = path.read_bytes()
    if len(data) != size:
        raise ValueError(
            f"{label} is 0x{len(data):x} bytes; expected exactly 0x{size:x}"
        )
    return data


def read_manifest(path: Path, partition_size: int, label: str) -> bytes:
    data = path.read_bytes()
    if not data or len(data) > partition_size:
        raise ValueError(
            f"{label} size 0x{len(data):x} exceeds 0x{partition_size:x}"
        )
    if len(data) > 0xD5 and any(byte != 0xFF for byte in data[0xD5:]):
        raise ValueError(f"{label} has non-erased bytes after the 0xd5 record")
    return data.ljust(partition_size, b"\xFF")


def validate_control(data: bytes, layout) -> dict[str, object]:
    words = struct.unpack_from("<10I", data, 0x1000)
    expected_primary = layout.xip_base + layout.by_role(
        "bl1_primary_manifest"
    ).offset
    expected_secondary = layout.xip_base + layout.by_role(
        "bl1_secondary_manifest"
    ).offset
    if words[0] != BL1_CONTROL_MAGIC:
        raise ValueError("BL1 control record magic is invalid")
    if words[1] not in (1, 2):
        raise ValueError("BL1 control preferred slot is invalid")
    if words[2] != expected_primary or words[3] != expected_secondary:
        raise ValueError("BL1 control Manifest addresses disagree with the CSV")
    if words[6] != 0 or words[7] != 0 or words[8] != 0:
        raise ValueError("staging control must not enable/lock security or debug")
    if any(byte != 0xFF for byte in data[0x2000:0x3000]):
        raise ValueError("OTP-simulation control page is not erased")
    return {
        "boot_flag": words[1],
        "primary_manifest_xip": words[2],
        "secondary_manifest_xip": words[3],
        "security_boot_ena": words[6],
        "security_boot_print_dis": words[7],
        "jtag_dis": words[8],
        "otp_simulation_erased": True,
    }


def validate_manifest(data: bytes, label: str, expected_xip: int,
                      bl2_raw: bytes) -> dict[str, object]:
    if len(data) < 0xD5:
        raise ValueError(f"{label} is shorter than the official record")
    magic, layout_version, version, total_size = struct.unpack_from(
        "<4I", data, 0
    )
    static_addr, load_addr, image_size, entry = struct.unpack_from(
        "<4I", data, 0x20
    )
    if magic != BEKEN_MANIFEST_MAGIC or layout_version != 0x00010001:
        raise ValueError(f"{label} header is not the official tool format")
    if total_size != 0xD5 or static_addr != expected_xip:
        raise ValueError(f"{label} size/static address disagrees with staging")
    if load_addr != entry or image_size != len(bl2_raw):
        raise ValueError(f"{label} BL2 descriptor is inconsistent")
    if data[0x30:0x50] != hashlib.sha256(bl2_raw).digest():
        raise ValueError(f"{label} BL2 digest is incorrect")
    return {
        "layout_version": layout_version,
        "manifest_version": version,
        "static_xip": static_addr,
        "load_address": load_addr,
        "image_size": image_size,
        "image_sha256": sha256(bl2_raw),
        "record_size": total_size,
    }


def validate_all_image(data: bytes, layout, cp, ap,
                       label: str) -> dict[str, object]:
    logical_pair = layout.logical_size(cp) + layout.logical_size(ap)
    signed_size = logical_pair // 0x1000 * 0x1000 - 0x1000
    if signed_size % layout.crc_data_size:
        raise ValueError(f"{label} signed size is not CRC aligned")
    encoded_size = (
        signed_size // layout.crc_data_size * layout.crc_total_size
    )
    logical = decode(data[:encoded_size])
    if len(logical) != signed_size:
        raise ValueError(f"{label} decoded size is incorrect")
    if struct.unpack_from("<I", logical, 0)[0] != MCUBOOT_IMAGE_MAGIC:
        raise ValueError(f"{label} has no MCUboot header")

    status_absolute = align_up(cp.offset + cp.size + ap.size - 0x1000,
                               layout.crc_total_size)
    status_relative = status_absolute - cp.offset
    if (data[status_relative:status_relative + 4] != XIP_STATUS_MAGIC or
            data[status_relative + 32:status_relative + 36] != XIP_STATUS_MAGIC):
        raise ValueError(f"{label} XIP status words are absent")
    return {
        "signed_logical_size": signed_size,
        "encoded_size": encoded_size,
        "mcuboot_magic": f"0x{MCUBOOT_IMAGE_MAGIC:08x}",
        "xip_status_relative": status_relative,
    }


def bl2_partition_image(layout, partition, source: Path,
                        logical_size: int) -> tuple[bytes, dict[str, object]]:
    raw = source.read_bytes()
    if not raw or len(raw) > logical_size:
        raise ValueError(
            f"{partition.name} BL2 size 0x{len(raw):x} exceeds authorized "
            f"window 0x{logical_size:x}"
        )
    if logical_size % layout.crc_data_size:
        raise ValueError("BL2 logical size must align to a CRC data block")

    aligned_physical = align_up(partition.offset, layout.crc_total_size)
    virtual_partition = physical_to_virtual(
        aligned_physical, layout.crc_total_size, layout.crc_data_size
    )
    virtual_code = align_up(virtual_partition, CPU_VECTOR_ALIGNMENT)
    physical_code = virtual_to_physical(
        virtual_code, layout.crc_total_size, layout.crc_data_size
    )
    relative_code = physical_code - partition.offset
    encoded = expand(raw.ljust(logical_size, b"\xFF"))
    if relative_code < 0 or relative_code + len(encoded) > partition.size:
        raise ValueError(f"{partition.name} encoded BL2 exceeds its partition")

    image = bytearray(b"\xFF" * partition.size)
    image[relative_code:relative_code + len(encoded)] = encoded
    report = {
        "partition": partition.name,
        "partition_offset": partition.offset,
        "partition_size": partition.size,
        "physical_code_offset": physical_code,
        "relative_code_offset": relative_code,
        "xip_code_address": layout.xip_base + virtual_code,
        "raw_size": len(raw),
        "raw_sha256": sha256(raw),
        "authorized_logical_size": logical_size,
        "encoded_size": len(encoded),
        "encoded_sha256": sha256(encoded),
    }
    return bytes(image), report


def place(image: bytearray, offset: int, data: bytes, label: str) -> dict:
    end = offset + len(data)
    if offset < 0 or end > len(image):
        raise ValueError(f"{label} is outside the factory image")
    image[offset:end] = data
    return {
        "name": label,
        "offset": offset,
        "size": len(data),
        "sha256": sha256(data),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--partition-csv", type=Path,
                        default=DEFAULT_PARTITION_CSV)
    parser.add_argument("--bl1-control", type=Path, required=True)
    parser.add_argument("--primary-manifest", type=Path, required=True)
    parser.add_argument("--secondary-manifest", type=Path, required=True)
    parser.add_argument("--primary-bl2", type=Path, required=True)
    parser.add_argument("--secondary-bl2", type=Path, required=True)
    parser.add_argument("--primary-all", type=Path, required=True)
    parser.add_argument("--secondary-all", type=Path, required=True)
    parser.add_argument("--bl2-logical-size", type=lambda value: int(value, 0),
                        default=DEFAULT_BL2_LOGICAL_SIZE)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    layout = load_layout(args.partition_csv)
    if layout.layout_name != SECUREBOOT_XIP_LAYOUT:
        raise ValueError("factory assembly requires the secureboot XIP layout")

    control = layout.by_role("bl1_control")
    primary_manifest = layout.by_role("bl1_primary_manifest")
    secondary_manifest = layout.by_role("bl1_secondary_manifest")
    primary_bl2 = layout.by_role("bl1_primary_bl2")
    secondary_bl2 = layout.by_role("bl1_secondary_bl2")
    primary_cp = layout.by_role("primary_cp_app")
    primary_ap = layout.by_role("primary_ap_app")
    secondary_cp = layout.by_role("secondary_cp_app")
    secondary_ap = layout.by_role("secondary_ap_app")

    control_data = read_exact(
        args.bl1_control, control.size, "BL1 control image"
    )
    primary_manifest_data = read_manifest(
        args.primary_manifest, primary_manifest.size, "Primary Manifest"
    )
    secondary_manifest_data = read_manifest(
        args.secondary_manifest, secondary_manifest.size, "Secondary Manifest"
    )
    primary_bl2_data, primary_bl2_report = bl2_partition_image(
        layout, primary_bl2, args.primary_bl2, args.bl2_logical_size
    )
    secondary_bl2_data, secondary_bl2_report = bl2_partition_image(
        layout, secondary_bl2, args.secondary_bl2, args.bl2_logical_size
    )
    primary_all_size = primary_cp.size + primary_ap.size
    secondary_all_size = secondary_cp.size + secondary_ap.size
    primary_all_data = read_exact(
        args.primary_all, primary_all_size, "Primary ALL"
    )
    secondary_all_data = read_exact(
        args.secondary_all, secondary_all_size, "Secondary ALL"
    )

    control_report = validate_control(control_data, layout)
    primary_raw = args.primary_bl2.read_bytes()
    secondary_raw = args.secondary_bl2.read_bytes()
    primary_manifest_report = validate_manifest(
        primary_manifest_data, "Primary Manifest",
        primary_bl2_report["xip_code_address"], primary_raw
    )
    secondary_manifest_report = validate_manifest(
        secondary_manifest_data, "Secondary Manifest",
        secondary_bl2_report["xip_code_address"], secondary_raw
    )
    primary_all_report = validate_all_image(
        primary_all_data, layout, primary_cp, primary_ap, "Primary ALL"
    )
    secondary_all_report = validate_all_image(
        secondary_all_data, layout, secondary_cp, secondary_ap,
        "Secondary ALL"
    )

    factory = bytearray(b"\xFF" * layout.flash_size)
    placements = [
        place(factory, control.offset, control_data, control.name),
        place(factory, primary_manifest.offset, primary_manifest_data,
              primary_manifest.name),
        place(factory, secondary_manifest.offset, secondary_manifest_data,
              secondary_manifest.name),
        place(factory, primary_bl2.offset, primary_bl2_data,
              primary_bl2.name),
        place(factory, secondary_bl2.offset, secondary_bl2_data,
              secondary_bl2.name),
        place(factory, primary_cp.offset, primary_all_data, "primary_all"),
        place(factory, secondary_cp.offset, secondary_all_data,
              "secondary_all"),
    ]

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(factory)
    report = {
        "format": 1,
        "status": "secureboot-staging-host-only",
        "layout_id": layout.layout_id,
        "layout_source": str(args.partition_csv),
        "factory_image": str(args.output),
        "factory_size": len(factory),
        "factory_sha256": sha256(factory),
        "legacy_bootable": False,
        "secure_boot_enabled": False,
        "hardware_verified": False,
        "otp_efuse_written": False,
        "bl1_control": control_report,
        "manifests": [primary_manifest_report, secondary_manifest_report],
        "bl2": [primary_bl2_report, secondary_bl2_report],
        "all_images": [primary_all_report, secondary_all_report],
        "placements": placements,
    }
    report_path = args.output.with_suffix(args.output.suffix + ".json")
    report_path.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(f"BK7258 secureboot factory FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
