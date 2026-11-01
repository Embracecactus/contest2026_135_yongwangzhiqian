# SPDX-License-Identifier: Apache-2.0
"""Training-only mining, real-context windows and warm-start boundaries."""

import importlib.util
import json
from pathlib import Path
import sys

import pytest

LIB = Path(__file__).resolve().parents[3] / "tools/bk7258/_lib"
sys.path.insert(0, str(LIB))
import voice_kws_mining as mining


def fixture(root):
    root.mkdir()
    entries = []
    for i, (split, label) in enumerate(
        (s, l)
        for s in mining.kws.SPLITS
        for l in ("silence", "unknown", "nihao_openvela")
    ):
        p = root / f"{i}.wav"
        count = 80000 if split == "train" and label == "unknown" else 48000
        mining.write_audio(p, (i + 1).to_bytes(2, "little", signed=True) * count)
        row = dict(
            path=p.name,
            sha256=mining.kws._sha256(p),
            label=label,
            split=split,
            speaker=f"voice-{i}",
            source_id=f"source-{i}",
            recording_kind="real",
            consent=True,
        )
        if count == 80000:
            row["window_offset_ms"] = 2000
        entries.append(row)
    doc = dict(
        schema=mining.kws.SCHEMA,
        frontend=mining.kws.FRONTEND_V2,
        wake_label="nihao_openvela",
        wake_phrase="你好，openvela",
        labels=["silence", "unknown", "nihao_openvela"],
        entries=entries,
    )
    doc["sessions"] = [
        dict(
            session_id=f"session-{i}",
            source_id=entries[i]["source_id"],
            speaker=entries[i]["speaker"],
            split=entries[i]["split"],
            consent=True,
            recording_kind="real",
            timeline_start_ms=0,
            segments=[
                dict(
                    kind="audio",
                    start_ms=0,
                    path=entries[i]["path"],
                    sha256=entries[i]["sha256"],
                )
            ],
            events=[],
            session_end_ms=3000,
        )
        for i in (4, 7)
    ]
    p = root / "manifest.json"
    p.write_text(json.dumps(doc))
    return p, doc


def test_mining_keeps_heldout_and_only_adds_train_unknown(tmp_path, monkeypatch):
    manifest, doc = fixture(tmp_path / "source")
    model = tmp_path / "model.tflite"
    model.write_bytes(b"fixture")
    calls = []

    class Detector:
        policy = {"threshold": 0.6}

        def __init__(self, *args):
            pass

        def scan(self, pcm):
            calls.append(pcm)
            return [(4500, 0.9), (4600, 0.8)]

    monkeypatch.setattr(mining, "Detector", Detector)
    original_copy = mining.copy_manifest

    def interrupted_copy(*args):
        original_copy(*args)
        raise OSError("simulated interrupted preparation")

    monkeypatch.setattr(mining, "copy_manifest", interrupted_copy)
    with pytest.raises(OSError, match="interrupted"):
        mining.mine(manifest, model, tmp_path / "mined")
    assert not (tmp_path / "mined").exists()
    assert (tmp_path / "mined.attempt-1").is_dir()
    monkeypatch.setattr(mining, "copy_manifest", original_copy)
    result = mining.mine(manifest, model, tmp_path / "mined")
    assert len(calls) == 2 and result["added_windows"] == 1
    out = json.loads((tmp_path / "mined/manifest.json").read_text())
    for original, copied in zip(doc["entries"], out["entries"]):
        assert {k: v for k, v in original.items() if k != "path"} == {
            k: v for k, v in copied.items() if k != "path"
        }
    added = out["entries"][-1]
    assert added["split"] == "train" and added["label"] == "unknown"
    assert added["source_id"] == doc["entries"][1]["source_id"]
    assert added["window_offset_ms"] == 2000
    assert added["negative_category"] == "unknown"
    assert added["mining_origin"] == "train_false_trigger"
    assert result["heldout_used_for_mining"] is False
    records, _, _ = mining.kws._validate(tmp_path / "mined/manifest.json")
    for record in records:
        if "window_offset_ms" in record:
            assert len(mining.kws._continuous_history(record, 2000)) == 64000


def test_mining_rejects_cross_split_and_does_not_select_positive(tmp_path):
    _, doc = fixture(tmp_path / "source")
    rows = mining.training_sources(doc)
    assert len(rows) == 1 and rows[0]["label"] == "unknown"
    doc["entries"][4]["source_id"] = rows[0]["source_id"]
    with pytest.raises(ValueError, match="cross_split"):
        mining.training_sources(doc)


def test_event_window_preserves_adjacent_context_and_pads_edges():
    pcm = bytes(range(256)) * 1250
    chunk, offset = mining.event_window(pcm, 6500)
    assert offset == 2000 and chunk == pcm[1500 * 32 : 6500 * 32]
    chunk, offset = mining.event_window(pcm[:16000], 200)
    assert offset == 0 and len(chunk) == 96000 and chunk[:16000] == pcm[:16000]
    chunk, offset = mining.event_window(pcm, 4500)
    assert offset == 2000 and chunk == pcm[: 5000 * 32]


def test_warm_start_rejects_changed_checkpoint_before_loading(tmp_path, monkeypatch):
    kws = mining.kws
    model = tmp_path / "model_int8.tflite"
    model.write_bytes(b"model")
    weights = tmp_path / "best.weights.h5"
    weights.write_bytes(b"weights")
    metadata = dict(
        architecture_family=kws.STREAM_ARCHITECTURE,
        channels=4,
        checkpoint_sha256=kws._sha256(weights),
        dataset_sha256="training-source",
    )
    monkeypatch.setattr(
        kws,
        "_read_candidate_metadata",
        lambda *args: (metadata, tmp_path / "metadata.json"),
    )
    loaded = []

    class Model:
        def load_weights(self, path):
            loaded.append(path)

    result = kws._initialize_candidate_weights(Model(), model, (), kws.FRONTEND_V2, 4)
    assert len(loaded) == 1 and result["source_dataset_sha256"] == "training-source"
    weights.write_bytes(b"changed")
    with pytest.raises(kws.KwsError, match="initialization_contract_mismatch"):
        kws._initialize_candidate_weights(Model(), model, (), kws.FRONTEND_V2, 4)
    assert len(loaded) == 1


def test_recording_with_positive_region_is_never_mined_as_wholly_negative(tmp_path):
    _, doc = fixture(tmp_path / "source")
    doc["entries"][2]["source_id"] = doc["entries"][1]["source_id"]
    assert mining.training_sources(doc) == []
