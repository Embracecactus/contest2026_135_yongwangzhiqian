#!/usr/bin/env python3
"""Verify and describe the fixed N15-F PSRAM/J-Link transfer package."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from bk7258_ab_layout import ERASE_SIZE, PAIR_B_SIZE
from pack_bk7258_ota_metadata import (
    BOOT_METADATA_DESCRIPTOR_OFFSET,
    BOOT_METADATA_RECORD_SIZE,
)
from pack_bk7258_ota_stage import STAGE_DESCRIPTOR_SIZE


CANDIDATE_FILE = "s_app-candidate.bin"
DESCRIPTOR_FILE = "bk7258-ota-stage.bin"
RECORD_FILE = "bk7258-ota-pending-record.bin"
METADATA_FILE = "bk7258-ota-metadata.bin"
PAIR_REPORT_FILE = "bk7258-ota-pair.json"
METADATA_REPORT_FILE = "bk7258-ota-metadata.json"
JLINK_FILE = "bk7258-ota-psram.jlink"
TRANSFER_REPORT_FILE = "bk7258-ota-transfer.json"

EXPECTED_ABI = {
    "BK7258_OTA_TRANSFER_CANDIDATE_ADDRESS": 0x60800000,
    "BK7258_OTA_TRANSFER_CANDIDATE_SIZE": PAIR_B_SIZE,
    "BK7258_OTA_TRANSFER_DESCRIPTOR_ADDRESS": 0x60800000 + PAIR_B_SIZE,
    "BK7258_OTA_TRANSFER_RECORD_ADDRESS": 0x60800000 + PAIR_B_SIZE + ERASE_SIZE,
    "BK7258_OTA_TRANSFER_RECORD_SIZE": 512,
    "BK7258_OTA_TRANSFER_END": 0x60800000 + PAIR_B_SIZE + ERASE_SIZE + 512,
}


class TransferVerificationError(RuntimeError):
    """Raised when the validation transfer package is not self-consistent."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise TransferVerificationError(message)


def sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def load_json(path: Path) -> dict[str, object]:
    value = json.loads(path.read_text(encoding="utf-8"))
    require(isinstance(value, dict), f"{path.name} root must be an object")
    return value


def source_abi(repo: Path) -> dict[str, int]:
    header = (
        repo
        / "board/bk7258_t5ai/chip/include/bk7258_ota_staging.h"
    ).read_text(encoding="utf-8")
    for fragment in (
        "#define BK7258_OTA_TRANSFER_CANDIDATE_ADDRESS  0x60800000u",
        "BK7258_ROLE_SLOT_B_PAIR_SIZE",
        "#define BK7258_OTA_TRANSFER_DESCRIPTOR_ADDRESS",
        "#define BK7258_OTA_TRANSFER_RECORD_ADDRESS",
        "BK7258_FLASH_ERASE_SIZE",
        "#define BK7258_OTA_TRANSFER_RECORD_SIZE        512u",
        "#define BK7258_OTA_TRANSFER_END",
    ):
        require(fragment in header, f"transfer ABI source drift: {fragment}")

    parsed = dict(EXPECTED_ABI)

    require(
        parsed["BK7258_OTA_TRANSFER_CANDIDATE_ADDRESS"]
        + parsed["BK7258_OTA_TRANSFER_CANDIDATE_SIZE"]
        == parsed["BK7258_OTA_TRANSFER_DESCRIPTOR_ADDRESS"],
        "candidate and descriptor are not contiguous",
    )
    require(
        parsed["BK7258_OTA_TRANSFER_DESCRIPTOR_ADDRESS"]
        + STAGE_DESCRIPTOR_SIZE
        <= parsed["BK7258_OTA_TRANSFER_RECORD_ADDRESS"],
        "descriptor overlaps pending record",
    )
    require(
        parsed["BK7258_OTA_TRANSFER_RECORD_ADDRESS"]
        + parsed["BK7258_OTA_TRANSFER_RECORD_SIZE"]
        == parsed["BK7258_OTA_TRANSFER_END"],
        "record end does not close the transfer window",
    )
    require(
        parsed["BK7258_OTA_TRANSFER_CANDIDATE_ADDRESS"] >= 0x60800000
        and parsed["BK7258_OTA_TRANSFER_END"] <= 0x61000000,
        "transfer window leaves the reserved upper 8 MiB PSRAM half",
    )
    return parsed


def verify(
    package: Path,
    repo: Path,
    *,
    expected_target_slot: str | None = None,
    expected_bank: int | None = None,
) -> tuple[dict[str, object], str]:
    abi = source_abi(repo)
    candidate = (package / CANDIDATE_FILE).read_bytes()
    descriptor = (package / DESCRIPTOR_FILE).read_bytes()
    record = (package / RECORD_FILE).read_bytes()
    metadata = (package / METADATA_FILE).read_bytes()
    pair_report = load_json(package / PAIR_REPORT_FILE)
    metadata_report = load_json(package / METADATA_REPORT_FILE)

    require(
        len(candidate) == abi["BK7258_OTA_TRANSFER_CANDIDATE_SIZE"],
        "candidate does not fill the exact fixed transfer slot",
    )
    require(
        len(descriptor) == STAGE_DESCRIPTOR_SIZE,
        "stage descriptor size is not 384 bytes",
    )
    require(
        len(record) == BOOT_METADATA_RECORD_SIZE,
        "pending record size is not 512 bytes",
    )
    require(len(metadata) == 4096, "metadata sector size is not 4 KiB")
    require(metadata[: len(record)] == record, "pending record differs from metadata record 0")
    embedded = record[
        BOOT_METADATA_DESCRIPTOR_OFFSET :
        BOOT_METADATA_DESCRIPTOR_OFFSET + STAGE_DESCRIPTOR_SIZE
    ]
    require(descriptor == embedded, "standalone descriptor differs from pending record")

    metadata_format = metadata_report.get("format")
    rbl = pair_report.get("rbl")
    require(isinstance(rbl, dict), "pair report RBL entry is missing")
    physical = rbl.get("physical_container")
    require(isinstance(physical, dict), "physical candidate report is missing")

    candidate_hash = sha256(candidate)
    descriptor_hash = sha256(descriptor)
    record_hash = sha256(record)
    require(physical.get("sha256") == candidate_hash, "pair candidate SHA-256 mismatch")
    target_slot = "b"
    metadata_bank = 0
    if metadata_format == 2:
        descriptor_report = metadata_report.get("descriptor")
        gates = metadata_report.get("gates")
        require(isinstance(descriptor_report, dict),
                "format-2 descriptor report is missing")
        require(isinstance(gates, dict), "format-2 safety gates are missing")
        target_slot = metadata_report.get("target_slot")
        metadata_bank = metadata_report.get("bank")
        require(target_slot in ("a", "b"),
                "format-2 target slot must be A or B")
        require(metadata_bank in (0, 1),
                "format-2 metadata bank must be 0 or 1")
        require(descriptor_report.get("target_slot") == target_slot,
                "format-2 descriptor target-slot mismatch")
        require(metadata_report.get("bank_sha256") == sha256(metadata),
                "format-2 metadata-bank SHA-256 mismatch")
        require(metadata_report.get("record_sha256") == record_hash,
                "format-2 pending-record SHA-256 mismatch")
        require(metadata_report.get("descriptor_sha256") == descriptor_hash,
                "format-2 descriptor SHA-256 mismatch")
        require(descriptor_report.get("physical_sha256") == candidate_hash,
                "format-2 candidate SHA-256 mismatch")
        require(descriptor_report.get("descriptor_size") == len(descriptor),
                "format-2 descriptor size mismatch")
        require(gates.get("board_write_authorized") is False,
                "format-2 host package authorizes board write")
    elif metadata_format == 1:
        secondary = metadata_report.get("secondary")
        stage_entry = metadata_report.get("stage_descriptor")
        record_entry = metadata_report.get("pending_record")
        require(isinstance(secondary, dict), "metadata secondary entry is missing")
        require(isinstance(stage_entry, dict), "metadata stage descriptor entry is missing")
        require(isinstance(record_entry, dict), "metadata pending record entry is missing")
        require(secondary.get("candidate_sha256") == candidate_hash,
                "metadata candidate SHA-256 mismatch")
        require(secondary.get("descriptor_sha256") == descriptor_hash,
                "metadata descriptor SHA-256 mismatch")
        require(stage_entry.get("size") == len(descriptor),
                "descriptor report size mismatch")
        require(stage_entry.get("sha256") == descriptor_hash,
                "descriptor report SHA-256 mismatch")
        require(record_entry.get("size") == len(record),
                "record report size mismatch")
        require(record_entry.get("sha256") == record_hash,
                "record report SHA-256 mismatch")
        require(metadata_report.get("validation_profile") is True,
                "format-1 transfer package is not a validation profile")
    else:
        raise TransferVerificationError("unsupported metadata format")

    if expected_target_slot is not None:
        require(target_slot == expected_target_slot,
                "transfer target slot differs from the caller-pinned slot")
    if expected_bank is not None:
        require(metadata_bank == expected_bank,
                "transfer metadata bank differs from the caller-pinned bank")

    generation = pair_report.get("generation")
    timestamp = pair_report.get("timestamp")
    version = pair_report.get("version")
    base_version = pair_report.get("base_version")
    require(isinstance(generation, int) and generation > 0, "invalid generation")
    require(isinstance(timestamp, int) and timestamp >= 0, "invalid timestamp")
    require(isinstance(version, str) and version, "invalid version")
    require(isinstance(base_version, str) and base_version, "invalid base version")

    jlink = "\n".join(
        (
            "halt",
            f'loadfile "{CANDIDATE_FILE}" 0x{abi["BK7258_OTA_TRANSFER_CANDIDATE_ADDRESS"]:08x} noreset',
            f'verifybin "{CANDIDATE_FILE}", 0x{abi["BK7258_OTA_TRANSFER_CANDIDATE_ADDRESS"]:08x}',
            f'loadfile "{DESCRIPTOR_FILE}" 0x{abi["BK7258_OTA_TRANSFER_DESCRIPTOR_ADDRESS"]:08x} noreset',
            f'verifybin "{DESCRIPTOR_FILE}", 0x{abi["BK7258_OTA_TRANSFER_DESCRIPTOR_ADDRESS"]:08x}',
            f'loadfile "{RECORD_FILE}" 0x{abi["BK7258_OTA_TRANSFER_RECORD_ADDRESS"]:08x} noreset',
            f'verifybin "{RECORD_FILE}", 0x{abi["BK7258_OTA_TRANSFER_RECORD_ADDRESS"]:08x}',
            "go",
            "exit",
            "",
        )
    )
    report: dict[str, object] = {
        "format": metadata_format,
        "status": "pass",
        "transport": "jlink-fixed-psram",
        "jlink_write_command": "loadfile-noreset",
        "implicit_reset_forbidden": True,
        "requires_psram_capacity": 0x01000000,
        "transfer_abi": abi,
        "generation": generation,
        "timestamp": timestamp,
        "version": version,
        "base_version": base_version,
        "target_slot": target_slot,
        "metadata_bank": metadata_bank,
        "artifacts": {
            CANDIDATE_FILE: {"size": len(candidate), "sha256": candidate_hash},
            DESCRIPTOR_FILE: {"size": len(descriptor), "sha256": descriptor_hash},
            RECORD_FILE: {"size": len(record), "sha256": record_hash},
        },
        "target_preflight": f"bkota prepare-transfer {generation} N15-WRITE-{generation}",
        "target_validate": (
            f"bkota validate-mem {generation} {timestamp} {version} {base_version}"
        ),
        "target_stage": (
            f"bkota stage-mem {generation} {timestamp} {version} "
            f"{base_version} <timeout-ms> N15-WRITE-{generation}"
        ),
        "target_publish": (
            f"bkota publish-mem {generation} <timeout-ms> N15-WRITE-{generation}"
        ),
        "watchdog_restore": "physical reset required after stage/publish",
        "automatic_reset": False,
        "flash_write_performed": False,
        "board_write_authorized": False,
    }
    return report, jlink


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--jlink-output", type=Path)
    parser.add_argument("--expected-target-slot", choices=("a", "b"))
    parser.add_argument("--expected-bank", type=int, choices=(0, 1))
    parser.add_argument(
        "--check-only",
        action="store_true",
        help="Compare existing report/J-Link files without modifying the package",
    )
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[3]
    try:
        report, jlink = verify(
            args.package,
            repo,
            expected_target_slot=args.expected_target_slot,
            expected_bank=args.expected_bank,
        )
        output = args.output or args.package / TRANSFER_REPORT_FILE
        jlink_output = args.jlink_output or args.package / JLINK_FILE
        if args.check_only:
            require(args.output is None and args.jlink_output is None,
                    "--check-only cannot be combined with output paths")
            require(output.is_file(), "existing transfer report is missing")
            require(jlink_output.is_file(), "existing J-Link plan is missing")
            require(load_json(output) == report, "existing transfer report drift")
            require(
                jlink_output.read_text(encoding="ascii") == jlink,
                "existing J-Link plan drift",
            )
        else:
            output.write_text(
                json.dumps(report, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            jlink_output.write_text(jlink, encoding="ascii")
    except (OSError, ValueError, TransferVerificationError) as error:
        print(f"BK7258 N15-F transfer verification FAIL: {error}")
        return 1

    if args.json:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print(
            "BK7258 N15-F transfer verification PASS: "
            f"generation={report['generation']} fixed_psram=true "
            "writes_enabled=false board_authorized=false"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
