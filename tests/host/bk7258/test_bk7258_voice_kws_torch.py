# SPDX-License-Identifier: Apache-2.0
"""Cross-backend score/loss parity and real CUDA checkpoint continuation."""

import importlib.util
import json
from pathlib import Path

import numpy as np
import pytest


ROOT = Path(__file__).resolve().parents[3]


def module(name):
    spec = importlib.util.spec_from_file_location(
        name, ROOT / "tools/bk7258/_lib" / f"{name}.py"
    )
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


def test_acoustic_readout_is_causal_and_auxiliary_loss_excludes_wake_head():
    torch = pytest.importorskip("torch")
    backend = module("voice_kws_torch")
    torch.manual_seed(6)
    model = backend.build_model(torch, 4)
    with torch.no_grad():
        for parameter in model.parameters():
            parameter.fill_(0.05)
    x = torch.rand((2, 149, 40, 1))
    changed_future = x.clone()
    changed_future[:, 80:] += 10
    features = model.encode(x)
    torch.testing.assert_close(
        features[:, :, :80],
        model.encode(changed_future)[:, :, :80],
        rtol=0,
        atol=0,
    )
    # A teacher projection is a training-only consumer: it must update the
    # acoustic path without silently repurposing the three target labels.
    projection = torch.nn.Linear(4, 8)
    loss = (projection(features.mean(-1)) - torch.ones(2, 8)).square().mean()
    loss.backward()
    assert model.frequency.weight.grad.abs().sum() > 0
    assert model.classifier.weight.grad is None
    assert model.classifier.bias.grad is None
    assert model.logits(x).shape == (2, 149, 1, 3)


def test_auxiliary_encoder_transfer_preserves_fresh_target_head(tmp_path, monkeypatch):
    tf = pytest.importorskip("tensorflow")
    kws = module("voice_kws")
    tf.keras.utils.set_random_seed(100)
    source = kws._streaming_models(tf, 4)[3]
    checkpoint = tmp_path / "best.weights.h5"
    source.save_weights(str(checkpoint))
    model_path = tmp_path / "model_int8.tflite"
    model_path.write_bytes(b"test metadata binding")
    metadata = {
        "schema": kws.SCHEMA,
        "wake_label": "nihao_wenwen",
        "wake_phrase": "你好问问",
        "labels": ["silence", "unknown", "nihao_wenwen"],
        "model_sha256": kws._sha256(model_path),
        "checkpoint_sha256": kws._sha256(checkpoint),
        "dataset_sha256": "0" * 64,
        "frontend": kws.FRONTEND,
        "frontend_inputs_sha256": kws._frontend_provenance(),
        "architecture_family": kws.STREAM_ARCHITECTURE,
        "channels": 4,
        "model_authorization": {
            "status": "candidate",
            "training_data_authorization": "manifest_entry_consent_true",
        },
    }
    (tmp_path / "metadata.json").write_text(json.dumps(metadata))
    admitted = []

    def check_package(path, metadata_path, output):
        assert path == model_path and metadata_path == tmp_path / "metadata.json"
        admitted.append(path)
        return {"sha256": "1" * 64}

    # Real TFLite admission has its own frontend/quantization regression tests.
    # This fixture isolates cross-label checkpoint transfer from conversion.
    monkeypatch.setattr(kws, "package", check_package)
    tf.keras.utils.set_random_seed(200)
    target = kws._streaming_models(tf, 4)[3]
    initial_head = target.get_layer("classifier").get_weights()
    result = kws._initialize_encoder_weights(
        target, model_path, kws.FRONTEND, 4, "last"
    )
    assert result["source_labels"][-1] == "nihao_wenwen"
    assert admitted == [model_path]
    assert result["source_package_sha256"] == "1" * 64
    assert "classifier" not in result["copied_layers"]
    for layer in source.layers:
        if layer.name != "classifier" and layer.get_weights():
            for actual, expected in zip(
                target.get_layer(layer.name).get_weights(), layer.get_weights()
            ):
                np.testing.assert_array_equal(actual, expected)
    for actual, expected in zip(
        target.get_layer("classifier").get_weights(), initial_head
    ):
        np.testing.assert_array_equal(actual, expected)
    assert not np.array_equal(
        initial_head[0], source.get_layer("classifier").get_weights()[0]
    )
    before = target.get_weights()

    def reject_package(*args):
        raise kws.KwsError("model_frontend_binding_mismatch")

    monkeypatch.setattr(kws, "package", reject_package)
    with pytest.raises(kws.KwsError, match="model_frontend_binding_mismatch"):
        kws._initialize_encoder_weights(target, model_path, kws.FRONTEND, 4, "last")
    for actual, expected in zip(target.get_weights(), before):
        np.testing.assert_array_equal(actual, expected)
    checkpoint.write_bytes(b"changed checkpoint")
    with pytest.raises(kws.KwsError, match="initialization_contract_mismatch"):
        kws._initialize_encoder_weights(target, model_path, kws.FRONTEND, 4, "last")


def test_encoder_thaw_updates_only_the_active_layers():
    torch = pytest.importorskip("torch")
    backend = module("voice_kws_torch")
    torch.manual_seed(12)
    model = backend.build_model(torch, 4)
    # Keep the causal ReLU path active so every permitted weight has a gradient.
    with torch.no_grad():
        for parameter in model.parameters():
            parameter.fill_(0.05)
        model.classifier.weight[2].fill_(0.07)
    optimizer = torch.optim.Adam(model.parameters(), lr=0.001)
    x = torch.ones((2, 149, 40, 1))
    previous = {name: p.detach().clone() for name, p in model.named_parameters()}
    changed = []
    for epoch in range(3):
        backend.thaw_encoder(model, epoch, (1, 2))
        optimizer.zero_grad(set_to_none=True)
        loss = torch.nn.functional.cross_entropy(
            model.logits(x)[:, -1, 0], torch.tensor([2, 2])
        )
        loss.backward()
        optimizer.step()
        now = {name: p.detach().clone() for name, p in model.named_parameters()}
        changed.append(
            {name for name in now if not torch.equal(now[name], previous[name])}
        )
        previous = now
    assert changed[0] == {"classifier.weight", "classifier.bias"}
    assert changed[1] == changed[0] | {
        "depth.3.weight",
        "point.3.weight",
        "depth.4.weight",
        "point.4.weight",
    }
    assert changed[2] == set(previous)


def test_negative_confirmation_is_a_300ms_pair_and_keeps_bounded_gradients():
    torch = pytest.importorskip("torch")
    backend = module("voice_kws_torch")
    # Same number of high frames; only the final sequence contains a 300 ms pair.
    margin = torch.full((3, 149), -10.0)
    margin[0, 50] = 1000
    margin[1, 50:52] = 1000
    margin[2, [50, 65]] = 1000
    margin.requires_grad_()
    loss = backend.negative_confirmation_penalty(torch, margin)
    assert loss[0] == loss[1] == 0
    assert loss[2] > 0
    loss.sum().backward()
    assert torch.isfinite(loss).all() and torch.isfinite(margin.grad).all()
    assert margin.grad.abs().max() <= 1
    assert torch.count_nonzero(margin.grad[:2]) == 0
    assert margin.grad[2, 50] > 0 and margin.grad[2, 65] > 0


def test_positive_confirmation_requires_both_complete_endpoints_and_all_phases():
    torch = pytest.importorskip("torch")
    backend = module("voice_kws_torch")
    ce = torch.full((2, 149), 2.0, requires_grad=True)
    mask = torch.zeros((2, 149), dtype=torch.bool)
    mask[0, 80:110] = True  # exactly 15 eligible decision-phase pairs
    mask[1, 80:90] = True  # too short: use original loss, no imaginary earlier label
    fallback = torch.tensor([7.0, 9.0])
    loss = backend.positive_confirmation_loss(torch, ce, mask, fallback)
    torch.testing.assert_close(loss, torch.tensor([2.0, 9.0]))
    loss.sum().backward()
    assert torch.isfinite(ce.grad).all()
    assert torch.count_nonzero(ce.grad[0, :80]) == 0
    assert torch.count_nonzero(ce.grad[0, 110:]) == 0
    assert torch.count_nonzero(ce.grad[1]) == 0
    assert torch.all(ce.grad[0, 80:110] > 0)


def test_confirmation_objective_respects_history_completion_and_storage_padding():
    torch = pytest.importorskip("torch")
    backend = module("voice_kws_torch")
    packed = torch.tensor([[1, 48000, 149, 249], [2, 19680, 149, 249]])
    logits = torch.randn((2, 299, 1, 3), generator=torch.Generator().manual_seed(41))
    logits.requires_grad_()
    loss, _ = backend.frame_loss(
        torch,
        packed,
        logits.softmax(-1),
        1,
        1,
        "max",
        logits=logits,
        positive_reduction="max",
        positive_hold_ms=600,
        packed_labels=True,
        negative_pair_weight=0.5,
        positive_confirmation=True,
    )
    loss.sum().backward()
    assert torch.isfinite(loss).all() and torch.isfinite(logits.grad).all()
    assert torch.count_nonzero(logits.grad[:, 249:]) == 0
    assert torch.count_nonzero(logits.grad[0, :100]) == 0
    # Complete at source row 60; the existing 600 ms final deadline is row 190.
    assert torch.count_nonzero(logits.grad[1, :160]) == 0
    assert torch.count_nonzero(logits.grad[1, 191:]) == 0
    assert torch.count_nonzero(logits.grad[1, 160:191]) > 0


def test_confirmation_uses_worst_endpoint_best_pair_then_mean_phase():
    torch = pytest.importorskip("torch")
    backend = module("voice_kws_torch")
    ce = torch.full((1, 149), 2.0)
    ce[0, 30], ce[0, 45], ce[0, 60] = 8.0, 4.0, 1.0
    ce.requires_grad_()
    mask = torch.zeros((1, 149), dtype=torch.bool)
    mask[:, 30:75] = True
    loss = backend.positive_confirmation_loss(torch, ce, mask, torch.tensor([99.0]))
    # Phase 0 chooses max(4,1)=4 over max(8,4)=8. Other 14 phases cost 2.
    torch.testing.assert_close(loss, torch.tensor([(4 + 14 * 2) / 15]))
    loss.sum().backward()
    assert ce.grad[0, 30] == 0 and ce.grad[0, 60] == 0
    assert ce.grad[0, 45] == pytest.approx(1 / 15)


def test_confirmation_disabled_retains_exact_legacy_loss_and_extremes_are_finite():
    torch = pytest.importorskip("torch")
    backend = module("voice_kws_torch")
    packed = torch.tensor([[1, 48000, 149, 249], [2, 19680, 149, 249]])
    logits = torch.zeros((2, 299, 1, 3))
    logits[:, :, :, 2] = 1000
    logits.requires_grad_()
    args = dict(logits=logits, positive_reduction="max", positive_hold_ms=600)
    original, _ = backend.frame_loss(
        torch, packed, logits.softmax(-1), 1, 1, "max", **args
    )
    off, _ = backend.frame_loss(
        torch,
        packed,
        logits.softmax(-1),
        1,
        1,
        "max",
        **args,
        negative_pair_weight=0,
        positive_confirmation=False,
    )
    torch.testing.assert_close(original, off, rtol=0, atol=0)
    actual, _ = backend.frame_loss(
        torch,
        packed,
        logits.softmax(-1),
        1,
        1,
        "max",
        **args,
        negative_pair_weight=0.5,
        positive_confirmation=True,
    )
    actual.sum().backward()
    assert torch.isfinite(actual).all() and torch.isfinite(logits.grad).all()
    assert logits.grad.abs().max() <= 3


def test_short_long_readout_preserves_initial_predictor_and_causal_states():
    """A shorter readout must add useful gradients without changing initialization.

    This checks full/step/Torch equivalence and unchanged cache shapes before
    spending a training run on the receptive-field hypothesis.
    """
    tf = pytest.importorskip("tensorflow")
    torch = pytest.importorskip("torch")
    kws, backend = module("voice_kws"), module("voice_kws_torch")
    tf.keras.utils.set_random_seed(91)
    old = kws._streaming_models(tf, 4)
    new = kws._streaming_models(tf, 4, "short-long")
    kws._initialize_short_long_readout(old[3], new[3])
    values = np.random.default_rng(43).normal(size=(1, 299, 40, 1)).astype(np.float32)
    np.testing.assert_allclose(new[3](values), old[3](values), atol=1e-7)
    assert [t.shape for t in old[1].inputs] == [t.shape for t in new[1].inputs]
    assert new[3].count_params() - old[3].count_params() == 4 * 3
    assert kws._streaming_equivalence(new, values[0], np) < 1e-6
    model = backend.build_model(torch, 4, readout="short-long")
    backend.copy_from_keras(torch, model, new[3])
    inputs = torch.tensor(values)
    np.testing.assert_allclose(model(inputs).detach(), new[3](values), atol=1e-6)
    loss = -model(inputs)[0, -1, 0, 2].log()
    loss.backward()
    short_gradient = model.classifier.weight.grad[:, :4]
    assert torch.isfinite(short_gradient).all() and short_gradient.abs().sum() > 0
    with torch.no_grad():
        model.classifier.weight[:, :4] -= 0.01 * short_gradient
    backend.copy_to_keras(model, new[3])
    np.testing.assert_allclose(model(inputs).detach(), new[3](values), atol=1e-6)
    assert kws._streaming_equivalence(new, values[0], np) < 1e-6


@pytest.mark.parametrize("positive_hold_ms", [0, 600, 800])
def test_causal_graph_roundtrip_and_masked_loss_gradients(positive_hold_ms):
    tf = pytest.importorskip("tensorflow")
    torch = pytest.importorskip("torch")
    kws, backend = module("voice_kws"), module("voice_kws_torch")
    tf.keras.utils.set_random_seed(73)
    keras = kws._streaming_models(tf, 4)[3]
    model = backend.build_model(torch, 4)
    backend.copy_from_keras(torch, model, keras)
    values = np.random.default_rng(13).normal(size=(2, 249, 40, 1)).astype(np.float32)
    actual = model(torch.tensor(values)).detach().numpy()
    expected = keras(values).numpy()
    np.testing.assert_allclose(actual, expected, atol=1e-6)
    backend.copy_to_keras(model, keras)
    np.testing.assert_allclose(keras(values).numpy(), expected, atol=1e-6)
    packed = np.array([[1, 48000, 149, 249], [2, 19680, 149, 249]], np.int32)
    for reduction in ("mean", "max"):
        # Break frame ties so both autodiff backends select the same max frame.
        scores = (
            np.random.default_rng(8).uniform(0.01, 1, (2, 299, 1, 3)).astype(np.float32)
        )
        scores /= scores.sum(-1, keepdims=True)
        a = tf.Variable(scores)
        with tf.GradientTape() as tape:
            tf_loss = kws._streaming_negative_frame_loss(
                tf, 1, 1, reduction, positive_hold_ms=positive_hold_ms
            )(packed, a)
            objective = tf.reduce_sum(tf_loss)
        grad = tape.gradient(objective, a).numpy()
        b = torch.tensor(scores, requires_grad=True)
        loss, _ = backend.frame_loss(
            torch,
            torch.tensor(packed),
            b,
            1,
            1,
            reduction,
            positive_hold_ms=positive_hold_ms,
        )
        loss.sum().backward()
        np.testing.assert_allclose(loss.detach().numpy(), tf_loss.numpy(), atol=1e-6)
        # Keras sparse CE normalizes probabilities, so compare tangent gradients
        # through softmax rather than unconstrained probability coordinates.
        ga = scores * (grad - (grad * scores).sum(-1, keepdims=True))
        gb = scores * (
            b.grad.numpy() - (b.grad.numpy() * scores).sum(-1, keepdims=True)
        )
        np.testing.assert_allclose(ga, gb, atol=2e-6)
        np.testing.assert_array_equal(b.grad.numpy()[:, 249:], 0)
        np.testing.assert_array_equal(b.grad.numpy()[0, :100], 0)
        if positive_hold_ms:
            # Word ends at 1230 ms. Only the configured fresh interval has
            # gradients; later padding/tail must never acquire positive loss.
            last = 100 + (19680 + positive_hold_ms * 16 - 480) // 320
            np.testing.assert_array_equal(b.grad.numpy()[1, last + 1 :], 0)
            assert np.any(b.grad.numpy()[1, 160 : last + 1])


def test_feature_cache_reuses_only_matching_complete_content(tmp_path, monkeypatch):
    kws = module("voice_kws")
    calls = []

    def extract(records, numpy, *, path, **kwargs):
        calls.append(True)
        values = numpy.lib.format.open_memmap(
            path, mode="w+", dtype="float32", shape=(2, 149, 40, 1)
        )
        values[:] = 0.25
        return values

    monkeypatch.setattr(kws, "_features", extract)
    contract = {"manifest": "fixed", "recipe": "fixed"}
    cache = tmp_path / "cache"
    first = kws._cached_training_features(
        [], np, cache, contract, frontend="v1", streaming=False
    )
    second = kws._cached_training_features(
        [], np, cache, contract, frontend="v1", streaming=False
    )
    np.testing.assert_array_equal(first, second)
    assert len(calls) == 1
    with pytest.raises(kws.KwsError, match="feature_cache_contract_or_hash_mismatch"):
        kws._cached_training_features(
            [], np, cache, {"manifest": "different"}, frontend="v1", streaming=False
        )
    with (cache / "features.npy").open("r+b") as stream:
        stream.seek(-1, 2)
        stream.write(b"x")
    with pytest.raises(kws.KwsError, match="feature_cache_contract_or_hash_mismatch"):
        kws._cached_training_features(
            [], np, cache, contract, frontend="v1", streaming=False
        )


def test_cuda_cli_rejects_incompatible_graph_and_resume(tmp_path):
    kws = module("voice_kws")
    for options in (
        {"training_backend": "torch-cuda"},
        {"training_backend": "invalid"},
        {"resume_checkpoint": tmp_path / "resume.pt"},
    ):
        with pytest.raises(kws.KwsError, match="training_arguments_invalid"):
            kws.train(
                tmp_path / "missing.json",
                tmp_path / "out",
                epochs=1,
                batch_size=2,
                seed=1,
                **options,
            )


@pytest.mark.parametrize("state_scales", [None, [0.01] * 5])
@pytest.mark.parametrize("classifier_only", [False, True])
def test_cuda_resume_matches_uninterrupted_training_and_rejects_changed_data(
    tmp_path, state_scales, classifier_only
):
    tf = pytest.importorskip("tensorflow")
    torch = pytest.importorskip("torch")
    if not torch.cuda.is_available():
        pytest.skip("CUDA device unavailable")
    kws, backend = module("voice_kws"), module("voice_kws_torch")
    features = np.random.default_rng(9).normal(size=(6, 249, 40, 1)).astype(np.float32)
    targets = np.array([0, 1, 2, 0, 1, 2], np.int32)
    weights = np.ones(6, np.float32)

    def fit(name, epochs, resume=None, data=features):
        output = tmp_path / name
        output.mkdir()
        tf.keras.utils.set_random_seed(41)
        model = kws._streaming_models(tf, 4)[3]
        before = {
            layer.name: layer.get_weights()
            for layer in model.layers
            if layer.name != "classifier"
        }
        result = backend.fit_streaming(
            model,
            data,
            targets,
            weights,
            np.arange(4),
            np.arange(4, 6),
            channels=4,
            epochs=epochs,
            batch_size=2,
            seed=41,
            negative_weight=1,
            positive_weight=0,
            reduction="max",
            output=output,
            resume=resume,
            state_scales=state_scales,
            classifier_only=classifier_only,
        )
        if classifier_only:
            for layer in model.layers:
                if layer.name == "classifier":
                    continue
                for original, after in zip(before[layer.name], layer.get_weights()):
                    np.testing.assert_array_equal(original, after)
        return result

    first, _ = fit("first", 2)
    resumed, _ = fit("resumed", 3, tmp_path / "first/resume-last.pt")
    full, _ = fit("full", 3)
    assert len(first.history["loss"]) == 2
    for key in full.history:
        np.testing.assert_allclose(resumed.history[key], full.history[key], atol=1e-7)
    a = torch.load(tmp_path / "resumed/resume-last.pt", weights_only=True)
    b = torch.load(tmp_path / "full/resume-last.pt", weights_only=True)
    for key in a["model"]:
        torch.testing.assert_close(a["model"][key], b["model"][key], rtol=0, atol=0)
    with pytest.raises(RuntimeError, match="kws_resume_contract_mismatch"):
        fit("invalid", 4, tmp_path / "first/resume-last.pt", features + 0.01)


def test_block_shuffle_covers_each_row_and_bounds_locality():
    backend = module("voice_kws_torch")
    rows = np.arange(15000)[::2]
    a = backend.block_shuffled_indices(rows, 42, 1024)
    np.testing.assert_array_equal(np.sort(a), rows)
    np.testing.assert_array_equal(a, backend.block_shuffled_indices(rows, 42, 1024))
    assert not np.array_equal(a, backend.block_shuffled_indices(rows, 43, 1024))
    blocks = a // 1024
    assert len(np.flatnonzero(np.diff(blocks))) > len(np.unique(blocks)) * 2
    for block in np.unique(blocks):
        positions = np.flatnonzero(blocks == block)
        assert positions[-1] - positions[0] < 8 * 512


def test_bounded_derivatives_preserve_source_class_and_reject_heldout():
    kws = module("voice_kws")
    rows = [
        dict(source_id=str(source), label=label, split="train", ordinal=i)
        for source in range(3)
        for label in ("unknown", "nihao_openvela")
        for i in range(20)
    ]
    result = kws._bounded_derivatives(rows, 2, 9)
    assert len(result) == 12
    assert result == kws._bounded_derivatives(rows, 2, 9)
    assert {(r["source_id"], r["label"]) for r in result} == {
        (r["source_id"], r["label"]) for r in rows
    }
    with pytest.raises(kws.KwsError, match="derived_heldout_record"):
        kws._bounded_derivatives([dict(rows[0], split="validation")], 2, 9)


def test_cold_mix_keeps_original_cold_and_real_continuous_history():
    kws = module("voice_kws")
    pcm = bytes(96000)
    base = dict(source_id="source", label="nihao_openvela", split="train", pcm=pcm)
    real = dict(
        base, label="unknown", window_offset_ms=2000, frontend_warmup_pcm=bytes(64000)
    )
    held = dict(base, split="validation")
    rows = [base, real, held] + [
        dict(base, source_id=f"source-{i}", augmentation="rolling") for i in range(32)
    ]
    background = [dict(base, label="unknown")]
    kws._assign_frontend_history(rows, background, 2000, 7, True, True)
    assert "frontend_warmup_pcm" not in base and "frontend_warmup_pcm" not in held
    assert len(real["frontend_warmup_pcm"]) == 64000
    count = sum("frontend_warmup_pcm" in r for r in rows[3:])
    assert 0 < count < 32


def test_stable_loss_recovers_saturated_wrong_predictions():
    torch = pytest.importorskip("torch")
    backend = module("voice_kws_torch")
    logits = torch.tensor([[[[100.0, -100.0, -100.0]]] * 149], requires_grad=True)
    target = torch.tensor([[2, 16000, 149, 149]])
    loss, _ = backend.frame_loss(
        torch, target, logits.softmax(-1), 1, 1, "max", logits=logits
    )
    loss.sum().backward()
    assert torch.isfinite(loss).all() and loss.item() > 100
    assert logits.grad[..., 2].sum() < -0.9
    rng = torch.Generator().manual_seed(9)
    data = torch.randn((2, 149, 1, 3), generator=rng)
    targets = torch.tensor([[1, 48000, 149, 149], [2, 16000, 149, 149]])
    a, _ = backend.frame_loss(torch, targets, data.softmax(-1), 1, 1, "max")
    b, _ = backend.frame_loss(
        torch, targets, data.softmax(-1), 1, 1, "max", logits=data
    )
    torch.testing.assert_close(a, b)


def test_spectral_augmentation_keeps_padding_and_is_seeded():
    torch = pytest.importorskip("torch")
    backend = module("voice_kws_torch")
    x = torch.ones((16, 249, 40, 1))
    x[:, 149:] = 0
    a = backend.augment_spectrum(torch, x, torch.Generator().manual_seed(8))
    b = backend.augment_spectrum(torch, x, torch.Generator().manual_seed(8))
    assert torch.equal(a, b) and not torch.equal(a, x)
    assert not a[:, 149:].count_nonzero()


def test_parallel_tempo_preserves_serial_audio_order_and_split():
    kws = module("voice_kws")
    pcm = (np.sin(np.arange(48000) * 0.1) * 1000).astype("<i2").tobytes()
    rows = [
        dict(source_id=f"s{i}", label=kws.DEFAULT_WAKE_LABEL, split="train", pcm=pcm)
        for i in range(2)
    ]
    held = dict(rows[0], source_id="held", split="validation")
    expected, counts = kws._tempo_copies_serial(rows, kws.DEFAULT_WAKE_LABEL)
    actual, observed = kws._tempo_copies(rows + [held], kws.DEFAULT_WAKE_LABEL)
    assert counts == observed
    assert [(r["source_id"], r["tempo_factor"], r["pcm"]) for r in actual] == [
        (r["source_id"], r["tempo_factor"], r["pcm"]) for r in expected
    ]


def test_tempo_keeps_quiet_tail_and_completion_supervision():
    kws = module("voice_kws")
    pcm = np.zeros(48000, dtype="<i2")
    pcm[8000:24000] = (np.sin(np.arange(16000) * 0.1) * 1000).astype("<i2")
    # A low energy tail outside the RMS crop still belongs to the source.
    pcm[40000:46000] = 1
    raw = pcm.tobytes()
    row = dict(source_id="tail", label=kws.DEFAULT_WAKE_LABEL, split="train",
               pcm=raw, complete_pcm_span=kws._complete_pcm_span(raw))
    variants, counts = kws._tempo_copies_serial([row], kws.DEFAULT_WAKE_LABEL, 5000)
    for variant in variants:
        rate = variant["tempo_factor"]
        assert counts[f"completion_supervised:{rate}"] == 1
        end = variant["complete_pcm_span"][1]
        # The 2.4 s crop retains the quiet tail; the old 1.2 s crop cannot.
        assert end > 32000 / rate
        assert end <= len(variant["pcm"]) // 2
        assert variant["source_id"] == "tail"
        assert variant["split"] == "train"
        if rate == 1.5:
            labels = np.array([[2, end, 149, 249]], np.int32)
            torch = pytest.importorskip("torch")
            backend = module("voice_kws_torch")
            scores = torch.full((1, 249, 1, 3), 1 / 3, requires_grad=True)
            loss, _ = backend.frame_loss(torch, torch.tensor(labels), scores, 1, 1, "max")
            loss.sum().backward()
            assert scores.grad[0, 100:248, 0, 2].abs().sum() > 0
            assert scores.grad[0, :100, 0, 2].abs().sum() == 0


def test_hard_positive_loss_focuses_completed_frames_only():
    tf = pytest.importorskip("tensorflow")
    torch = pytest.importorskip("torch")
    kws, backend = module("voice_kws"), module("voice_kws_torch")
    rng = np.random.default_rng(52)
    values = rng.uniform(0.1, 1, (2, 249, 1, 3)).astype(np.float32)
    values /= values.sum(-1, keepdims=True)
    labels = np.array([[2, 16000, 149, 249], [1, 48000, 149, 249]], np.int32)
    a = tf.Variable(values)
    with tf.GradientTape() as tape:
        expected = kws._streaming_negative_frame_loss(tf, 1, 1, "max", "max")(labels, a)
        total = tf.reduce_sum(expected)
    grad = tape.gradient(total, a).numpy()
    b = torch.tensor(values, requires_grad=True)
    actual, _ = backend.frame_loss(
        torch, torch.tensor(labels), b, 1, 1, "max", positive_reduction="max"
    )
    actual.sum().backward()
    np.testing.assert_allclose(actual.detach().numpy(), expected.numpy(), atol=1e-6)
    # No positive loss may teach a wake before the complete waveform ends.
    assert np.count_nonzero(b.grad.numpy()[0, :149]) == 0
    mean, _ = backend.frame_loss(torch, torch.tensor(labels), b, 1, 1, "max")
    assert actual[0] >= mean[0]


def test_state_fake_quantization_keeps_export_float_graph_and_gradients():
    torch = pytest.importorskip("torch")
    backend = module("voice_kws_torch")
    model = backend.build_model(torch, 4, state_scales=[0.25] * 5)
    values = torch.tensor([0.1, 0.3, 0.6], requires_grad=True)
    quantized = model.state_value(values, 0)
    torch.testing.assert_close(quantized, torch.tensor([0.0, 0.25, 0.5]))
    quantized.sum().backward()
    torch.testing.assert_close(values.grad, torch.ones(3))
    plain = backend.build_model(torch, 4)
    plain.load_state_dict(model.state_dict())
    model.eval()
    plain.eval()
    x = torch.randn(2, 149, 40, 1)
    torch.testing.assert_close(model(x), plain(x), rtol=0, atol=0)


def test_speed_mass_is_not_changed_by_variant_multiplicity():
    kws = module("voice_kws")
    records = [
        dict(
            split="train", label=kws.DEFAULT_WAKE_LABEL, source_id="one", tempo_factor=t
        )
        for t in [0.75, 1, 1, 1.15, 1.25, 1.35, 1.5, 1.7]
    ]
    records += [
        dict(split="train", label="unknown", source_id="negative"),
        dict(split="validation", label=kws.DEFAULT_WAKE_LABEL, source_id="heldout"),
    ]
    weights = np.ones(len(records), np.float32)
    report = kws._balance_positive_speed_mass(
        records, weights, kws.DEFAULT_WAKE_LABEL, True
    )
    assert sum(weights[:8]) == pytest.approx(8)
    assert weights[0] == pytest.approx(sum(weights[1:3]))
    assert sum(weights[1:3]) == pytest.approx(sum(weights[3:8]))
    np.testing.assert_array_equal(weights[8:], 1)
    assert report["before_mass"] == {"slow": 1, "normal": 2, "fast": 5}
    assert report["after_mass"]["fast"] == pytest.approx(8 / 3)
    halfway = np.ones(len(records), np.float32)
    report = kws._balance_positive_speed_mass(
        records, halfway, kws.DEFAULT_WAKE_LABEL, True, 0.5
    )
    np.testing.assert_allclose(halfway, (weights + 1) / 2)
    assert report["interpolation_strength"] == 0.5


@pytest.mark.parametrize(
    "options",
    [
        {"streaming_negative_pair_weight": 0.5},
        {"streaming_negative_pair_weight": float("nan")},
        {"streaming_negative_pair_weight": 3},
        {"streaming_positive_confirmation": True},
        {"positive_speed_balance_strength": 0.5},
        {"positive_speed_balance": True, "positive_speed_balance_strength": 1.1},
    ],
)
def test_temporal_training_rejects_incompatible_arguments_before_data(
    tmp_path, options
):
    kws = module("voice_kws")
    with pytest.raises(kws.KwsError, match="training_arguments_invalid"):
        kws.train(
            tmp_path / "absent.json",
            tmp_path / "out",
            epochs=1,
            batch_size=2,
            seed=1,
            **options,
        )


def test_specaugment_keeps_coordinates_values_padding_and_determinism():
    torch = pytest.importorskip("torch")
    backend = module("voice_kws_torch")
    x = (
        torch.arange(12 * 180 * 40, dtype=torch.float32).reshape(12, 180, 40, 1) % 666
        + 1
    )
    lengths = torch.tensor([149, 169] * 6)
    before = x.clone()

    def augment():
        return backend.augment_spectrum(
            torch, x, torch.Generator().manual_seed(21), "specaugment", lengths
        )

    actual = augment()
    assert torch.equal(actual, augment()) and torch.equal(x, before)
    assert (actual == 0).any() and (actual != 0).any()
    assert torch.all((actual == 0) | (actual == before))
    for i, end in enumerate(lengths):
        assert torch.equal(actual[i, end:], before[i, end:])
    assert torch.equal(
        backend.augment_spectrum(torch, x, torch.Generator(), "none", lengths), x
    )


def test_final_only_target_loss_uses_real_end_not_storage_padding():
    tf = pytest.importorskip("tensorflow")
    torch = pytest.importorskip("torch")
    kws, backend = module("voice_kws"), module("voice_kws_torch")
    packed = np.array([[2, 19680, 149, 249], [1, 48000, 149, 249]], np.int32)
    values = np.random.default_rng(9).uniform(0.1, 1, (2, 349, 1, 3)).astype(np.float32)
    values /= values.sum(-1, keepdims=True)
    x = torch.tensor(values, requires_grad=True)
    loss, _ = backend.frame_loss(
        torch,
        torch.tensor(packed),
        x,
        1,
        0,
        "max",
        positive_hold_ms=600,
        packed_labels=True,
    )
    expected = kws._streaming_negative_frame_loss(
        tf, 1, 0, "max", positive_hold_ms=600, packed_labels=True
    )(packed, values)
    np.testing.assert_allclose(loss.detach().numpy(), expected.numpy(), atol=1e-6)
    loss.sum().backward()
    nonzero = np.flatnonzero(np.any(x.grad.numpy()[0] != 0, axis=(1, 2)))
    np.testing.assert_array_equal(nonzero, [190])
    np.testing.assert_array_equal(x.grad.numpy()[:, 249:], 0)
    np.testing.assert_array_equal(x.grad.numpy()[1, :100], 0)


def test_last_epoch_export_matches_checkpoint_state(tmp_path):
    tf = pytest.importorskip("tensorflow")
    torch = pytest.importorskip("torch")
    if not torch.cuda.is_available():
        pytest.skip("CUDA unavailable")
    kws, backend = module("voice_kws"), module("voice_kws_torch")
    tf.keras.utils.set_random_seed(21)
    model = kws._streaming_models(tf, 4)[3]
    features = np.random.default_rng(9).normal(size=(6, 149, 40, 1)).astype(np.float32)
    targets = np.array([0, 1, 2, 0, 1, 2], np.int32)
    backend.fit_streaming(
        model,
        features,
        targets,
        np.ones(6, np.float32),
        np.arange(4),
        np.arange(4, 6),
        channels=4,
        epochs=3,
        batch_size=2,
        seed=21,
        negative_weight=1,
        positive_weight=0,
        reduction="max",
        output=tmp_path,
        checkpoint_selection="last",
        feature_augmentation="specaugment",
    )
    checkpoint = torch.load(
        tmp_path / "resume-last.pt", map_location="cpu", weights_only=True
    )
    exported = backend.build_model(torch, 4)
    backend.copy_from_keras(torch, exported, model)
    for name, value in exported.state_dict().items():
        torch.testing.assert_close(value, checkpoint["model"][name], rtol=0, atol=0)
