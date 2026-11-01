# SPDX-License-Identifier: Apache-2.0
"""Mine only labelled training negatives through the existing C trigger path."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import wave

try:
    from . import voice_kws as kws
except ImportError:
    import voice_kws as kws


def training_sources(document):
    """Never reinterpret a held-out recording or a positive as a negative."""
    held = {e["source_id"] for e in document["entries"] if e["split"] != "train"}
    positive_sources = {
        e["source_id"] for e in document["entries"] if e["label"] not in kws.BASE_LABELS
    }
    positive_sources.update(
        s["source_id"] for s in document.get("sessions", []) if s.get("events")
    )
    result = {}
    for e in document["entries"]:
        if e["split"] == "train" and e["label"] == "unknown":
            if e["source_id"] in held:
                raise ValueError("mining_source_cross_split")
            if e["source_id"] in positive_sources:
                continue
            result.setdefault((e["source_id"], e["sha256"]), e)
    return sorted(result.values(), key=lambda e: (e["sha256"], e["source_id"]))


def read_audio(path):
    with wave.open(str(path), "rb") as f:
        if (f.getframerate(), f.getnchannels(), f.getsampwidth(), f.getcomptype()) != (
            16000,
            1,
            2,
            "NONE",
        ):
            raise ValueError("mining_pcm_format_invalid")
        return f.readframes(f.getnframes())


def write_audio(path, pcm):
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(16000)
        f.writeframes(pcm)


def staging_directory(destination):
    """Keep failed attempts for diagnosis; never expose partial final data."""
    if destination.exists():
        raise ValueError("output_must_be_new")
    for attempt in range(1, 4):
        path = destination.with_name(destination.name + f".attempt-{attempt}")
        if not path.exists():
            path.mkdir(parents=True)
            return path
    raise ValueError("data_preparation_attempts_exhausted")


class Detector:
    def __init__(self, model, wake_contract, frontend):
        import numpy as np
        import tensorflow as tf

        metadata, _ = kws._read_candidate_metadata(model, wake_contract, frontend)
        self.np, self.frontend = np, frontend
        self.interpreter = kws._evaluation_interpreter(tf, model_path=str(model))
        self.interpreter.allocate_tensors()
        self.streaming = metadata.get("architecture_family") == kws.STREAM_ARCHITECTURE
        self.incremental = (
            kws._StreamingInt8(self.interpreter, np) if self.streaming else None
        )
        self.library = kws._kws_library()
        self.policy = kws._policy_from_c(self.library)

    def scan(self, pcm, trace=None):
        lib, np, errors = self.library, self.np, []

        def infer(_, pointer, scores):
            try:
                x = np.ctypeslib.as_array(
                    pointer, shape=(40 if self.streaming else kws.FEATURES,)
                ).copy()
                values = (
                    self.incremental.step(x)
                    if self.incremental
                    else kws._predict_int8(
                        self.interpreter, x.reshape(kws.FEATURE_ROWS, 40, 1), np
                    )[1]
                )
                for i, value in enumerate(values):
                    scores[i] = float(value)
                if trace is not None:
                    trace.append((end_ms, *map(float, values)))
                return 0
            except Exception as exc:
                errors.append(exc)
                return -1

        callback = lib._callback_type(infer)
        reset = (
            lib._reset_type(lambda _: self.incremental.reset())
            if self.streaming
            else None
        )
        version = kws.FRONTEND_VERSIONS[self.frontend]
        host = (
            lib.bkvoice_kws_host_create_stream(callback, reset, None, version)
            if self.streaming
            else lib.bkvoice_kws_host_create_version(callback, None, version)
        )
        if not host:
            raise RuntimeError("mining_bridge_initialize_failed")
        events = []
        try:
            for offset in range(0, len(pcm) - 639, 640):
                frame = (ctypes.c_int16 * 320).from_buffer_copy(
                    pcm[offset : offset + 640]
                )
                score = ctypes.c_float()
                end_ms = offset // 32 + 20
                rc = lib.bkvoice_kws_host_feed(
                    host, frame, 320, end_ms, ctypes.byref(score)
                )
                if rc < 0:
                    raise RuntimeError("mining_inference_failed") from (
                        errors[0] if errors else None
                    )
                if rc == 1:
                    events.append((end_ms, float(score.value)))
        finally:
            lib.bkvoice_kws_host_destroy(host)
        return events


def copy_manifest(document, source_root, output):
    """Keep all held-out content and identities byte-for-byte; only prefix paths."""
    doc = json.loads(json.dumps(document))
    refs = list(doc["entries"])
    refs += [
        seg
        for session in doc.get("sessions", [])
        for seg in session["segments"]
        if seg["kind"] == "audio"
    ]
    for ref in refs:
        src = kws._safe_audio(source_root.resolve(), ref["path"])
        rel = Path("original") / ref["path"]
        dst = output / rel
        if not dst.exists():
            dst.parent.mkdir(parents=True, exist_ok=True)
            try:
                os.link(src, dst)
            except OSError:
                shutil.copy2(src, dst)
        if kws._sha256(dst) != ref["sha256"]:
            raise ValueError("mining_copy_hash_mismatch")
        ref["path"] = rel.as_posix()
    return doc


def event_window(pcm, end_ms):
    # Retain 2 seconds of preceding real context where available, then 3 seconds
    # ending at/after the false trigger. Padding is explicit at recording edges.
    start = max(0, end_ms - 3000)
    # The training frontend requires a full 2 s adjacent history whenever a
    # window offset is present. For early hits, move the source window forward
    # rather than emitting a 1..1999 ms warmup that audit accepts but train rejects.
    if start:
        start = max(2000, start)
    history = max(0, start - 2000)
    data = pcm[history * 32 : (start + 3000) * 32]
    data += bytes(max(0, (start + 3000 - history) * 32 - len(data)))
    return data, start - history


def mine(
    manifest, model, output, *, max_sources=2000, max_seconds=3600, max_windows=2000
):
    if output.exists() or min(max_sources, max_seconds, max_windows) <= 0:
        raise ValueError("mining_output_or_limit_invalid")
    document = kws._read_manifest(manifest)
    # Audit sessions too; a source in a held-out session cannot enter mining.
    records, _, wake_contract = kws._validate(manifest)
    kws._validate_sessions(document, manifest, records)
    held = {
        s["source_id"] for s in document.get("sessions", []) if s["split"] != "train"
    }
    detector = Detector(model, wake_contract, document["frontend"])
    selected, elapsed, scanned, triggers = [], 0.0, 0, 0
    for row in training_sources(document):
        if row["source_id"] in held:
            raise ValueError("mining_session_cross_split")
        if scanned >= max_sources or elapsed >= max_seconds:
            break
        pcm = read_audio(kws._safe_audio(manifest.parent.resolve(), row["path"]))
        remaining = int((max_seconds - elapsed) * 50) * 640
        pcm = pcm[:remaining]
        if not pcm:
            break
        events = detector.scan(pcm)
        elapsed += len(pcm) / 32000
        scanned += 1
        triggers += len(events)
        # Cap each original source at two windows; deduplicate overlapping hits.
        accepted = []
        for end, score in sorted(events, key=lambda x: (-x[1], x[0])):
            if any(abs(end - other) < 3000 for other in accepted):
                continue
            accepted.append(end)
            selected.append((score, row, end, event_window(pcm, end)))
            if len(accepted) == 2:
                break
        if scanned % 50 == 0:
            print(
                json.dumps(
                    dict(
                        stage="mining_train_only",
                        sources=scanned,
                        seconds=elapsed,
                        false_events=triggers,
                    )
                ),
                flush=True,
            )
    destination = output
    output = staging_directory(destination)
    doc = copy_manifest(document, manifest.parent, output)
    provenance, hashes = [], set()
    for score, row, end, (pcm, offset) in sorted(selected, key=lambda x: -x[0]):
        if len(provenance) >= max_windows:
            break
        digest = hashlib.sha256(pcm).hexdigest()
        if digest in hashes:
            continue
        hashes.add(digest)
        path = output / "mined" / f"{digest}.wav"
        write_audio(path, pcm)
        e = {
            k: row[k]
            for k in (
                "label",
                "split",
                "speaker",
                "source_id",
                "recording_kind",
                "consent",
            )
        }
        e.update(
            path=path.relative_to(output).as_posix(),
            sha256=kws._sha256(path),
            negative_category=row.get(
                "negative_category",
                (
                    row["source_id"].rsplit(":", 1)[-1]
                    if ":" in row["source_id"]
                    else row["label"]
                ),
            ),
            mining_origin="train_false_trigger",
        )
        if offset:
            e["window_offset_ms"] = offset
        doc["entries"].append(e)
        provenance.append(
            dict(
                source_id=row["source_id"],
                original_sha256=row["sha256"],
                trigger_end_ms=end,
                score=score,
                output_sha256=e["sha256"],
            )
        )
    result = dict(
        schema="kws-train-negative-mining-v1",
        source_split="train",
        input_manifest_sha256=kws._sha256(manifest),
        model_sha256=kws._sha256(model),
        policy=detector.policy,
        scanned_sources=scanned,
        scanned_seconds=elapsed,
        false_trigger_events=triggers,
        added_windows=len(provenance),
        windows=provenance,
        label_authority="existing audited unknown training labels; no automatic relabelling of positives",
        heldout_used_for_mining=False,
    )
    (output / "manifest.json").write_text(json.dumps(doc, indent=2) + "\n")
    kws.audit(output / "manifest.json")
    (output / "mining.json").write_text(json.dumps(result, indent=2) + "\n")
    output.rename(destination)
    return result


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("manifest", "model", "output"):
        p.add_argument("--" + name, type=Path, required=True)
    p.add_argument("--max-sources", type=int, default=2000)
    p.add_argument("--max-seconds", type=int, default=3600)
    p.add_argument("--max-windows", type=int, default=2000)
    a = p.parse_args()
    print(
        json.dumps(
            mine(
                a.manifest,
                a.model,
                a.output,
                max_sources=a.max_sources,
                max_seconds=a.max_seconds,
                max_windows=a.max_windows,
            ),
            indent=2,
        )
    )
