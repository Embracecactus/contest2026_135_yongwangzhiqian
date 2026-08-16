#!/usr/bin/env python3
"""Focused checks for the BK7258 vector-table linker entry contract."""

from __future__ import annotations

import shutil
import subprocess
import unittest
from pathlib import Path


BOARD = Path(__file__).resolve().parents[1]
REPOSITORY = BOARD.parents[2]
SCRIPTS = BOARD / "scripts"


class LinkerEntryTests(unittest.TestCase):
    def test_cp_and_ap_scripts_select_the_vector_table(self) -> None:
        for name in ("ld.script", "ld_ap.script"):
            source = (SCRIPTS / name).read_text(encoding="utf-8")
            self.assertEqual(source.count("ENTRY(_vectors)"), 1, name)
            self.assertNotIn("ENTRY(__start)", source, name)
            self.assertIn("EXTERN(_vectors)", source, name)

    def test_classic_and_cmake_link_options_match_vector_entry(self) -> None:
        for path in (BOARD / "chip/Make.defs", BOARD / "chip/CMakeLists.txt"):
            source = path.read_text(encoding="utf-8")
            self.assertEqual(source.count("entry=_vectors"), 1, str(path))
            self.assertNotIn("entry=__start", source, str(path))

    def test_preprocessed_cp_and_ap_scripts_retain_vector_entry(self) -> None:
        compiler = shutil.which("arm-none-eabi-gcc")
        config_includes = sorted(
            path.parent.parent
            for path in (REPOSITORY / "cmake_out").glob("*/include/nuttx/config.h")
        )
        if compiler is None or not config_includes:
            self.skipTest("BK7258 cross compiler/configured include tree unavailable")

        include_dirs = [
            config_includes[0],
            REPOSITORY / "nuttx/include",
            BOARD / "chip",
            BOARD / "include",
        ]
        for name in ("ld.script", "ld_ap.script"):
            command = [compiler, "-E", "-P", "-x", "c"]
            for path in include_dirs:
                command.extend(("-I", str(path)))
            command.append(str(SCRIPTS / name))
            result = subprocess.run(
                command,
                check=True,
                capture_output=True,
                text=True,
            )
            self.assertIn("ENTRY(_vectors)", result.stdout, name)
            self.assertNotIn("ENTRY(__start)", result.stdout, name)


if __name__ == "__main__":
    unittest.main(verbosity=2)
