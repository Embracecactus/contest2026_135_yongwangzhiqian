#!/usr/bin/env python3
"""Verify the BK7258 CSV partition source, generators, and migration gates."""

from __future__ import annotations

import argparse
import json
import tempfile
from pathlib import Path

from gen_bk7258_partitions import (
    DEFAULT_INPUT,
    DEFAULT_OUTPUT_DIR,
    PartitionLayout,
    PartitionLayoutError,
    generated_contents,
    load_layout,
    sync_generated,
    verify_sdk_compatibility,
)


class VerificationError(RuntimeError):
    """Raised when the partition architecture does not fail closed."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise VerificationError(message)


def fixture(source: str, replacements: tuple[tuple[str, str], ...]) -> str:
    result = source
    for old, new in replacements:
        require(old in result, f"fixture source fragment is absent: {old!r}")
        result = result.replace(old, new, 1)
    return result


def load_fixture(root: Path, name: str, content: str) -> PartitionLayout:
    path = root / f"{name}.csv"
    path.write_text(content, encoding="utf-8")
    return load_layout(path)


def expect_rejected(root: Path, name: str, content: str) -> str:
    try:
        load_fixture(root, name, content)
    except PartitionLayoutError as error:
        return str(error)
    raise VerificationError(f"unsafe fixture was accepted: {name}")


def verify(sdk_source: Path | None = None) -> dict[str, object]:
    baseline = load_layout(DEFAULT_INPUT)
    stale = sync_generated(baseline, DEFAULT_OUTPUT_DIR, True)
    require(not stale, f"generated artifacts are stale: {stale!r}")
    outputs = generated_contents(baseline)
    require(
        baseline.layout_id in outputs["bk7258_partition_layout.h"],
        "generated C header omits layout_id",
    )
    require(
        "BK7258_ROLE_OTA_METADATA_MIRROR_OFFSET" in outputs[
            "bk7258_partition_layout.h"
        ],
        "generated C header omits the metadata mirror role",
    )
    require(
        "#define BK7258_ROLE_SLOT_A_AP_SDK_ID 2" in outputs[
            "bk7258_partition_layout.h"
        ]
        and "#define BK7258_SDK_PARTITIONS_TABLE_SIZE 13" in outputs[
            "bk7258_partition_layout.h"
        ],
        "generated C header omits the pinned SDK partition ABI",
    )

    source = DEFAULT_INPUT.read_text(encoding="utf-8")
    negative: dict[str, str] = {}
    with tempfile.TemporaryDirectory(prefix="bk7258-partition-tests-") as temp:
        root = Path(temp)
        dynamic = load_fixture(
            root,
            "dynamic",
            fixture(
                source,
                (
                    ("primary_cp_app,,1360K", "primary_cp_app,,1292K"),
                    ("s_app,,2516K", "s_app,,2448K"),
                ),
            ),
        )
        require(dynamic.layout_id != baseline.layout_id, "layout_id did not change")
        require(
            dynamic.by_role("slot_a_cp").size
            == baseline.by_role("slot_a_cp").size - 68 * 1024,
            "dynamic CP size did not propagate",
        )
        require(
            dynamic.by_role("slot_a_ap").offset
            == baseline.by_role("slot_a_ap").offset - 68 * 1024,
            "auto AP offset did not follow the resized CP partition",
        )
        require(
            dynamic.by_role("ota_metadata_primary").offset
            == baseline.by_role("ota_metadata_primary").offset - 136 * 1024,
            "downstream automatic offsets did not follow the resized A/B pair",
        )
        dynamic_header = generated_contents(dynamic)["bk7258_partition_layout.h"]
        require(
            f"0x{dynamic.by_role('slot_a_ap').offset:08x}"
            in dynamic_header,
            "dynamic AP offset did not propagate to the generated C ABI",
        )
        require(
            f"0x{dynamic.by_role('slot_b_pair').size:08x}"
            in dynamic_header,
            "dynamic slot-B size did not propagate to the generated C ABI",
        )
        require(
            "#define BK7258_ROLE_SLOT_A_AP_SDK_ID 2" in dynamic_header,
            "dynamic geometry changed a reserved SDK partition ID",
        )

        negative["pair-size"] = expect_rejected(
            root,
            "pair-size",
            fixture(source, (("s_app,,2516K", "s_app,,2448K"),)),
        )
        negative["erase-alignment"] = expect_rejected(
            root,
            "erase-alignment",
            fixture(source, (("littlefs,0x600000", "littlefs,0x600001"),)),
        )
        negative["overlap"] = expect_rejected(
            root,
            "overlap",
            fixture(source, (("littlefs,0x600000", "littlefs,0x500000"),)),
        )
        negative["missing-role"] = expect_rejected(
            root,
            "missing-role",
            fixture(
                source,
                ((",TRUE,TRUE,littlefs", ",TRUE,TRUE,littlefs_missing"),),
            ),
        )
        negative["tail-move"] = expect_rejected(
            root,
            "tail-move",
            fixture(source, (("easyflash,0x7fa000", "easyflash,0x7f9000"),)),
        )
        negative["metadata-size"] = expect_rejected(
            root,
            "metadata-size",
            fixture(
                source,
                (("ota_fina_mirror,,4K", "ota_fina_mirror,,8K"),),
            ),
        )

        sdk_result = None
        dynamic_sdk_result = None
        if sdk_source is not None:
            sdk_result = verify_sdk_compatibility(baseline, sdk_source)
            dynamic_sdk_result = verify_sdk_compatibility(dynamic, sdk_source)
            require(
                sdk_result["official_reference_geometry_match"] is True,
                "default project geometry no longer matches the official reference",
            )
            require(
                dynamic_sdk_result["official_reference_geometry_match"] is False,
                "dynamic fixture unexpectedly matches the official reference",
            )
            require(
                dynamic_sdk_result["project_csv_accepted_by_sdk_parser"] is True,
                "official SDK parser rejected the valid dynamic fixture",
            )
            require(
                dynamic_sdk_result["project_csv_accepted_by_sdk_generator"] is True,
                "official SDK header generator rejected the valid dynamic fixture",
            )
            require(
                dynamic_sdk_result["sdk_partition_ids"]
                == sdk_result["sdk_partition_ids"],
                "dynamic geometry changed the SDK partition ABI",
            )

    return {
        "format": 1,
        "status": "pass",
        "layout_id": baseline.layout_id,
        "layout_sha256": baseline.layout_sha256,
        "partition_count": len(baseline.partitions),
        "generated_artifacts": sorted(outputs),
        "dynamic_fixture": {
            "layout_id": dynamic.layout_id,
            "cp_size": dynamic.by_role("slot_a_cp").size,
            "ap_offset": dynamic.by_role("slot_a_ap").offset,
            "metadata_offset": dynamic.by_role("ota_metadata_primary").offset,
        },
        "negative_cases": negative,
        "official_sdk": sdk_result,
        "dynamic_sdk": dynamic_sdk_result,
        "writes_enabled": False,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk-source", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--json", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        result = verify(args.sdk_source)
    except (VerificationError, PartitionLayoutError, OSError, ValueError) as error:
        print(f"FAIL bk7258-partitions: {error}")
        return 1
    encoded = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded, encoding="utf-8")
    if args.json:
        print(encoded, end="")
    else:
        print(
            "PASS bk7258-partitions: "
            f"layout_id={result['layout_id']} partitions={result['partition_count']} "
            f"dynamic=accepted negative={len(result['negative_cases'])}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
