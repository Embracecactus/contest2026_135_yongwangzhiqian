#!/usr/bin/env python3
"""Build a source-compatible BK7258 ``primary_all`` reference artifact.

The official v3.1.1.9 packer has a different order from the board's normal
CP/AP pair adapter:

    logical CP/AP placement -> MCUboot sign/pad -> optional Flash-AES
    -> 32+2 CRC expansion -> physical tail/status placement

That order is reconstructed from the read-only SDK ``bl1_sign.py``,
``bl2_sign.py`` and ``partition.py`` sources.  This script deliberately
keeps the result separate from the board-proven CP/AP path.  The BK7258
BootROM's final CRC/AES read view and secure-boot enable state are still not
available, so the result is a host-reference artifact, never silently a
bootable image.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
from dataclasses import asdict, dataclass
from pathlib import Path

from bk7258_crc_expand import expand
from gen_bk7258_partitions import load_layout


HEADER_SIZE = 0x1000
ALIGN = 1
MAX_ALIGN = 8
TAIL_SIZE = 0x1000
XIP_STATUS_MAGIC = b"\xef\xbe\xad\xde"
DEFAULT_PARTITION_CSV = (
    Path(__file__).resolve().parent.parent
    / "partitions/bk7258/secureboot_xip_cp_ap.csv"
)

AES_COMMAND = (
    "encrypt", "-infile", "{input}", "-startaddress", "{start}",
    "-keywords", "{key}", "-outfile", "{output}",
)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) // alignment * alignment


def parse_int(value: str) -> int:
    return int(value, 0)


def require_file(path: Path, label: str) -> None:
    if not path.is_file():
        raise SystemExit(f"missing {label}: {path}")


@dataclass(frozen=True)
class Component:
    name: str
    input_size: int
    input_sha256: str
    logical_offset: int
    logical_slot_size: int
    code_capacity: int


@dataclass(frozen=True)
class SlotGeometry:
    slot_name: str
    cp_partition: str
    ap_partition: str
    flash_partition_start: int
    cp_logical_size: int
    ap_logical_size: int
    physical_pair_size: int
    crc_data_size: int
    crc_total_size: int

    @property
    def signed_slot_size(self) -> int:
        # partition.py computes vir_sign_size as floor_align(
        # vir_partition_size, 4096) - TAIL_SZ.
        logical_pair = self.cp_logical_size + self.ap_logical_size
        return (logical_pair // HEADER_SIZE) * HEADER_SIZE - TAIL_SIZE

    @property
    def signed_payload_capacity(self) -> int:
        return self.signed_slot_size - HEADER_SIZE

    @property
    def ap_code_offset(self) -> int:
        return self.cp_logical_size - HEADER_SIZE


def load_slot_geometry(partition_csv: Path, slot_name: str) -> SlotGeometry:
    layout = load_layout(partition_csv)
    prefix = "primary" if slot_name == "primary_all" else "secondary"
    cp = layout.by_role(f"{prefix}_cp_app")
    ap = layout.by_role(f"{prefix}_ap_app")
    if cp.end != ap.offset:
        raise SystemExit(f"{cp.name} and {ap.name} are not contiguous")
    return SlotGeometry(
        slot_name=slot_name,
        cp_partition=cp.name,
        ap_partition=ap.name,
        flash_partition_start=cp.offset,
        cp_logical_size=layout.logical_size(cp),
        ap_logical_size=layout.logical_size(ap),
        physical_pair_size=cp.size + ap.size,
        crc_data_size=layout.crc_data_size,
        crc_total_size=layout.crc_total_size,
    )


def make_logical_merge(cp_source: Path, ap_source: Path,
                       output: Path,
                       geometry: SlotGeometry) -> tuple[list[Component], bytes]:
    """Recreate ``Partitions.gen_bin_for_bl2_signing`` for CP/AP.

    The first MCUboot header is added later by imgtool.  Consequently the AP
    code begins at CP logical-slot-size minus one header, not at the end of
    the CP slot.  This is the subtle offset that the per-component board
    adapter must not use for the official ``primary_all`` image.
    """

    cp = cp_source.read_bytes()
    ap = ap_source.read_bytes()
    cp_capacity = geometry.cp_logical_size - HEADER_SIZE
    ap_capacity = geometry.ap_logical_size - HEADER_SIZE
    if len(cp) > cp_capacity:
        raise SystemExit(
            f"CP input 0x{len(cp):x} exceeds official code capacity "
            f"0x{cp_capacity:x}"
        )
    if len(ap) > ap_capacity:
        raise SystemExit(
            f"AP input 0x{len(ap):x} exceeds official code capacity "
            f"0x{ap_capacity:x}"
        )
    if geometry.ap_code_offset + len(ap) > geometry.signed_payload_capacity:
        raise SystemExit("CP/AP logical code exceeds the signed primary_all slot")

    merged_size = geometry.ap_code_offset + len(ap)
    merged = bytearray(b"\xff" * merged_size)
    merged[0:len(cp)] = cp
    merged[geometry.ap_code_offset:geometry.ap_code_offset + len(ap)] = ap
    output.write_bytes(merged)
    components = [
        Component("cp", len(cp), sha256(cp), 0, geometry.cp_logical_size,
                  cp_capacity),
        Component("ap", len(ap), sha256(ap), geometry.ap_code_offset,
                  geometry.ap_logical_size,
                  ap_capacity),
    ]
    return components, bytes(merged)


def sign_merged(imgtool: Path, key: Path, merged: Path, output: Path,
                version: str, security_counter: str,
                geometry: SlotGeometry) -> None:
    """Run the pinned NuttX imgtool with the official BL2 parameters."""

    command = [
        sys.executable, str(imgtool), "create", "--key", str(key),
        "--public-key-format", "full", "--max-align", str(MAX_ALIGN),
        "--align", str(ALIGN), "--version", version,
        "--security-counter", security_counter, "--pad-header",
        "--header-size", hex(HEADER_SIZE), "--slot-size",
        hex(geometry.signed_slot_size), "--pad", "--boot-record", "SPE",
        "--endian", "little", str(merged), str(output),
    ]
    subprocess.run(command, check=True)
    image = output.read_bytes()
    if len(image) != geometry.signed_slot_size:
        raise SystemExit(
            f"imgtool output is 0x{len(image):x}; official slot is "
            f"0x{geometry.signed_slot_size:x}"
        )


def encrypt_image(source: Path, destination: Path, *, aes_tool: Path | None,
                  aes_key: str | None, flash_partition_start: int) -> bytes:
    if aes_tool is None:
        destination.write_bytes(source.read_bytes())
        return destination.read_bytes()
    if not aes_key:
        raise SystemExit("--aes-tool requires --aes-key-file")
    command = [
        str(aes_tool),
        *(
            item.format(
                input=str(source), output=str(destination),
                start=hex(physical_to_virtual(flash_partition_start)),
                key=aes_key,
            )
            for item in AES_COMMAND
        ),
    ]
    # The v3.1.1.9 ``beken_aes`` binary is also used by the SDK through
    # ``run_cmd_not_check_ret`` and returns status 1 even when it has emitted
    # a valid output.  Treat the output file and its exact size as the
    # authoritative success signal, while still rejecting a missing or
    # truncated result.  This preserves the SDK's observable behavior without
    # swallowing a real command failure.
    result = subprocess.run(command, check=False)
    if result.returncode not in (0, 1):
        raise SystemExit(
            f"AES tool failed with status {result.returncode}: {aes_tool}"
        )
    if not destination.is_file():
        raise SystemExit("AES tool did not produce an output file")
    return destination.read_bytes()


def ceil_align(value: int, alignment: int) -> int:
    return align_up(value, alignment)


def physical_to_virtual(address: int) -> int:
    """Match SDK ``common.py:phy2virtual`` exactly."""

    return (address % 34) + ((address // 34) * 32)


def crc_and_place(source: Path, crc_output: Path, physical_output: Path,
                  geometry: SlotGeometry) -> dict:
    prepared = source.read_bytes()
    crc = expand(prepared)
    if len(prepared) % geometry.crc_data_size:
        raise SystemExit("signed image is not aligned to a CRC data block")
    expected = (
        len(prepared) // geometry.crc_data_size * geometry.crc_total_size
    )
    if len(crc) != expected:
        raise SystemExit(
            f"CRC size 0x{len(crc):x} != expected 0x{expected:x}"
        )
    if len(crc) > geometry.physical_pair_size:
        raise SystemExit("CRC-expanded primary_all exceeds the physical pair")
    crc_output.write_bytes(crc)

    # partition.py reserves the final 4 KiB XIP status region and writes the
    # two status words into the CRC stream.  Keep the full physical pair
    # erased elsewhere so this reference can be compared byte-for-byte with
    # a future official output without touching the board.
    status_absolute = ceil_align(
        geometry.flash_partition_start + geometry.physical_pair_size - TAIL_SIZE,
        geometry.crc_total_size,
    )
    status_offset = status_absolute - geometry.flash_partition_start
    if status_offset + 36 > geometry.physical_pair_size:
        raise SystemExit("XIP status placement exceeds the physical pair")
    physical = bytearray(b"\xff" * geometry.physical_pair_size)
    physical[:len(crc)] = crc
    physical[status_offset:status_offset + 4] = XIP_STATUS_MAGIC
    physical[status_offset + 32:status_offset + 36] = XIP_STATUS_MAGIC
    physical_output.write_bytes(physical)
    return {
        "logical_size": len(prepared),
        "crc_size": len(crc),
        "crc_sha256": sha256(crc),
        "physical_size": len(physical),
        "physical_sha256": sha256(physical),
        "xip_status_absolute": status_absolute,
        "xip_status_relative": status_offset,
        "xip_status_magic": XIP_STATUS_MAGIC.hex(),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cp-raw", type=Path, required=True)
    parser.add_argument("--ap-raw", type=Path, required=True)
    parser.add_argument("--key", type=Path, required=True)
    parser.add_argument("--imgtool", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--security-counter", default="auto")
    parser.add_argument("--slot-name", default="primary_all",
                        choices=("primary_all", "secondary_all"))
    parser.add_argument(
        "--partition-csv",
        type=Path,
        default=DEFAULT_PARTITION_CSV,
        help="project secureboot staging profile (not an SDK-installed file)",
    )
    parser.add_argument(
        "--aes-tool", type=Path,
        help="external Beken AES tool; omitted means the no-AES branch",
    )
    parser.add_argument(
        "--aes-key-file", type=Path,
        help="file containing the AES keyword; never copied to the output",
    )
    args = parser.parse_args()
    require_file(args.partition_csv, "secureboot partition CSV")
    geometry = load_slot_geometry(args.partition_csv, args.slot_name)

    for path, label in (
        (args.cp_raw, "CP input"), (args.ap_raw, "AP input"),
        (args.key, "MCUboot signing key"), (args.imgtool, "NuttX imgtool"),
    ):
        require_file(path, label)
    if args.aes_tool is not None:
        require_file(args.aes_tool, "AES tool")
        if args.aes_key_file is None:
            raise SystemExit("--aes-tool requires --aes-key-file")
        require_file(args.aes_key_file, "AES key file")
        aes_key = args.aes_key_file.read_text(encoding="ascii").strip()
        if not aes_key:
            raise SystemExit("AES key file is empty")
    else:
        if args.aes_key_file is not None:
            raise SystemExit("--aes-key-file requires --aes-tool")
        aes_key = None

    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="bk7258-secureboot-") as temp:
        tmpdir = Path(temp)
        logical_path = tmpdir / f"{args.slot_name}_code.bin"
        components, logical = make_logical_merge(
            args.cp_raw, args.ap_raw, logical_path, geometry
        )
        logical_output = args.output / logical_path.name
        logical_output.write_bytes(logical)

        signed_path = args.output / f"{args.slot_name}_code_signed.bin"
        sign_merged(
            args.imgtool, args.key, logical_path, signed_path,
            args.version, args.security_counter, geometry,
        )

        prepared_path = tmpdir / f"{args.slot_name}_code_prepared.bin"
        prepared = encrypt_image(
            signed_path, prepared_path,
            aes_tool=args.aes_tool, aes_key=aes_key,
            flash_partition_start=geometry.flash_partition_start,
        )
        if len(prepared) != geometry.signed_slot_size:
            raise SystemExit("AES changed the signed image size unexpectedly")
        prepared_output = args.output / f"{args.slot_name}_code_prepared.bin"
        prepared_output.write_bytes(prepared)

        crc_path = args.output / f"{args.slot_name}_code_signed_crc.bin"
        physical_path = args.output / f"{args.slot_name}_code_crc_padded.bin"
        crc_report = crc_and_place(
            prepared_path, crc_path, physical_path, geometry
        )

    report = {
        "format": 2,
        "status": "host-reference-only",
        "consumer": (
            "v3.1.1.9 source-compatible pipeline model; BK7258 BootROM "
            "CRC/AES read view remains unproven"
        ),
        "documented_order": [
            "logical-cp-ap-placement",
            "pinned-nuttx-mcuboot-sign-and-pad",
            "optional-aes-on-signed-primary-all",
            "32+2-crc-expansion",
            "physical-tail-and-xip-status-placement",
        ],
        "official_source": {
            "sdk_release": "v3.1.1.9",
            "bl1_sign": "tools/env_tools/beken_utils/scripts/bl1_sign.py",
            "bl2_sign": "tools/env_tools/beken_utils/scripts/bl2_sign.py",
            "partition_pipeline": "tools/env_tools/beken_utils/scripts/partition.py",
            "secureboot_enabled_in_bk7258_csv": False,
        },
        "sdk_boundary": {
            "runtime": "v3.1.1.9",
            "sdk_source_modified": False,
            "nuttx_source_modified": False,
        },
        "slot_name": args.slot_name,
        "version": args.version,
        "security_counter": args.security_counter,
        "layout": {
            "partition_csv": str(args.partition_csv),
            "cp_partition": geometry.cp_partition,
            "ap_partition": geometry.ap_partition,
            "header_size": HEADER_SIZE,
            "cp_code_offset": 0,
            "ap_code_offset": geometry.ap_code_offset,
            "logical_payload_size": geometry.signed_payload_capacity,
            "signed_slot_size": geometry.signed_slot_size,
            "physical_pair_size": geometry.physical_pair_size,
            "flash_partition_start": geometry.flash_partition_start,
        },
        "align": ALIGN,
        "max_align": MAX_ALIGN,
        "aes": {
            "enabled": args.aes_tool is not None,
            "tool": str(args.aes_tool) if args.aes_tool else None,
            "start_address": physical_to_virtual(
                geometry.flash_partition_start
            ),
            "key_in_report": False,
        },
        "components": [asdict(component) for component in components],
        "logical_merge": {
            "file": logical_output.name,
            "size": logical_output.stat().st_size,
            "sha256": sha256(logical),
        },
        "signed": {
            "file": signed_path.name,
            "size": signed_path.stat().st_size,
            "sha256": sha256(signed_path.read_bytes()),
        },
        "prepared": {
            "file": prepared_output.name,
            "size": prepared_output.stat().st_size,
            "sha256": sha256(prepared),
            "aes_applied": args.aes_tool is not None,
        },
        "crc": {
            "file": crc_path.name,
            **{key: value for key, value in crc_report.items()
               if key in ("logical_size", "crc_size", "crc_sha256")},
        },
        "physical": {
            "file": physical_path.name,
            **{key: value for key, value in crc_report.items()
               if key in ("physical_size", "physical_sha256",
                          "xip_status_absolute", "xip_status_relative",
                          "xip_status_magic")},
        },
        "otp_efuse_written": False,
        "hardware_verified": False,
    }
    (args.output / "secureboot-pipeline.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
