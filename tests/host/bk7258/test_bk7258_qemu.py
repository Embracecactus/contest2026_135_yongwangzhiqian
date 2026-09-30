#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""CLI boundary tests; native model tests run when BK7258_QEMU is provided."""
from __future__ import annotations

import os
import select
import subprocess
import sys
import tempfile
import shlex
import platform
import time
import unittest
import json
import re
import hashlib
import argparse
import shutil
from unittest import mock
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPOSITORY / "tools/bk7258"))
from _lib import qemu  # noqa: E402


class Inputs(unittest.TestCase):
    def test_fresh_nor_uses_physical_layout_offsets_not_pair_offset_zero(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            artifacts = {}
            rows = []
            for index, name in enumerate(("boot", "cp", "ap")):
                artifacts[name] = root / f"{name}.bin"
                artifacts[name].write_bytes(bytes([index + 1]) * 34)
                rows.append(
                    argparse.Namespace(
                        artifact=name, offset=index * 0x11000, size=0x10000, name=name
                    )
                )
            artifacts["pair"] = root / "pair.bin"
            artifacts["pair"].write_bytes(b"this is not a complete physical image")
            manifest = argparse.Namespace(
                boot="direct",
                finalized_artifacts=artifacts,
                layout=argparse.Namespace(flash_size=8 * 1024 * 1024, partitions=rows),
            )
            argv, evidence = qemu._diagnostic_nor(manifest, root)
            image = (root / "diagnostic-nor.bin").read_bytes()
            self.assertEqual(len(image), 8 * 1024 * 1024)
            for index in range(3):
                self.assertEqual(
                    image[index * 0x11000 : index * 0x11000 + 34],
                    bytes([index + 1]) * 34,
                )
            self.assertEqual(image[-4096:], b"\xff" * 4096)
            self.assertEqual(
                (root / "diagnostic-nor.status").read_bytes(),
                b"\x00\x00\x20" + bytes(509),
            )
            self.assertEqual(len(evidence["placements"]), 3)
            self.assertIn("unit=0", argv[1])
            with self.assertRaisesRegex(qemu.QemuError, "already exists"):
                qemu._diagnostic_nor(manifest, root)
            self.assertEqual((root / "diagnostic-nor.bin").read_bytes(), image)

    def test_fresh_nor_rejects_non_direct_or_non_8m_geometry(self):
        for boot, size in (("mcuboot", 8 * 1024 * 1024), ("direct", 4 * 1024 * 1024)):
            manifest = argparse.Namespace(
                boot=boot, layout=argparse.Namespace(flash_size=size)
            )
            with self.subTest(boot=boot, size=size), self.assertRaises(qemu.QemuError):
                qemu._diagnostic_nor(manifest, Path("unused"))

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

    def test_execution_timeout_must_be_finite_and_positive(self):
        for timeout in (0, -1, float("inf"), float("nan")):
            with self.subTest(timeout=timeout), self.assertRaises(qemu.QemuError):
                qemu.run(
                    REPOSITORY,
                    argparse.Namespace(qemu_command="native-nsh", timeout=timeout),
                )

    def test_native_nsh_rejects_a_product_profile(self):
        from _lib import build

        manifest = argparse.Namespace(
            boot="direct", provenance={"profiles": {"cp": "product/app"}}
        )
        with tempfile.TemporaryDirectory() as temporary, mock.patch.object(
            build, "load_build_manifest", return_value=manifest
        ):
            with self.assertRaisesRegex(qemu.QemuError, "native_nsh build manifest"):
                qemu.run(
                    REPOSITORY,
                    argparse.Namespace(
                        qemu_command="native-nsh",
                        timeout=1,
                        build_manifest=Path("unused.json"),
                        output=Path(temporary),
                    ),
                )

    def test_native_nsh_launch_failure_replaces_stale_evidence(self):
        from _lib import build

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            elf = root / "nuttx.elf"
            elf.write_bytes(b"\x7fELF")
            manifest_file = root / "build-manifest.json"
            manifest_file.write_text("{}")
            manifest = argparse.Namespace(
                boot="direct",
                physical_board="aidk_ai_toy",
                elfs={"cp": elf},
                provenance={
                    "profiles": {"cp": "boards/bk7258/aidk_ai_toy/configs/native_nsh"}
                },
            )
            output = root / "evidence"
            output.mkdir()
            for executable in (root / "missing-qemu", Path("/bin/false")):
                (output / "evidence.json").write_text('{"status":"passed"}')
                (output / "uart.log").write_text("old success")
                with mock.patch.object(
                    build, "load_build_manifest", return_value=manifest
                ):
                    with self.assertRaises(qemu.QemuError):
                        qemu.run(
                            REPOSITORY,
                            argparse.Namespace(
                                qemu_command="native-nsh",
                                timeout=0.5,
                                qemu=executable,
                                build_manifest=manifest_file,
                                output=output,
                            ),
                        )
                report = json.loads((output / "evidence.json").read_text())
                self.assertEqual(report["status"], "failed")
                self.assertIn("error", report)
                self.assertNotIn("old success", (output / "uart.log").read_text())

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
        for name in ("serial.in", "serial.out"):
            os.mkfifo(self.root / name)
        self.serial_in = os.open(self.root / "serial.in", os.O_RDWR | os.O_NONBLOCK)
        self.serial_out = os.open(self.root / "serial.out", os.O_RDWR | os.O_NONBLOCK)
        self.addCleanup(os.close, self.serial_in)
        self.addCleanup(os.close, self.serial_out)
        self.stderr = (self.root / "stderr").open("wb")
        self.irq_events = []
        self.process = subprocess.Popen(
            [
                os.environ["BK7258_QEMU"],
                "-M",
                "t5_board",
                "-accel",
                "qtest",
                "-display",
                "none",
                "-monitor",
                "none",
                "-serial",
                f"pipe:{self.root / 'serial'}",
                "-qtest",
                "stdio",
            ]
            + getattr(self, "extra_args", []),
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=self.stderr,
            bufsize=0,
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
        while True:
            ready, _, _ = select.select([self.process.stdout], [], [], 5)
            self.assertTrue(ready, "qtest command timed out")
            answer = self.process.stdout.readline().decode().strip()
            if not answer.startswith("IRQ "):
                break
            self.irq_events.append(answer)
        self.assertTrue(answer.startswith("OK"), answer)
        return int(answer.split()[1], 16) if len(answer.split()) > 1 else None

    def rx(self, data):
        before = (self.qt("readl 0x44820018") >> 8) & 0xFF
        os.write(self.serial_in, data)
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            count = (self.qt("readl 0x44820018") >> 8) & 0xFF
            if count == before + len(data):
                return
        self.fail("UART did not receive the host bytes")

    def uart_rx_setup(self):
        for address, value in (
            (0x44820008, 1),
            (0x44820010, 0xE01B),
            (0x44820014, 0x4020),
            (0x44820020, 0x40),
            (0x44010080, 0x10),
        ):
            self.qt(f"writel {address:#x} {value:#x}")

    def test_uart_idle_deadline_mask_w1c_and_retrigger(self):
        self.uart_rx_setup()
        self.rx(b"A")
        self.qt("clock_step 276923")
        self.assertEqual(self.qt("readl 0x44820024"), 0)
        self.qt("clock_step 1")
        self.assertEqual(self.qt("readl 0x44820024"), 0x40)
        self.assertEqual(self.qt("readl 0x440100a0"), 16)
        self.qt("writel 0x44820020 0")
        self.assertEqual(self.qt("readl 0x440100a0"), 0)
        self.qt("writel 0x44820020 0x40")
        self.assertEqual(self.qt("readl 0x440100a0"), 16)
        self.qt("writel 0x44820024 0x40")
        self.qt("clock_step 1000000")
        self.assertEqual(self.qt("readl 0x44820024"), 0)
        self.rx(b"B")
        self.qt("clock_step 276924")
        self.assertEqual(self.qt("readl 0x44820024"), 0x40)
        self.assertEqual(self.qt("readl 0x4482001c"), ord("A") << 8)
        self.assertEqual(self.qt("readl 0x4482001c"), ord("B") << 8)

    def test_uart_idle_receive_restart_drain_disable_reset(self):
        self.uart_rx_setup()
        self.rx(b"A")
        self.qt("clock_step 200000")
        self.rx(b"B")
        self.qt("clock_step 200000")
        self.assertEqual(self.qt("readl 0x44820024"), 0)
        for _ in range(2):
            self.qt("readl 0x4482001c")
        self.qt("clock_step 1000000")
        self.assertEqual(self.qt("readl 0x44820024"), 0)
        self.rx(b"C")
        self.qt("writel 0x44820010 0xe019")
        self.qt("clock_step 1000000")
        self.assertEqual(self.qt("readl 0x44820024"), 0)
        self.qt("writel 0x44820010 0xe01b")
        self.qt("clock_step 276924")
        self.assertEqual(self.qt("readl 0x44820024"), 0x40)
        self.qt("writel 0x44820008 0")
        self.qt("clock_step 1000000")
        self.assertEqual(self.qt("readl 0x44820024"), 0)
        self.assertEqual(self.qt("readl 0x44820018"), 0x1A0000)

    def test_uart_idle_long_interval_and_config_readback(self):
        self.uart_rx_setup()
        self.qt("writel 0x44820014 0x34020")  # 256 bit-times
        self.qt("writel 0x44820028 0x200ff")  # flow disabled, configuration only
        self.qt("writel 0x4482002c 0xabcde")  # wake disabled
        self.assertEqual(self.qt("readl 0x44820028"), 0x200FF)
        self.assertEqual(self.qt("readl 0x4482002c"), 0xABCDE)
        self.rx(b"A")
        self.qt("clock_step 2215384")
        self.assertEqual(self.qt("readl 0x44820024"), 0)
        self.qt("clock_step 1")
        self.assertEqual(self.qt("readl 0x44820024"), 0x40)

    def test_aon_watchdog_keys_feed_stop_and_expiry(self):
        self.qt("writel 0x44010080 0x10")
        self.qt("writel 0x44000600 0xa5000a")
        self.qt("clock_step 20000000")
        self.assertEqual(self.qt("readl 0x44010080"), 16)
        self.assertEqual(self.qt("readl 0x44000600"), 0)
        for value in (0x5A000A, 0xA5000B):
            self.qt(f"writel 0x44000600 {value:#x}")
        self.assertEqual(self.qt("readl 0x44000600"), 0)
        for value in (0x5A000A, 0xA5000A):
            self.qt(f"writel 0x44000600 {value:#x}")
        self.qt("clock_step 9000000")
        for value in (0x5A000A, 0xA5000A):
            self.qt(f"writel 0x44000600 {value:#x}")
        self.qt("clock_step 9000000")
        self.assertEqual(self.qt("readl 0x44010080"), 16)
        for value in (0x5A0000, 0xA50000):
            self.qt(f"writel 0x44000600 {value:#x}")
        self.qt("clock_step 20000000")
        self.assertEqual(self.qt("readl 0x44010080"), 16)
        for value in (0x5A0002, 0xA50002):
            self.qt(f"writel 0x44000600 {value:#x}")
        self.qt("clock_step 2000000")
        self.assertEqual(self.qt("readl 0x44010080"), 0)
        self.assertEqual(self.qt("readl 0x44000600"), 0)

    def test_apb_watchdog_divider_gate_and_nmi(self):
        self.qt("irq_intercept_out /machine/soc/wdt[1] sysbus-irq")
        self.qt("writel 0x44010080 0x10")
        self.qt("writel 0x44010028 0xc")
        self.qt("writel 0x44010030 0x80000000")
        self.qt("writel 0x44800008 1")
        for value in (0x5A0014, 0xA50014):
            self.qt(f"writel 0x44800010 {value:#x}")
        self.qt("clock_step 5000000")
        self.qt("writel 0x44010030 0")
        self.qt("clock_step 100000000")
        self.assertEqual(self.qt("readl 0x44010080"), 16)
        self.qt("writel 0x44010030 0x80000000")
        self.qt("clock_step 4999999")
        self.assertEqual(self.qt("readl 0x44010080"), 16)
        self.assertNotIn("IRQ raise 0", self.irq_events)
        self.qt("clock_step 1")
        self.assertIn("IRQ raise 0", self.irq_events)
        self.assertIn("IRQ lower 0", self.irq_events)
        self.assertEqual(self.qt("readl 0x44010080"), 16)  # NMI is not reset
        self.assertEqual(self.qt("readl 0x44800010"), 20)
        self.qt("clock_step 100000000")
        self.assertEqual(self.irq_events.count("IRQ raise 0"), 1)

    def test_apb_watchdog_held_reset_rejects_arming(self):
        self.qt("writel 0x44010080 0x10")
        self.qt("writel 0x44010030 0x80000000")
        for value in (0x5A0001, 0xA50001):
            self.qt(f"writel 0x44800010 {value:#x}")
        self.qt("clock_step 100000000")
        self.assertEqual(self.qt("readl 0x44800010"), 0)
        self.assertEqual(self.qt("readl 0x44010080"), 16)

    def test_aon_commit_keys_and_gpio_retention(self):
        pin = 0x4400040C
        self.qt(f"writel {pin:#x} 0x86")  # output high, input monitor
        self.assertEqual(self.qt(f"readl {pin:#x}"), 0x87)
        self.qt("writel 0x44000000 0x80001234")
        self.qt("writel 0x44000094 0xbdb4aa55")
        self.assertEqual(self.qt("readl 0x440001ec"), 0)
        for key in (0x424B55AA, 0xBDB4AA55):
            self.qt(f"writel 0x44000094 {key:#x}")
        self.assertEqual(self.qt("readl 0x540001ec"), 0x80001234)
        self.qt(f"writel {pin:#x} 0x84")  # shadow low, pad remains retained high
        self.assertEqual(self.qt(f"readl {pin:#x}"), 0x85)
        self.qt("writel 0x44000000 0x1234")
        for key in (0x424B55AA, 0xBDB4AA55):
            self.qt(f"writel 0x44000094 {key:#x}")
        self.assertEqual(self.qt(f"readl {pin:#x}"), 0x84)
        # A watchdog warm reset preserves committed boot state, not uncommitted.
        self.qt("writel 0x44000000 0xdead")
        for key in (0x5A0001, 0xA50001):
            self.qt(f"writel 0x44000600 {key:#x}")
        self.qt("clock_step 1000000")
        self.assertEqual(self.qt("readl 0x44000000"), 0x1234)
        self.assertEqual(self.qt("readl 0x440001ec"), 0x1234)
        self.assertEqual(self.qt(f"readl {pin:#x}"), 0x28)

    def test_gpio_edge_level_w1c_and_high_irq_route(self):
        pin = 0x44000408
        self.qt("set_irq_in /machine/soc/aon gpio-in 2 0")
        self.qt(f"writel {pin:#x} 0x180c")  # rising edge, input enabled
        self.qt("set_irq_in /machine/soc/aon gpio-in 2 1")
        self.assertEqual(self.qt("readl 0x44000500"), 4)
        self.assertEqual(self.qt("readl 0x440100a4"), 0)
        self.qt("writel 0x44010084 0x800000")  # GPIO physical IRQ55
        self.assertEqual(self.qt("readl 0x440100a4"), 0x800000)
        self.qt(f"writel {pin:#x} 0x080c")  # mask a latched source
        self.assertEqual(self.qt("readl 0x440100a4"), 0)
        self.assertEqual(self.qt("readl 0x44000500"), 4)
        self.qt(f"writel {pin:#x} 0x180c")
        self.assertEqual(self.qt("readl 0x440100a4"), 0x800000)
        self.qt("writel 0x44000500 4")
        self.assertEqual(self.qt("readl 0x440100a4"), 0)
        self.qt("set_irq_in /machine/soc/aon gpio-in 2 1")
        self.assertEqual(self.qt("readl 0x44000500"), 0)
        self.qt(f"writel {pin:#x} 0x140c")  # high-level IRQ reasserts after W1C
        self.qt("writel 0x44000500 4")
        self.assertEqual(self.qt("readl 0x44000500"), 4)
        self.qt("set_irq_in /machine/soc/aon gpio-in 2 0")
        self.qt(f"writel {pin:#x} 0x340c")  # per-pin W1C
        self.assertEqual(self.qt("readl 0x44000500"), 0)
        # Disabling the input prevents a new edge from asserting an interrupt.
        self.qt(f"writel {pin:#x} 0x1808")
        self.qt("set_irq_in /machine/soc/aon gpio-in 2 1")
        self.assertEqual(self.qt("readl 0x44000500"), 0)

    def test_analog_transfer_busy_and_reset_cancellation(self):
        self.qt("writel 0x44010100 0x12345678")
        self.assertEqual(self.qt("readl 0x440100e8"), 1)
        self.assertEqual(self.qt("readl 0x44010100"), 0)
        self.qt("writel 0x44010100 0xdeadbeef")  # busy transfer cannot overwrite
        self.qt("clock_step 999")
        self.assertEqual(self.qt("readl 0x440100e8"), 1)
        self.qt("clock_step 1")
        self.assertEqual(self.qt("readl 0x440100e8"), 0)
        self.assertEqual(self.qt("readl 0x54010100"), 0x12345678)
        for key in (0x5A0001, 0xA50001):
            self.qt(f"writel 0x44000600 {key:#x}")
        self.qt("clock_step 999500")
        self.qt("writel 0x44010100 0xa5")
        self.qt("clock_step 500")
        self.qt("clock_step 1000")
        self.assertEqual(self.qt("readl 0x44010100"), 0)
        self.assertEqual(self.qt("readl 0x440100e8"), 0)

    def test_clock_monitor_timed_ratio_mask_and_peer_routes(self):
        self.qt("writel 0x448a0008 1")
        self.qt("writel 0x44010080 0x200000")
        self.qt("writel 0x44010088 0x200000")
        for window in (32, 64, 128, 1023):
            self.qt("writel 0x448a0014 0")
            self.qt("writel 0x448a0020 7")
            self.qt(f"writel 0x448a0010 {window}")
            self.qt("writel 0x448a0014 1")  # count, completion masked
            self.qt(f"clock_step {window * 31250 - 1}")
            self.assertEqual(self.qt("readl 0x448a0020"), 0)
            self.qt("clock_step 1")
            self.assertEqual(self.qt("readl 0x548a0018"), 26000000 * window // 32000)
            self.assertEqual(self.qt("readl 0x448a0020"), 1)
            self.assertEqual(self.qt("readl 0x440100a0"), 0)
            self.qt("writel 0x448a0014 3")  # late unmask preserves completion
            self.assertEqual(self.qt("readl 0x440100a0"), 0x200000)
            self.assertEqual(self.qt("readl 0x440100a8"), 0x200000)
            self.qt("writel 0x44010080 0")
            self.assertEqual(self.qt("readl 0x440100a8"), 0x200000)
            self.qt("writel 0x44010080 0x200000")
            self.qt("writel 0x448a0020 1")
            self.assertEqual(self.qt("readl 0x440100a0"), 0)
            self.assertEqual(self.qt("readl 0x440100a8"), 0)
            self.qt("clock_step 50000000")
            self.assertEqual(self.qt("readl 0x448a0020"), 0)  # no auto-retrigger

    def test_clock_monitor_busy_disable_and_reset_cancel(self):
        self.qt("writel 0x448a0008 1")
        self.qt("writel 0x448a0010 64")
        self.qt("writel 0x448a0014 3")
        self.qt("writel 0x448a0010 128")
        self.assertEqual(self.qt("readl 0x448a0010"), 64)
        self.qt("clock_step 1000000")
        self.qt("writel 0x448a0014 0")
        self.qt("clock_step 5000000")
        self.assertEqual(self.qt("readl 0x448a0020"), 0)
        self.qt("writel 0x448a0014 3")
        self.qt("clock_step 1000000")
        self.qt("writel 0x448a0008 0")
        self.qt("clock_step 5000000")
        for offset in (0x10, 0x14, 0x18, 0x20):
            self.assertEqual(self.qt(f"readl {0x448a0000 + offset:#x}"), 0)
        self.qt("writel 0x448a0010 32")  # held reset ignores configuration
        self.assertEqual(self.qt("readl 0x448a0010"), 0)
        self.qt("writel 0x448a0008 1")
        self.qt("writel 0x448a0014 3")  # zero window cannot fabricate completion
        self.qt("clock_step 5000000")
        self.assertEqual(self.qt("readl 0x448a0020"), 0)
        self.assertEqual(self.qt("readl 0x448a0018"), 0)

    def test_clock_monitor_lost_source_invalidates_and_requires_rearm(self):
        self.qt("writel 0x448a0008 1")
        self.qt("writel 0x448a0010 64")
        self.qt("writel 0x448a0014 3")
        self.qt("clock_step 1000000")
        self.qt("writel 0x44010114 0x4000")  # ANA5 ROSC power-down
        self.qt("clock_step 1000")
        self.qt("clock_step 5000000")
        self.assertEqual(self.qt("readl 0x448a0020"), 0)
        self.qt("writel 0x448a0014 0")
        self.qt("writel 0x448a0014 3")  # stopped source also rejects a new start
        self.qt("clock_step 5000000")
        self.assertEqual(self.qt("readl 0x448a0020"), 0)
        self.qt("writel 0x44010114 0")
        self.qt("clock_step 5000000")
        self.assertEqual(self.qt("readl 0x448a0020"), 0)
        self.qt("writel 0x448a0014 0")
        self.qt("writel 0x448a0014 3")
        self.qt("clock_step 2000000")
        self.assertEqual(self.qt("readl 0x448a0020"), 1)
        self.assertEqual(self.qt("readl 0x448a0018"), 52000)

    def mailbox_setup(self):
        self.qt("writel 0x41000008 5")  # reset released, cross-channel setup allowed
        for channel, (start, length) in enumerate(((0, 2), (2, 3), (5, 3))):
            base = 0x41000040 + channel * 0x40
            self.qt(f"writel {base:#x} {0x100 | start:#x}")
            self.qt(f"writel {base + 4:#x} {length << 1 | 1:#x}")
            self.qt(f"writel {0x44010084 + channel * 8:#x} 0x80000000")

    def mailbox_send(self, source, destination, data0, data1):
        base = 0x41000040 + source * 0x40
        for offset, value in ((8, data0), (12, data1), (16, destination)):
            self.qt(f"writel {base + offset:#x} {value:#x}")

    def mailbox_receive(self, destination):
        base = 0x41000040 + destination * 0x40
        return tuple(self.qt(f"readl {base + offset:#x}") for offset in (20, 24, 28))

    def test_mailbox_cross_channel_payloads_and_private_irq_routes(self):
        self.mailbox_setup()
        self.assertEqual(self.qt("readl 0x51000000"), 0x6D61696C)
        self.assertEqual(self.qt("readl 0x41000004"), 0x20000)
        for source in range(3):
            for destination in range(3):
                if source == destination:
                    continue
                data = (0x28001000 + source, 0 if destination == 2 else 16)
                self.mailbox_send(source, destination, *data)
                base = 0x41000040 + destination * 0x40
                self.assertEqual(self.qt(f"readl {base + 0x20:#x}"), 4)
                for core in range(3):
                    self.assertEqual(
                        self.qt(f"readl {0x440100a4 + core * 8:#x}"),
                        0x80000000 if core == destination else 0,
                    )
                self.assertEqual(self.mailbox_receive(destination), (source, *data))
                self.assertEqual(self.qt(f"readl {base + 0x20:#x}"), 2)
                # RX-data reads are latches, not additional FIFO pop operations.
                self.assertEqual(self.qt(f"readl {base + 0x18:#x}"), data[0])
                self.assertEqual(self.qt("readl 0x4100000c"), 0)

    def test_mailbox_full_preserves_order_wraparound_and_w1c(self):
        self.mailbox_setup()
        for value in (11, 22, 33):
            self.mailbox_send(0, 1, value, 16)
        self.assertEqual(self.qt("readl 0x410000a0"), 13)
        self.mailbox_send(0, 1, 44, 16)
        self.assertTrue(self.qt("readl 0x41000040") & (1 << 18))
        self.qt("writel 0x41000040 0x40100")
        self.assertFalse(self.qt("readl 0x41000040") & (1 << 18))
        self.assertEqual(self.mailbox_receive(1), (0, 11, 16))
        self.mailbox_send(2, 1, 55, 0)
        self.assertEqual(self.mailbox_receive(1), (0, 22, 16))
        self.assertEqual(self.mailbox_receive(1), (0, 33, 16))
        self.assertEqual(self.mailbox_receive(1), (2, 55, 0))
        self.assertEqual(self.qt("readl 0x410000a0"), 2)
        self.mailbox_send(0, 7, 99, 16)
        self.assertTrue(self.qt("readl 0x41000040") & (1 << 16))
        self.qt("writel 0x41000040 0x10100")
        self.assertFalse(self.qt("readl 0x41000040") & (1 << 16))
        self.assertEqual(self.qt("readl 0x4100000c"), 0)

    def test_mailbox_protection_mask_and_nonempty_disable(self):
        self.mailbox_setup()
        self.mailbox_send(0, 1, 0x1234, 16)
        self.qt("writel 0x41000080 2")  # channel RX IRQ mask, keep FIFO start
        self.assertEqual(self.qt("readl 0x440100ac"), 0)
        self.assertEqual(self.qt("readl 0x410000a0"), 4)
        self.qt("writel 0x41000080 0x102")
        self.assertEqual(self.qt("readl 0x440100ac"), 0x80000000)
        self.qt("writel 0x4401008c 0")
        self.assertEqual(self.qt("readl 0x440100ac"), 0)
        self.qt("writel 0x4401008c 0x80000000")
        self.qt("writel 0x41000084 6")  # model disable preserves queued entries
        self.assertEqual(self.qt("readl 0x440100ac"), 0)
        self.qt("writel 0x41000084 7")
        self.assertEqual(self.qt("readl 0x440100ac"), 0x80000000)
        self.qt("writel 0x41000008 1")  # hardware channel ownership enforced
        self.assertEqual(self.qt("readl 0x41000094"), 0xF)
        self.assertTrue(self.qt("readl 0x41000080") & (1 << 17))
        self.assertEqual(self.qt("readl 0x410000a0"), 4)
        self.qt("writel 0x41000090 2")  # CPU0 cannot send through channel1
        self.assertTrue(self.qt("readl 0x41000080") & (1 << 16))
        self.assertEqual(self.qt("readl 0x410000e0"), 2)
        self.qt("writel 0x41000008 5")
        self.qt("writel 0x41000080 0x20102")
        self.assertTrue(self.qt("readl 0x41000080") & (1 << 16))
        self.assertFalse(self.qt("readl 0x41000080") & (1 << 17))
        self.assertEqual(self.mailbox_receive(1), (0, 0x1234, 16))

    def test_mailbox_invalid_partition_and_reset_cancel(self):
        self.mailbox_setup()
        self.mailbox_send(0, 1, 1, 16)
        self.qt("writel 0x41000084 6")
        self.qt("writel 0x410000c4 6")
        self.qt("writel 0x410000c0 0x102")
        self.qt("writel 0x410000c4 7")  # cannot overlap a disabled nonempty FIFO
        self.assertEqual(self.qt("readl 0x410000c4"), 6)
        self.qt("writel 0x410000c0 0x107")
        self.qt("writel 0x410000c4 7")  # cannot address beyond eight total slots
        self.assertEqual(self.qt("readl 0x410000c4"), 6)
        self.qt("writel 0x41000008 0")
        for channel in range(3):
            self.assertEqual(self.qt(f"readl {0x41000060 + channel * 0x40:#x}"), 2)
            self.assertEqual(self.qt(f"readl {0x440100a4 + channel * 8:#x}"), 0)
        self.mailbox_send(0, 1, 2, 16)
        self.assertEqual(self.qt("readl 0x410000a0"), 2)
        self.assertEqual(self.qt("readl 0x4100000c"), 0)

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


@unittest.skipUnless(os.environ.get("BK7258_QEMU"), "set BK7258_QEMU for Flash checks")
class FlashModel(unittest.TestCase):
    qt = NativeModel.qt
    stop = NativeModel.stop

    def setUp(self):
        self.images = tempfile.TemporaryDirectory()
        self.addCleanup(self.images.cleanup)
        self.image = Path(self.images.name) / "nor.bin"
        self.status_image = Path(self.images.name) / "nor.status"
        self.image.write_bytes(b"\xff" * (8 * 1024 * 1024))
        self.status_image.write_bytes(b"\x00\x00\x20" + bytes(509))
        self.extra_args = [
            "-drive",
            f"if=pflash,unit=0,format=raw,file={self.image}",
            "-drive",
            f"if=pflash,unit=1,format=raw,file={self.status_image}",
            "-d",
            "guest_errors,unimp",
        ]
        NativeModel.setUp(self)
        self.qt("writel 0x44030008 1")

    def qmp(self, command):
        os.write(
            self.qmp_in,
            json.dumps({"execute": command, "id": command}).encode() + b"\n",
        )
        deadline = time.monotonic() + 5
        data = bytearray()
        while time.monotonic() < deadline:
            ready, _, _ = select.select([self.qmp_out], [], [], 0.1)
            if ready:
                data.extend(os.read(self.qmp_out, 65536))
                while b"\n" in data:
                    line, _, rest = data.partition(b"\n")
                    data[:] = rest
                    row = json.loads(line)
                    if row.get("id") == command:
                        self.assertIn("return", row, row)
                        return row["return"]
        self.fail("QMP command timed out")

    def restart_with_qmp(self, readonly=False):
        self.stop()
        root = Path(self.images.name)
        for name in ("qmp.in", "qmp.out"):
            os.mkfifo(root / name)
        self.qmp_in = os.open(root / "qmp.in", os.O_RDWR | os.O_NONBLOCK)
        self.qmp_out = os.open(root / "qmp.out", os.O_RDWR | os.O_NONBLOCK)
        self.addCleanup(os.close, self.qmp_in)
        self.addCleanup(os.close, self.qmp_out)
        if readonly:
            self.extra_args[1] += ",readonly=on"
        self.extra_args += ["-qmp", f"pipe:{root / 'qmp'}"]
        NativeModel.setUp(self)
        self.qmp("qmp_capabilities")
        self.qt("writel 0x44030008 1")

    def test_flash_readonly_host_image_stops_vm_without_claiming_commit(self):
        before = hashlib.sha256(self.image.read_bytes()).hexdigest()
        self.restart_with_qmp(readonly=True)
        self.program(0x2000, b"\x00" * 32)
        self.assertEqual(self.qmp("query-status")["status"], "io-error")
        self.assertEqual(hashlib.sha256(self.image.read_bytes()).hexdigest(), before)
        self.assertEqual(self.qt("readl 0x02002000"), 0xFFFFFFFF)

    def test_flash_out_of_array_program_stops_without_claiming_commit(self):
        self.restart_with_qmp()
        self.program(0x800000, bytes(32))
        self.assertEqual(self.qmp("query-status")["status"], "internal-error")
        self.assertEqual(self.image.read_bytes(), b"\xff" * (8 * 1024 * 1024))

    def test_flash_invalid_sr3_write_stops_without_claiming_commit(self):
        self.restart_with_qmp()
        self.qt("writel 0x4403001c 0x10011")  # custom WRSR opcode 11h
        self.qt("writel 0x44030028 0x0c000800")  # reserved SR3 bit 1
        self.operation(4)
        self.assertEqual(self.qmp("query-status")["status"], "internal-error")
        self.assertEqual(self.status_image.read_bytes(), b"\x00\x00\x20" + bytes(509))

    def test_flash_unsupported_physical_status_mode_stops_without_success(self):
        self.restart_with_qmp()
        self.write_status(0x0100)  # SRP1 special-order mode has no model.
        self.assertEqual(self.qmp("query-status")["status"], "internal-error")
        self.assertEqual(self.status_image.read_bytes(), b"\x00\x00\x20" + bytes(509))

    def operation(self, operation, address=0, wait=True):
        self.qt(f"writel 0x44030054 {operation << 24 | address:#x}")
        self.qt("writel 0x44030010 0x60000000")
        if wait:
            self.assertTrue(self.qt("readl 0x44030010") & 0x80000000)
            self.qt("clock_step 1000")
            self.assertFalse(self.qt("readl 0x44030010") & 0x80000000)

    def program(self, address, data, wait=True):
        self.assertEqual(len(data), 32)
        for pos in range(0, 32, 4):
            value = int.from_bytes(data[pos : pos + 4], "little")
            self.qt(f"writel 0x44030014 {value:#x}")
        self.operation(12, address, wait)

    def read_array(self, address):
        self.operation(5, address)
        return b"".join(
            self.qt("readl 0x44030018").to_bytes(4, "little") for _ in range(8)
        )

    def write_status(self, value):
        self.qt(f"writel 0x44030028 {0x0c000000 | value << 10:#x}")
        self.operation(7)

    def read_status(self):
        self.operation(3)
        low = self.qt("readl 0x44030024") & 0xFF
        self.operation(6)
        return low | (self.qt("readl 0x44030024") & 0xFF) << 8

    def test_flash_id_requires_transaction_and_array_program_is_and_only(self):
        self.assertEqual(self.qt("readl 0x44030020"), 0)
        self.operation(20, wait=False)
        self.qt("clock_step 999")
        self.assertEqual(self.qt("readl 0x44030020"), 0)
        self.assertTrue(self.qt("readl 0x44030010") & 0x80000000)
        self.qt("clock_step 1")
        self.assertEqual(self.qt("readl 0x54030020"), 0xC86517)
        self.program(0x1000, b"\x5a" * 32)
        self.program(0x1000, b"\xf0" * 32)
        self.assertEqual(self.read_array(0x1000), b"\x50" * 32)
        self.operation(13, 0x1000)
        self.assertEqual(self.read_array(0x1000), b"\xff" * 32)

    def test_flash_status_protects_real_range_and_reset_preserves_nv(self):
        self.write_status(0x0238)  # QE=1, BP=01110: lower 4 MiB protected
        self.assertEqual(self.read_status(), 0x0238)
        self.program(0x1000, bytes(32))
        self.assertEqual(self.read_array(0x1000), b"\xff" * 32)
        self.program(0x500000, b"\x12" * 32)
        self.assertEqual(self.read_array(0x500000), b"\x12" * 32)
        self.qt("writel 0x44030008 0")
        self.qt("writel 0x44030008 1")
        self.assertEqual(self.read_status(), 0x0238)
        self.write_status(0x0200)  # remove BP while keeping QE
        self.program(0x1000, bytes(32))
        self.write_status(0x0238)
        self.operation(13, 0x1000)
        self.assertEqual(self.read_array(0x1000), bytes(32))
        self.operation(13, 0x500000)
        self.assertEqual(self.read_array(0x500000), b"\xff" * 32)

    def test_flash_busy_reset_cancel_and_fresh_process_persistence(self):
        self.program(0x2000, b"\xa5" * 32, wait=False)
        self.qt("clock_step 999")
        self.assertEqual(self.image.read_bytes()[0x2000:0x2020], b"\xff" * 32)
        self.qt("writel 0x44030008 0")
        self.qt("clock_step 1000000")
        self.qt("writel 0x44030008 1")
        self.assertEqual(self.read_array(0x2000), b"\xff" * 32)
        self.program(0x2000, b"\x3c" * 32)
        self.write_status(0x0200)
        self.stop()
        self.assertEqual(self.image.read_bytes()[0x2000:0x2020], b"\x3c" * 32)
        self.assertEqual(self.status_image.read_bytes(), b"\x00\x02\x20" + bytes(509))
        NativeModel.setUp(self)
        self.qt("writel 0x44030008 1")
        self.assertEqual(self.read_array(0x2000), b"\x3c" * 32)
        self.assertEqual(self.read_status(), 0x0200)

    def test_flash_rejects_incoherent_entry_and_backing_selection(self):
        self.stop()
        elf = Path(self.images.name) / "unused.elf"
        elf.write_bytes(b"\x7fELF")
        for args, message in (
            (self.extra_args[2:4], "status drive requires the array drive"),
            (self.extra_args + ["-kernel", str(elf)], "choose physical NOR image"),
            (
                self.extra_args + ["-global", "bk7258-soc.xip-size=4096"],
                "requires its exact XIP size",
            ),
        ):
            with self.subTest(message=message):
                result = subprocess.run(
                    [
                        os.environ["BK7258_QEMU"],
                        "-M",
                        "t5_board",
                        "-S",
                        "-display",
                        "none",
                        "-monitor",
                        "none",
                        "-serial",
                        "null",
                        "-qmp",
                        "stdio",
                        *args,
                    ],
                    input=b'{"execute":"qmp_capabilities"}\n{"execute":"quit"}\n',
                    capture_output=True,
                    timeout=5,
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(message, result.stderr.decode())

    def test_flash_rejects_malformed_image_geometry_and_status(self):
        self.stop()
        for array_size, status, message in (
            (4096, b"\x00\x00\x20" + bytes(509), "drive must be exactly"),
            (8 * 1024 * 1024, bytes(1024), "status-drive must be exactly"),
            (
                8 * 1024 * 1024,
                b"\x01\x00\x20" + bytes(509),
                "unsupported or volatile bits",
            ),
            (8 * 1024 * 1024, b"\x00\x00\x20\x01" + bytes(508), "reserved padding"),
        ):
            with self.subTest(
                array_size=array_size, status_size=len(status), message=message
            ):
                self.image.write_bytes(b"\xff" * array_size)
                self.status_image.write_bytes(status)
                result = subprocess.run(
                    [
                        os.environ["BK7258_QEMU"],
                        "-M",
                        "t5_board",
                        "-S",
                        "-display",
                        "none",
                        "-monitor",
                        "none",
                        "-serial",
                        "null",
                        "-qmp",
                        "stdio",
                        *self.extra_args,
                    ],
                    input=b'{"execute":"qmp_capabilities"}\n{"execute":"quit"}\n',
                    capture_output=True,
                    timeout=5,
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(message, result.stderr.decode())

    def test_flash_quad_reads_require_qe_and_failed_reads_do_not_reuse_fifo(self):
        self.qt("writel 0x44030028 0x0c000020")
        self.qt("readl 0x02000000")
        self.stderr.flush()
        self.assertIn("quad XIP without QE", (self.root / "stderr").read_text())
        self.write_status(0x0200)
        self.qt("writel 0x44030028 0x0c000020")
        self.assertEqual(self.qt("readl 0x02000000"), 0xFFFFFFFF)
        self.operation(5, 0)
        self.operation(5, 0xFFFFF0)  # outside the modeled physical array
        self.qt("readl 0x44030018")
        self.stderr.flush()
        self.assertIn("RX FIFO underflow", (self.root / "stderr").read_text())

    def test_flash_xip_crc_aliases_and_raw_array_share_one_backing(self):
        from _lib.image import crc16

        data = bytes(range(32))
        self.program(0, data)
        self.qt("readl 0x02000000")  # data programmed, CRC still erased
        self.assertEqual((self.qt("readl 0x44030024") >> 8) & 0xFF, 1)
        self.program(32, crc16(data).to_bytes(2, "big") + b"\xff" * 30)
        self.assertEqual(self.qt("readl 0x02000000"), 0x03020100)
        self.assertEqual(self.qt("readl 0x1200001c"), 0x1F1E1D1C)
        self.assertEqual(self.read_array(0), data)
        self.assertEqual(self.read_array(32)[:2], crc16(data).to_bytes(2, "big"))
        self.operation(13, 0)
        self.assertEqual(self.qt("readl 0x02000000"), 0xFFFFFFFF)
        self.assertEqual(self.qt("readl 0x12000000"), 0xFFFFFFFF)


def product_stop_contract(manifest):
    """Bind a known missing device to the actual, hash-checked CP config."""
    profile = "boards/bk7258/aidk_ai_toy/configs/app"
    if (
        manifest.physical_board != "aidk_ai_toy"
        or manifest.provenance is None
        or manifest.provenance["profiles"]["cp"] != profile
    ):
        raise ValueError("product stop probe requires the AIDK app profile")
    config = manifest.elfs["cp"].parent / ".config"
    raw = config.read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    document = json.loads(manifest.source.read_text())
    if digest != document["roles"]["cp"]["resolved_config_sha256"]:
        raise ValueError("product CP resolved config hash mismatch")
    enabled = set(re.findall(r"(?m)^(CONFIG_\w+)=y$", raw.decode()))
    flags = tuple(
        name in enabled
        for name in (
            "CONFIG_BK7258_MCUBOOT_IMAGE",
            "CONFIG_BK7258_OTA",
            "CONFIG_BK7258_PM_SOFT_OFF",
        )
    )
    # MCUboot enables OTA, which makes the product soft-off path reachable.
    # Its reset-cause read precedes Flash initialization. Direct excludes it.
    contracts = {
        "direct": ((False, False, False), "0x4980c000", "RF controller"),
        "mcuboot": ((True, True, True), "0x440001e8", "AON PMU R7A reset cause"),
    }
    if manifest.boot not in contracts or flags != contracts[manifest.boot][0]:
        raise ValueError("unsupported product boot/config stop contract")
    _, address, device = contracts[manifest.boot]
    return address, device, digest


class ProductStopInputs(unittest.TestCase):
    def contract(self, boot="direct", flags=(), profile="app", corrupt=False):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            raw = "".join(f"CONFIG_BK7258_{name}=y\n" for name in flags).encode()
            (root / ".config").write_bytes(raw)
            source = root / "build-manifest.json"
            source.write_text(
                json.dumps(
                    {
                        "roles": {
                            "cp": {
                                "resolved_config_sha256": (
                                    "0" * 64
                                    if corrupt
                                    else hashlib.sha256(raw).hexdigest()
                                )
                            }
                        }
                    }
                )
            )
            return product_stop_contract(
                argparse.Namespace(
                    boot=boot,
                    physical_board="aidk_ai_toy",
                    source=source,
                    elfs={"cp": root / "nuttx"},
                    provenance={
                        "profiles": {
                            "cp": f"boards/bk7258/aidk_ai_toy/configs/{profile}"
                        }
                    },
                )
            )

    def test_direct_physical_nor_has_the_rf_stop(self):
        self.assertEqual(self.contract()[0], "0x4980c000")

    def test_mcuboot_has_the_earlier_reset_cause_stop(self):
        self.assertEqual(
            self.contract("mcuboot", ("MCUBOOT_IMAGE", "OTA", "PM_SOFT_OFF"))[0],
            "0x440001e8",
        )

    def test_other_profiles_are_not_product_evidence(self):
        with self.assertRaisesRegex(ValueError, "AIDK app profile"):
            self.contract(profile="native_nsh")

    def test_modified_config_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "hash mismatch"):
            self.contract(corrupt=True)

    def test_unknown_or_mismatched_boot_config_is_rejected(self):
        for boot, flags in (
            ("unknown", ()),
            ("mcuboot", ()),
            ("direct", ("MCUBOOT_IMAGE",)),
            ("mcuboot", ("MCUBOOT_IMAGE", "OTA")),
            ("direct", ("PM_SOFT_OFF",)),
        ):
            with self.subTest(boot=boot, flags=flags), self.assertRaisesRegex(
                ValueError, "unsupported product boot/config"
            ):
                self.contract(boot, flags)


@unittest.skipUnless(
    os.environ.get("BK7258_QEMU")
    and os.environ.get("BK7258_PRODUCT_BUILD_MANIFEST")
    and os.environ.get("BK7258_NM"),
    "set BK7258_PRODUCT_BUILD_MANIFEST and BK7258_NM for the product stop probe",
)
class ProductCPStop(unittest.TestCase):
    def test_original_product_stops_at_its_known_missing_device(self):
        from _lib import build

        if os.environ.get("BK7258_QEMU_EVIDENCE"):
            attempted = Path(os.environ["BK7258_QEMU_EVIDENCE"])
            attempted.mkdir(parents=True, exist_ok=True)
            (attempted / "product-stop.json").write_text(
                '{"status":"failed","error":"probe attempt has not completed"}\n'
            )
        manifest = build.load_build_manifest(
            REPOSITORY, Path(os.environ["BK7258_PRODUCT_BUILD_MANIFEST"])
        )
        expected, device, config_digest = product_stop_contract(manifest)
        elf = manifest.elfs["cp"]
        symbols = subprocess.check_output(
            [os.environ["BK7258_NM"], "-n", str(elf)], text=True, timeout=10
        )
        vectors = re.findall(r"(?m)^([0-9a-fA-F]+) \w _vectors$", symbols)
        self.assertEqual(len(vectors), 1)
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(os.environ.get("BK7258_QEMU_EVIDENCE", temporary))
            output.mkdir(parents=True, exist_ok=True)
            mmio = output / "product-stop-mmio.log"
            uart = output / "product-stop-uart.log"
            # A previous probe must not satisfy the first-fault poll before
            # the new emulator has opened its log files.
            mmio.write_text("")
            uart.write_bytes(b"")
            argv = qemu.command(
                REPOSITORY, Path(os.environ["BK7258_QEMU"]), "aidk_ai_toy", elf
            )
            nor = None
            if manifest.boot == "direct":
                drives, nor = qemu._diagnostic_nor(manifest, output)
                index = argv.index("-kernel")
                del argv[index : index + 2]
                argv += drives
            argv[argv.index("stdio")] = f"file:{uart}"
            with (output / "product-stop-stderr.log").open("wb") as stderr:
                process = subprocess.Popen(
                    argv
                    + [
                        "-global",
                        f"bk7258-soc.boot-vector=0x{vectors[0]}",
                        "-qmp",
                        "stdio",
                        "-d",
                        "int,guest_errors,unimp",
                        "-D",
                        str(mmio),
                    ],
                    stdin=subprocess.PIPE,
                    stdout=subprocess.PIPE,
                    stderr=stderr,
                )
                trace = ""
                try:
                    deadline = time.monotonic() + 30
                    while time.monotonic() < deadline and process.poll() is None:
                        if mmio.exists():
                            trace = mmio.read_text(errors="replace")
                            if "fault address" in trace or len(trace) > 2 * 1024 * 1024:
                                break
                        time.sleep(0.01)
                finally:
                    try:
                        process.communicate(
                            b'{"execute":"qmp_capabilities"}\n' b'{"execute":"quit"}\n',
                            timeout=5,
                        )
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.communicate(timeout=5)
                trace = mmio.read_text(errors="replace")
            faults = re.findall(r"fault address (0x[0-9a-f]+)", trace)
            known_stop = (
                faults
                and faults[0] == expected
                and process.returncode == 0
                and b"NuttShell (NSH)" not in uart.read_bytes()
            )
            (output / "resolved.config").write_bytes(
                (elf.parent / ".config").read_bytes()
            )
            (output / "build-manifest.json").write_bytes(manifest.source.read_bytes())
            evidence = {
                "status": "blocked" if known_stop else "failed",
                "scope": "unchanged product CP ELF entry probe; bootloader not executed",
                "boot": manifest.boot,
                "cp_profile": manifest.provenance["profiles"]["cp"],
                "resolved_config_sha256": config_digest,
                "build_manifest_sha256": hashlib.sha256(
                    manifest.source.read_bytes()
                ).hexdigest(),
                "elf_sha256": hashlib.sha256(elf.read_bytes()).hexdigest(),
                "vector": f"0x{vectors[0]}",
                "first_fault": faults[0] if faults else None,
                "expected_first_fault": expected,
                "expected_missing_device": device,
                "physical_nor": nor,
                "launch_argv": argv,
            }
            (output / "product-stop.json").write_text(
                json.dumps(evidence, indent=2) + "\n"
            )
            self.assertTrue(faults, "product did not reach the known MMIO stop")
            self.assertEqual(faults[0], expected)
            self.assertEqual(process.returncode, 0, "QEMU did not exit cleanly")
            self.assertNotIn(b"NuttShell (NSH)", uart.read_bytes())


@unittest.skipUnless(
    os.environ.get("BK7258_QEMU")
    and (os.environ.get("BK7258_CC") or shutil.which("arm-none-eabi-gcc")),
    "set BK7258_QEMU and BK7258_CC for native MMIO instruction-fetch checks",
)
class FlashInstructionFetch(unittest.TestCase):
    def test_split_thumb_instruction_and_recorded_disassembly(self):
        from _lib.image import crc_encode

        compiler = Path(
            os.environ.get("BK7258_CC") or shutil.which("arm-none-eabi-gcc")
        )
        objcopy = compiler.with_name("arm-none-eabi-objcopy")
        fixture = REPOSITORY / "tests/host/bk7258/qemu"
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            for bad, bad_crc, expected in (
                (False, False, 0),
                (True, False, 1),
                (False, True, 2),
            ):
                with self.subTest(bad_result=bad, bad_crc=bad_crc):
                    elf, raw = output / "cross.elf", output / "cross.bin"
                    subprocess.run(
                        [
                            str(compiler),
                            "-nostdlib",
                            "-mcpu=cortex-m33",
                            "-mthumb",
                            *(["-DBAD_RESULT"] if bad else []),
                            "-T",
                            str(fixture / "xip_cross_page.ld"),
                            str(fixture / "xip_cross_page.S"),
                            "-o",
                            str(elf),
                        ],
                        check=True,
                        timeout=30,
                    )
                    subprocess.run(
                        [str(objcopy), "-O", "binary", str(elf), str(raw)],
                        check=True,
                        timeout=10,
                    )
                    image = bytearray(b"\xff" * (8 * 1024 * 1024))
                    encoded = crc_encode(raw.read_bytes())
                    image[0x11000 : 0x11000 + len(encoded)] = encoded
                    if bad_crc:
                        image[(0x11000 // 32) * 34] ^= 1
                    nor = output / "nor.bin"
                    nor.write_bytes(image)
                    # No ELF loader: instruction bytes must come from CRC XIP.
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
                            "-semihosting-config",
                            "enable=on,target=native",
                            "-drive",
                            f"if=pflash,unit=0,format=raw,file={nor}",
                            "-d",
                            "in_asm,guest_errors",
                            "-D",
                            str(output / "instructions.log"),
                        ],
                        capture_output=True,
                        timeout=10,
                    )
                    self.assertEqual(
                        result.returncode, expected, result.stderr.decode()
                    )
                    trace = (output / "instructions.log").read_text()
                    self.assertIn("0x02010040" if bad_crc else "0x02010ffe", trace)
                    if bad_crc:
                        self.assertIn("XIP CRC mismatch", trace)


@unittest.skipUnless(
    os.environ.get("BK7258_QEMU_SOURCE") and os.environ.get("BK7258_QEMU_BUILD"),
    "set BK7258_QEMU_SOURCE and BK7258_QEMU_BUILD for production NOR helper checks",
)
class NorBackendModel(unittest.TestCase):
    def test_production_nor_helpers_with_injected_block_errors(self):
        source = Path(os.environ["BK7258_QEMU_SOURCE"]).resolve()
        build = Path(os.environ["BK7258_QEMU_BUILD"]).resolve()
        flags = shlex.split(
            subprocess.check_output(
                ["pkg-config", "--cflags", "--libs", "glib-2.0"], text=True
            )
        )
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / "nor_backend_test"
            subprocess.run(
                [
                    os.environ.get("CC", "cc"),
                    "-std=gnu11",
                    "-O1",
                    "-g",
                    "-ffunction-sections",
                    "-fdata-sections",
                    "-Wall",
                    "-Werror",
                    "-Wno-unused-function",
                    "-D_GNU_SOURCE",
                    "-DCONFIG_SOFTMMU",
                    "-DCOMPILING_SYSTEM_VS_USER",
                    "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all",
                    f'-DNOR_SOURCE="{source}/hw/block/bk7258_nor.c"',
                    f"-I{build}",
                    f"-I{source}",
                    f"-I{source}/include",
                    f"-I{source}/host/include/{platform.machine()}",
                    f"-I{source}/host/include/generic",
                    "-isystem",
                    str(source / "linux-headers"),
                    str(REPOSITORY / "tests/host/bk7258/qemu/nor_backend_test.c"),
                    "-Wl,--gc-sections",
                    *flags,
                    "-o",
                    str(binary),
                ],
                check=True,
                timeout=60,
            )
            subprocess.run([str(binary)], check=True, timeout=60)


@unittest.skipUnless(
    os.environ.get("BK7258_QEMU_SOURCE") and os.environ.get("BK7258_QEMU_BUILD"),
    "set BK7258_QEMU_SOURCE and BK7258_QEMU_BUILD for production timer tests",
)
class SysTickModel(unittest.TestCase):
    def test_production_systick_and_upstream_ptimer(self):
        source = Path(os.environ["BK7258_QEMU_SOURCE"]).resolve()
        build = Path(os.environ["BK7258_QEMU_BUILD"]).resolve()
        flags = shlex.split(
            subprocess.check_output(
                ["pkg-config", "--cflags", "--libs", "glib-2.0"], text=True
            )
        )
        with tempfile.TemporaryDirectory() as directory:
            for test in (
                REPOSITORY / "tests/host/bk7258/qemu/systick_clock_test.c",
                source / "tests/unit/ptimer-test.c",
            ):
                binary = Path(directory) / test.stem
                subprocess.run(
                    [
                        os.environ.get("CC", "cc"),
                        "-std=gnu11",
                        "-O1",
                        "-g",
                        "-ffunction-sections",
                        "-fdata-sections",
                        "-D_GNU_SOURCE",
                        "-DCONFIG_SOFTMMU",
                        "-DCOMPILING_SYSTEM_VS_USER",
                        f'-DSYSTICK_SOURCE="{source}/hw/timer/armv7m_systick.c"',
                        f"-I{build}",
                        f"-I{source}",
                        f"-I{source}/include",
                        f"-I{source}/host/include/{platform.machine()}",
                        f"-I{source}/host/include/generic",
                        f"-I{source}/tests/unit",
                        f"-I{source}/hw/timer",
                        "-isystem",
                        str(source / "linux-headers"),
                        str(test),
                        str(source / "hw/core/ptimer.c"),
                        str(source / "tests/unit/ptimer-test-stubs.c"),
                        "-Wl,--gc-sections",
                        *flags,
                        "-o",
                        str(binary),
                    ],
                    check=True,
                    timeout=60,
                )
                subprocess.run([str(binary)], check=True, timeout=60)


if __name__ == "__main__":
    unittest.main()
