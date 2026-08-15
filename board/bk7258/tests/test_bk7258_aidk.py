#!/usr/bin/env python3
"""Focused host-only checks for the schematic-only AIDK AI Toy binding."""

from __future__ import annotations

import json
import sys
import unittest
from pathlib import Path


SCRIPT_ROOT = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPT_ROOT))

from bk7258_framework import (  # noqa: E402
    build_plan,
    load_catalog,
    load_json,
    validate_sdk_lock,
    validate_sdk_registry,
    validate_sdk_set,
)
from bk7258_resource_graph import (  # noqa: E402
    resolve_resource_graph,
    validate_resource_graph,
)


REPOSITORY = Path(__file__).resolve().parents[3]


class AidkBoardTest(unittest.TestCase):
    def test_catalog_and_sdk_lock_are_board_product_mode_specific(self) -> None:
        catalog = load_catalog(REPOSITORY)
        board = catalog["boards"]["aidk_ai_toy"]
        product = catalog["products"]["aidk_ai_toy_bringup"]
        self.assertEqual(board["bindings"]["console"], {
            "uart": "uart0", "baud": 115200,
            "flow_control": False, "rts_reset": False,
        })
        self.assertEqual(board["bindings"]["debug"], {
            "swd": False, "boot_hold": False, "rtt": False,
        })
        self.assertEqual(product["board"], "aidk_ai_toy")
        self.assertEqual(product["mode"], "bringup")
        registry_path = REPOSITORY / "board/bk7258/scripts/bk7258_sdk_registry.json"
        registry = load_json(registry_path)
        sdk_set_path = REPOSITORY / product["sdk_set"]
        sdk_lock_path = REPOSITORY / product["sdk_lock"]
        sdk_set = load_json(sdk_set_path)
        sdk_lock = load_json(sdk_lock_path)
        self.assertIs(validate_sdk_registry(REPOSITORY, registry), registry)
        self.assertIs(validate_sdk_set(sdk_set, registry), sdk_set)
        self.assertIs(validate_sdk_lock(
            REPOSITORY, registry_path, sdk_set_path, sdk_lock, registry, sdk_set), sdk_lock)

    def test_resource_graph_resolves_exactly_one_aidk_plan(self) -> None:
        graph_path = SCRIPT_ROOT / "bk7258_resource_graph_aidk_ai_toy.json"
        graph = load_json(graph_path)
        self.assertIs(validate_resource_graph(REPOSITORY, graph), graph)
        resolved = resolve_resource_graph(REPOSITORY, graph)
        self.assertTrue(resolved["resolved"])
        self.assertEqual(resolved["board_selection"]["candidates"], ["aidk_ai_toy"])
        self.assertEqual(resolved["board_constraints"]["port_identity"]["port"],
                         "dynamic-usb-serial")
        self.assertEqual(build_plan(REPOSITORY, "aidk_ai_toy_bringup")["board"]["id"],
                         "aidk_ai_toy")

    def test_board_layer_is_minimal_and_wired_without_unverified_devices(self) -> None:
        root = REPOSITORY / "board/bk7258"
        header = (root / "boards/aidk_ai_toy/include/bk7258_board_config.h").read_text()
        source = (root / "boards/aidk_ai_toy/src/bk7258_board_bringup.c").read_text()
        self.assertIn('#define BK7258_BOARD_HARDWARE_VERIFIED           0', header)
        self.assertIn('#define BK7258_BOARD_CONSOLE_UART_ID             0', header)
        self.assertIn('#define BK7258_BOARD_CONSOLE_BAUD                115200u', header)
        self.assertIn('#define BK7258_BOARD_CONSOLE_FLOW_CONTROL        0', header)
        self.assertIn('#define BK7258_BOARD_CONFLICT_P20_P21_SC7A20_SWD 1', header)
        self.assertIn('#define BK7258_BOARD_CONFLICT_P0_P1_MFRC522_CN1  1', header)
        self.assertIn('#define BK7258_BOARD_CONFLICT_P8_P9_32K_KEY3_MOTOR 1', header)
        self.assertIn('int bk7258_board_devices_initialize(void)', source)
        self.assertIn('return OK;', source)
        self.assertIn('config BK7258_BOARD_AIDK_AI_TOY',
                      (root / "Kconfig").read_text())
        self.assertIn('boards/aidk_ai_toy', (root / "CMakeLists.txt").read_text())
        self.assertIn('boards$(DELIM)aidk_ai_toy',
                      (root / "scripts/Make.defs").read_text())

    def test_board_selection_and_audio_binding_fail_closed(self) -> None:
        root = REPOSITORY / "board/bk7258"
        make_defs = (root / "scripts/Make.defs").read_text()
        cmake = (root / "CMakeLists.txt").read_text()
        chip_make = (root / "chip/Make.defs").read_text()
        chip_cmake = (root / "chip/CMakeLists.txt").read_text()
        board_make = (root / "src/Makefile").read_text()
        board_cmake = (root / "src/CMakeLists.txt").read_text()
        board_kconfig = (root / "Kconfig").read_text()
        chip_kconfig = (root / "chip/Kconfig").read_text()
        aidk_fragment = json.loads((SCRIPT_ROOT /
            "bk7258_fragment_catalog_board_aidk_ai_toy.json").read_text())
        core_fragment = json.loads((SCRIPT_ROOT /
            "bk7258_fragment_catalog_board_t5ai_core.json").read_text())

        self.assertIn("Select exactly one BK7258 physical board", make_defs)
        self.assertIn("Select exactly one BK7258 physical board", cmake)
        self.assertIn("$(words $(BK7258_BOARD_VARIANT_SELECTIONS)),1",
                      make_defs)
        self.assertIn("BK7258_BOARD_VARIANT_COUNT EQUAL 1", cmake)
        self.assertIn("else ifeq ($(CONFIG_BK7258_BOARD_T5AI_CORE),y)",
                      make_defs)
        self.assertIn("elseif(CONFIG_BK7258_BOARD_T5AI_CORE)", cmake)
        self.assertNotIn("boards$(DELIM)", chip_make)
        self.assertNotIn("/boards/", chip_cmake)
        self.assertNotIn("CONFIG_BK7258_BOARD_T5", chip_make)
        self.assertNotIn("CONFIG_BK7258_BOARD_T5", chip_cmake)
        self.assertIn("BK7258_AUD requires a physical-board audio binding",
                      board_make)
        self.assertIn("BK7258_AUD requires a physical-board audio binding",
                      board_cmake)
        self.assertIn("$(BK7258_BOARD_SDK_DIR)$(DELIM)include", board_make)
        self.assertIn("${BK7258_SDK_ROLE_DIR}/include", board_cmake)
        self.assertIn("CONFIG_FREERTOS=0", board_make)
        self.assertIn("CONFIG_FREERTOS=0", board_cmake)
        self.assertIn("config BK7258_BOARD_HAS_MIC_BINDING", board_kconfig)
        self.assertIn("config BK7258_BOARD_HAS_USER_GPIO_BINDING",
                      board_kconfig)
        self.assertIn("config BK7258_BOARD_HAS_SDIO_BINDING",
                      board_kconfig)
        self.assertIn("depends on BK7258_BOARD_HAS_AUDIO_BINDING",
                      chip_kconfig)
        self.assertIn("BK7258_BOARD_HAS_MIC_BINDING", chip_kconfig)
        self.assertIn("BK7258_BOARD_HAS_USER_GPIO_BINDING", chip_kconfig)
        self.assertIn("BK7258_BOARD_HAS_SDIO_BINDING", chip_kconfig)
        self.assertNotIn("BK7258_T5_BOARD", chip_kconfig)
        self.assertEqual(aidk_fragment["symbols"],
                         {"CONFIG_BK7258_BOARD_AIDK_AI_TOY": "y"})
        self.assertEqual(core_fragment["symbols"],
                         {"CONFIG_BK7258_BOARD_T5AI_CORE": "y"})

    def test_chip_sources_consume_only_typed_board_bindings(self) -> None:
        root = REPOSITORY / "board/bk7258"
        chip = root / "chip"

        for path in chip.rglob("*"):
            if path.suffix not in {".c", ".h"}:
                continue

            text = path.read_text(encoding="utf-8")
            self.assertNotIn("#include <arch/board/board.h>", text,
                             str(path))
            self.assertNotRegex(text,
                                r"\bBK7258_BOARD_(?!BINDING)[A-Z0-9_]+\b",
                                str(path))

        dvp = (chip / "ap/bk7258_dvp.c").read_text(encoding="utf-8")
        self.assertNotIn("GC2145", dvp)
        self.assertNotIn("0x78u >> 1", dvp)
        self.assertNotIn("0xf2u", dvp)

        board_make = (root / "src/Makefile").read_text(encoding="utf-8")
        board_cmake = (root / "src/CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("bk7258_platform.c bk7258_board_bringup.c", board_make)
        self.assertIn("${BK7258_BOARD_VARIANT_DIR}/src/bk7258_board_bringup.c",
                      board_cmake)


if __name__ == "__main__":
    unittest.main(verbosity=2)
