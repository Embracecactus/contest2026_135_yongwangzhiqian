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
        self.assertEqual(self.ledger["rows"][0]["current_owner"], "vendor_common_glue")
        self.assertIs(validate_resource_graph(REPOSITORY, self.graph), self.graph)
        resolved = resolve_resource_graph(REPOSITORY, self.graph)
        self.assertTrue(resolved["resolved"])
        self.assertEqual(resolved["schema"], "bk7258.resolved-resource-graph/1")
        self.assertEqual(resolved["kind"], "resolved-resource-graph")
        self.assertEqual(set(resolved["roles"]), {"bl1", "bl2", "cp", "ap"})
        self.assertEqual(resolved["phases"], ["download", "boot", "hold", "runtime", "suspend", "restart"])

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


if __name__ == "__main__":
    unittest.main(verbosity=2)
