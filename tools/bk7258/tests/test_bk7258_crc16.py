#!/usr/bin/env python3
"""Focused byte-exact checks for the shared BK7258 32+2 CRC codec."""

from __future__ import annotations

import importlib.util
import json
import struct
import subprocess
import sys
import tempfile
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
    SCRIPT = REPOSITORY / "board/bk7258/scripts/bk7258_crc16.py"

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

    def test_cli_validates_vectors_and_writes_manifest(self) -> None:
        with tempfile.TemporaryDirectory(prefix="bk7258-crc-cli-") as directory:
            root = Path(directory)
            raw = bytearray(b"\xff" * 0x108)
            struct.pack_into("<II", raw, 0, 0x28001000, 0x02010041)
            raw[0x100:0x108] = b"BK7236\0\0"
            source = root / "app.bin"
            output = root / "app_crc.bin"
            source.write_bytes(raw)

            result = subprocess.run(
                [
                    sys.executable,
                    str(self.SCRIPT),
                    "--in",
                    str(source),
                    "--out",
                    str(output),
                    "--xip-base",
                    "0x02010000",
                    "--max-size",
                    "0x1000",
                    "--pad-size",
                    "0x120",
                    "--require-magic",
                ],
                text=True,
                capture_output=True,
                check=False,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            padded = bytes(raw).ljust(0x120, b"\xff")
            self.assertEqual(output.read_bytes(), expand(padded))
            manifest = json.loads(
                output.with_suffix(".bin.json").read_text(encoding="utf-8")
            )
            self.assertEqual(manifest["input_size"], len(raw))
            self.assertEqual(manifest["logical_size"], len(padded))
            self.assertEqual(manifest["physical_size"], len(output.read_bytes()))
            self.assertEqual(manifest["execution_base"], 0x02010000)

    def test_cli_preserves_fail_closed_image_checks(self) -> None:
        with tempfile.TemporaryDirectory(prefix="bk7258-crc-negative-") as directory:
            root = Path(directory)

            def run(raw: bytes, *extra: str) -> subprocess.CompletedProcess[str]:
                source = root / "bad.bin"
                output = root / "bad_crc.bin"
                source.write_bytes(raw)
                return subprocess.run(
                    [
                        sys.executable,
                        str(self.SCRIPT),
                        "--in",
                        str(source),
                        "--out",
                        str(output),
                        "--xip-base",
                        "0x02010000",
                        "--max-size",
                        "0x1000",
                        *extra,
                    ],
                    text=True,
                    capture_output=True,
                    check=False,
                )

            valid = bytearray(b"\xff" * 0x108)
            struct.pack_into("<II", valid, 0, 0x28001000, 0x02010041)
            valid[0x100:0x108] = b"BK7236\0\0"
            cases = (
                (b"\0" * 4, (), "too small"),
                (struct.pack("<II", 0, 0x02010041), (), "outside BK7258 SRAM"),
                (
                    struct.pack("<II", 0x28001000, 0x02010040),
                    (),
                    "is not Thumb",
                ),
                (
                    struct.pack("<II", 0x28001000, 0x02011001),
                    (),
                    "outside image execution range",
                ),
                (bytes(valid), ("--max-size", "0x100"), "exceeds slot"),
                (
                    bytes(valid[:0x100] + b"BADMAGIC"),
                    ("--require-magic",),
                    "missing BK7236 magic",
                ),
                (bytes(valid), ("--pad-size", "0x100"), "smaller than input"),
            )
            for raw, extra, expected in cases:
                with self.subTest(expected=expected):
                    result = run(raw, *extra)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(expected, result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
