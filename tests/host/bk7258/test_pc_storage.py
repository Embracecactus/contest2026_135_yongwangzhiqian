#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Temporary-data owner for the production PC storage worker tests."""
import subprocess
import sys
import tempfile
from pathlib import Path

if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="shaniu-pc-storage-") as root:
        result = subprocess.run(
            [
                Path(__file__).resolve().parent / "build/test_pc_storage",
                sys.argv[1],
                root,
            ],
            timeout=30,
        )
        raise SystemExit(result.returncode if result.returncode >= 0 else 1)
