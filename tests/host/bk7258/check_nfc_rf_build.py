#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check actual target artifacts: RF driver and production service coexist."""
import argparse
import json
import subprocess
from pathlib import Path


def check(config, elf, link_map, nm):
    values = dict(
        line.split("=", 1)
        for line in config.read_text().splitlines()
        if line.startswith("CONFIG_") and "=" in line
    )
    for name in ("CL_MFRC522_RF", "BK7258_NFC_SERVICE", "BK7258_AIDK_MFRC522"):
        assert values.get("CONFIG_" + name) == "y", name + " missing"
    for name in ("CL_MFRC522", "CL_MFRC522_FRAME", "CL_ISODEP"):
        assert values.get("CONFIG_" + name) != "y", name + " unexpectedly enabled"
    symbols = subprocess.check_output([nm, str(elf)], text=True)
    for name in ("mfrc522_register", "bk7258_nfc_service_start"):
        matches = [line for line in symbols.splitlines() if line.split()[-1] == name]
        assert len(matches) == 1, name + " missing or duplicated"
    assert "mfrc522_rf.c.o" in link_map.read_text(), "controlled driver not linked"
    return {
        "status": "PASS",
        "layer": "target-build",
        "config": str(config),
        "elf": str(elf),
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--map", dest="link_map", type=Path, required=True)
    parser.add_argument("--nm", default="arm-none-eabi-nm")
    args = parser.parse_args()
    print(json.dumps(check(**vars(args)), indent=2))
