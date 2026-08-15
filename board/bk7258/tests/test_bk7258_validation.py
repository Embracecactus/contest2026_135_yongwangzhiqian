#!/usr/bin/env python3
"""Focused host checks for the BK7258 validation descriptor skeleton."""

from __future__ import annotations

import copy
import sys
import unittest
from pathlib import Path


SCRIPT_ROOT = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPT_ROOT))

from bk7258_framework import FrameworkError, canonical_json, load_json  # noqa: E402
from bk7258_validation import (  # noqa: E402
    OUTCOME_SCHEMA,
    validate_descriptor_set,
    validation_outcome,
)


REPOSITORY = Path(__file__).resolve().parents[3]


class ValidationDescriptorTest(unittest.TestCase):
    def setUp(self) -> None:
        self.descriptors = load_json(SCRIPT_ROOT / "bk7258_validation_descriptors.json")

    def test_descriptor_set_and_27_profile_mapping_are_valid(self) -> None:
        result = validate_descriptor_set(REPOSITORY, self.descriptors)
        self.assertEqual(result["descriptors"], 5)
        self.assertEqual(result["legacy"]["profiles"], 27)
        self.assertEqual(result["legacy"]["migration_state"], "migration_pending")
        self.assertEqual(self.descriptors["migration_policy"]["production_auto_start"],
                         "migration_pending")
        self.assertEqual(self.descriptors["serialization"]["claim_policy"], "exclusive")

    def test_descriptor_rejects_chip_board_entrypoint_and_vendor_run(self) -> None:
        broken = copy.deepcopy(self.descriptors)
        broken["descriptors"][0]["entrypoint"] = "board/bk7258/chip/fake.c"
        with self.assertRaises(FrameworkError):
            validate_descriptor_set(REPOSITORY, broken)

        broken = copy.deepcopy(self.descriptors)
        broken["descriptors"][0]["run"] = "vendor_sdk:call"
        with self.assertRaises(FrameworkError):
            validate_descriptor_set(REPOSITORY, broken)

    def test_outcome_is_stable_and_category_skip_is_explicit(self) -> None:
        descriptor = self.descriptors["descriptors"][2]
        outcome = validation_outcome(descriptor, "SKIP",
                                     "category_not_all_compatible")
        self.assertEqual(outcome["schema"], OUTCOME_SCHEMA)
        self.assertEqual(outcome["status"], "SKIP")
        self.assertEqual(canonical_json(outcome), canonical_json(outcome))
        self.assertEqual(outcome["resource_claims"], ["board_gpio"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
