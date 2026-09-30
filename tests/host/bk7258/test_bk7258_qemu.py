#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""CLI boundary tests; native model tests run when BK7258_QEMU is provided."""
from __future__ import annotations

import os
import select
import subprocess
import sys
import tempfile
import unittest
import json
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPOSITORY / "tools/bk7258"))
from _lib import qemu  # noqa: E402


class Inputs(unittest.TestCase):
    def test_boards_are_discovered(self):
        self.assertEqual(
            qemu.boards(REPOSITORY), ["aidk_ai_toy", "t5_board", "t5ai_core"]
        )

    def test_invalid_board_is_rejected(self):
        with self.assertRaises(qemu.QemuError):
            qemu.command(REPOSITORY, Path("missing"), "../t5_board", Path("missing"))

    def test_flash_binary_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / "image.bin"
            image.write_bytes(b"not an ELF")
            with self.assertRaises(qemu.QemuError):
                qemu.command(REPOSITORY, Path(sys.executable), "t5_board", image)

    def test_semihosting_is_explicit(self):
        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / "diagnostic.elf"
            image.write_bytes(b"\x7fELF")
            normal = qemu.command(REPOSITORY, Path(sys.executable), "t5_board", image)
            trusted = qemu.command(
                REPOSITORY, Path(sys.executable), "t5_board", image, True
            )
            self.assertNotIn("-semihosting-config", normal)
            self.assertIn("-semihosting-config", trusted)

    def test_unpublished_pin_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "contest2026_135_yongwangzhiqian.xml").write_text("<manifest/>")
            with self.assertRaisesRegex(qemu.QemuError, "published manifest pin"):
                qemu.resolve_source(root, None, None)


@unittest.skipUnless(
    os.environ.get("BK7258_QEMU"), "set BK7258_QEMU for native model checks"
)
class NativeModel(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.stderr = (self.root / "stderr").open("wb")
        self.process = subprocess.Popen(
            [
                os.environ["BK7258_QEMU"],
                "-M",
                "t5_board",
                "-display",
                "none",
                "-monitor",
                "none",
                "-serial",
                "null",
                "-S",
                "-qtest",
                "stdio",
            ],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=self.stderr,
        )
        self.addCleanup(self.temp.cleanup)
        self.addCleanup(self.stderr.close)
        self.addCleanup(self.stop)

    def stop(self):
        self.process.terminate()
        self.process.communicate(timeout=5)

    def qt(self, text):
        self.process.stdin.write((text + "\n").encode())
        self.process.stdin.flush()
        ready, _, _ = select.select([self.process.stdout], [], [], 5)
        self.assertTrue(ready, "qtest command timed out")
        answer = self.process.stdout.readline().decode().strip()
        self.assertTrue(answer.startswith("OK"), answer)
        return int(answer.split()[1], 16) if len(answer.split()) > 1 else None

    def test_three_physical_cpus(self):
        result = subprocess.run(
            [
                os.environ["BK7258_QEMU"],
                "-M",
                "t5_board",
                "-display",
                "none",
                "-monitor",
                "none",
                "-serial",
                "null",
                "-S",
                "-qmp",
                "stdio",
            ],
            input='{"execute":"qmp_capabilities"}\n'
            '{"execute":"query-cpus-fast","id":"cpus"}\n'
            '{"execute":"quit"}\n',
            capture_output=True,
            text=True,
            timeout=5,
            check=True,
        )
        replies = [json.loads(line) for line in result.stdout.splitlines()]
        cpus = next(row["return"] for row in replies if row.get("id") == "cpus")
        self.assertEqual([cpu["cpu-index"] for cpu in cpus], [0, 1, 2])

    def test_sram_and_ns_mmio_aliases(self):
        self.qt("writel 0x28000020 0x12345678")
        for address in (0x08000020, 0x18000020, 0x38000020):
            self.assertEqual(self.qt(f"readl {address:#x}"), 0x12345678)
        self.qt("writel 0x54010080 0x10")
        self.assertEqual(self.qt("readl 0x44010080"), 0x10)

    def test_uart_w1c_route_and_reset_defaults(self):
        self.assertEqual(self.qt("readl 0x44820018"), 0x1A0000)
        for address, value in (
            (0x44820008, 1),
            (0x44820010, 3),
            (0x44820020, 32),
            (0x4482001C, 65),
        ):
            self.qt(f"writel {address:#x} {value:#x}")
        self.assertEqual(self.qt("readl 0x44820024"), 32)
        self.assertEqual(self.qt("readl 0x440100a0"), 0)
        self.qt("writel 0x44010080 0x10")
        self.assertEqual(self.qt("readl 0x440100a0"), 16)
        self.qt("writel 0x44820024 0x20")
        self.assertEqual(self.qt("readl 0x440100a0"), 0)
        self.assertEqual(self.qt("readl 0x44010014"), 2)
        self.assertEqual(self.qt("readl 0x44010018"), 10)


if __name__ == "__main__":
    unittest.main()
