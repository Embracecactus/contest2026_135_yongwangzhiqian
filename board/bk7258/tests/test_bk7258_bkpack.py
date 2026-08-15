#!/usr/bin/env python3
"""Focused host checks for the metadata-only BK7258 package boundary."""

from __future__ import annotations

import copy
import sys
import unittest
from pathlib import Path


SCRIPT_ROOT = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPT_ROOT))

from bk7258_framework import (  # noqa: E402
    FrameworkError,
    load_json,
    pack_prepare,
    pack_verify,
    validate_bkpack,
)


REPOSITORY = Path(__file__).resolve().parents[3]


class BkpackTest(unittest.TestCase):
    def test_schema_and_application_package_are_strict_metadata(self) -> None:
        schema = load_json(SCRIPT_ROOT / "bk7258_bkpack_schema.json")
        self.assertEqual(schema["schema"], "bk7258.bkpack/1")
        self.assertEqual(schema["strict"]["apps_plan"],
                         "exactly-one-named-plan-per-package")
        package = pack_prepare(REPOSITORY, "t5ai_core_bringup")
        self.assertIs(validate_bkpack(package), package)
        self.assertFalse(package["hardware_verified"])
        self.assertIsNone(package["signed_digest"])
        self.assertEqual(len(package["apps_plan"]["roles"]), 2)

    def test_factory_has_separate_kind_and_plan(self) -> None:
        package = pack_prepare(REPOSITORY, "t5ai_core_bringup", kind="factory")
        self.assertEqual(package["kind"], "factory")
        self.assertEqual(package["plan"]["kind"], "factory")
        self.assertEqual(package["apps_plan"]["kind"], "named-apps-plan")
        self.assertIs(pack_verify(None, package)["hardware_verified"], False)

    def test_ranges_reject_overlap_outside_and_ambiguous_roles(self) -> None:
        package = pack_prepare(REPOSITORY, "t5ai_core_bringup")
        overlap = copy.deepcopy(package)
        overlap["ranges"][1]["start"] = overlap["ranges"][0]["start"]
        with self.assertRaises(FrameworkError):
            validate_bkpack(overlap)
        outside = copy.deepcopy(package)
        outside["ranges"][0]["end"] = outside["partition"]["flash_size"] + 1
        with self.assertRaises(FrameworkError):
            validate_bkpack(outside)
        ambiguous = copy.deepcopy(package)
        ambiguous["ranges"][1]["role"] = ambiguous["ranges"][0]["role"]
        with self.assertRaises(FrameworkError):
            validate_bkpack(ambiguous)

    def test_extra_artifact_and_signed_digest_are_rejected(self) -> None:
        package = pack_prepare(REPOSITORY, "t5ai_core_bringup")
        extra = copy.deepcopy(package)
        extra["artifacts"][0]["name"] = "surprise.bin"
        with self.assertRaises(FrameworkError):
            validate_bkpack(extra)
        signed = copy.deepcopy(package)
        signed["signed_digest"] = "0" * 64
        with self.assertRaises(FrameworkError):
            validate_bkpack(signed)


if __name__ == "__main__":
    unittest.main(verbosity=2)
