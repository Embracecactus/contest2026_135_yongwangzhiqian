#!/usr/bin/env python3
"""Pack a minimal BK7236 bootloader into Beken 32-byte-data + 2-byte-CRC flash format."""

from __future__ import annotations

import argparse
import json
import struct
import sys
from dataclasses import asdict, dataclass
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR.parent / "scripts"))

from gen_bk7258_partitions import DEFAULT_INPUT, load_layout


LAYOUT = load_layout(DEFAULT_INPUT)
BOOT_PARTITION = LAYOUT.by_role("boot")
MAGIC = b'BK7236\x10\x00'
MAGIC_LOGICAL_OFFSET = 0x100
FLASH_BASE = LAYOUT.xip_base + LAYOUT.logical_offset(BOOT_PARTITION)
BOOTLOADER_LOGICAL_SIZE = LAYOUT.logical_size(BOOT_PARTITION)
CRC_PACKET = LAYOUT.crc_data_size
CRC_TOTAL = LAYOUT.crc_total_size
BOOTLOADER_PHYSICAL_SIZE = BOOT_PARTITION.size
DEFAULT_IN = Path('/home/lijian/project/TuyaOpen/zephyr-bk7258-port/out/custom_bootloader/bk7236_min_bl.bin')
DEFAULT_OUT = Path('/home/lijian/project/TuyaOpen/zephyr-bk7258-port/out/custom_bootloader/bk7236_min_bl_crc.bin')


@dataclass
class BootloaderInfo:
    layout_id: str
    input_size: int
    logical_size: int
    physical_size: int
    link_address: int
    sp: int
    reset: int
    magic_logical_offset: int
    magic_physical_offset: int
    magic: str


def crc16(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x8005
            else:
                crc <<= 1
    return crc & 0xFFFF


def crc_expand(data: bytes) -> bytes:
    out = bytearray()
    for off in range(0, len(data), CRC_PACKET):
        block = data[off:off + CRC_PACKET]
        if len(block) < CRC_PACKET:
            block += b'\xff' * (CRC_PACKET - len(block))
        out += block
        out += struct.pack('>H', crc16(block))
    return bytes(out)


def physical_offset_for_logical(logical_offset: int) -> int:
    return (logical_offset // CRC_PACKET) * CRC_TOTAL + (logical_offset % CRC_PACKET)


def prepare_bootloader(data: bytearray) -> tuple[int, int]:
    min_size = MAGIC_LOGICAL_OFFSET + len(MAGIC)
    if len(data) < min_size:
        data.extend(b'\xff' * (min_size - len(data)))

    sp, reset = struct.unpack_from('<II', data, 0)
    if not (0x28000000 <= sp <= 0x280A0000):
        raise ValueError(f'initial SP 0x{sp:08x} outside expected SRAM')
    if (reset & 1) == 0:
        raise ValueError(f'reset vector 0x{reset:08x} is not a Thumb address')
    if not (FLASH_BASE <= (reset & ~1) < FLASH_BASE + len(data)):
        raise ValueError(
            f'reset 0x{reset:08x} does not target 0x{FLASH_BASE:08x}..'
            f'0x{FLASH_BASE + len(data):08x}; rebuild bootloader for 0x{FLASH_BASE:08x}'
        )

    data[MAGIC_LOGICAL_OFFSET:MAGIC_LOGICAL_OFFSET + len(MAGIC)] = MAGIC

    if len(data) > BOOTLOADER_LOGICAL_SIZE:
        raise ValueError(f'bootloader 0x{len(data):x} exceeds logical slot 0x{BOOTLOADER_LOGICAL_SIZE:x}')
    data.extend(b'\xff' * (BOOTLOADER_LOGICAL_SIZE - len(data)))
    return sp, reset


def build_image(args: argparse.Namespace) -> BootloaderInfo:
    raw = bytearray(args.input.read_bytes())
    input_size = len(raw)
    sp, reset = prepare_bootloader(raw)
    encoded = crc_expand(raw)
    if len(encoded) != BOOTLOADER_PHYSICAL_SIZE:
        raise ValueError(f'encoded size 0x{len(encoded):x} != expected 0x{BOOTLOADER_PHYSICAL_SIZE:x}')

    magic_physical_offset = physical_offset_for_logical(MAGIC_LOGICAL_OFFSET)
    if encoded[magic_physical_offset:magic_physical_offset + len(MAGIC)] != MAGIC:
        raise ValueError(f'physical magic verify failed at 0x{magic_physical_offset:x}')

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(encoded)

    info = BootloaderInfo(
        layout_id=LAYOUT.layout_id,
        input_size=input_size,
        logical_size=len(raw),
        physical_size=len(encoded),
        link_address=FLASH_BASE,
        sp=sp,
        reset=reset,
        magic_logical_offset=MAGIC_LOGICAL_OFFSET,
        magic_physical_offset=magic_physical_offset,
        magic=MAGIC.hex(),
    )
    args.out.with_suffix('.json').write_text(json.dumps(asdict(info), indent=2) + '\n')
    return info


def existing_path(value: str) -> Path:
    path = Path(value).expanduser().resolve()
    if not path.exists():
        raise argparse.ArgumentTypeError(f'path does not exist: {path}')
    return path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--in', dest='input', type=existing_path, default=DEFAULT_IN)
    parser.add_argument('--out', type=Path, default=DEFAULT_OUT)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    args.out = args.out.expanduser().resolve()
    info = build_image(args)
    print('Minimal BK7236 bootloader image generated:')
    for key, value in asdict(info).items():
        if isinstance(value, int):
            print(f'  {key}: 0x{value:x}')
        else:
            print(f'  {key}: {value}')
    print(f'  output: {args.out}')


if __name__ == '__main__':
    main()
