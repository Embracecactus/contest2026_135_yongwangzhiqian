#!/usr/bin/env python3
"""Focused metadata and resource-graph checks for T5-Board bring-up."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path


SCRIPT_ROOT = Path(__file__).resolve().parents[3] / "tools" / "bk7258"
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


class T5BoardProductTest(unittest.TestCase):
    def test_product_is_conservative_mcuboot_pair_with_locked_layout(self) -> None:
        catalog = load_catalog(REPOSITORY)
        product = catalog["products"]["t5_board_bringup"]

        self.assertEqual(product["board"], "t5_board")
        self.assertEqual(product["family"], "t5_board")
        self.assertEqual(product["mode"], "bringup")
        self.assertEqual(product["boot"], "mcuboot")
        self.assertEqual(product["features"], ["mcuboot-ab"])
        self.assertEqual(
            product["partition_layout"],
            {
                "layout_id": "bk7258-v3119-ab-124ebfab37ca1fcd",
                "layout_sha256": (
                    "124ebfab37ca1fcd9971c5aba7b9f214f0500df74cdc394c88ec602020732d8a"
                ),
                "source": "board/bk7258/partitions/bk7258/auto_partitions.csv",
            },
        )
        self.assertEqual(
            set(product["roles"]["cp"]), {"fragments", "legacy_profile"}
        )
        self.assertEqual(
            set(product["roles"]["ap"]), {"fragments", "legacy_profile"}
        )
        self.assertNotIn("seed_profile", product["roles"]["bl2"])
        self.assertEqual(product["roles"]["bl2"]["legacy_profile"], "bl2_mcuboot")

        registry_path = SCRIPT_ROOT / "bk7258_sdk_registry.json"
        registry = load_json(registry_path)
        sdk_set_path = REPOSITORY / product["sdk_set"]
        sdk_lock_path = REPOSITORY / product["sdk_lock"]
        sdk_set = load_json(sdk_set_path)
        sdk_lock = load_json(sdk_lock_path)
        self.assertIs(validate_sdk_registry(REPOSITORY, registry), registry)
        self.assertIs(validate_sdk_set(sdk_set, registry), sdk_set)
        self.assertIs(
            validate_sdk_lock(
                REPOSITORY,
                registry_path,
                sdk_set_path,
                sdk_lock,
                registry,
                sdk_set,
            ),
            sdk_lock,
        )

    def test_resource_graph_resolves_only_the_verified_bringup_contract(self) -> None:
        graph = load_json(SCRIPT_ROOT / "bk7258_resource_graph_t5_board.json")
        self.assertIs(validate_resource_graph(REPOSITORY, graph), graph)
        resolved = resolve_resource_graph(REPOSITORY, graph)

        self.assertTrue(resolved["resolved"])
        self.assertEqual(resolved["board_selection"]["candidates"], ["t5_board"])
        self.assertEqual(set(resolved["roles"]), {"bl1", "bl2", "cp", "ap"})
        self.assertEqual(
            {row["id"] for row in graph["resources"]["pins_functions"]},
            {"loader_uart0_bkfil", "swd_p0_p1_cp"},
        )
        pins = {row["id"]: row for row in graph["resources"]["pins_functions"]}
        self.assertEqual(pins["loader_uart0_bkfil"]["function"],
                         "uart0_bkfil_loader")
        self.assertEqual(pins["swd_p0_p1_cp"]["pins"], ["p0", "p1"])
        resource_rows = [
            row
            for category, rows in graph["resources"].items()
            if category != "memory_psram"
            for row in rows
        ]
        resource_text = str(resource_rows).lower()
        self.assertFalse(
            any(token in resource_text for token in ("sdio", "camera", "lcd", "speaker"))
        )
        self.assertNotIn("psram", {row["kind"] for row in graph["resources"]["memory_psram"]})
        self.assertEqual(build_plan(REPOSITORY, "t5_board_bringup")["board"]["id"], "t5_board")


if __name__ == "__main__":
    unittest.main(verbosity=2)
