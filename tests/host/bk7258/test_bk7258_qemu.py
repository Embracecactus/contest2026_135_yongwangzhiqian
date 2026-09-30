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
from unittest import mock
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
            ],
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


@unittest.skipUnless(
    os.environ.get("BK7258_QEMU")
    and os.environ.get("BK7258_PRODUCT_CP_ELF")
    and os.environ.get("BK7258_NM"),
    "set BK7258_PRODUCT_CP_ELF and BK7258_NM for the unchanged product stop probe",
)
class ProductCPStop(unittest.TestCase):
    def test_original_product_stops_at_unimplemented_flash_controller(self):
        elf = Path(os.environ["BK7258_PRODUCT_CP_ELF"]).resolve()
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
                    deadline = time.monotonic() + 5
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
            evidence = {
                "status": "blocked",
                "scope": "unchanged product CP direct ELF probe",
                "elf_sha256": hashlib.sha256(elf.read_bytes()).hexdigest(),
                "vector": f"0x{vectors[0]}",
                "first_fault": faults[0] if faults else None,
                "expected_missing_device": "Flash controller at 0x44030008",
            }
            (output / "product-stop.json").write_text(
                json.dumps(evidence, indent=2) + "\n"
            )
            self.assertTrue(faults, "product did not reach the known MMIO stop")
            self.assertEqual(faults[0], "0x44030008")
            self.assertNotIn(b"NuttShell (NSH)", uart.read_bytes())


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
