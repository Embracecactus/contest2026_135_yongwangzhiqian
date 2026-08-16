#!/usr/bin/env python3
"""Focused byte-exact checks for the shared BK7258 32+2 CRC codec."""

from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPOSITORY / "board/bk7258/scripts"))

from bk7258_crc16 import CodecError, decode, expand  # noqa: E402


VENDOR_CRC_PATH = (
    REPOSITORY / "tools/vendor/bk7258-sdk-v3.1.1.9/bk_crc16.py"
)
_VENDOR_SPEC = importlib.util.spec_from_file_location(
    "bk7258_sdk_v3119_crc16_vendor", VENDOR_CRC_PATH)
_VENDOR_MODULE = importlib.util.module_from_spec(_VENDOR_SPEC)
_VENDOR_SPEC.loader.exec_module(_VENDOR_MODULE)
_VENDOR_CODEC = _VENDOR_MODULE.bk_crc16()


class Crc16CodecTest(unittest.TestCase):
    def test_expand_is_byte_exact_with_vendor_sdk(self) -> None:
        for size in (0, 1, 31, 32, 33, 63, 64, 100, 4096):
            data = bytes(i % 256 for i in range(size))
            self.assertEqual(expand(data), _VENDOR_CODEC.crc16_data(data))

    def test_decode_round_trip_includes_padding(self) -> None:
        for size in (0, 1, 31, 32, 33, 64, 100):
            data = bytes((i * 7) & 0xFF for i in range(size))
            padded = data + b"\xff" * ((-size) % 32)
            self.assertEqual(decode(expand(data)), padded)

    def test_decode_rejects_corrupt_packet(self) -> None:
        encoded = bytearray(expand(b"\x01\x02\x03"))
        encoded[-1] ^= 0xFF
        with self.assertRaises(CodecError):
            decode(bytes(encoded))


if __name__ == "__main__":
    unittest.main(verbosity=2)
