#!/usr/bin/env python3
"""Focused host-only checks for the schematic-only AIDK AI Toy binding."""

from __future__ import annotations

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


if __name__ == "__main__":
    unittest.main(verbosity=2)
