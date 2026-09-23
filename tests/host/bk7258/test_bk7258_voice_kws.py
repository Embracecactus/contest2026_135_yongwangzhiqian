#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Synthetic contract checks for the local BKVoice KWS dataset auditor."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import tempfile
import wave
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location(
    "bkvoice_kws", ROOT / "tools/bk7258/_lib/voice_kws.py"
)
kws = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(kws)
LABELS = (*kws.BASE_LABELS, kws.DEFAULT_WAKE_LABEL)


def test_tempo_retains_lineage_and_excludes_validation_and_overlong():
    import math
    import struct

    pcm = b"".join(
        struct.pack("<h", int(5000 * math.sin(i * 0.1))) for i in range(kws.SAMPLES)
    )
    original = dict(
        pcm=pcm,
        split="train",
        label=kws.DEFAULT_WAKE_LABEL,
        source_id="tempo-source",
        speaker="tempo-speaker",
    )
    records, counts = kws._tempo_copies(
        [original, {**original, "split": "validation"}, {**original, "split": "test"}],
        kws.DEFAULT_WAKE_LABEL,
    )
    assert counts["overlong:0.75"] == 1
    assert len(records) == 2
    assert {r["augmentation"] for r in records} == {"tempo:1.25", "tempo:1.5"}
    for record in records:
        assert len(record["pcm"]) == kws.SAMPLES * 2
        assert record["source_id"] == original["source_id"]
        assert record["speaker"] == original["speaker"]
        assert record["split"] == "train"
    assert original["pcm"] == pcm


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
        [(split, label) for split in kws.SPLITS for label in LABELS], 1
    ):
        name = f"{split}-{label}.wav"
        entries.append(
            {
                "path": name,
                "speaker": f"p-{split}-{label}",
                "split": split,
                "label": label,
                "sha256": _wav(root / name, index),
                "consent": True,
                "source_id": f"source-{index}",
                "recording_kind": "synthetic",
            }
        )
    manifest = root / "dataset.json"
    manifest.write_text(
        json.dumps(
            {
                "schema": kws.SCHEMA,
                "frontend": kws.FRONTEND,
                "labels": list(LABELS),
                "entries": entries,
            }
        ),
        encoding="utf-8",
    )
    return manifest


def test_audit_returns_only_aggregate_data() -> None:
    with tempfile.TemporaryDirectory() as name:
        manifest = _manifest(Path(name))
        result = kws.audit(manifest)
        assert result["status"] == "candidate"
        assert result["counts"]["train"]["nihao_openvela"] == 1
        rendered = json.dumps(result)
        assert "p-train" not in rendered and ".wav" not in rendered


def test_feature_warmup_is_versioned_bounded_and_not_returned() -> None:
    import numpy as np

    signal = (np.sin(np.arange(kws.SAMPLES) * 0.17) * 3000).astype("<i2")
    record = {"pcm": signal.tobytes()}
    warm = {**record, "frontend_warmup_pcm": signal[:16000].tobytes()}
    cold_v1 = kws._features([record], np)
    warm_v1 = kws._features([warm], np)
    assert np.array_equal(cold_v1, warm_v1)
    cold_v2 = kws._features([record], np, frontend=kws.FRONTEND_V2)
    warm_v2 = kws._features([warm], np, frontend=kws.FRONTEND_V2)
    assert cold_v2.shape == warm_v2.shape == (1, kws.FEATURE_ROWS, 40, 1)
    assert not np.array_equal(cold_v2, warm_v2)
    for prefix in (b"\0", bytes(320640)):
        try:
            kws._features([{**record, "frontend_warmup_pcm": prefix}], np)
        except kws.KwsError as error:
            assert str(error) == "frontend_warmup_invalid"
        else:
            raise AssertionError("invalid frontend history accepted")


def test_frontend_provenance_hashes_upstream_inputs_without_absolute_paths() -> None:
    provenance = kws._frontend_provenance()
    assert any(
        name.endswith("/app/bk7258/bk7258_voice_kws_frontend.c") for name in provenance
    )
    assert (
        "apps/mlearning/tflite-micro/tflite-micro/tensorflow/lite/experimental/"
        "microfrontend/lib/frontend.c"
    ) in provenance
    assert (
        "apps/mlearning/tflite-micro/tflite-micro/tensorflow/lite/experimental/"
        "microfrontend/lib/kiss_fft_int16.cc"
    ) in provenance
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
        positive = next(
            entry for entry in document["entries"] if entry["label"] == "nihao_openvela"
        )
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
        bounded = kws._training_derivatives(
            records, kws.DEFAULT_WAKE_LABEL, unknown_shift_step_ms=400
        )
        shifts = [
            r
            for r in bounded
            if r.get("augmentation") == "ordinary_speech_zero_padded_shift"
        ]
        assert len(shifts) == 8
        assert (
            len(
                [
                    r
                    for r in dense
                    if r.get("augmentation") == "ordinary_speech_zero_padded_shift"
                ]
            )
            == 32
        )
        source = next(
            r for r in records if r["split"] == "train" and r["label"] == "unknown"
        )
        for record in shifts:
            assert (
                record["split"],
                record["label"],
                record["source_id"],
                record["speaker"],
            ) == ("train", "unknown", source["source_id"], source["speaker"])
            assert len(record["pcm"]) == kws.SAMPLES * 2
        # Both endpoints keep their full truncate/zero-pad semantics; the
        # result must never be a circular shift.
        assert shifts[0]["pcm"] == source["pcm"][51200:] + bytes(51200)
        assert shifts[-1]["pcm"] == bytes(51200) + source["pcm"][:-51200]
        try:
            kws._training_derivatives(
                records, kws.DEFAULT_WAKE_LABEL, unknown_shift_step_ms=300
            )
        except kws.KwsError as error:
            assert str(error) == "unknown_shift_step_invalid"
        else:
            raise AssertionError("unbounded negative shift accepted")


def test_streaming_shared_weights_int8_state_and_reset(tmp_path) -> None:
    import os
    import numpy as np
    import pytest

    tf = pytest.importorskip("tensorflow")
    tf.keras.utils.set_random_seed(719)
    models = kws._streaming_models(tf, 32)
    rng = np.random.default_rng(719)
    # Positive internal weights keep the synthetic probe non-degenerate across
    # all five dilations; this is structural evidence, not a trained candidate.
    for layer in models[0].layers:
        weights = layer.get_weights()
        if weights:
            values = [rng.uniform(.01, .08, w.shape).astype(np.float32) for w in weights]
            if layer.name == "classifier":
                values = [rng.uniform(-.2, .2, w.shape).astype(np.float32) for w in weights]
            layer.set_weights(values)
    sequence = rng.uniform(0, 15, (333, 40, 1)).astype(np.float32)
    with tf.GradientTape() as tape:
        prediction = models[0](sequence[np.newaxis], training=True)
        loss = -tf.math.log(prediction[0, 2])
    gradients = tape.gradient(loss, models[0].trainable_weights)
    assert all(g is not None and np.all(np.isfinite(g.numpy())) for g in gradients)
    assert kws._streaming_equivalence(models, sequence, np) < 1e-5
    converter = tf.lite.TFLiteConverter.from_keras_model(models[1])
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.representative_dataset = lambda: kws._streaming_representative(
        models, sequence[np.newaxis], [0], np
    )
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter.inference_input_type = tf.int8
    converter.inference_output_type = tf.int8
    data = converter.convert()
    interpreter = tf.lite.Interpreter(model_content=data, num_threads=1)
    interpreter.allocate_tensors()
    runner = kws._StreamingInt8(interpreter, np)
    report = kws._streaming_contract_report(runner.contract)
    assert report["state_bytes"] == 7936
    expected = runner.sequence(sequence)
    assert np.ptp(expected[:, 0]) > .01
    runner.reset()
    actual = np.concatenate([runner.sequence(sequence[a:b], reset=False)
                             for a, b in [(0, 1), (1, 17), (17, 249), (249, 333)]])
    np.testing.assert_array_equal(actual, expected)
    np.testing.assert_array_equal(runner.sequence(sequence), expected)
    assert all(a.dtype == np.int8 for a in runner.states)
    # The existing native model fixture consumes these public synthetic bytes.
    output = Path(os.environ.get("BKVOICE_STREAM_PROBE_DIR", tmp_path))
    output.mkdir(parents=True, exist_ok=True)
    (output / "probe.tflite").write_bytes(data)
    sequence.astype("<f4").tofile(output / "features.f32")
    expected.astype("<f4").tofile(output / "scores.f32")
    metadata = {
        "schema": kws.SCHEMA,
        "wake_label": kws.DEFAULT_WAKE_LABEL,
        "wake_phrase": kws.DEFAULT_WAKE_PHRASE,
        "model_sha256": hashlib.sha256(data).hexdigest(),
        "frontend": kws.FRONTEND,
        "labels": list(LABELS),
        "architecture_family": kws.STREAM_ARCHITECTURE,
        "streaming_contract": report,
    }
    for name, detail in (("input", runner.contract["frame"]), ("output", runner.contract["score"])):
        metadata[name + "_shape"] = detail["shape"].tolist()
        metadata[name + "_quantization"] = list(detail["quantization"])
    metadata_path = tmp_path / "metadata.json"
    metadata_path.write_text(json.dumps(metadata))
    packaged = tmp_path / "probe.wkm"
    assert kws.package(output / "probe.tflite", metadata_path, packaged)["status"] == "candidate"
    assert packaged.read_bytes()[:4] == b"WKM1"
    metadata["streaming_contract"]["state_bytes"] += 1
    metadata_path.write_text(json.dumps(metadata))
    with pytest.raises(kws.KwsError, match="model_quantization_mismatch"):
        kws.package(output / "probe.tflite", metadata_path, tmp_path / "bad.wkm")
    metadata["frontend"] = kws.FRONTEND_V2
    metadata["streaming_contract"]["state_bytes"] -= 1
    metadata_path.write_text(json.dumps(metadata))
    with pytest.raises(kws.KwsError, match="model_frontend_binding_mismatch"):
        kws.package(output / "probe.tflite", metadata_path, tmp_path / "false-v2.wkm")
    migrated = tmp_path / "bound"
    migration = kws.bind_frontend(output / "probe.tflite", metadata_path, migrated)
    assert migration["computation_preserved"]
    assert (output / "probe.tflite").read_bytes() == data
    rebound = (migrated / "model_int8.tflite").read_bytes()
    assert kws._tflite_frontend(rebound) == kws.FRONTEND_V2
    bound_interpreter = tf.lite.Interpreter(model_content=rebound, num_threads=1)
    bound_interpreter.allocate_tensors()
    np.testing.assert_array_equal(kws._StreamingInt8(bound_interpreter, np).sequence(sequence), expected)
    with pytest.raises(kws.KwsError, match="model_frontend_already_bound"):
        kws._embed_tflite_frontend(rebound, kws.FRONTEND)
    v2 = tmp_path / "v2.wkm"
    assert kws.package(migrated / "model_int8.tflite", migrated / "metadata.json", v2)["status"] == "candidate"
    assert v2.read_bytes()[:4] == b"WKM2"
    assert v2.read_bytes()[136:140] == b"\x00\x00\x00\x02"
    assert v2.read_bytes()[140:] == rebound
    assert len(v2.read_bytes()) == len(rebound) + 140
    wrong = json.loads((migrated / "metadata.json").read_text())
    wrong["frontend"] = kws.FRONTEND
    metadata_path.write_text(json.dumps(wrong))
    with pytest.raises(kws.KwsError, match="model_frontend_binding_mismatch"):
        kws.package(migrated / "model_int8.tflite", metadata_path, tmp_path / "false-v1.wkm")
    metadata["frontend"] = "unknown-frontend"
    metadata_path.write_text(json.dumps(metadata))
    with pytest.raises(kws.KwsError, match="model_metadata_contract_invalid"):
        kws.package(output / "probe.tflite", metadata_path, tmp_path / "unknown.wkm")

    class InvalidState:
        def get_input_details(self):
            import copy
            details = copy.deepcopy(interpreter.get_input_details())
            for detail in details:
                if detail["shape"].tolist() == [1, 8, 1, 32]:
                    detail["shape"][1] = 7
            return details

        def get_output_details(self):
            return interpreter.get_output_details()

    with pytest.raises(kws.KwsError, match="streaming_state_shape_invalid"):
        kws._streaming_contract(InvalidState(), np)


def test_streaming_negative_frame_loss_masks_history_and_positive_timing(tmp_path) -> None:
    import numpy as np
    import pytest

    tf = pytest.importorskip("tensorflow")
    values = np.zeros((2, 249, 1, 3), dtype=np.float32)
    values[..., 0] = .1
    values[..., 1] = .8
    values[..., 2] = .1
    values[1, -1, 0] = [.05, .05, .9]
    labels = tf.constant([1, 2], tf.int32)
    loss = kws._streaming_negative_frame_loss(tf, 1.0)
    baseline = loss(labels, tf.constant(values)).numpy()
    final_only = tf.keras.losses.sparse_categorical_crossentropy(
        labels, tf.constant(values[:, -1, 0, :])
    ).numpy()
    assert baseline[0] > final_only[0]
    np.testing.assert_allclose(baseline[1], final_only[1], atol=1e-7)

    changed = values.copy()
    changed[0, 0, 0] = [.05, .05, .9]  # Synthetic history has no label here.
    changed[1, 150, 0] = [.05, .05, .9]  # Positive phrase end is unknown.
    np.testing.assert_allclose(loss(labels, tf.constant(changed)), baseline, atol=1e-7)
    changed[0, 100, 0] = [.05, .05, .9]  # First source-clip row is a known negative.
    updated = loss(labels, tf.constant(changed)).numpy()
    assert updated[0] > baseline[0]
    np.testing.assert_allclose(updated[1], baseline[1], atol=1e-7)
    with tf.GradientTape() as tape:
        predictions = tf.Variable(values)
        objective = tf.reduce_sum(loss(labels, predictions))
    gradients = tape.gradient(objective, predictions).numpy()
    assert gradients[0, 100, 0, 2] > 0
    np.testing.assert_array_equal(gradients[0, 0, 0], 0)
    np.testing.assert_array_equal(gradients[1, 150, 0], 0)

    tf.keras.utils.set_random_seed(59)
    models = kws._streaming_models(tf, 4)
    input_rows = tf.ones((2, 249, 40, 1), tf.float32)
    sequence = models[3](input_rows, training=False)
    np.testing.assert_allclose(
        sequence[:, -1, 0, :], models[0](input_rows, training=False), atol=1e-6
    )
    models[3].compile(optimizer="adam", loss=loss,
                      metrics=[kws._streaming_last_frame_accuracy(tf)])
    batch_loss = models[3].test_on_batch(input_rows, labels,
                                         sample_weight=np.array([2.0, .5], np.float32))
    assert np.all(np.isfinite(batch_loss))
    expected = loss(labels, sequence).numpy()
    np.testing.assert_allclose(batch_loss[0], (2 * expected[0] + .5 * expected[1]) / 2,
                               rtol=1e-5)
    weights = tmp_path / "sequence.weights.h5"
    models[3].save_weights(weights)
    restored = kws._streaming_models(tf, 4)[0]
    restored.load_weights(weights)
    np.testing.assert_allclose(restored(input_rows, training=False),
                               models[0](input_rows, training=False), atol=1e-6)


def test_streaming_negative_frame_option_defaults_off_and_rejects_other_architectures(tmp_path) -> None:
    import pytest

    parser = argparse.ArgumentParser()
    kws.add_arguments(parser.add_subparsers(dest="command", required=True))
    required = ["kws", "train", "--manifest", "missing.json", "--output", str(tmp_path / "new")]
    assert parser.parse_args(required).streaming_negative_frame_loss_weight == 0
    chosen = parser.parse_args(required + ["--architecture", kws.STREAM_ARCHITECTURE,
                                           "--streaming-negative-frame-loss-weight", "1"])
    assert chosen.streaming_negative_frame_loss_weight == 1
    with pytest.raises(kws.KwsError, match="training_arguments_invalid"):
        kws.train(tmp_path / "missing.json", tmp_path / "new", epochs=1, batch_size=1,
                  seed=1, architecture="ds-cnn", streaming_negative_frame_loss_weight=1)
    with pytest.raises(kws.KwsError, match="training_arguments_invalid"):
        kws.train(tmp_path / "missing.json", tmp_path / "new", epochs=1, batch_size=1,
                  seed=1, architecture=kws.STREAM_ARCHITECTURE,
                  streaming_negative_frame_loss_weight=float("nan"))


def test_lazy_derivatives_and_feature_memmap_do_not_retain_window_copies() -> None:
    import numpy as np

    class Frontend:
        @staticmethod
        def bkvoice_kws_features(_pcm, _samples, output, features):
            for index in range(features):
                output[index] = index
            return 0

    with tempfile.TemporaryDirectory() as name:
        records, _, _ = kws._validate(_manifest(Path(name)))
        for record in records:
            if record["label"] == kws.DEFAULT_WAKE_LABEL:
                record["pcm"] = (1000).to_bytes(2, "little", signed=True) * kws.SAMPLES
        eager = kws._training_derivatives(
            records, kws.DEFAULT_WAKE_LABEL, unknown_shift_step_ms=800
        )
        derived = kws._training_derivatives(
            records, kws.DEFAULT_WAKE_LABEL, unknown_shift_step_ms=800, lazy=True
        )
        assert derived and len(derived) == len(eager)
        assert all("pcm" not in record for record in derived)
        for old, lazy in zip(eager, derived):
            assert (
                lazy["split"], lazy["label"], lazy["source_id"], lazy["speaker"],
                lazy["augmentation"], kws._record_pcm(lazy)
            ) == (
                old["split"], old["label"], old["source_id"], old["speaker"],
                old["augmentation"], old["pcm"]
            )
        original = kws._frontend_library
        kws._frontend_library = lambda: Frontend()
        try:
            cache = Path(name) / "features.npy"
            features = kws._features(derived[:2], np, path=cache)
            assert isinstance(features, np.memmap)
            assert features.shape == (2, kws.FEATURE_ROWS, 40, 1)
            assert cache.is_file() and features[1, 148, 39, 0] == kws.FEATURES - 1
        finally:
            kws._frontend_library = original
