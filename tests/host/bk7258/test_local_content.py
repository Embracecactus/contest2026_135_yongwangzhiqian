#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Existing contract runner adapter; only the ROMFS file boundary is copied."""
from pathlib import Path
import shutil
import subprocess
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]


class LocalContentTest(unittest.TestCase):
    def test_production_pipeline(self):
        fixture = HERE / "build/local-content.pcm"
        try:
            for case in (
                "success",
                "card",
                "cancel-pending",
                "cancel-active",
                "cancel-close-error",
                "drain-failure",
                "corrupt",
                "missing",
            ):
                with self.subTest(case=case):
                    shutil.copyfile(
                        ROOT / "app/bk7258/assets/local_rhythm.pcm", fixture
                    )
                    if case == "corrupt":
                        with fixture.open("r+b") as stream:
                            stream.write(b"\xff\xff")
                    if case == "missing":
                        fixture.unlink()
                    subprocess.run(
                        [HERE / "build/test_local_content", case], check=True
                    )
        finally:
            fixture.unlink(missing_ok=True)


if __name__ == "__main__":
    unittest.main()
