#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Two production-test processes share files, never globals or mocked state.
Process restart on POSIX is not a power-cut or target LittleFS durability test.
"""
import subprocess
import sys


def main():
    binary, phase, root, cert, golden = sys.argv[1:]
    assert phase in ("before-publish", "after-publish")
    writer, reader = (
        ("rename-unknown", "restart-old")
        if phase == "before-publish"
        else ("directory-sync-unknown", "restart-new")
    )
    for scenario in (writer, reader):
        result = subprocess.run(
            [binary, scenario, root, cert, golden], capture_output=True, text=True
        )
        if result.returncode or "CONTRACT_PASS" not in result.stdout:
            print(scenario, result.stdout, result.stderr)
            return 1
    print("CONTRACT_PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
