#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Create resumable private ASR drafts for an audited BKVoice corpus."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path


TOOL_VERSION = "1.0.0"
FORMAT = "bkvoice-transcription-draft-v1"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


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


def clean_text(value: object) -> str:
    text = " ".join(str(value).replace("|", "｜").split())
    return text.strip()


def load_corpus(path: Path) -> list[dict[str, object]]:
    rows = []
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        try:
            row = json.loads(line)
        except json.JSONDecodeError as error:
            raise ValueError(f"invalid corpus manifest line {number}") from error
        if not isinstance(row, dict) or not row.get("accepted"):
            continue
        if row.get("split") not in ("train", "eval") or not row.get("audio_file"):
            raise ValueError(f"incomplete accepted corpus row {number}")
        rows.append(row)
    if not rows:
        raise ValueError("corpus has no accepted rows")
    return rows


def load_existing(path: Path) -> dict[str, dict[str, object]]:
    existing = {}
    if not path.exists():
        return existing
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        try:
            row = json.loads(line)
        except json.JSONDecodeError as error:
            raise ValueError(f"invalid draft line {number}") from error
        audio_file = str(row.get("audio_file", ""))
        if not audio_file or audio_file in existing:
            raise ValueError(f"invalid or duplicate draft row {number}")
        existing[audio_file] = row
    return existing


def write_lists(
    output: Path, raw_dir: Path, speaker_id: str, rows: list[dict[str, object]]
) -> dict[str, str]:
    hashes = {}
    raw_root = raw_dir.resolve()
    for split in ("train", "eval", "all"):
        selected = (
            rows if split == "all" else [row for row in rows if row["split"] == split]
        )
        lines = []
        for row in selected:
            # Keep the sanitized corpus filename.  Resolving the final symlink
            # would leak the original export path into private tool logs and
            # makes basename-based GPT-SoVITS preprocessing inconsistent.
            audio_path = raw_root / str(row["audio_file"])
            lines.append(f"{audio_path}|{speaker_id}|ZH|{row['text']}")
        path = output / f"draft-{split}.list"
        path.write_text("\n".join(lines) + ("\n" if lines else ""), encoding="utf-8")
        hashes[split] = sha256_file(path)
    return hashes


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus-manifest", required=True, type=Path)
    parser.add_argument("--raw-dir", required=True, type=Path)
    parser.add_argument("--worklog", required=True, type=Path)
    parser.add_argument("--out-dir", required=True, type=Path)
    parser.add_argument("--model-cache", required=True, type=Path)
    parser.add_argument("--model-id", default="FunAudioLLM/Fun-ASR-Nano-2512")
    parser.add_argument("--hub", choices=("ms", "hf"), default="ms")
    parser.add_argument("--device", choices=("cuda", "cpu"), default="cuda")
    parser.add_argument("--limit", type=int)
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args(argv)
    if args.limit is not None and args.limit <= 0:
        parser.error("--limit must be positive")
    return args


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv if argv is not None else sys.argv[1:])
    worklog = json.loads(args.worklog.read_text(encoding="utf-8"))
    if worklog.get("consent", {}).get("revoked") is not False:
        raise SystemExit("consent is revoked or missing")
    if worklog.get("stages", {}).get("quality_filter") != "PASS":
        raise SystemExit("quality filter has not passed")
    if worklog.get("stages", {}).get("dataset_split") != "PASS":
        raise SystemExit("dataset split has not passed")

    try:
        corpus = load_corpus(args.corpus_manifest)
    except (OSError, ValueError) as error:
        raise SystemExit(str(error)) from error
    raw_dir = args.raw_dir.resolve()
    if not raw_dir.is_dir():
        raise SystemExit("--raw-dir must be an existing directory")
    output = args.out_dir.resolve()
    draft_path = output / "draft-private.jsonl"
    if output.exists() and not args.resume:
        raise SystemExit("output exists; pass --resume to continue")
    output.mkdir(parents=True, exist_ok=True)
    try:
        existing = load_existing(draft_path)
    except (OSError, ValueError) as error:
        raise SystemExit(str(error)) from error

    pending = [row for row in corpus if str(row["audio_file"]) not in existing]
    if args.limit is not None:
        pending = pending[: args.limit]

    model = None
    if pending:
        os.environ["MODELSCOPE_CACHE"] = str(args.model_cache.resolve())
        os.environ["HF_HOME"] = str((args.model_cache / "huggingface").resolve())
        from funasr import AutoModel  # pylint: disable=import-outside-toplevel

        model = AutoModel(
            model=args.model_id,
            hub=args.hub,
            trust_remote_code=args.hub == "hf",
            vad_model="fsmn-vad",
            device=args.device,
            disable_update=True,
        )

    processed = 0
    nonpass = 0
    with draft_path.open("a", encoding="utf-8") as draft:
        for row in pending:
            audio_file = str(row["audio_file"])
            source = raw_dir / audio_file
            try:
                result = model.generate(input=str(source))[0]
                text = clean_text(result.get("text", ""))
                status = "PASS" if text else "EMPTY"
            except Exception as error:  # Keep the long job resumable.
                text = ""
                status = "ERROR_" + type(error).__name__.upper()
            record = {
                "format": FORMAT,
                "audio_file": audio_file,
                "sha256": row["sha256"],
                "split": row["split"],
                "language": "ZH",
                "text": text,
                "status": status,
            }
            draft.write(
                json.dumps(
                    record, ensure_ascii=False, sort_keys=True, separators=(",", ":")
                )
                + "\n"
            )
            draft.flush()
            existing[audio_file] = record
            processed += 1
            nonpass += status != "PASS"
            if processed == 1 or processed % 25 == 0:
                print(
                    f"BKVOICE ASR progress={processed}/{len(pending)} nonpass={nonpass}"
                )

    recorded = [
        existing[str(row["audio_file"])]
        for row in corpus
        if str(row["audio_file"]) in existing
    ]
    ordered = [row for row in recorded if row["status"] == "PASS"]
    empty_count = sum(row["status"] == "EMPTY" for row in recorded)
    error_count = sum(str(row["status"]).startswith("ERROR_") for row in recorded)
    complete = len(recorded) == len(corpus) and error_count == 0
    list_hashes = write_lists(output, raw_dir, str(worklog["speaker_id"]), ordered)
    created = datetime.now(timezone.utc).replace(microsecond=0).isoformat()
    audit = {
        "format": FORMAT,
        "tool_version": TOOL_VERSION,
        "created_utc": created,
        "model_id": args.model_id,
        "hub": args.hub,
        "device": args.device,
        "corpus_manifest_sha256": sha256_file(args.corpus_manifest),
        "counts": {
            "expected": len(corpus),
            "processed": len(recorded),
            "drafted": len(ordered),
            "excluded_empty": empty_count,
            "errors": error_count,
            "pending": len(corpus) - len(recorded),
            "train": sum(row["split"] == "train" for row in ordered),
            "eval": sum(row["split"] == "eval" for row in ordered),
        },
        "private_draft_sha256": sha256_file(draft_path),
        "list_sha256": list_hashes,
        "status": "DRAFT_READY_REVIEW_REQUIRED" if complete else "PARTIAL",
    }
    audit_path = output / "transcription-audit.json"
    atomic_json(audit_path, audit)

    if complete and args.limit is None:
        worklog["status"] = "TRANSCRIPTION_DRAFTED"
        worklog["stages"]["transcription"] = "DRAFT_READY_REVIEW_REQUIRED"
        worklog["events"].append(
            {
                "created_utc": created,
                "event": "transcription_draft_completed",
                "status": "REVIEW_REQUIRED",
                "audit_sha256": sha256_file(audit_path),
                "utterances": len(ordered),
            }
        )
        atomic_json(args.worklog, worklog)
    print(
        "BKVOICE_TRANSCRIPTION_"
        + ("DRAFT_PASS" if complete else "PARTIAL")
        + " "
        + json.dumps(audit["counts"], sort_keys=True)
    )
    return 0 if error_count == 0 else 2


if __name__ == "__main__":
    raise SystemExit(main())
