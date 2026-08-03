#!/usr/bin/env python3
"""Fail-closed verifier for the accepted BK7258 contiguous A/B migration."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from bk7258_ab_layout import LAYOUT_ID, LayoutError, report as layout_report


SCRIPT_DIR = Path(__file__).resolve().parent
BOARD_DIR = SCRIPT_DIR.parent


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require_fragments(path: Path, fragments: tuple[str, ...]) -> dict[str, str]:
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as error:
        raise LayoutError(f"cannot read {path}: {error}") from error
    missing = [fragment for fragment in fragments if fragment not in text]
    if missing:
        raise LayoutError(f"{path.name} layout drift; missing {missing!r}")
    return {"path": str(path), "sha256": sha256(path)}


def verify_team_contracts() -> dict[str, object]:
    contracts = {
        "amp_header": require_fragments(
            BOARD_DIR / "chip/include/bk7258_amp.h",
            (
                "#define BK7258_CP_FLASH_SIZE             0x00140000u",
                "#define BK7258_AP_FLASH_OFFSET           0x00150000u",
                "#define BK7258_AP_FLASH_SIZE             0x00110000u",
                "#define BK7258_AB_SECONDARY_START        0x00286000u",
                "#define BK7258_DATA_RAW_PHYSICAL_OFFSET  0x00600000u",
                "#define BK7258_CALIBRATION_TAIL_START    0x007fa000u",
            ),
        ),
        "cp_linker": require_fragments(
            SCRIPT_DIR / "ld.script",
            ("ORIGIN = 0x02010000, LENGTH = 0x140000",),
        ),
        "ap_linker": require_fragments(
            SCRIPT_DIR / "ld_ap.script",
            (
                "ORIGIN = 0x02150000, LENGTH = 0x110000",
                "AP __vector_core0_table must be at 0x02150000",
            ),
        ),
        "postbuild": require_fragments(
            SCRIPT_DIR / "postbuild.sh",
            (
                'MAX_SIZE="0x00140000"',
                'XIP_BASE="0x02150000"',
                'MAX_SIZE="0x00110000"',
                'PHYSICAL_OFFSET="0x00165000"',
            ),
        ),
        "debug_sop": require_fragments(
            SCRIPT_DIR / "bk7258_auto_debug.sh",
            (
                '"layout_id": "bk7258-v3.1.1.9-contiguous-ab-v1"',
                'verify_bk7258_factory_layout.py" --package "$DUAL_DIR"',
                "0x11000 + CP_IMAGE_SIZE <= 0x165000",
                "0x165000 + AP_IMAGE_SIZE <= 0x286000",
                "AP_IMAGE_WIN}@0x165000-${AP_LENGTH_HEX}",
            ),
        ),
        "build_wrapper": require_fragments(
            SCRIPT_DIR / "build_dual_image.sh",
            (
                'BK7258_SDK_BUNDLE_VERSION}" != "v3.1.1.9"',
                'verify_bk7258_factory_layout.py"',
                'bk7258-factory-layout.json"',
            ),
        ),
        "boot_table": require_fragments(
            BOARD_DIR / "bootloader/boot_main.c",
            (
                '"cp_app",     "beken_onchip_crc", 0x010000L, 0x140000L',
                '"ap_app",     "beken_onchip_crc", 0x150000L, 0x110000L',
            ),
        ),
        "mtd": require_fragments(
            BOARD_DIR / "chip/cp/bk7258_flash_mtd.c",
            ("BK7258_DATA_RAW_PHYSICAL_OFFSET", "BK7258_DATA_RAW_PHYSICAL_SIZE"),
        ),
        "packer": require_fragments(
            SCRIPT_DIR / "pack_dual_image.py",
            ("LAYOUT_ID", "PAIR_B_START", "CALIBRATION_TAIL_START"),
        ),
        "factory_verifier": require_fragments(
            SCRIPT_DIR / "verify_bk7258_factory_layout.py",
            (
                "factory prefix must end before usr_config at 0x4fc000",
                "B seed is not a byte-exact A pair copy",
                "factory image contains unexpected bytes",
                '"included_in_image": False',
                '"included_in_images": False',
            ),
        ),
    }
    return {"layout_id": LAYOUT_ID, "files": contracts}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk-source", type=Path)
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        result = layout_report(args.sdk_source)
        result["team_contracts"] = verify_team_contracts()
    except (LayoutError, OSError, ValueError) as error:
        print(f"BK7258 accepted A/B layout verification FAIL: {error}")
        return 1
    encoded = json.dumps(result, indent=2, sort_keys=True)
    if args.output is not None:
        args.output.write_text(encoded + "\n", encoding="utf-8")
    if args.json:
        print(encoded)
    else:
        print(
            "BK7258 accepted A/B layout verification PASS: "
            f"layout_id={LAYOUT_ID} writes_enabled=false"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
