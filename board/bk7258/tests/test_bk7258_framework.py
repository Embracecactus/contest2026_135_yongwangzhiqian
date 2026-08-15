#!/usr/bin/env python3
"""Fast schema and resolver checks for the canonical BK7258 script path."""

from __future__ import annotations

import copy
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT_ROOT = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPT_ROOT))

from bk7258_framework import (  # noqa: E402
    FrameworkError,
    canonical_json,
    classic_report,
    load_catalog,
    load_json,
    merge_symbols,
    resolve,
    validate_board,
    validate_ir,
)


REPOSITORY = Path(__file__).resolve().parents[3]


class FrameworkTest(unittest.TestCase):
    def test_catalog_and_roles_resolve_deterministically(self) -> None:
        catalog = load_catalog(REPOSITORY)
        self.assertEqual(set(catalog["boards"]), {"t5ai_core", "t5_board"})
        cp = resolve(REPOSITORY, "t5ai_core_bringup", "cp")
        ap = resolve(REPOSITORY, "t5ai_core_bringup", "ap")
        self.assertIsNone(cp["symbols"]["CONFIG_BK7258_AP_CORE"])
        self.assertEqual(ap["symbols"]["CONFIG_BK7258_AP_CORE"], "y")
        self.assertEqual(cp, resolve(REPOSITORY, "t5ai_core_bringup", "cp"))
        self.assertIs(validate_ir(cp), cp)

    def test_exact_board_mode_and_symbol_conflicts_fail_closed(self) -> None:
        with self.assertRaises(FrameworkError):
            resolve(REPOSITORY, "t5ai_core_bringup", "cp", board_id="t5_board")
        with self.assertRaises(FrameworkError):
            resolve(REPOSITORY, "t5ai_core_bringup", "cp", mode="application")
        with self.assertRaises(FrameworkError):
            resolve(REPOSITORY, "unknown", "cp")
        with self.assertRaises(FrameworkError):
            merge_symbols([{"symbols": {"CONFIG_X": "y"}}, {"symbols": {"CONFIG_X": None}}])

    def test_strict_duplicate_and_ir_identity_checks(self) -> None:
        with tempfile.TemporaryDirectory(prefix="bk7258-framework-") as directory:
            duplicate = Path(directory) / "duplicate.json"
            duplicate.write_text('{"a":1,"a":2}\n', encoding="utf-8")
            with self.assertRaises(FrameworkError):
                load_json(duplicate)
        board = copy.deepcopy(load_catalog(REPOSITORY)["boards"]["t5ai_core"])
        board["bindings"]["console"]["rts_reset"] = True
        with self.assertRaises(FrameworkError):
            validate_board(board)
        ir = resolve(REPOSITORY, "t5ai_core_bringup", "cp")
        broken = copy.deepcopy(ir)
        broken["identity_sha256"] = "0" * 64
        with self.assertRaises(FrameworkError):
            validate_ir(broken)

    def test_classic_report_is_explicit_adapter_boundary(self) -> None:
        report = classic_report(REPOSITORY)
        self.assertEqual(report["status"], "feasible-with-adapter")
        self.assertTrue(report["repository_relative_source_view"])
        self.assertNotIn(str(REPOSITORY), canonical_json(report).decode())


if __name__ == "__main__":
    unittest.main(verbosity=2)
