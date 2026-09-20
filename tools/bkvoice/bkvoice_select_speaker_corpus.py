#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Select a private, high-confidence single-speaker GPT-SoVITS corpus.

Message direction establishes only the candidate set.  This tool adds an
independent speaker-embedding check, derives a robust dominant-speaker
centroid, rejects voice outliers, and selects a short high-quality first-round
train/eval corpus.  Filenames, embeddings, and transcripts remain in the
private output directory; the aggregate audit and worklog contain hashes and
counts only.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path


TOOL_VERSION = "1.0.0"
FORMAT = "bkvoice-speaker-selection-v1"


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


def load_jsonl(path: Path) -> list[dict[str, object]]:
    rows = []
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        try:
            row = json.loads(line)
        except json.JSONDecodeError as error:
            raise ValueError(f"invalid JSONL line {number} in {path.name}") from error
        if not isinstance(row, dict):
            raise ValueError(f"non-object JSONL line {number} in {path.name}")
        rows.append(row)
    return rows


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus-manifest", required=True, type=Path)
    parser.add_argument("--transcription-draft", required=True, type=Path)
    parser.add_argument("--raw-dir", required=True, type=Path)
    parser.add_argument("--worklog", required=True, type=Path)
    parser.add_argument("--gpt-sovits-root", required=True, type=Path)
    parser.add_argument("--out-dir", required=True, type=Path)
    parser.add_argument("--device", choices=("cuda", "cpu"), default="cuda")
    parser.add_argument("--target-train-minutes", type=float, default=15.0)
    parser.add_argument("--target-eval-minutes", type=float, default=2.0)
    parser.add_argument("--min-dominant-fraction", type=float, default=0.60)
    parser.add_argument("--min-speaker-similarity", type=float, default=0.35)
    parser.add_argument("--min-selection-duration-ms", type=float, default=1200.0)
    parser.add_argument("--max-selection-duration-ms", type=float, default=8000.0)
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--limit", type=int)
    args = parser.parse_args(argv)
    if args.target_train_minutes <= 0 or args.target_eval_minutes <= 0:
        parser.error("target durations must be positive")
    if not 0.5 <= args.min_dominant_fraction <= 1:
        parser.error("--min-dominant-fraction must be in [0.5, 1]")
    if not -1 <= args.min_speaker_similarity <= 1:
        parser.error("--min-speaker-similarity must be in [-1, 1]")
    if args.limit is not None and args.limit <= 0:
        parser.error("--limit must be positive")
    return args


def load_candidates(corpus_path: Path, draft_path: Path) -> list[dict[str, object]]:
    corpus = {}
    for row in load_jsonl(corpus_path):
        audio_file = row.get("audio_file")
        if row.get("accepted") and audio_file:
            corpus[str(audio_file)] = row
    candidates = []
    for row in load_jsonl(draft_path):
        audio_file = str(row.get("audio_file", ""))
        if row.get("status") != "PASS" or audio_file not in corpus:
            continue
        source = corpus[audio_file]
        if row.get("sha256") != source.get("sha256") or row.get("split") != source.get(
            "split"
        ):
            raise ValueError(f"draft/corpus mismatch for {audio_file}")
        text = str(row.get("text", "")).strip()
        if not text:
            raise ValueError(f"PASS draft has empty text for {audio_file}")
        candidates.append(
            {
                "audio_file": audio_file,
                "sha256": source["sha256"],
                "split": source["split"],
                "metrics": source.get("metrics", {}),
                "text": text,
            }
        )
    if not candidates:
        raise ValueError("no transcribed corpus candidates")
    return candidates


def load_embedding_cache(path: Path) -> dict[str, dict[str, object]]:
    if not path.exists():
        return {}
    rows = {}
    for row in load_jsonl(path):
        audio_file = str(row.get("audio_file", ""))
        embedding = row.get("embedding")
        if not audio_file or not isinstance(embedding, list) or len(embedding) != 192:
            raise ValueError("invalid speaker embedding cache row")
        if audio_file in rows and rows[audio_file].get("sha256") != row.get("sha256"):
            raise ValueError("speaker embedding cache hash changed")
        # An append-only cache may contain a failed attempt followed by a
        # successful retry.  The most recent record is authoritative.
        rows[audio_file] = row
    return rows


def quantiles(values: list[float]) -> dict[str, float | None]:
    import numpy as np  # pylint: disable=import-outside-toplevel

    if not values:
        return {"p05": None, "p25": None, "p50": None, "p75": None, "p95": None}
    result = np.quantile(
        np.asarray(values, dtype=np.float64), [0.05, 0.25, 0.5, 0.75, 0.95]
    )
    return {
        name: round(float(value), 6)
        for name, value in zip(("p05", "p25", "p50", "p75", "p95"), result)
    }


def build_model(root: Path, weight: Path, device_name: str):
    import torch  # pylint: disable=import-outside-toplevel

    module_dir = root / "GPT_SoVITS/eres2net"
    sys.path.insert(0, str(module_dir))
    from ERes2NetV2 import ERes2NetV2  # pylint: disable=import-outside-toplevel
    import kaldi as kaldi_module  # pylint: disable=import-outside-toplevel

    if device_name == "cuda" and not torch.cuda.is_available():
        raise RuntimeError("CUDA requested but unavailable")
    device = torch.device(device_name)
    model = ERes2NetV2(baseWidth=24, scale=4, expansion=4)
    state = torch.load(weight, map_location="cpu", weights_only=False)
    model.load_state_dict(state)
    model.eval().to(device)
    return torch, kaldi_module, model, device


def analyze_audio(
    path: Path, torch_module, kaldi_module, model, device
) -> tuple[list[float], float | None]:
    import librosa  # pylint: disable=import-outside-toplevel
    import numpy as np  # pylint: disable=import-outside-toplevel
    import soundfile  # pylint: disable=import-outside-toplevel

    audio, sample_rate = soundfile.read(path, dtype="float32", always_2d=True)
    waveform = audio.mean(axis=1)
    if sample_rate != 16000:
        waveform = librosa.resample(waveform, orig_sr=sample_rate, target_sr=16000)
    tensor = torch_module.from_numpy(np.asarray(waveform, dtype=np.float32)).to(device)
    features = kaldi_module.fbank(
        tensor.unsqueeze(0), num_mel_bins=80, sample_frequency=16000, dither=0
    ).unsqueeze(0)
    with torch_module.inference_mode():
        embedding = model(features).float()
        embedding = torch_module.nn.functional.normalize(embedding, dim=1)
    values = embedding.squeeze(0).cpu().tolist()

    # Pitch is only a secondary diagnostic; speaker identity is decided by the
    # learned embedding.  YIN is deterministic and substantially cheaper than
    # a second neural model for this first-round corpus audit.
    try:
        pitch = librosa.yin(
            waveform, fmin=65.0, fmax=400.0, sr=16000, frame_length=1024, hop_length=256
        )
        finite = pitch[np.isfinite(pitch)]
        pitch_hz = round(float(np.median(finite)), 3) if finite.size else None
    except (ValueError, FloatingPointError):
        pitch_hz = None
    return values, pitch_hz


def speaker_scores(embeddings):
    import numpy as np  # pylint: disable=import-outside-toplevel

    matrix = np.asarray(embeddings, dtype=np.float32)
    matrix /= np.maximum(np.linalg.norm(matrix, axis=1, keepdims=True), 1e-12)
    similarities = matrix @ matrix.T
    count = len(matrix)
    neighbor_count = min(20, max(1, count - 1))
    np.fill_diagonal(similarities, -1.0)
    neighbors = np.partition(similarities, -neighbor_count, axis=1)[:, -neighbor_count:]
    density = neighbors.mean(axis=1)
    anchor_count = max(1, int(math.ceil(count * 0.50)))
    anchors = np.argpartition(density, -anchor_count)[-anchor_count:]
    centroid = matrix[anchors].mean(axis=0)
    centroid /= max(float(np.linalg.norm(centroid)), 1e-12)
    scores = matrix @ centroid
    q25, q75 = np.quantile(scores, [0.25, 0.75])
    lower_fence = float(q25 - 1.5 * (q75 - q25))
    return scores, density, lower_fence


def text_is_suitable(text: str) -> bool:
    compact = "".join(text.split())
    if not 2 <= len(compact) <= 60:
        return False
    cjk = sum("\u3400" <= char <= "\u9fff" for char in compact)
    return cjk / max(len(compact), 1) >= 0.50


def quality_score(row: dict[str, object]) -> float:
    metrics = row.get("metrics", {})
    duration = float(metrics.get("duration_ms", 0.0))
    rms = float(metrics.get("rms_dbfs", -100.0))
    active = float(metrics.get("active_ratio", 0.0))
    similarity = float(row["speaker_similarity"])
    duration_score = max(0.0, 1.0 - abs(duration - 4000.0) / 4000.0)
    level_score = max(0.0, 1.0 - abs(rms + 24.0) / 24.0)
    return (
        0.55 * similarity + 0.20 * duration_score + 0.15 * active + 0.10 * level_score
    )


def select_duration(
    rows: list[dict[str, object]], target_ms: float
) -> list[dict[str, object]]:
    chosen = []
    seen_text = set()
    total = 0.0
    for row in sorted(rows, key=quality_score, reverse=True):
        text_hash = hashlib.sha256(str(row["text"]).encode("utf-8")).hexdigest()
        if text_hash in seen_text:
            continue
        chosen.append(row)
        seen_text.add(text_hash)
        total += float(row["metrics"]["duration_ms"])
        if total >= target_ms:
            break
    return chosen


def write_private_outputs(
    output: Path, raw_dir: Path, speaker_id: str, selected: list[dict[str, object]]
) -> dict[str, str]:
    private_path = output / "selected-private.jsonl"
    private_rows = []
    for row in selected:
        private_rows.append(
            {
                "format": FORMAT,
                "audio_file": row["audio_file"],
                "sha256": row["sha256"],
                "split": row["split"],
                "text": row["text"],
                "speaker_similarity": round(float(row["speaker_similarity"]), 6),
                "pitch_hz": row.get("pitch_hz"),
                "metrics": row["metrics"],
            }
        )
    private_path.write_text(
        "".join(
            json.dumps(row, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
            + "\n"
            for row in private_rows
        ),
        encoding="utf-8",
    )

    hashes = {"private": sha256_file(private_path)}
    raw_root = raw_dir.resolve()
    for split in ("train", "eval", "all"):
        subset = (
            selected
            if split == "all"
            else [row for row in selected if row["split"] == split]
        )
        lines = [
            f"{raw_root / str(row['audio_file'])}|{speaker_id}|ZH|{row['text']}"
            for row in subset
        ]
        path = output / f"selected-{split}.list"
        path.write_text("\n".join(lines) + ("\n" if lines else ""), encoding="utf-8")
        hashes[split] = sha256_file(path)
    return hashes


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv if argv is not None else sys.argv[1:])
    worklog = json.loads(args.worklog.read_text(encoding="utf-8"))
    if worklog.get("consent", {}).get("revoked") is not False:
        raise SystemExit("consent is revoked or missing")
    if worklog.get("stages", {}).get("transcription") != "DRAFT_READY_REVIEW_REQUIRED":
        raise SystemExit("transcription draft is not ready")
    try:
        candidates = load_candidates(args.corpus_manifest, args.transcription_draft)
    except (OSError, ValueError) as error:
        raise SystemExit(str(error)) from error
    raw_dir = args.raw_dir.resolve()
    root = args.gpt_sovits_root.resolve()
    weight = root / "GPT_SoVITS/pretrained_models/sv/pretrained_eres2netv2w24s4ep4.ckpt"
    if not raw_dir.is_dir() or not root.is_dir() or not weight.is_file():
        raise SystemExit("raw corpus, GPT-SoVITS root, or speaker model is missing")

    output = args.out_dir.resolve()
    cache_path = output / "speaker-embeddings-private.jsonl"
    if output.exists() and not args.resume:
        raise SystemExit("output exists; pass --resume to continue")
    output.mkdir(parents=True, exist_ok=True)
    try:
        cached = load_embedding_cache(cache_path)
    except (OSError, ValueError) as error:
        raise SystemExit(str(error)) from error
    pending = [
        row
        for row in candidates
        if str(row["audio_file"]) not in cached
        or cached[str(row["audio_file"])].get("status") != "PASS"
    ]
    if args.limit is not None:
        pending = pending[: args.limit]

    if pending:
        try:
            torch_module, kaldi_module, model, device = build_model(
                root, weight, args.device
            )
        except (ImportError, OSError, RuntimeError) as error:
            raise SystemExit(f"speaker model initialization failed: {error}") from error
        with cache_path.open("a", encoding="utf-8") as cache:
            for index, row in enumerate(pending, 1):
                source = raw_dir / str(row["audio_file"])
                try:
                    embedding, pitch_hz = analyze_audio(
                        source, torch_module, kaldi_module, model, device
                    )
                    record = {
                        "audio_file": row["audio_file"],
                        "sha256": row["sha256"],
                        "embedding": embedding,
                        "pitch_hz": pitch_hz,
                        "status": "PASS",
                    }
                except Exception as error:  # Keep a long GPU audit resumable.
                    record = {
                        "audio_file": row["audio_file"],
                        "sha256": row["sha256"],
                        "embedding": [0.0] * 192,
                        "pitch_hz": None,
                        "status": "ERROR_" + type(error).__name__.upper(),
                    }
                cache.write(
                    json.dumps(record, sort_keys=True, separators=(",", ":")) + "\n"
                )
                cache.flush()
                cached[str(row["audio_file"])] = record
                if index == 1 or index % 25 == 0:
                    print(f"BKVOICE speaker progress={index}/{len(pending)}")

    complete_rows = [row for row in candidates if str(row["audio_file"]) in cached]
    errors = [
        row
        for row in complete_rows
        if cached[str(row["audio_file"])]["status"] != "PASS"
    ]
    complete = len(complete_rows) == len(candidates) and not errors
    if not complete or args.limit is not None:
        counts = {
            "expected": len(candidates),
            "processed": len(complete_rows),
            "errors": len(errors),
            "pending": len(candidates) - len(complete_rows),
        }
        print("BKVOICE_SPEAKER_AUDIT_PARTIAL " + json.dumps(counts, sort_keys=True))
        return 2 if errors else 0

    embeddings = [cached[str(row["audio_file"])]["embedding"] for row in candidates]
    scores, densities, lower_fence = speaker_scores(embeddings)
    threshold = max(args.min_speaker_similarity, lower_fence)
    for index, row in enumerate(candidates):
        row["speaker_similarity"] = float(scores[index])
        row["speaker_density"] = float(densities[index])
        row["pitch_hz"] = cached[str(row["audio_file"])].get("pitch_hz")
        row["speaker_match"] = float(scores[index]) >= threshold

    matched = [row for row in candidates if row["speaker_match"]]
    dominant_fraction = len(matched) / len(candidates)
    if dominant_fraction < args.min_dominant_fraction:
        raise SystemExit(
            f"dominant speaker fraction {dominant_fraction:.3f} is below required "
            f"{args.min_dominant_fraction:.3f}"
        )

    eligible = []
    for row in matched:
        duration = float(row.get("metrics", {}).get("duration_ms", 0.0))
        if (
            args.min_selection_duration_ms <= duration <= args.max_selection_duration_ms
            and text_is_suitable(str(row["text"]))
        ):
            eligible.append(row)
    train = select_duration(
        [row for row in eligible if row["split"] == "train"],
        args.target_train_minutes * 60000.0,
    )
    evaluation = select_duration(
        [row for row in eligible if row["split"] == "eval"],
        args.target_eval_minutes * 60000.0,
    )
    selected = train + evaluation
    train_ms = sum(float(row["metrics"]["duration_ms"]) for row in train)
    eval_ms = sum(float(row["metrics"]["duration_ms"]) for row in evaluation)
    if (
        train_ms < args.target_train_minutes * 60000.0
        or eval_ms < args.target_eval_minutes * 60000.0
    ):
        raise SystemExit(
            "eligible dominant-speaker corpus cannot meet requested duration"
        )

    hashes = write_private_outputs(
        output, raw_dir, str(worklog["speaker_id"]), selected
    )
    pitch_values = [
        float(row["pitch_hz"]) for row in matched if row.get("pitch_hz") is not None
    ]
    created = datetime.now(timezone.utc).replace(microsecond=0).isoformat()
    audit = {
        "format": FORMAT,
        "tool_version": TOOL_VERSION,
        "created_utc": created,
        "model_sha256": sha256_file(weight),
        "corpus_manifest_sha256": sha256_file(args.corpus_manifest),
        "transcription_draft_sha256": sha256_file(args.transcription_draft),
        "parameters": {
            "target_train_minutes": args.target_train_minutes,
            "target_eval_minutes": args.target_eval_minutes,
            "min_dominant_fraction": args.min_dominant_fraction,
            "min_speaker_similarity": args.min_speaker_similarity,
            "adaptive_similarity_lower_fence": round(lower_fence, 6),
            "effective_similarity_threshold": round(threshold, 6),
            "min_selection_duration_ms": args.min_selection_duration_ms,
            "max_selection_duration_ms": args.max_selection_duration_ms,
        },
        "counts": {
            "candidates": len(candidates),
            "speaker_match": len(matched),
            "speaker_outlier": len(candidates) - len(matched),
            "eligible": len(eligible),
            "selected_train": len(train),
            "selected_eval": len(evaluation),
        },
        "dominant_speaker_fraction": round(dominant_fraction, 6),
        "selected_train_duration_ms": round(train_ms, 3),
        "selected_eval_duration_ms": round(eval_ms, 3),
        "speaker_similarity": quantiles([float(value) for value in scores]),
        "speaker_density": quantiles([float(value) for value in densities]),
        "pitch_hz_secondary_diagnostic": quantiles(pitch_values),
        "private_artifact_sha256": hashes,
        "status": "PASS",
    }
    audit_path = output / "speaker-selection-audit.json"
    atomic_json(audit_path, audit)

    worklog["status"] = "DATASET_SELECTED"
    worklog["stages"]["asset_selection"] = "PASS"
    worklog["stages"]["speaker_verification"] = "PASS"
    for candidate in worklog.get("model_candidates", []):
        if candidate.get("id") == "gpt-sovits-finetune":
            candidate["status"] = "READY_TO_PREPROCESS"
    worklog["events"].append(
        {
            "created_utc": created,
            "event": "speaker_corpus_selected",
            "status": "PASS",
            "audit_sha256": sha256_file(audit_path),
            "candidate_count": len(candidates),
            "speaker_match_count": len(matched),
            "selected_train_count": len(train),
            "selected_eval_count": len(evaluation),
        }
    )
    atomic_json(args.worklog, worklog)
    print(
        "BKVOICE_SPEAKER_SELECTION_PASS " + json.dumps(audit["counts"], sort_keys=True)
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
