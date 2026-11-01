# SPDX-License-Identifier: Apache-2.0
"""CUDA training for the existing causal KWS graph; Keras still owns export."""

from __future__ import annotations

import copy
import hashlib
import json
import os
import math
from pathlib import Path
from types import SimpleNamespace


# Product decision cadence: 15 feature hops (20 ms each), two confirmations.
# The CLI checks the C policy before enabling either training-only objective.
CONFIRMATION_HOPS = 15


def negative_confirmation_penalty(torch, margin):
    """Penalize the five hardest 300 ms pairs, above the release-score margin.

    A pair is only as strong as its weaker endpoint. Keep the existing max-frame
    loss as well: this term must not replace supervision of isolated spikes.
    Log odds avoid saturated probability clamps and keep gradients bounded.
    """
    paired = torch.minimum(
        margin[:, :-CONFIRMATION_HOPS], margin[:, CONFIRMATION_HOPS:]
    )
    excess = torch.relu(paired - math.log(0.2 / 0.8))
    return excess.topk(5, dim=1).values.mean(1)


def positive_confirmation_loss(torch, wake_ce, mask, fallback):
    """Best complete-word pair per decision phase, then average over phases.

    Both endpoints must lie after conservative waveform completion and before
    the existing fresh-label deadline. No inferred phoneme or word-end labels.
    All 15 phases are supervised because source onset is not clock aligned.
    """
    pair_mask = mask[:, :-CONFIRMATION_HOPS] & mask[:, CONFIRMATION_HOPS:]
    pair_loss = torch.maximum(
        wake_ce[:, :-CONFIRMATION_HOPS], wake_ce[:, CONFIRMATION_HOPS:]
    )
    losses, eligible = [], []
    for phase in range(CONFIRMATION_HOPS):
        valid = pair_mask[:, phase::CONFIRMATION_HOPS]
        values = pair_loss[:, phase::CONFIRMATION_HOPS]
        exists = valid.any(1)
        # Finite sentinel: all-invalid phases must have finite zero gradients.
        best = values.masked_fill(~valid, torch.finfo(values.dtype).max).min(1).values
        losses.append(torch.where(exists, best, 0.0))
        eligible.append(exists)
    count = torch.stack(eligible, 1).sum(1)
    aggregate = torch.stack(losses, 1).sum(1) / count.clamp(min=1)
    return torch.where(count > 0, aggregate, fallback)


def build_model(torch, channels, state_scales=None, readout="last"):
    """Match the existing Keras graph, including causal padding and activations."""

    if readout not in ("last", "short-long"):
        raise ValueError("kws_streaming_readout_invalid")

    class Streaming(torch.nn.Module):
        def __init__(self):
            super().__init__()
            self.frequency = torch.nn.Conv1d(40, channels, 1, bias=False)
            self.depth = torch.nn.ModuleList(
                torch.nn.Conv1d(
                    channels, channels, 9, groups=channels, dilation=2**i, bias=False
                )
                for i in range(5)
            )
            self.point = torch.nn.ModuleList(
                torch.nn.Conv1d(channels, channels, 1, bias=False) for _ in range(5)
            )
            self.classifier = torch.nn.Conv1d(
                channels * (2 if readout == "short-long" else 1), 3, 1
            )

        def state_value(self, values, index):
            if self.training and state_scales is not None:
                return torch.fake_quantize_per_tensor_affine(
                    values, float(state_scales[index]), 0, 0, 255
                )
            return values

        def encode(self, inputs):
            """Causal acoustic features for training-only auxiliary supervision."""
            values = self.state_value(
                torch.relu(self.frequency(inputs[..., 0].transpose(1, 2))), 0
            )
            for i, (depth, point) in enumerate(zip(self.depth, self.point)):
                values = torch.relu(
                    point(
                        torch.relu(
                            depth(torch.nn.functional.pad(values, (8 * 2**i, 0)))
                        )
                    )
                )
                if i == 2:
                    short = values
                if i < 4:
                    values = self.state_value(values, i + 1)
            if readout == "short-long":
                values = torch.cat((short, values), dim=1)
            return values

        def logits(self, inputs):
            return self.classifier(self.encode(inputs)).transpose(1, 2).unsqueeze(2)

        def forward(self, inputs):
            return self.logits(inputs).softmax(-1)

    return Streaming()


def copy_from_keras(torch, model, keras_model):
    with torch.no_grad():
        arrays = keras_model.get_layer("frequency").get_weights()
        model.frequency.weight.copy_(
            torch.as_tensor(arrays[0][0, :, 0, :].T[:, :, None])
        )
        for i in range(5):
            weight = keras_model.get_layer(f"causal_{i}").get_weights()[0]
            model.depth[i].weight.copy_(
                torch.as_tensor(weight[:, 0, :, 0].T[:, None, :])
            )
            weight = keras_model.get_layer(f"pointwise_{i}").get_weights()[0]
            model.point[i].weight.copy_(torch.as_tensor(weight[0, 0].T[:, :, None]))
        weight, bias = keras_model.get_layer("classifier").get_weights()
        model.classifier.weight.copy_(torch.as_tensor(weight[0, 0].T[:, :, None]))
        model.classifier.bias.copy_(torch.as_tensor(bias))


def copy_to_keras(model, keras_model):
    def array(value):
        return value.detach().cpu().numpy()

    keras_model.get_layer("frequency").set_weights(
        [array(model.frequency.weight)[:, :, 0].T[None, :, None, :]]
    )
    for i in range(5):
        keras_model.get_layer(f"causal_{i}").set_weights(
            [array(model.depth[i].weight)[:, 0, :].T[:, None, :, None]]
        )
        keras_model.get_layer(f"pointwise_{i}").set_weights(
            [array(model.point[i].weight)[:, :, 0].T[None, None, :, :]]
        )
    keras_model.get_layer("classifier").set_weights(
        [
            array(model.classifier.weight)[:, :, 0].T[None, None, :, :],
            array(model.classifier.bias),
        ]
    )


def frame_loss(
    torch,
    labels,
    sequence,
    negative_weight,
    positive_weight,
    reduction,
    logits=None,
    positive_reduction="mean",
    positive_hold_ms=0,
    packed_labels=False,
    negative_pair_weight=0.0,
    positive_confirmation=False,
):
    """Same masked objective as voice_kws, with no loss on history or padding."""
    packed_labels = packed_labels or positive_weight > 0
    if packed_labels:
        packed = labels.long().reshape(-1, 4)
        labels, complete, source_rows, valid_rows = packed.unbind(1)
        last_rows = valid_rows - 1
        if positive_hold_ms:
            fresh_rows = (
                valid_rows
                - source_rows
                + (complete + positive_hold_ms * 16 - 480) // 320
            )
            last_rows = torch.where(
                labels == 2, torch.minimum(last_rows, fresh_rows), last_rows
            )
        last = sequence[torch.arange(len(labels), device=labels.device), last_rows, 0]
        positions = valid_rows[:, None] - 149 + torch.arange(149, device=labels.device)
        wake = sequence[:, :, 0, 2].gather(1, positions)
    else:
        labels = labels.long().reshape(-1)
        last = sequence[:, -1, 0]
        wake = sequence[:, -149:, 0, 2]
    final = -last.clamp(1e-7, 1 - 1e-7).log().gather(1, labels[:, None]).squeeze(1)
    negative = -torch.log1p(-wake.clamp(0, 1 - 1e-7))
    if logits is not None:
        logp = logits.log_softmax(-1)
        if packed_labels:
            final_logp = logp[
                torch.arange(len(labels), device=labels.device), last_rows, 0
            ]
            rows = valid_rows[:, None] - 149 + torch.arange(149, device=labels.device)
            negative_all = torch.logsumexp(logits[:, :, 0], -1) - torch.logsumexp(
                logits[:, :, 0, :2], -1
            )
            negative = negative_all.gather(1, rows)
        else:
            final_logp = logp[:, -1, 0]
            negative = torch.logsumexp(logits[:, -149:, 0], -1) - torch.logsumexp(
                logits[:, -149:, 0, :2], -1
            )
        final = -final_logp.gather(1, labels[:, None]).squeeze(1)
    negative = negative.max(1).values if reduction == "max" else negative.mean(1)
    result = final + negative_weight * torch.where(labels == 2, 0.0, negative)
    if negative_pair_weight:
        if logits is None or not packed_labels:
            raise ValueError("kws_confirmation_requires_logits_and_time_labels")
        margin = logits[:, :, 0, 2] - torch.logsumexp(logits[:, :, 0, :2], -1)
        pair_penalty = negative_confirmation_penalty(torch, margin.gather(1, rows))
        result = result + negative_pair_weight * torch.where(
            labels == 2, 0.0, pair_penalty
        )
    if positive_weight:
        positions = torch.arange(sequence.shape[1], device=labels.device)[None, :]
        starts = (valid_rows - source_rows)[:, None]
        ends = 480 + (positions - starts) * 320
        mask = (
            (labels[:, None] == 2)
            & (positions >= starts)
            & (positions < last_rows[:, None])
            & (ends >= complete[:, None])
        )
        count = mask.sum(1)
        wake_log = (
            logp[:, :, 0, 2]
            if logits is not None
            else sequence[:, :, 0, 2].clamp(1e-7, 1).log()
        )
        positive = (
            (-wake_log * mask).max(1).values
            if positive_reduction == "max"
            else -(wake_log * mask).sum(1) / count.clamp(min=1)
        )
        if positive_confirmation:
            # Include the same valid final target in eligible pairs. The legacy
            # term excludes it because final CE already supervises that row.
            pair_mask = mask | (
                (labels[:, None] == 2)
                & (positions == last_rows[:, None])
                & (positions >= starts)
                & (ends >= complete[:, None])
            )
            positive = positive_confirmation_loss(torch, -wake_log, pair_mask, positive)
        result = torch.where(
            count > 0,
            (result + positive_weight * positive) / (1 + positive_weight),
            result,
        )
    return result, last.argmax(1) == labels


def augment_spectrum(torch, x, generator, mode="legacy", valid_rows=None):
    """Train-only frequency warp and band masking; keep all time rows and padding."""
    if mode == "none":
        return x
    if mode == "specaugment":
        # microWakeWord's verified training pattern: zero time/frequency
        # spans. Adapt to variable causal sequences: never shift labels,
        # rescale PCAN values, or put a mask in the storage-padding region.
        batch, rows, bands, _ = x.shape
        valid = (
            torch.full((batch, 1), rows, device=x.device, dtype=torch.long)
            if valid_rows is None
            else valid_rows.long().reshape(-1, 1)
        )
        time = torch.arange(rows, device=x.device)[None]
        frequency = torch.arange(bands, device=x.device)[None]
        mask = torch.zeros((batch, rows, bands, 1), device=x.device, dtype=torch.bool)
        enabled = torch.rand((batch, 1), device=x.device, generator=generator) < 0.5
        for _ in range(2):
            width = torch.randint(
                0, 5, (batch, 1), device=x.device, generator=generator
            )
            width = torch.minimum(width, valid)
            start = (
                torch.rand((batch, 1), device=x.device, generator=generator)
                * (valid - width + 1)
            ).long()
            mask |= ((time >= start) & (time < start + width) & enabled)[
                :, :, None, None
            ]
            width = torch.randint(
                0, 5, (batch, 1), device=x.device, generator=generator
            )
            start = (
                torch.rand((batch, 1), device=x.device, generator=generator)
                * (bands - width + 1)
            ).long()
            mask |= ((frequency >= start) & (frequency < start + width) & enabled)[
                :, None, :, None
            ] & (time < valid)[:, :, None, None]
        return x.masked_fill(mask, 0)
    if mode != "legacy":
        raise ValueError("kws_feature_augmentation_invalid")
    batch, rows, bands, _ = x.shape
    scale = 0.85 + 0.30 * torch.rand((batch, 1), device=x.device, generator=generator)
    positions = (
        (torch.arange(bands, device=x.device)[None] - (bands - 1) / 2) / scale
        + (bands - 1) / 2
    ).clamp(0, bands - 1)
    lo = positions.long()
    hi = (lo + 1).clamp(max=bands - 1)
    fraction = (positions - lo)[:, None, :, None]
    a = x.gather(2, lo[:, None, :, None].expand(-1, rows, -1, 1))
    b = x.gather(2, hi[:, None, :, None].expand(-1, rows, -1, 1))
    warped = a + fraction * (b - a)
    start = torch.randint(
        0, bands - 3, (batch, 1), device=x.device, generator=generator
    )
    freq = torch.arange(bands, device=x.device)[None]
    mask = ((freq >= start) & (freq < start + 3))[:, None, :, None]
    warped = warped.masked_fill(mask, 0)
    use = torch.rand((batch, 1, 1, 1), device=x.device, generator=generator) < 0.5
    return torch.where(use, warped, x)


def block_shuffled_indices(indices, seed, block_rows=4096):
    """Shuffle physical blocks and rows inside each block, bounding mmap page churn."""
    import numpy as np

    indices = np.sort(np.asarray(indices))
    if not len(indices):
        return indices
    blocks = indices // block_rows
    boundaries = np.flatnonzero(np.diff(blocks)) + 1
    groups = np.split(indices, boundaries)
    rng = np.random.default_rng(seed)
    order = rng.permutation(len(groups))
    # A bounded pool mixes source/class runs while limiting mmap working set.
    return np.concatenate(
        [
            rng.permutation(
                np.concatenate([groups[i] for i in order[start : start + 8]])
            )
            for start in range(0, len(groups), 8)
        ]
    )


def thaw_encoder(model, epoch, milestones):
    """Use completed epochs: head, then upper two blocks, then all layers."""
    upper, all_layers = milestones
    for name, parameter in model.named_parameters():
        trainable = (
            name.startswith("classifier.")
            or epoch >= all_layers
            or (
                epoch >= upper
                and name.startswith(("depth.3.", "point.3.", "depth.4.", "point.4."))
            )
        )
        parameter.requires_grad_(trainable)
        if not trainable:
            parameter.grad = None
    return [
        name for name, parameter in model.named_parameters() if parameter.requires_grad
    ]


def fit_streaming(
    keras_model,
    features,
    targets,
    weights,
    train_indices,
    validation_indices,
    *,
    channels,
    epochs,
    batch_size,
    seed,
    negative_weight,
    positive_weight,
    reduction,
    output,
    resume=None,
    learning_rate=0.001,
    positive_reduction="mean",
    state_scales=None,
    classifier_only=False,
    positive_hold_ms=0,
    packed_labels=False,
    feature_augmentation="legacy",
    checkpoint_selection="validation-loss",
    negative_pair_weight=0.0,
    positive_confirmation=False,
    readout="last",
    encoder_thaw_epochs=(0, 0),
):
    import numpy as np
    import torch

    if not torch.cuda.is_available():
        raise RuntimeError("kws_cuda_unavailable")
    torch.manual_seed(seed)
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    torch.backends.cudnn.benchmark = False
    torch.backends.cudnn.deterministic = True
    torch.cuda.set_per_process_memory_fraction(0.45)
    model = build_model(torch, channels, state_scales=state_scales, readout=readout)
    copy_from_keras(torch, model, keras_model)
    model = model.cuda()
    if classifier_only:
        for name, parameter in model.named_parameters():
            parameter.requires_grad_(name.startswith("classifier."))
    optimizer = torch.optim.Adam(
        [p for p in model.parameters() if p.requires_grad], lr=learning_rate, eps=1e-7
    )
    contract = {
        "features": hashlib.sha256(features).hexdigest(),
        "targets": hashlib.sha256(targets).hexdigest(),
        "weights": hashlib.sha256(weights).hexdigest(),
        "train_indices": hashlib.sha256(train_indices).hexdigest(),
        "validation_indices": hashlib.sha256(validation_indices).hexdigest(),
        "backend_source": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "channels": channels,
        "readout": readout,
        "seed": seed,
        "batch_size": batch_size,
        "learning_rate": learning_rate,
        "positive_reduction": positive_reduction,
        "state_scales": state_scales,
        "classifier_only": classifier_only,
        "encoder_thaw_epochs": list(encoder_thaw_epochs),
        "positive_hold_ms": positive_hold_ms,
        "packed_labels": packed_labels,
        "feature_augmentation": feature_augmentation,
        "checkpoint_selection": checkpoint_selection,
        "negative_weight": negative_weight,
        "positive_weight": positive_weight,
        "reduction": reduction,
        "negative_pair_weight": negative_pair_weight,
        "positive_confirmation": positive_confirmation,
    }
    history = {k: [] for k in ("loss", "accuracy", "val_loss", "val_accuracy")}
    best, best_state, start = float("inf"), None, 0
    if resume is not None:
        state = torch.load(resume, map_location="cuda", weights_only=True)
        if state["contract"] != contract:
            raise RuntimeError("kws_resume_contract_mismatch")
        model.load_state_dict(state["model"])
        optimizer.load_state_dict(state["optimizer"])
        history, best, best_state, start = (
            state["history"],
            state["best"],
            state["best_model"],
            state["epoch"],
        )
        if epochs <= start:
            raise RuntimeError("kws_resume_epochs_not_extended")

    def run(indices, training):
        # Evaluate with the same simulated state grid; optimizer and input
        # augmentation remain disabled on held-out rows. Export uses eval().
        model.train(training or state_scales is not None)
        loss_sum, correct, count = 0.0, 0, 0
        import time

        started = time.monotonic()
        augmentation_rng = torch.Generator(device="cuda").manual_seed(seed + epoch)
        for offset in range(0, len(indices), batch_size):
            chosen = indices[offset : offset + batch_size]
            values = features[chosen] if training else features[chosen, :149]
            x = torch.as_tensor(np.array(values), device="cuda")
            y = torch.as_tensor(targets[chosen], device="cuda")
            if training:
                x = augment_spectrum(
                    torch,
                    x,
                    augmentation_rng,
                    feature_augmentation,
                    y[:, 3] if packed_labels or positive_weight else None,
                )
            with torch.set_grad_enabled(training):
                logits = model.logits(x)
                losses, hits = frame_loss(
                    torch,
                    y,
                    logits.softmax(-1),
                    negative_weight,
                    positive_weight,
                    reduction,
                    logits=logits,
                    positive_reduction=positive_reduction,
                    positive_hold_ms=positive_hold_ms,
                    packed_labels=packed_labels,
                    negative_pair_weight=negative_pair_weight,
                    positive_confirmation=positive_confirmation,
                )
                if training:
                    losses = losses * torch.as_tensor(weights[chosen], device="cuda")
                loss = losses.mean()
                if training:
                    optimizer.zero_grad(set_to_none=True)
                    loss.backward()
                    optimizer.step()
            loss_sum += loss.item() * len(chosen)
            correct += hits.sum().item()
            count += len(chosen)
            if training and (offset == 0 or (offset // batch_size + 1) % 100 == 0):
                print(
                    json.dumps(
                        dict(
                            stage="training_batch",
                            epoch=epoch + 1,
                            samples=count,
                            total=len(indices),
                            elapsed_seconds=round(time.monotonic() - started, 2),
                        )
                    ),
                    flush=True,
                )
        if not count:
            raise RuntimeError("kws_empty_training_or_validation")
        return loss_sum / count, correct / count

    for epoch in range(start, epochs):
        if encoder_thaw_epochs != (0, 0):
            trainable = thaw_encoder(model, epoch, encoder_thaw_epochs)
            print(
                json.dumps(
                    dict(
                        stage="encoder_thaw",
                        epoch=epoch + 1,
                        trainable_parameters=trainable,
                    )
                ),
                flush=True,
            )
        indices = (
            np.random.default_rng(seed + epoch).permutation(train_indices)
            if features.nbytes <= 4 * 1024**3
            else block_shuffled_indices(train_indices, seed + epoch)
        )
        loss, accuracy = run(indices, True)
        val_loss, val_accuracy = run(validation_indices, False)
        values = dict(
            loss=loss, accuracy=accuracy, val_loss=val_loss, val_accuracy=val_accuracy
        )
        if not all(np.isfinite(value) for value in values.values()):
            raise RuntimeError("kws_nonfinite_training")
        for key, value in values.items():
            history[key].append(value)
        if val_loss < best:
            best, best_state = val_loss, copy.deepcopy(model.state_dict())
        state = dict(
            contract=contract,
            model=model.state_dict(),
            optimizer=optimizer.state_dict(),
            epoch=epoch + 1,
            history=history,
            best=best,
            best_model=best_state,
        )
        temporary = output / "resume-last.pt.tmp"
        torch.save(state, temporary)
        os.replace(temporary, output / "resume-last.pt")
        print(
            json.dumps(
                dict(
                    stage="training_epoch",
                    backend="torch-cuda",
                    epoch=epoch + 1,
                    **values,
                )
            ),
            flush=True,
        )
    if checkpoint_selection == "validation-loss":
        model.load_state_dict(best_state)
    model.eval()
    copy_to_keras(model, keras_model)
    probe = np.array(features[train_indices[:2]])
    with torch.no_grad():
        expected = model(torch.as_tensor(probe, device="cuda")).cpu().numpy()
    error = float(np.max(np.abs(expected - keras_model(probe, training=False).numpy())))
    if error > 2e-5:
        raise RuntimeError("kws_torch_keras_export_mismatch")
    keras_model.save_weights(str(output / "best.weights.h5"))
    (output / "cuda-training.json").write_text(
        json.dumps(
            {
                "torch_version": torch.__version__,
                "device": torch.cuda.get_device_name(),
                "max_keras_score_error": error,
                "contract": contract,
                "resume_epoch": start,
                "epochs_completed": len(history["loss"]),
                "selected_epoch": (
                    int(np.argmin(history["val_loss"])) + 1
                    if checkpoint_selection == "validation-loss"
                    else len(history["loss"])
                ),
                "peak_allocated_bytes": torch.cuda.max_memory_allocated(),
            },
            indent=2,
        )
        + "\n"
    )
    return SimpleNamespace(history=history), keras_model.get_weights()
