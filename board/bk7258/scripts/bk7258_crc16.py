#!/usr/bin/env python3
"""Shared BK7258 32-byte + CRC16 flash packet codec (Beken format).

The app-image expander (``bk7258_crc_expand.py``) and the BL1 packer
(``bootloader/bk7258_bl1_pack.py crc``) encode logical images into
32-byte data + 2-byte big-endian CRC16 packets.  The CRC algorithm is the
vendor SDK helper pinned under ``tools/vendor/bk7258-sdk-v3.1.1.9``; this
module is the single host-side entry point for that codec so both consumers
cannot drift apart.

Official counterpart: the Beken SDK CRC helper (``bk_crc16.py``) used by the
vendor packer.  Outputs of this module must stay byte-exact with that helper.
"""

from __future__ import annotations

import importlib.util
import struct
from pathlib import Path

PACKET_DATA = 32
PACKET_TOTAL = 34
PADDING_BYTE = b"\xff"

_VENDOR_CRC_PATH = (
    Path(__file__).resolve().parents[3]
    / "tools/vendor/bk7258-sdk-v3.1.1.9/bk_crc16.py"
)
_VENDOR_CRC_SPEC = importlib.util.spec_from_file_location(
    "bk7258_sdk_v3119_crc16", _VENDOR_CRC_PATH
)
if _VENDOR_CRC_SPEC is None or _VENDOR_CRC_SPEC.loader is None:
    raise ImportError(f"cannot load vendored SDK CRC helper: {_VENDOR_CRC_PATH}")
_VENDOR_CRC_MODULE = importlib.util.module_from_spec(_VENDOR_CRC_SPEC)
_VENDOR_CRC_SPEC.loader.exec_module(_VENDOR_CRC_MODULE)
_VENDOR_CRC = _VENDOR_CRC_MODULE.bk_crc16()


class CodecError(ValueError):
    """Raised when a 32+2 encoded image is malformed."""


def crc16(data: bytes) -> int:
    """Return the Beken CRC16 of ``data`` (big-endian packet word)."""

    return _VENDOR_CRC_MODULE.crc16(data, 0, len(data))


def expand(data: bytes) -> bytes:
    """Encode logical ``data`` into 32+2 packets with 0xff tail padding."""

    return _VENDOR_CRC.crc16_data(data)


def decode(data: bytes) -> bytes:
    """Verify and remove every 32+2 packet, returning the logical image.

    The decoded image includes the 0xff padding that ``expand`` appended, so
    the caller must strip its own logical-length padding when needed.
    """

    if len(data) % PACKET_TOTAL:
        raise CodecError(
            f"encoded image size 0x{len(data):x} is not a multiple of "
            f"{PACKET_TOTAL}"
        )

    output = bytearray()
    for offset in range(0, len(data), PACKET_TOTAL):
        block = data[offset : offset + PACKET_DATA]
        stored_crc = struct.unpack_from(">H", data, offset + PACKET_DATA)[0]
        observed_crc = crc16(block)
        if stored_crc != observed_crc:
            raise CodecError(
                f"CRC16 mismatch at encoded offset 0x{offset:x}: "
                f"expected 0x{stored_crc:04x}, got 0x{observed_crc:04x}"
            )
        output.extend(block)
    return bytes(output)


def physical_offset_for_logical(logical_offset: int) -> int:
    """Map a logical image offset to its 32+2 packet-flash offset."""

    return (logical_offset // PACKET_DATA) * PACKET_TOTAL + (
        logical_offset % PACKET_DATA
    )
