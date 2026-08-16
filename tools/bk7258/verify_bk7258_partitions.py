#!/usr/bin/env python3
"""Verify the BK7258 CSV partition source and generated consumers."""

from __future__ import annotations



import argparse
import json
import tempfile
from pathlib import Path

from bk7258_paths import load_board_script

gen_bk7258_partitions = load_board_script("gen_bk7258_partitions")
from gen_bk7258_partitions import (
    DEFAULT_HEADER,
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


def verify(
    sdk_source: Path | None = None,
    input_path: Path = DEFAULT_INPUT,
    expected_id: str | None = None,
    expected_sha256: str | None = None,
    output_dir: Path = DEFAULT_OUTPUT_DIR,
    header_path: Path = DEFAULT_HEADER,
) -> dict[str, object]:
    baseline = load_layout(input_path)
    require(
        (expected_id is None) == (expected_sha256 is None),
        "expected layout ID and SHA-256 must be supplied together",
    )
    if expected_id is not None:
        require(
            baseline.layout_id == expected_id
            and baseline.layout_sha256 == expected_sha256,
            "resolved partition layout identity mismatch",
        )
    stale = sync_generated(baseline, output_dir, True, header_path)
    require(not stale, f"generated artifacts are stale: {stale!r}")
    outputs = generated_contents(baseline)
    require(
        baseline.layout_id in outputs["bk7258_partition_layout.h"],
        "generated C header omits layout_id",
    )
    require(
        "BK7258_ROLE_BL1_PRIMARY_MANIFEST_OFFSET" in outputs[
            "bk7258_partition_layout.h"
        ],
        "generated C header omits the primary BL1 Manifest role",
    )
    require(
        all(
            role in outputs["bk7258_partition_layout.h"]
            for role in (
                "BK7258_ROLE_BL1_PRIMARY_MANIFEST_OFFSET",
                "BK7258_ROLE_BL1_SECONDARY_MANIFEST_OFFSET",
            )
        ),
        "generated C header omits a BL1 Manifest role",
    )
    require(
        "#define BK7258_ROLE_SLOT_A_AP_SDK_ID 2" in outputs[
            "bk7258_partition_layout.h"
        ]
        and "#define BK7258_ROLE_LITTLEFS_SDK_ID 13" in outputs[
            "bk7258_partition_layout.h"
        ]
        and "#define BK7258_SDK_PARTITIONS_TABLE_SIZE 14" in outputs[
            "bk7258_partition_layout.h"
        ],
        "generated C header omits the pinned SDK partition ABI",
    )

    source = input_path.read_text(encoding="utf-8")
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
            dynamic.by_role("slot_b_pair").end
            == baseline.by_role("slot_b_pair").end - 136 * 1024,
            "resized A/B pair end did not propagate",
        )
        require(
            dynamic.by_role("vendor_config").offset
            == baseline.by_role("vendor_config").offset,
            "fixed vendor partition moved with the application pair",
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
        negative["manifest-size"] = expect_rejected(
            root,
            "manifest-size",
            fixture(
                source,
                (("primary_manifest,0x50b000,4K", "primary_manifest,0x50b000,8K"),),
            ),
        )
        negative["manifest-writable"] = expect_rejected(
            root,
            "manifest-writable",
            fixture(
                source,
                (
                    (
                        "primary_manifest,0x50b000,4K,data,TRUE,FALSE",
                        "primary_manifest,0x50b000,4K,data,TRUE,TRUE",
                    ),
                ),
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
        "source": baseline.report()["source"],
        "layout_id": baseline.layout_id,
        "layout_sha256": baseline.layout_sha256,
        "partition_count": len(baseline.partitions),
        "generated_artifacts": sorted(outputs),
        "dynamic_fixture": {
            "layout_id": dynamic.layout_id,
            "cp_size": dynamic.by_role("slot_a_cp").size,
            "ap_offset": dynamic.by_role("slot_a_ap").offset,
            "secondary_pair_end": dynamic.by_role("slot_b_pair").end,
        },
        "negative_cases": negative,
        "official_sdk": sdk_result,
        "dynamic_sdk": dynamic_sdk_result,
        "writes_enabled": False,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=DEFAULT_INPUT)
    parser.add_argument("--expect-layout-id")
    parser.add_argument("--expect-layout-sha256")
    parser.add_argument("--sdk-source", type=Path)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR)
    parser.add_argument("--header", type=Path, default=DEFAULT_HEADER)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--json", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        result = verify(
            args.sdk_source,
            args.input,
            args.expect_layout_id,
            args.expect_layout_sha256,
            args.output_dir,
            args.header,
        )
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
