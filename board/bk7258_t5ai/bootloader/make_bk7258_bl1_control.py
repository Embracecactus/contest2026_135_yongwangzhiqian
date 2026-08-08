#!/usr/bin/env python3
"""Create the 12 KiB Beken-compatible XIP ``bl1_control`` image.

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

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path


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


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
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
    return parser.parse_args()


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


def main() -> int:
    args = parse_args()
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


if __name__ == "__main__":
    raise SystemExit(main())
