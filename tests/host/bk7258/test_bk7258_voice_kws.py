#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Synthetic contract checks for the local BKVoice KWS dataset auditor."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import tempfile
import wave
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("bkvoice_kws", ROOT / "tools/bk7258/_lib/voice_kws.py")
kws = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(kws)
LABELS = (*kws.BASE_LABELS, kws.DEFAULT_WAKE_LABEL)


def _wav(path: Path, value: int = 1, rate: int = 16000) -> str:
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as stream:
        stream.setnchannels(1)
        stream.setsampwidth(2)
        stream.setframerate(rate)
        stream.writeframes((value.to_bytes(2, "little", signed=True)) * kws.SAMPLES)
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _manifest(root: Path) -> Path:
    entries = []
    for index, (split, label) in enumerate(
            [(split, label) for split in kws.SPLITS for label in LABELS], 1):
            name = f"{split}-{label}.wav"
            entries.append({"path": name, "speaker": f"p-{split}-{label}", "split": split,
                            "label": label, "sha256": _wav(root / name, index), "consent": True,
                            "source_id": f"source-{index}", "recording_kind": "synthetic"})
    manifest = root / "dataset.json"
    manifest.write_text(json.dumps({"schema": kws.SCHEMA, "frontend": kws.FRONTEND,
                                    "labels": list(LABELS), "entries": entries}), encoding="utf-8")
    return manifest


def test_audit_returns_only_aggregate_data() -> None:
    with tempfile.TemporaryDirectory() as name:
        manifest = _manifest(Path(name))
        result = kws.audit(manifest)
        assert result["status"] == "candidate"
        assert result["counts"]["train"]["nihao_openvela"] == 1
        rendered = json.dumps(result)
        assert "p-train" not in rendered and ".wav" not in rendered


def test_frontend_provenance_hashes_upstream_inputs_without_absolute_paths() -> None:
    provenance = kws._frontend_provenance()
    assert any(name.endswith("/app/bk7258/bk7258_voice_kws_frontend.c")
               for name in provenance)
    assert ("apps/mlearning/tflite-micro/tflite-micro/tensorflow/lite/experimental/"
            "microfrontend/lib/frontend.c") in provenance
    assert ("apps/mlearning/tflite-micro/tflite-micro/tensorflow/lite/experimental/"
            "microfrontend/lib/kiss_fft_int16.cc") in provenance
    assert "apps/math/kissfft/kissfft/kiss_fft.c" in provenance
    assert "apps/math/kissfft/kissfft/tools/kiss_fftr.h" in provenance
    assert all(len(value) == 64 for value in provenance.values())
    assert all(not Path(name).is_absolute() for name in provenance)


def test_rejects_cross_split_speaker_source_and_hash() -> None:
    with tempfile.TemporaryDirectory() as name:
        root = Path(name)
        manifest = _manifest(root)
        document = json.loads(manifest.read_text(encoding="utf-8"))
        document["entries"][0]["speaker"] = document["entries"][3]["speaker"]
        manifest.write_text(json.dumps(document), encoding="utf-8")
        try:
            kws.audit(manifest)
        except kws.KwsError as error:
            assert str(error) == "speaker_cross_split"
        else:
            raise AssertionError("cross-split speaker accepted")
        document["entries"][0]["speaker"] = "fixed"
        document["entries"][3]["speaker"] = "other"
        original_source = document["entries"][3]["source_id"]
        document["entries"][3]["source_id"] = document["entries"][0]["source_id"]
        manifest.write_text(json.dumps(document), encoding="utf-8")
        try:
            kws.audit(manifest)
        except kws.KwsError as error:
            assert str(error) == "source_cross_split"
        else:
            raise AssertionError("cross-split source lineage accepted")
        document["entries"][3]["source_id"] = original_source
        document["entries"][3]["path"] = document["entries"][0]["path"]
        document["entries"][3]["sha256"] = document["entries"][0]["sha256"]
        manifest.write_text(json.dumps(document), encoding="utf-8")
        try:
            kws.audit(manifest)
        except kws.KwsError as error:
            assert str(error) == "audio_cross_split"
        else:
            raise AssertionError("cross-split audio accepted")


def test_rejects_escape_hash_mismatch_and_silent_positive() -> None:
    with tempfile.TemporaryDirectory() as name:
        root = Path(name)
        manifest = _manifest(root)
        document = json.loads(manifest.read_text(encoding="utf-8"))
        document["entries"][0]["path"] = "../outside.wav"
        manifest.write_text(json.dumps(document), encoding="utf-8")
        try:
            kws.audit(manifest)
        except kws.KwsError as error:
            assert str(error) == "entry_path_invalid"
        else:
            raise AssertionError("path traversal accepted")
        document = json.loads(_manifest(root).read_text(encoding="utf-8"))
        document["entries"][0]["sha256"] = "0" * 64
        manifest.write_text(json.dumps(document), encoding="utf-8")
        try:
            kws.audit(manifest)
        except kws.KwsError as error:
            assert str(error) == "entry_hash_mismatch"
        else:
            raise AssertionError("bad hash accepted")
        document = json.loads(_manifest(root).read_text(encoding="utf-8"))
        malformed = document["entries"][0]
        malformed["sha256"] = _wav(root / malformed["path"], 7, rate=8000)
        manifest.write_text(json.dumps(document), encoding="utf-8")
        try:
            kws.audit(manifest)
        except kws.KwsError as error:
            assert str(error) == "audio_format_invalid"
        else:
            raise AssertionError("wrong sample rate accepted")
        document = json.loads(_manifest(root).read_text(encoding="utf-8"))
        positive = next(entry for entry in document["entries"] if entry["label"] == "nihao_openvela")
        positive["sha256"] = _wav(root / positive["path"], 0)
        manifest.write_text(json.dumps(document), encoding="utf-8")
        try:
            kws.audit(manifest)
        except kws.KwsError as error:
            assert str(error) == "positive_audio_silent"
        else:
            raise AssertionError("silent positive accepted")


def test_bounded_negative_shifts_preserve_labels_lineage_and_split() -> None:
    with tempfile.TemporaryDirectory() as name:
        records, _, _ = kws._validate(_manifest(Path(name)))
        for record in records:
            if record["label"] == kws.DEFAULT_WAKE_LABEL:
                record["pcm"] = (1000).to_bytes(2, "little", signed=True) * kws.SAMPLES
        dense = kws._training_derivatives(records, kws.DEFAULT_WAKE_LABEL)
        bounded = kws._training_derivatives(records, kws.DEFAULT_WAKE_LABEL,
                                            unknown_shift_step_ms=400)
        shifts = [r for r in bounded if r.get("augmentation") == "ordinary_speech_zero_padded_shift"]
        assert len(shifts) == 8
        assert len([r for r in dense if r.get("augmentation") == "ordinary_speech_zero_padded_shift"]) == 32
        source = next(r for r in records if r["split"] == "train" and r["label"] == "unknown")
        for record in shifts:
            assert (record["split"], record["label"], record["source_id"], record["speaker"]) == (
                "train", "unknown", source["source_id"], source["speaker"])
            assert len(record["pcm"]) == kws.SAMPLES * 2
        # 两个端点保留完整的裁切/补零语义，不能变成循环移位。
        assert shifts[0]["pcm"] == source["pcm"][51200:] + bytes(51200)
        assert shifts[-1]["pcm"] == bytes(51200) + source["pcm"][:-51200]
        try:
            kws._training_derivatives(records, kws.DEFAULT_WAKE_LABEL, unknown_shift_step_ms=300)
        except kws.KwsError as error:
            assert str(error) == "unknown_shift_step_invalid"
        else:
            raise AssertionError("unbounded negative shift accepted")
