#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build a private, reproducible BKVoice training corpus from audited WAVs.

The output directory must stay outside the source export and outside Git.  It
contains sanitized symlinks plus a private manifest; the public audit and the
training worklog contain aggregate metrics and hashes only.
"""

from __future__ import annotations

import argparse
import audioop
import hashlib
import json
import math
import os
import sys
import tempfile
import wave
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath


TOOL_VERSION = "1.0.0"
AUDIT_FORMAT = "bkvoice-corpus-audit-v1"
PRIVATE_FORMAT = "bkvoice-corpus-private-v1"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def dbfs(value: int, full_scale: int) -> float:
    return round(20.0 * math.log10(max(value, 1) / full_scale), 3)


def is_within(path: Path, root: Path) -> bool:
    try:
        path.resolve().relative_to(root.resolve())
    except ValueError:
        return False
    return True


def read_rows(path: Path) -> list[dict[str, object]]:
    rows = []
    for line_number, line in enumerate(
        path.read_text(encoding="utf-8").splitlines(), 1
    ):
        try:
            row = json.loads(line)
        except json.JSONDecodeError as error:
            raise ValueError(f"invalid private manifest line {line_number}") from error
        required = (
            "source_path",
            "sha256",
            "direction",
            "association_status",
            "sample_rate",
            "channels",
            "duration_ms",
        )
        if not isinstance(row, dict) or any(key not in row for key in required):
            raise ValueError(f"incomplete private manifest line {line_number}")
        source_path = PurePosixPath(str(row["source_path"]))
        if source_path.is_absolute() or ".." in source_path.parts:
            raise ValueError(f"unsafe source path on line {line_number}")
        if row["direction"] != "received" or row["association_status"] != "associated":
            raise ValueError(f"untrusted speaker association on line {line_number}")
        rows.append(row)
    if not rows:
        raise ValueError("private manifest is empty")
    return rows


def analyze(path: Path, frame_ms: int) -> dict[str, object]:
    with wave.open(str(path), "rb") as audio:
        channels = audio.getnchannels()
        sample_width = audio.getsampwidth()
        sample_rate = audio.getframerate()
        frame_count = audio.getnframes()
        compression = audio.getcomptype()
        payload = audio.readframes(frame_count)

    full_scale = (1 << (sample_width * 8 - 1)) - 1 if sample_width > 0 else 0
    if full_scale <= 0 or frame_count <= 0:
        raise ValueError("empty or unsupported WAV")
    rms = audioop.rms(payload, sample_width)
    peak = audioop.max(payload, sample_width)
    frame_samples = max(1, sample_rate * frame_ms // 1000)
    frame_bytes = frame_samples * channels * sample_width
    active = 0
    total = 0
    active_floor = full_scale * (10.0 ** (-45.0 / 20.0))
    for offset in range(0, len(payload), frame_bytes):
        frame = payload[offset : offset + frame_bytes]
        if not frame:
            continue
        total += 1
        if audioop.rms(frame, sample_width) >= active_floor:
            active += 1

    # Count near-full-scale samples without exposing waveform content.
    clip_threshold = max(1, full_scale - 7)
    clipped = 0
    sample_count = frame_count * channels
    for offset in range(0, len(payload), sample_width):
        value = int.from_bytes(
            payload[offset : offset + sample_width], "little", signed=True
        )
        if abs(value) >= clip_threshold:
            clipped += 1

    return {
        "sample_rate": sample_rate,
        "channels": channels,
        "sample_width": sample_width,
        "compression": compression,
        "duration_ms": round(frame_count * 1000.0 / sample_rate, 3),
        "rms_dbfs": dbfs(rms, full_scale),
        "peak_dbfs": dbfs(peak, full_scale),
        "active_ratio": round(active / max(total, 1), 6),
        "clipped_ratio": round(clipped / max(sample_count, 1), 8),
    }


def decision(metrics: dict[str, object], args: argparse.Namespace) -> list[str]:
    reasons = []
    if metrics["sample_rate"] != args.sample_rate:
        reasons.append("sample_rate")
    if metrics["channels"] != 1:
        reasons.append("channels")
    if metrics["sample_width"] != 2 or metrics["compression"] != "NONE":
        reasons.append("encoding")
    duration = float(metrics["duration_ms"])
    if duration < args.min_duration_ms:
        reasons.append("too_short")
    if duration > args.max_duration_ms:
        reasons.append("too_long")
    if float(metrics["rms_dbfs"]) < args.min_rms_dbfs:
        reasons.append("low_level")
    if float(metrics["active_ratio"]) < args.min_active_ratio:
        reasons.append("low_activity")
    if float(metrics["clipped_ratio"]) > args.max_clipped_ratio:
        reasons.append("clipping")
    return reasons


def atomic_json(path: Path, document: dict[str, object]) -> None:
    payload = json.dumps(document, sort_keys=True, indent=2) + "\n"
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=path.name + ".", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as output:
            output.write(payload)
        os.replace(temporary, path)
    except BaseException:
        Path(temporary).unlink(missing_ok=True)
        raise


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--export-root", required=True, type=Path)
    parser.add_argument("--private-manifest", required=True, type=Path)
    parser.add_argument("--worklog", required=True, type=Path)
    parser.add_argument("--out-dir", required=True, type=Path)
    parser.add_argument("--sample-rate", type=int, default=24000)
    parser.add_argument("--min-duration-ms", type=float, default=800.0)
    parser.add_argument("--max-duration-ms", type=float, default=12000.0)
    parser.add_argument("--min-rms-dbfs", type=float, default=-45.0)
    parser.add_argument("--min-active-ratio", type=float, default=0.20)
    parser.add_argument("--max-clipped-ratio", type=float, default=0.005)
    parser.add_argument("--eval-percent", type=int, default=10)
    parser.add_argument("--frame-ms", type=int, default=20)
    args = parser.parse_args(argv)
    if not 1 <= args.eval_percent <= 50:
        parser.error("--eval-percent must be in [1, 50]")
    if args.min_duration_ms <= 0 or args.max_duration_ms <= args.min_duration_ms:
        parser.error("invalid duration limits")
    if not 0 <= args.min_active_ratio <= 1 or not 0 <= args.max_clipped_ratio <= 1:
        parser.error("ratio limits must be in [0, 1]")
    return args


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv if argv is not None else sys.argv[1:])
    root = args.export_root.resolve()
    output = args.out_dir.resolve()
    if not root.is_dir():
        raise SystemExit("--export-root must be an existing directory")
    if is_within(output, root):
        raise SystemExit("--out-dir must be outside --export-root")
    if output.exists():
        raise SystemExit("refusing to overwrite an existing corpus")

    worklog = json.loads(args.worklog.read_text(encoding="utf-8"))
    private_sha256 = sha256_file(args.private_manifest)
    if worklog.get("source", {}).get("private_manifest_sha256") != private_sha256:
        raise SystemExit("private manifest hash does not match worklog")
    if worklog.get("consent", {}).get("revoked") is not False:
        raise SystemExit("consent is revoked or missing")
    if worklog.get("stages", {}).get("quality_filter") != "NOT_STARTED":
        raise SystemExit("quality_filter stage is not NOT_STARTED")

    try:
        rows = read_rows(args.private_manifest)
    except (OSError, ValueError) as error:
        raise SystemExit(str(error)) from error

    raw_dir = output / "raw"
    raw_dir.mkdir(parents=True)
    private_rows = []
    reasons: Counter[str] = Counter()
    accepted_ms = 0.0
    split_counts: Counter[str] = Counter()
    for index, row in enumerate(rows):
        source = root / PurePosixPath(str(row["source_path"]))
        if not source.is_file() or sha256_file(source) != row["sha256"]:
            reject = ["source_integrity"]
            metrics: dict[str, object] = {}
        else:
            try:
                metrics = analyze(source, args.frame_ms)
                reject = decision(metrics, args)
            except (OSError, EOFError, wave.Error, ValueError):
                metrics = {}
                reject = ["invalid_wav"]

        accepted = not reject
        split = None
        sanitized = None
        if accepted:
            split = (
                "eval"
                if int(str(row["sha256"])[:8], 16) % 100 < args.eval_percent
                else "train"
            )
            split_counts[split] += 1
            accepted_ms += float(metrics["duration_ms"])
            sanitized = f"{index:04d}-{str(row['sha256'])[:16]}.wav"
            (raw_dir / sanitized).symlink_to(source)
        else:
            reasons.update(reject)

        private_rows.append(
            {
                "format": PRIVATE_FORMAT,
                "path_sha256": row.get("path_sha256"),
                "sha256": row["sha256"],
                "audio_file": sanitized,
                "accepted": accepted,
                "split": split,
                "reject_reasons": reject,
                "metrics": metrics,
            }
        )

    private_path = output / "corpus-private.jsonl"
    private_path.write_text(
        "".join(
            json.dumps(row, sort_keys=True, separators=(",", ":")) + "\n"
            for row in private_rows
        ),
        encoding="utf-8",
    )
    created = datetime.now(timezone.utc).replace(microsecond=0).isoformat()
    audit = {
        "format": AUDIT_FORMAT,
        "tool_version": TOOL_VERSION,
        "created_utc": created,
        "source_private_manifest_sha256": private_sha256,
        "parameters": {
            "sample_rate": args.sample_rate,
            "min_duration_ms": args.min_duration_ms,
            "max_duration_ms": args.max_duration_ms,
            "min_rms_dbfs": args.min_rms_dbfs,
            "min_active_ratio": args.min_active_ratio,
            "max_clipped_ratio": args.max_clipped_ratio,
            "eval_percent": args.eval_percent,
            "frame_ms": args.frame_ms,
        },
        "counts": {
            "source": len(rows),
            "accepted": sum(split_counts.values()),
            "rejected": len(rows) - sum(split_counts.values()),
            "train": split_counts["train"],
            "eval": split_counts["eval"],
        },
        "accepted_duration_ms": round(accepted_ms, 3),
        "reject_reasons": dict(sorted(reasons.items())),
        "private_corpus_manifest_sha256": sha256_file(private_path),
    }
    audit_path = output / "corpus-audit.json"
    atomic_json(audit_path, audit)

    worklog["status"] = "CORPUS_PREPARED"
    worklog["stages"]["quality_filter"] = "PASS"
    worklog["stages"]["dataset_split"] = "PASS"
    worklog["events"].append(
        {
            "created_utc": created,
            "event": "quality_filter_completed",
            "status": "PASS",
            "audit_sha256": sha256_file(audit_path),
            "accepted": audit["counts"]["accepted"],
            "rejected": audit["counts"]["rejected"],
        }
    )
    atomic_json(args.worklog, worklog)
    print("BKVOICE_CORPUS_PREP_PASS " + json.dumps(audit["counts"], sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
