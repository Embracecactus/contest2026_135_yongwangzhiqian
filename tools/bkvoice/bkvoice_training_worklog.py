#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Initialize a privacy-minimized BKVoice training worklog.

The private source manifest is used only for validation.  Source paths and
transcripts are deliberately omitted from the worklog so it is safe to use as
the non-content audit trail for later local training stages.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath


FORMAT = "bkvoice-training-worklog-v1"
SPEAKER_RE = re.compile(r"^[A-Za-z0-9_.-]{1,47}$")
HASH_RE = re.compile(r"^[0-9a-f]{64}$")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def private_source_summary(path: Path) -> dict[str, object]:
    count = 0
    duration_ms = 0.0
    sample_rates: set[int] = set()
    channels: set[int] = set()
    content_hashes: Counter[str] = Counter()
    source_paths: set[str] = set()

    with path.open("r", encoding="utf-8") as source:
        for line_number, line in enumerate(source, 1):
            try:
                row = json.loads(line)
            except json.JSONDecodeError as error:
                raise ValueError(
                    f"invalid private manifest line {line_number}"
                ) from error
            if not isinstance(row, dict):
                raise ValueError(
                    f"private manifest line {line_number} is not an object"
                )

            required = (
                "source_path",
                "path_sha256",
                "sha256",
                "direction",
                "association_status",
                "duration_ms",
                "sample_rate",
                "channels",
            )
            if any(key not in row for key in required):
                raise ValueError(f"private manifest line {line_number} is incomplete")
            source_path = PurePosixPath(str(row["source_path"]))
            if source_path.is_absolute() or ".." in source_path.parts:
                raise ValueError(f"unsafe source path on line {line_number}")
            if str(source_path) in source_paths:
                raise ValueError(f"duplicate source path on line {line_number}")
            if (
                row["direction"] != "received"
                or row["association_status"] != "associated"
            ):
                raise ValueError(f"untrusted association on line {line_number}")
            if not HASH_RE.fullmatch(str(row["sha256"])) or not HASH_RE.fullmatch(
                str(row["path_sha256"])
            ):
                raise ValueError(f"invalid hash on line {line_number}")
            if (
                int(row["sample_rate"]) <= 0
                or int(row["channels"]) <= 0
                or float(row["duration_ms"]) <= 0
            ):
                raise ValueError(f"invalid audio metadata on line {line_number}")

            source_paths.add(str(source_path))
            content_hashes[str(row["sha256"])] += 1
            sample_rates.add(int(row["sample_rate"]))
            channels.add(int(row["channels"]))
            duration_ms += float(row["duration_ms"])
            count += 1

    if count == 0:
        raise ValueError("private manifest is empty")
    return {
        "utterance_count": count,
        "duration_ms": round(duration_ms, 3),
        "sample_rates": sorted(sample_rates),
        "channels": sorted(channels),
        "duplicate_content_count": sum(
            value - 1 for value in content_hashes.values() if value > 1
        ),
    }


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-audit", required=True, type=Path)
    parser.add_argument("--private-manifest", required=True, type=Path)
    parser.add_argument("--speaker-id", required=True)
    parser.add_argument(
        "--consent-status", choices=("user-attested", "recorded"), required=True
    )
    parser.add_argument("--consent-record", type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args(argv)
    if not SPEAKER_RE.fullmatch(args.speaker_id):
        parser.error("--speaker-id must be an anonymous BKVoice identifier")
    if args.consent_status == "recorded" and args.consent_record is None:
        parser.error("--consent-record is required for recorded consent")
    if args.consent_status != "recorded" and args.consent_record is not None:
        parser.error("--consent-record is only valid with recorded consent")
    return args


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv if argv is not None else sys.argv[1:])
    if args.out.exists():
        raise SystemExit("refusing to overwrite an existing worklog")

    audit = json.loads(args.source_audit.read_text(encoding="utf-8"))
    private_sha256 = sha256_file(args.private_manifest)
    if audit.get("direct_chat") is not True:
        raise SystemExit("source audit is not a direct chat")
    if audit.get("private_output_sha256") != private_sha256:
        raise SystemExit("private manifest hash does not match source audit")

    try:
        summary = private_source_summary(args.private_manifest)
    except (OSError, ValueError) as error:
        raise SystemExit(str(error)) from error

    consent_sha256 = (
        sha256_file(args.consent_record) if args.consent_record is not None else None
    )
    created = datetime.now(timezone.utc).replace(microsecond=0).isoformat()
    worklog = {
        "format": FORMAT,
        "created_utc": created,
        "status": "SOURCE_AUDITED",
        "speaker_id": args.speaker_id,
        "consent": {
            "status": args.consent_status,
            "record_sha256": consent_sha256,
            "revoked": False,
        },
        "source": {
            "audit_sha256": sha256_file(args.source_audit),
            "public_manifest_sha256": audit.get("output_sha256"),
            "private_manifest_sha256": private_sha256,
            **summary,
        },
        "stages": {
            "quality_filter": "NOT_STARTED",
            "transcription": "NOT_STARTED",
            "dataset_split": "NOT_STARTED",
            "zero_shot_baseline": "NOT_STARTED",
            "finetune": "NOT_STARTED",
            "evaluation": "NOT_STARTED",
            "asset_selection": "NOT_STARTED",
        },
        "model_candidates": [
            {
                "id": "fun-cosyvoice3-zero-shot",
                "role": "baseline",
                "status": "NOT_STARTED",
            },
            {
                "id": "gpt-sovits-finetune",
                "role": "primary-finetune",
                "status": "BLOCKED_DATA_PREP",
            },
            {
                "id": "f5-tts-finetune",
                "role": "alternate-finetune",
                "status": "BLOCKED_DATA_PREP",
            },
        ],
        "events": [
            {
                "created_utc": created,
                "event": "source_audit_accepted",
                "status": "PASS",
            }
        ],
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(
        json.dumps(worklog, sort_keys=True, indent=2) + "\n", encoding="utf-8"
    )
    print("BKVOICE_TRAINING_WORKLOG_INIT_PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
