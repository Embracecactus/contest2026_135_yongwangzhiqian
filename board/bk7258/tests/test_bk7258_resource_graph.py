#!/usr/bin/env python3
"""Focused host checks for BK7258 ownership and resource metadata."""

from __future__ import annotations

import copy
import sys
import unittest
from pathlib import Path


SCRIPT_ROOT = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPT_ROOT))

from bk7258_framework import FrameworkError, load_json  # noqa: E402
from bk7258_resource_graph import (  # noqa: E402
    resolve_resource_graph,
    validate_migration_ledger,
    validate_ownership_manifest,
    validate_resource_graph,
)


REPOSITORY = Path(__file__).resolve().parents[3]


class ResourceGraphTest(unittest.TestCase):
    def setUp(self) -> None:
        self.ownership = load_json(SCRIPT_ROOT / "bk7258_layer_ownership.json")
        self.ledger = load_json(SCRIPT_ROOT / "bk7258_compatibility_migration_ledger.json")
        self.graph = load_json(SCRIPT_ROOT / "bk7258_resource_graph_t5ai_core.json")

    def test_valid_ownership_ledger_and_paired_graph_resolve(self) -> None:
        self.assertIs(validate_ownership_manifest(REPOSITORY, self.ownership), self.ownership)
        self.assertIs(validate_migration_ledger(REPOSITORY, self.ledger), self.ledger)
        self.assertEqual(list(self.ownership["layers"]), ["architecture", "chip", "board"])
        self.assertEqual(self.ownership["layers"]["architecture"]["source"], "upstream_nuttx")
        self.assertEqual(self.ownership["responsibility_tags"]["vendor_common_glue"]["status"], "migration_pending")
        self.assertEqual(
            [row["name"] for row in self.ownership["init_phases"]],
            ["board_early_initialize", "board_late_initialize",
             "board_app_initialize", "board_app_finalinitialize"],
        )
        self.assertIn(".bkpack", {row["name"] for row in self.ownership["build_contract"]["artifacts"]})
        artifact_owners = {
            row["name"]: row["owner"]
            for row in self.ownership["build_contract"]["artifacts"]
        }
        self.assertEqual(artifact_owners["libboards.a"], "architecture")
        self.assertEqual(artifact_owners["libboard.a"], "board")
        artifact_status = {
            row["name"]: row["status"]
            for row in self.ownership["build_contract"]["artifacts"]
        }
        self.assertEqual(artifact_status["libboards.a"],
                         "classic_backend_internal")
        self.assertEqual(artifact_status["libboard.a"], "required")
        self.assertEqual(self.ledger["rows"][0]["current_owner"], "vendor_common_glue")
        self.assertIs(validate_resource_graph(REPOSITORY, self.graph), self.graph)
        resolved = resolve_resource_graph(REPOSITORY, self.graph)
        self.assertTrue(resolved["resolved"])
        self.assertEqual(resolved["schema"], "bk7258.resolved-resource-graph/1")
        self.assertEqual(resolved["kind"], "resolved-resource-graph")
        self.assertEqual(set(resolved["roles"]), {"bl1", "bl2", "cp", "ap"})
        self.assertEqual(resolved["phases"], ["download", "boot", "hold", "runtime", "suspend", "restart"])

    def test_partition_and_mtd_composition_are_board_owned(self) -> None:
        board_root = REPOSITORY / "board/bk7258"
        board_kconfig = (board_root / "Kconfig").read_text(encoding="utf-8")
        chip_kconfig = (board_root / "chip/Kconfig").read_text(encoding="utf-8")
        board_make = (board_root / "src/Makefile").read_text(encoding="utf-8")
        board_cmake = (board_root / "src/CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        chip_make = (board_root / "chip/Make.defs").read_text(encoding="utf-8")
        chip_cmake = (board_root / "chip/CMakeLists.txt").read_text(
            encoding="utf-8"
        )

        # MTD/LittleFS are product data-partition composition choices.  Keep
        # the public symbol names and dependency closure for old defconfigs,
        # but define them in the existing logical-board Kconfig rather than
        # the SoC mechanism Kconfig.  RPMsgFS remains chip-owned transport.
        for symbol in ("BK7258_FLASH_MTD", "BK7258_FLASH_LITTLEFS"):
            self.assertIn(f"config {symbol}", board_kconfig)
            self.assertNotIn(f"config {symbol}", chip_kconfig)
        self.assertIn(
            "depends on ARCH_BOARD_BK7258 && ARCH_CHIP_BK7258 && !BK7258_AP_CORE",
            board_kconfig,
        )
        self.assertIn("select MTD", board_kconfig)
        self.assertIn("depends on BK7258_FLASH_MTD", board_kconfig)
        self.assertIn("select FS_LITTLEFS", board_kconfig)
        self.assertNotIn("select MTD if BK7258_FLASH_MTD", chip_kconfig)
        self.assertIn("config BK7258_RPMSGFS", chip_kconfig)

        kconfig_rows = [
            row for row in self.ledger["rows"]
            if row["path"] == "board/bk7258/chip/Kconfig"
        ]
        self.assertEqual(len(kconfig_rows), 1)
        self.assertEqual(kconfig_rows[0]["current_owner"], "chip")
        self.assertEqual(kconfig_rows[0]["target_owner"], "board")
        self.assertEqual(
            set(kconfig_rows[0]["symbols"]),
            {"CONFIG_BK7258_FLASH_MTD", "CONFIG_BK7258_FLASH_LITTLEFS"},
        )

        sources = (
            "bk7258_sdk_partition.c",
            "bk7258_flash_mtd.c",
            "bk7258_flash_guard.c",
        )
        for name in sources:
            self.assertTrue((board_root / "src" / name).is_file(), name)
            self.assertFalse((board_root / "chip/cp" / name).exists(), name)
            self.assertIn(name, board_make)
            self.assertIn(name, board_cmake)
            self.assertNotIn(name, chip_make)
            self.assertNotIn(name, chip_cmake)

        partition_symbols = (
            "bk_flash_partition_get_info",
            "bk_flash_partition_read",
            "bk_flash_partition_write",
            "bk_flash_partition_erase",
            "bk_flash_partition_write_perm_check_by_addr",
        )
        board_root_make = (board_root / "scripts/Make.defs").read_text(
            encoding="utf-8"
        )
        board_root_cmake = (board_root / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        for symbol in partition_symbols:
            self.assertIn(symbol, board_root_make)
            self.assertIn(symbol, board_root_cmake)
            self.assertNotIn(symbol, chip_make)
            self.assertNotIn(symbol, chip_cmake)

        for name in ("bk7258_flash_mtd.h", "bk7258_flash_guard.h"):
            self.assertTrue((board_root / "src" / name).is_file(), name)
            self.assertFalse((board_root / "chip/cp" / name).exists(), name)

        chip_sources = list((board_root / "chip").rglob("*.c"))
        chip_sources.extend((board_root / "chip").rglob("*.h"))
        for path in chip_sources:
            text = path.read_text(encoding="utf-8")
            self.assertNotIn("bk7258_partition_layout.h", text, str(path))
            self.assertNotIn("<arch/board/", text, str(path))

        image_layout_consumers = {
            path.relative_to(REPOSITORY).as_posix()
            for path in chip_sources
            if "arch/board/bk7258_image_layout.h" in
            path.read_text(encoding="utf-8")
        }
        resolved_image_layout_debt = {
            "board/bk7258/chip/cp/bk7258_start.c",
            "board/bk7258/chip/cp/bk7258_ap_control.c",
            "board/bk7258/chip/cp/bk7258_bt_controller.c",
            "board/bk7258/chip/ap/bk7258_ap_start.c",
            "board/bk7258/chip/ap/bk7258_ap_main.c",
        }
        self.assertEqual(image_layout_consumers, set())
        ledger_paths = {row["path"] for row in self.ledger["rows"]}
        self.assertTrue(resolved_image_layout_debt <= ledger_paths)
        self.assertIn("board/bk7258/src/bk7258_mac_storage.c", ledger_paths)
        self.assertIn("bk7258_mac_storage.c", board_make)
        self.assertIn("bk7258_mac_storage.c", board_cmake)

        board_paths = self.ownership["layers"]["board"]["current_paths"]
        chip_paths = self.ownership["layers"]["chip"]["current_paths"]
        self.assertIn("board/bk7258/include", board_paths)
        self.assertIn("board/bk7258/partitions", board_paths)
        self.assertIn("board/bk7258/Kconfig", board_paths)
        self.assertNotIn("board/bk7258/include", chip_paths)

    def test_exact_one_board_and_sdk_singleton_policy_fail_closed(self) -> None:
        ambiguous = copy.deepcopy(self.graph)
        ambiguous["board_selection"]["candidates"] = ["t5ai_core", "t5_board"]
        with self.assertRaises(FrameworkError):
            validate_resource_graph(REPOSITORY, ambiguous)

        shared = copy.deepcopy(self.graph)
        shared["resources"]["sdk_singletons"][0]["max_instances"] = 2
        with self.assertRaises(FrameworkError):
            validate_resource_graph(REPOSITORY, shared)

    def test_representative_pin_and_devpath_conflicts_are_rejected(self) -> None:
        pin_conflict = copy.deepcopy(self.graph)
        duplicate_pin = copy.deepcopy(pin_conflict["resources"]["pins_functions"][0])
        duplicate_pin["id"] = "duplicate_console_pin"
        pin_conflict["resources"]["pins_functions"].append(duplicate_pin)
        with self.assertRaises(FrameworkError):
            validate_resource_graph(REPOSITORY, pin_conflict)

        devpath_conflict = copy.deepcopy(self.graph)
        devpath_conflict["resources"]["devpaths_minors"][1]["minor"] = 0
        with self.assertRaises(FrameworkError):
            validate_resource_graph(REPOSITORY, devpath_conflict)

    def test_board_transport_routes_do_not_conflate_console_and_loader(self) -> None:
        t5_board = load_json(SCRIPT_ROOT / "bk7258_resource_graph_t5_board.json")
        t5ai = load_json(SCRIPT_ROOT / "bk7258_resource_graph_t5ai_core.json")
        self.assertIs(validate_resource_graph(REPOSITORY, t5_board), t5_board)
        self.assertIs(validate_resource_graph(REPOSITORY, t5ai), t5ai)

        board_pins = {
            row["id"]: row for row in t5_board["resources"]["pins_functions"]
        }
        self.assertIn("loader_uart0_bkfil", board_pins)
        self.assertIn("swd_p0_p1_cp", board_pins)
        self.assertNotIn("console_uart0", board_pins)
        self.assertEqual(board_pins["loader_uart0_bkfil"]["function"],
                         "uart0_bkfil_loader")
        self.assertEqual(board_pins["swd_p0_p1_cp"]["pins"], ["p0", "p1"])

        t5ai_pins = t5ai["resources"]["pins_functions"]
        self.assertTrue(any(row["id"] == "console_uart1" and
                            row["function"] == "uart1_console"
                            for row in t5ai_pins))
        self.assertTrue(any(row["id"] == "uart1_irq" and
                            row["resource"] == "uart1"
                            for row in t5ai["resources"]["irq_dma_clock_power"]))


if __name__ == "__main__":
    unittest.main(verbosity=2)
