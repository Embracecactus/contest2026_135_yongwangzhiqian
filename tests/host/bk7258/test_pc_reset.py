#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Own temporary data for the real storage-worker reset executable."""
import subprocess
import sys
import tempfile
from pathlib import Path

if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="shaniu-pc-reset-") as root:
        result = subprocess.run(
            [Path(__file__).resolve().parent / "build/test_pc_reset", sys.argv[1], root]
        )
        raise SystemExit(result.returncode if result.returncode >= 0 else 1)
