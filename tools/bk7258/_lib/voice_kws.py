#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Offline candidate training for a manifest-selected BK7258 wake-word model.

This module intentionally has no corpus discovery or upload behaviour.  The
operator supplies a consented, local manifest; audit reports are aggregate
only, and trained output remains a candidate until separately accepted.
"""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import re
import stat
import subprocess
import sys
import tempfile
import wave
from collections import Counter
from pathlib import Path
from typing import Any, Iterable


SCHEMA = "bkvoice-kws-dataset-v1"
FRONTEND = "bkvoice-microfrontend-v1"
DEFAULT_WAKE_LABEL = "nihao_openvela"
DEFAULT_WAKE_PHRASE = "你好，open-vela"
BASE_LABELS = ("silence", "unknown")
SPLITS = ("train", "validation", "test")
# The deployed frontend consumes a three-second 16 kHz rolling context.  With
# its 30 ms window and 20 ms hop that produces 149 40-bin feature rows.
SAMPLES = 48000
FEATURE_ROWS = 149
FEATURES = FEATURE_ROWS * 40


class KwsError(RuntimeError):
    """A non-sensitive manifest or training error."""


def _wake_contract(document: dict[str, Any]) -> tuple[str, str, tuple[str, str, str]]:
    """Return the one target identity declared by a dataset manifest.

    Legacy manifests deliberately retain the original identity.  A new target
    must declare both an ASCII runtime label and the product phrase so a model
    cannot be relabelled after training without changing its provenance.
    """
    label = document.get("wake_label", DEFAULT_WAKE_LABEL)
    phrase = document.get("wake_phrase", DEFAULT_WAKE_PHRASE)
    explicit_label = "wake_label" in document
    explicit_phrase = "wake_phrase" in document
    if explicit_label != explicit_phrase:
        _fail("manifest_wake_contract_incomplete")
    if (
        not isinstance(label, str)
        or not re.fullmatch(r"[a-z][a-z0-9_]{0,63}", label)
        or label in BASE_LABELS
    ):
        _fail("manifest_wake_label_invalid")
    if (
        not isinstance(phrase, str)
        or not phrase.strip()
        or len(phrase) > 160
        or any(ord(character) < 32 for character in phrase)
    ):
        _fail("manifest_wake_phrase_invalid")
    return label, phrase, (*BASE_LABELS, label)


def add_arguments(
    subparsers: argparse._SubParsersAction[argparse.ArgumentParser],
) -> None:
    """Add ``kws audit`` and ``kws train`` below the maintained voice CLI."""
    kws = subparsers.add_parser("kws", help="audit or train a local KWS candidate")
    commands = kws.add_subparsers(dest="kws_command", required=True)
    audit = commands.add_parser(
        "audit", help="validate a consented local dataset manifest"
    )
    audit.add_argument("--manifest", required=True, type=Path)
    train = commands.add_parser(
        "train", help="train an INT8 candidate from a valid manifest"
    )
    train.add_argument("--manifest", required=True, type=Path)
    train.add_argument(
        "--output",
        required=True,
        type=Path,
        help="new, non-existent candidate output directory",
    )
    train.add_argument("--epochs", type=int, default=12)
    train.add_argument("--batch-size", type=int, default=16)
    train.add_argument("--seed", type=int, default=1337)
    train.add_argument(
        "--channels",
        type=int,
        default=32,
        help="DS-CNN pointwise channel count (1..64); export must fit 64 KiB",
    )
    train.add_argument(
        "--architecture",
        choices=("ds-cnn", "temporal-ds-cnn"),
        default="ds-cnn",
        help="spatial DS-CNN or full-frequency projection followed by temporal separable convolutions",
    )
    train.add_argument(
        "--initial-frequency-stride",
        type=int,
        choices=(1, 2, 4),
        default=2,
        help="DS-CNN frequency stride; temporal projection always spans all 40 bins",
    )
    train.add_argument(
        "--onset-hard-negatives",
        type=int,
        default=0,
        help="train-only silence-to-speech rolling windows; disabled by default",
    )
    train.add_argument(
        "--unknown-shift-step-ms",
        type=int,
        choices=(100, 200, 400, 800),
        default=100,
        help="train-only negative shift spacing; larger steps bound memory for larger corpora",
    )
    train.add_argument(
        "--positive-end-window-ms",
        type=int,
        default=0,
        help="keep complete positive phrase ends in the final 100..3000 ms of a rolling window; 0 retains all legal positions",
    )
    train.add_argument(
        "--pcm-level-augmentation",
        action="store_true",
        help="retain train PCM windows and add quiet copies at 0.25/0.1 gain before the official frontend",
    )
    train.add_argument(
        "--room-augmentation",
        action="store_true",
        help="add train-only synthetic reflections, bandwidth variation and low background noise before the official frontend",
    )
    evaluate = commands.add_parser(
        "evaluate",
        help="stream a frozen validation or test session set through the KWS C policy",
    )
    evaluate.add_argument("--manifest", required=True, type=Path)
    evaluate.add_argument(
        "--model", required=True, type=Path, help="existing full-INT8 TFLite candidate"
    )
    evaluate.add_argument(
        "--output",
        required=True,
        type=Path,
        help="new JSON report; contains aggregate metrics only",
    )
    evaluate.add_argument(
        "--split",
        choices=("validation", "test"),
        required=True,
        help="validation may select a policy; test is a frozen independent report",
    )
    evaluate.add_argument(
        "--frozen-policy",
        required=True,
        type=Path,
        help="new validation binding, or existing binding required for test",
    )
    package = commands.add_parser("package", help="package a candidate as a WKM1 model")
    package.add_argument("--model", required=True, type=Path)
    package.add_argument("--metadata", required=True, type=Path)
    package.add_argument("--output", required=True, type=Path)


def _fail(reason: str) -> None:
    raise KwsError(reason)


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _safe_audio(root: Path, value: Any) -> Path:
    if not isinstance(value, str) or not value:
        _fail("entry_path_invalid")
    relative = Path(value)
    if relative.is_absolute() or ".." in relative.parts:
        _fail("entry_path_invalid")
    candidate = root / relative
    # Refuse links in every supplied component, rather than merely resolving
    # the final target after it may already have escaped the dataset root.
    current = root
    for part in relative.parts:
        current = current / part
        try:
            mode = current.lstat().st_mode
        except OSError as error:
            raise KwsError("entry_unavailable") from error
        if stat.S_ISLNK(mode):
            _fail("entry_path_invalid")
    try:
        resolved = candidate.resolve(strict=True)
        resolved.relative_to(root)
        mode = candidate.lstat().st_mode
    except (OSError, ValueError) as error:
        raise KwsError("entry_unavailable") from error
    if not stat.S_ISREG(mode):
        _fail("entry_not_regular")
    return resolved


def _wav_pcm16(path: Path, *, exact_samples: int | None = None) -> bytes:
    try:
        with wave.open(str(path), "rb") as audio:
            if (
                audio.getnchannels() != 1
                or audio.getframerate() != 16000
                or audio.getsampwidth() != 2
                or audio.getcomptype() != "NONE"
                or (exact_samples is not None and audio.getnframes() != exact_samples)
            ):
                _fail("audio_format_invalid")
            frames = audio.readframes(audio.getnframes())
    except (OSError, EOFError, wave.Error) as error:
        raise KwsError("audio_format_invalid") from error
    if not frames or len(frames) % 2:
        _fail("audio_format_invalid")
    return frames


def _pcm16(path: Path) -> bytes:
    return _wav_pcm16(path, exact_samples=SAMPLES)


def _read_manifest(path: Path) -> dict[str, Any]:
    try:
        if stat.S_ISLNK(path.lstat().st_mode) or not stat.S_ISREG(path.stat().st_mode):
            _fail("manifest_invalid")
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise KwsError("manifest_invalid") from error
    if not isinstance(data, dict):
        _fail("manifest_invalid")
    return data


def _validate(
    manifest_path: Path,
) -> tuple[list[dict[str, Any]], dict[str, Any], tuple[str, str, tuple[str, str, str]]]:
    document = _read_manifest(manifest_path)
    wake_label, wake_phrase, labels = _wake_contract(document)
    if (
        document.get("schema") != SCHEMA
        or document.get("frontend") != FRONTEND
        or document.get("labels") != list(labels)
        or not isinstance(document.get("entries"), list)
    ):
        _fail("manifest_contract_invalid")
    root = manifest_path.parent.resolve()
    records: list[dict[str, Any]] = []
    speaker_splits: dict[str, set[str]] = {}
    source_splits: dict[str, set[str]] = {}
    hash_splits: dict[str, set[str]] = {}
    counts: Counter[tuple[str, str]] = Counter()
    for entry in document["entries"]:
        if not isinstance(entry, dict):
            _fail("entry_invalid")
        label, split, speaker, declared, source_id, recording_kind = (
            entry.get("label"),
            entry.get("split"),
            entry.get("speaker"),
            entry.get("sha256"),
            entry.get("source_id"),
            entry.get("recording_kind"),
        )
        if label not in labels or split not in SPLITS:
            _fail("entry_label_or_split_invalid")
        if not isinstance(speaker, str) or not speaker or len(speaker) > 128:
            _fail("entry_speaker_invalid")
        if not isinstance(source_id, str) or not re.fullmatch(
            r"[A-Za-z0-9][A-Za-z0-9._:-]{0,127}", source_id
        ):
            _fail("entry_source_id_missing")
        if recording_kind not in ("real", "synthetic"):
            _fail("entry_recording_kind_invalid")
        if entry.get("consent") is not True:
            _fail("entry_consent_missing")
        if not isinstance(declared, str) or not re.fullmatch(r"[0-9a-f]{64}", declared):
            _fail("entry_hash_invalid")
        audio = _safe_audio(root, entry.get("path"))
        actual = _sha256(audio)
        if actual != declared:
            _fail("entry_hash_mismatch")
        pcm = _pcm16(audio)
        if label == wake_label and not any(pcm):
            _fail("positive_audio_silent")
        speaker_splits.setdefault(speaker, set()).add(split)
        source_splits.setdefault(source_id, set()).add(split)
        hash_splits.setdefault(hashlib.sha256(pcm).hexdigest(), set()).add(split)
        counts[(split, label)] += 1
        records.append(
            {
                "path": audio,
                "split": split,
                "label": label,
                "speaker": speaker,
                "source_id": source_id,
                "recording_kind": recording_kind,
                "sha256": actual,
                "pcm": pcm,
                "category": entry.get(
                    "negative_category",
                    source_id.rsplit(":", 1)[-1] if ":" in source_id else label,
                ),
            }
        )
    if not records:
        _fail("dataset_empty")
    if any(len(value) > 1 for value in speaker_splits.values()):
        _fail("speaker_cross_split")
    # A source id names the original recording lineage.  Crops, re-encodes and
    # augmentations of that recording therefore cannot cross a split even if
    # their PCM hashes differ and speakers are unavailable or anonymous.
    if any(len(value) > 1 for value in source_splits.values()):
        _fail("source_cross_split")
    if any(len(value) > 1 for value in hash_splits.values()):
        _fail("audio_cross_split")
    if any(counts[(split, label)] == 0 for split in SPLITS for label in labels):
        _fail("class_or_split_missing")
    identity = [
        {
            key: record[key]
            for key in ("split", "label", "speaker", "source_id", "sha256")
        }
        for record in records
    ]
    encoded = json.dumps(
        sorted(identity, key=lambda item: json.dumps(item, sort_keys=True)),
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    report = {
        "schema": SCHEMA,
        "frontend": FRONTEND,
        "labels": list(labels),
        "wake_label": wake_label,
        "wake_phrase": wake_phrase,
        "status": "candidate",
        "entries": len(records),
        "counts": {
            split: {label: counts[(split, label)] for label in labels}
            for split in SPLITS
        },
        "recording_kind_counts": {
            kind: sum(record["recording_kind"] == kind for record in records)
            for kind in ("real", "synthetic")
        },
        # Format validity and class presence are deliberately weaker than
        # evidence that a candidate may be accepted for a product.
        "model_acceptance": {
            "accepted": False,
            "reason": "minimum_real_corpus_and_streaming_evidence_not_recorded",
        },
        "dataset_sha256": hashlib.sha256(encoded).hexdigest(),
    }
    return records, report, (wake_label, wake_phrase, labels)


def audit(manifest: Path) -> dict[str, Any]:
    """Return a privacy-preserving aggregate audit report or raise ``KwsError``."""
    records, report, _ = _validate(manifest)
    document = _read_manifest(manifest)
    if "sessions" in document:
        sessions = _validate_sessions(document, manifest, records)
        report["streaming_sessions"] = {
            split: sum(session["split"] == split for session in sessions)
            for split in SPLITS
        }
        report["streaming_session_sha256"] = _session_digest(sessions)
    return report


def _frontend_inputs() -> tuple[Path, Path, Path, list[Path], list[Path], list[Path]]:
    """Resolve every local source/header consumed by the host frontend build."""
    source = (
        Path(__file__).resolve().parents[3] / "app/bk7258/bk7258_voice_kws_frontend.c"
    )
    workspace = source.parents[3]
    tflm = workspace / "apps/mlearning/tflite-micro/tflite-micro"
    frontend = tflm / "tensorflow/lite/experimental/microfrontend/lib"
    kissfft = workspace / "apps/math/kissfft/kissfft"
    sources = [
        source,
        frontend / "frontend.c",
        frontend / "frontend_util.c",
        frontend / "fft.cc",
        frontend / "fft_util.cc",
        frontend / "kiss_fft_int16.cc",
        frontend / "filterbank.c",
        frontend / "filterbank_util.c",
        frontend / "log_lut.c",
        frontend / "log_scale.c",
        frontend / "log_scale_util.c",
        frontend / "noise_reduction.c",
        frontend / "noise_reduction_util.c",
        frontend / "pcan_gain_control.c",
        frontend / "pcan_gain_control_util.c",
        frontend / "window.c",
        frontend / "window_util.c",
    ]
    dependencies = [kissfft / "kiss_fft.c", kissfft / "tools/kiss_fftr.c"]
    headers = [
        source.with_suffix(".h"),
        frontend / "bits.h",
        frontend / "frontend.h",
        frontend / "frontend_util.h",
        frontend / "fft.h",
        frontend / "fft_util.h",
        frontend / "kiss_fft_common.h",
        frontend / "kiss_fft_int16.h",
        frontend / "filterbank.h",
        frontend / "filterbank_util.h",
        frontend / "log_lut.h",
        frontend / "log_scale.h",
        frontend / "log_scale_util.h",
        frontend / "noise_reduction.h",
        frontend / "noise_reduction_util.h",
        frontend / "pcan_gain_control.h",
        frontend / "pcan_gain_control_util.h",
        frontend / "window.h",
        frontend / "window_util.h",
        kissfft / "kiss_fft.h",
        kissfft / "_kiss_fft_guts.h",
        kissfft / "tools/kiss_fftr.h",
    ]
    if any(not item.is_file() for item in (*sources, *dependencies, *headers)):
        _fail("frontend_source_unavailable")
    return workspace, tflm, kissfft, sources, dependencies, headers


def _frontend_provenance() -> dict[str, str]:
    """Return content hashes keyed by OpenVela-root-relative input paths."""
    workspace, _, _, sources, dependencies, headers = _frontend_inputs()
    try:
        return {
            item.relative_to(workspace).as_posix(): _sha256(item)
            for item in (*sources, *dependencies, *headers)
        }
    except OSError as error:
        raise KwsError("frontend_source_unavailable") from error


def _frontend_library() -> ctypes.CDLL:
    _, tflm, kissfft, sources, _, _ = _frontend_inputs()
    with tempfile.TemporaryDirectory(prefix="bkvoice-kws-frontend-") as temporary:
        output = Path(temporary) / "frontend.so"
        try:
            objects: list[Path] = []
            for index, item in enumerate(sources):
                object_file = Path(temporary) / f"frontend-{index}.o"
                compiler = "c++" if item.suffix == ".cc" else "cc"
                standard = "-std=c++17" if item.suffix == ".cc" else "-std=c11"
                result = subprocess.run(
                    [
                        compiler,
                        "-c",
                        "-fPIC",
                        standard,
                        "-O2",
                        f"-I{tflm}",
                        f"-I{kissfft}",
                        str(item),
                        "-o",
                        str(object_file),
                    ],
                    check=False,
                    capture_output=True,
                    timeout=30,
                )
                if result.returncode != 0:
                    _fail("frontend_compile_failed")
                objects.append(object_file)
            result = subprocess.run(
                [
                    "c++",
                    "-shared",
                    "-o",
                    str(output),
                    *(str(item) for item in objects),
                    "-lm",
                ],
                check=False,
                capture_output=True,
                timeout=30,
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            raise KwsError("frontend_compile_failed") from error
        if result.returncode != 0:
            _fail("frontend_compile_failed")
        # CDLL keeps the mapped object usable after TemporaryDirectory exits.
        library = ctypes.CDLL(str(output))
    function = library.bkvoice_kws_features
    function.argtypes = [
        ctypes.POINTER(ctypes.c_int16),
        ctypes.c_size_t,
        ctypes.POINTER(ctypes.c_float),
        ctypes.c_size_t,
    ]
    function.restype = ctypes.c_int
    return library


def _features(records: Iterable[dict[str, Any]], numpy: Any) -> Any:
    library = _frontend_library()
    function = library.bkvoice_kws_features
    # Allocate the final array once: appending would let stack/astype hold
    # several complete copies of every feature at the same time.
    records = list(records)
    output = numpy.empty((len(records), FEATURE_ROWS, 40, 1), dtype=numpy.float32)
    for index, record in enumerate(records):
        pcm = (ctypes.c_int16 * SAMPLES).from_buffer_copy(record["pcm"])
        feature = (ctypes.c_float * FEATURES)()
        if function(pcm, SAMPLES, feature, FEATURES) != 0:
            _fail("frontend_feature_failed")
        output[index] = numpy.ctypeslib.as_array(feature).reshape(FEATURE_ROWS, 40, 1)
    return output


def _rate(numerator: int, denominator: int) -> float | None:
    return None if denominator == 0 else numerator / denominator


def _confusion_matrix(
    labels: Any, predicted: Any, numpy: Any, class_labels: tuple[str, str, str]
) -> list[list[int]]:
    matrix = numpy.zeros((len(class_labels), len(class_labels)), dtype=numpy.int64)
    for actual, result in zip(labels, predicted):
        matrix[int(actual), int(result)] += 1
    return matrix.tolist()


def _evaluate_float(
    model: Any,
    features: Any,
    labels: Any,
    numpy: Any,
    class_labels: tuple[str, str, str],
) -> dict[str, Any]:
    """Record the restored float model's validation classification in metadata."""
    labels = numpy.asarray(labels)
    predicted = numpy.argmax(model.predict(features, verbose=0), axis=1)
    positives = labels == 2
    unknown = labels == 1
    return {
        "positive_false_negative_rate": _rate(
            int(numpy.sum(positives & (predicted != 2))), int(numpy.sum(positives))
        ),
        "unknown_false_positive_rate": _rate(
            int(numpy.sum(unknown & (predicted == 2))), int(numpy.sum(unknown))
        ),
        "positive_samples": int(numpy.sum(positives)),
        "unknown_samples": int(numpy.sum(unknown)),
        "confusion_matrix": _confusion_matrix(labels, predicted, numpy, class_labels),
        "actual_counts": {
            label: int(numpy.sum(labels == index))
            for index, label in enumerate(class_labels)
        },
        "predicted_counts": {
            label: int(numpy.sum(predicted == index))
            for index, label in enumerate(class_labels)
        },
        "labels": list(class_labels),
    }


def _evaluate_int8(
    interpreter: Any,
    features: Any,
    labels: Any,
    numpy: Any,
    class_labels: tuple[str, str, str],
) -> dict[str, Any]:
    details_in = interpreter.get_input_details()[0]
    details_out = interpreter.get_output_details()[0]
    scale, zero = details_in["quantization"]
    if not scale:
        _fail("tflite_input_not_quantized")
    predicted = []
    for item in features:
        scaled = item / scale
        rounded = numpy.copysign(numpy.floor(numpy.abs(scaled) + 0.5), scaled)
        value = numpy.clip(rounded + zero, -128, 127).astype(numpy.int8)[None, ...]
        interpreter.set_tensor(details_in["index"], value)
        interpreter.invoke()
        predicted.append(
            int(numpy.argmax(interpreter.get_tensor(details_out["index"])[0]))
        )
    labels = numpy.asarray(labels)
    predicted = numpy.asarray(predicted)
    positives = labels == 2
    unknown = labels == 1
    return {
        "positive_false_negative_rate": _rate(
            int(numpy.sum(positives & (predicted != 2))), int(numpy.sum(positives))
        ),
        "unknown_false_positive_rate": _rate(
            int(numpy.sum(unknown & (predicted == 2))), int(numpy.sum(unknown))
        ),
        "positive_samples": int(numpy.sum(positives)),
        "unknown_samples": int(numpy.sum(unknown)),
        "confusion_matrix": _confusion_matrix(labels, predicted, numpy, class_labels),
        "actual_counts": {
            label: int(numpy.sum(labels == index))
            for index, label in enumerate(class_labels)
        },
        "predicted_counts": {
            label: int(numpy.sum(predicted == index))
            for index, label in enumerate(class_labels)
        },
        "labels": list(class_labels),
    }


def _speech_span(pcm: bytes) -> tuple[int, int]:
    """Return the 20 ms-aligned voiced span of one consented positive clip."""
    frames = [pcm[index * 640 : (index + 1) * 640] for index in range(SAMPLES // 320)]
    levels = []
    for frame in frames:
        values = memoryview(frame).cast("h")
        levels.append((sum(value * value for value in values) / len(values)) ** 0.5)
    threshold = max(250.0, max(levels) * 0.02)
    active = [index for index, level in enumerate(levels) if level >= threshold]
    if not active:
        _fail("positive_audio_silent")
    return active[0] * 320, min(SAMPLES, (active[-1] + 1) * 320)


def _derived_record(
    record: dict[str, Any], pcm: bytes, label: str, kind: str
) -> dict[str, Any]:
    derived = dict(record)
    derived.update({"pcm": pcm, "label": label, "augmentation": kind})
    return derived


def _insert(background: bytes, speech: bytes, offset: int) -> bytes:
    if offset < 0 or offset + len(speech) // 2 > SAMPLES:
        _fail("training_augmentation_invalid")
    result = bytearray(background)
    result[offset * 2 : (offset + len(speech) // 2) * 2] = speech
    return bytes(result)


def _training_derivatives(
    records: list[dict[str, Any]],
    wake_label: str,
    *,
    onset_hard_negatives: int = 0,
    unknown_shift_step_ms: int = 100,
    positive_end_window_ms: int = 0,
) -> list[dict[str, Any]]:
    """Make train-only continuous-window positives and confusable negatives.

    A source lineage never crosses a split: this deliberately derives solely
    from records that already passed manifest source/speaker split validation.
    Complete source phrases are placed at adjacent 100 ms stream positions.
    A positive window must retain the whole voiced phrase, including its end;
    partial phrase variants remain unknown rather than delayed wakes.
    """
    train = [record for record in records if record["split"] == "train"]
    positives = [record for record in train if record["label"] == wake_label]
    backgrounds = [
        record for record in train if record["label"] in ("unknown", "silence")
    ]
    unknowns = [record for record in train if record["label"] == "unknown"]
    if onset_hard_negatives < 0 or onset_hard_negatives > len(unknowns):
        _fail("onset_hard_negatives_invalid")
    if unknown_shift_step_ms not in (100, 200, 400, 800):
        _fail("unknown_shift_step_invalid")
    if positive_end_window_ms != 0 and (
        positive_end_window_ms < 100
        or positive_end_window_ms > 3000
        or positive_end_window_ms % 100 != 0
    ):
        _fail("positive_end_window_invalid")
    if not positives or not backgrounds or not unknowns:
        _fail("training_augmentation_invalid")
    derived: list[dict[str, Any]] = []
    for index, positive in enumerate(positives):
        start, end = _speech_span(positive["pcm"])
        speech = positive["pcm"][start * 2 : end * 2]
        # The original three-second recording is shifted 100 ms at a time
        # against ordinary background.  Shifts outside this interval would
        # truncate the first or last voiced phoneme, so they cannot be
        # positive labels.
        minimum = -((start // 1600) * 1600)
        maximum = ((SAMPLES - end) // 1600) * 1600
        if positive_end_window_ms:
            # A two-hit 300 ms runtime decision needs examples whose complete
            # phrase remains fresh in both adjacent scoring windows.
            fresh_minimum = SAMPLES - positive_end_window_ms * 16 - end
            fresh_minimum = ((fresh_minimum + 1599) // 1600) * 1600
            minimum = max(minimum, fresh_minimum)
        # Cover every legal 100 ms position in the three-second rolling
        # context.  Restricting this to a narrow central band teaches one
        # phrase position and delays a live trigger until trailing silence.
        first = minimum
        last = maximum
        for shift in range(first, last + 1, 1600):
            if shift == 0:
                continue
            background = backgrounds[(index * 5 + shift // 1600) % len(backgrounds)]
            if shift > 0:
                pcm = (
                    background["pcm"][: shift * 2]
                    + positive["pcm"][: (SAMPLES - shift) * 2]
                )
            else:
                cut = -shift
                pcm = positive["pcm"][cut * 2 :] + background["pcm"][: cut * 2]
            derived.append(
                _derived_record(
                    positive, pcm, wake_label, "complete_target_sliding_window"
                )
            )
        # Prefix, suffix and middle portions are deliberately incomplete.
        length = len(speech) // 2
        partials = (
            speech[: (length * 2 // 5) * 2],
            speech[(length * 3 // 5) * 2 :],
            speech[(length * 3 // 10) * 2 : (length * 7 // 10) * 2],
        )
        for partial_index, partial in enumerate(partials):
            background = unknowns[
                (index * len(partials) + partial_index) % len(unknowns)
            ]
            offset = 6400 + partial_index * 3200
            derived.append(
                _derived_record(
                    positive,
                    _insert(background["pcm"], partial, offset),
                    "unknown",
                    "incomplete_target_hard_negative",
                )
            )
    # Join different ordinary train utterances at the half-window boundary,
    # matching a live stream without treating a software pause as a release
    # event.
    for index, first in enumerate(unknowns):
        second = unknowns[(index * 7 + 3) % len(unknowns)]
        pcm = first["pcm"][:SAMPLES] + second["pcm"][SAMPLES:]
        derived.append(
            _derived_record(first, pcm, "unknown", "ordinary_speech_join_hard_negative")
        )
    # Hard negatives only zero-pad and keep their label; a larger step bounds
    # the memory added by the expanded corpus. The default still covers the
    # original every-100 ms positions and every setting keeps the full
    # +/-1.6 s endpoints.
    zeros = bytes(len(unknowns[0]["pcm"]))
    for unknown in unknowns:
        for shift in range(-25600, 25601, unknown_shift_step_ms * 16):
            if shift == 0:
                continue
            if shift > 0:
                pcm = zeros[: shift * 2] + unknown["pcm"][: (SAMPLES - shift) * 2]
            else:
                cut = -shift
                pcm = unknown["pcm"][cut * 2 :] + zeros[: cut * 2]
            derived.append(
                _derived_record(
                    unknown, pcm, "unknown", "ordinary_speech_zero_padded_shift"
                )
            )
    if onset_hard_negatives:
        silences = [record for record in train if record["label"] == "silence"]
        if not silences:
            _fail("training_augmentation_invalid")
        for index, unknown in enumerate(unknowns[:onset_hard_negatives]):
            lead = (9600, 12800, 16000)[index % 3]
            silence = silences[index % len(silences)]
            pcm = silence["pcm"][: lead * 2] + unknown["pcm"][: (SAMPLES - lead) * 2]
            derived.append(
                _derived_record(
                    unknown, pcm, "unknown", "silence_to_speech_onset_hard_negative"
                )
            )
    return derived


def _regular_file(path: Path, error: str) -> None:
    try:
        mode = path.lstat().st_mode
    except OSError as exc:
        raise KwsError(error) from exc
    if stat.S_ISLNK(mode) or not stat.S_ISREG(mode):
        _fail(error)


def _read_candidate_metadata(
    model: Path, wake_contract: tuple[str, str, tuple[str, str, str]]
) -> tuple[dict[str, Any], Path]:
    wake_label, wake_phrase, labels = wake_contract
    _regular_file(model, "model_unavailable")
    metadata_path = model.with_name("metadata.json")
    try:
        _regular_file(metadata_path, "model_metadata_unavailable")
        metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise KwsError("model_metadata_unavailable") from error
    authorization = (
        metadata.get("model_authorization") if isinstance(metadata, dict) else None
    )
    if (
        not isinstance(metadata, dict)
        or metadata.get("model_sha256") != _sha256(model)
        or metadata.get("schema") != SCHEMA
        or metadata.get("frontend") != FRONTEND
        or metadata.get("labels") != list(labels)
        or not isinstance(authorization, dict)
        or authorization.get("status") != "candidate"
        or authorization.get("training_data_authorization")
        != "manifest_entry_consent_true"
        or not isinstance(metadata.get("dataset_sha256"), str)
    ):
        _fail("model_metadata_contract_invalid")
    if metadata.get("frontend_inputs_sha256") != _frontend_provenance():
        _fail("model_frontend_provenance_mismatch")
    # Old default candidates predate explicit fields; non-default candidates
    # must bind both identity values in their metadata.
    if (
        metadata.get("wake_label", DEFAULT_WAKE_LABEL) != wake_label
        or metadata.get("wake_phrase", DEFAULT_WAKE_PHRASE) != wake_phrase
    ):
        _fail("model_wake_contract_mismatch")
    return metadata, metadata_path


def package(model: Path, metadata_path: Path, output: Path) -> dict[str, Any]:
    _regular_file(model, "model_unavailable")
    _regular_file(metadata_path, "model_metadata_unavailable")
    if not 1 <= model.stat().st_size <= 65536:
        _fail("model_size_invalid")
    try:
        metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise KwsError("model_metadata_unavailable") from error
    if not isinstance(metadata, dict):
        _fail("model_metadata_contract_invalid")
    label = metadata.get("wake_label")
    phrase = metadata.get("wake_phrase")
    raw = model.read_bytes()
    if (
        not isinstance(label, str)
        or not re.fullmatch(r"[a-z0-9_]{1,31}", label)
        or not isinstance(phrase, str)
        or not phrase.strip()
        or any(ord(c) < 32 for c in phrase)
        or len(phrase.encode("utf-8")) > 63
        or metadata.get("model_sha256") != hashlib.sha256(raw).hexdigest()
        or metadata.get("frontend") != FRONTEND
        or metadata.get("labels") != ["silence", "unknown", label]
        or len(raw) not in range(1, 65537)
    ):
        _fail("model_metadata_contract_invalid")
    try:
        import numpy as np
        import tensorflow as tf
    except ImportError as error:
        raise KwsError("training_dependencies_unavailable") from error
    interpreter = tf.lite.Interpreter(model_path=str(model))
    interpreter.allocate_tensors()
    if (
        len(interpreter.get_input_details()) != 1
        or len(interpreter.get_output_details()) != 1
    ):
        _fail("model_tensor_count_invalid")
    inp, out = interpreter.get_input_details()[0], interpreter.get_output_details()[0]
    operators = sorted(
        {
            x["op_name"]
            for x in interpreter._get_ops_details()
            if x["op_name"] != "DELEGATE"
        }
    )
    expected = [
        "AVERAGE_POOL_2D",
        "CONV_2D",
        "DEPTHWISE_CONV_2D",
        "FULLY_CONNECTED",
        "RESHAPE",
        "SOFTMAX",
    ]
    if (
        inp["shape"].tolist() != [1, 149, 40, 1]
        or out["shape"].tolist() != [1, 3]
        or inp["dtype"] != np.int8
        or out["dtype"] != np.int8
        or operators != expected
    ):
        _fail("model_export_incompatible")
    for name, tensor in (("input", inp), ("output", out)):
        scale, zero = tensor["quantization"]
        if (
            not np.isfinite(scale)
            or scale <= 0
            or zero not in range(-128, 128)
            or metadata.get(name + "_shape") != tensor["shape"].tolist()
            or metadata.get(name + "_quantization") != [float(scale), int(zero)]
        ):
            _fail("model_quantization_mismatch")
    header = (
        b"WKM1"
        + len(raw).to_bytes(4, "big")
        + hashlib.sha256(raw).digest()
        + label.encode("ascii").ljust(32, b"\0")
        + phrase.encode("utf-8").ljust(64, b"\0")
    )
    payload = header + raw
    if output.exists():
        if output.is_file() and output.read_bytes() == payload:
            return {
                "status": "candidate",
                "sha256": hashlib.sha256(payload).hexdigest(),
                "bytes": len(payload),
                "label": label,
                "phrase": phrase,
            }
        _fail("package_output_exists")
    if not output.parent.is_dir():
        _fail("package_output_invalid")
    with output.open("xb") as stream:
        stream.write(payload)
    return {
        "status": "candidate",
        "sha256": hashlib.sha256(payload).hexdigest(),
        "bytes": len(payload),
        "label": label,
        "phrase": phrase,
    }


def _session_digest(sessions: Iterable[dict[str, Any]]) -> str:
    identity = [
        {
            "split": session["split"],
            "source_id": session["source_id"],
            "segments": [
                {
                    key: segment[key]
                    for key in ("kind", "start_ms", "end_ms", "sha256")
                    if key in segment
                }
                for segment in session["segments"]
            ],
            "events": session["events"],
        }
        for session in sessions
    ]
    encoded = json.dumps(identity, sort_keys=True, separators=(",", ":")).encode(
        "utf-8"
    )
    return hashlib.sha256(encoded).hexdigest()


def _integer_ms(value: Any, error: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        _fail(error)
    return value


def _validate_sessions(
    document: dict[str, Any], manifest_path: Path, records: Iterable[dict[str, Any]]
) -> list[dict[str, Any]]:
    raw_sessions = document.get("sessions")
    if not isinstance(raw_sessions, list) or not raw_sessions:
        _fail("streaming_sessions_missing")
    root = manifest_path.parent.resolve()
    source_splits: dict[str, set[str]] = {}
    speaker_splits: dict[str, set[str]] = {}
    pcm_splits: dict[str, set[str]] = {}
    for record in records:
        source_splits.setdefault(record["source_id"], set()).add(record["split"])
        speaker_splits.setdefault(record["speaker"], set()).add(record["split"])
        pcm_splits.setdefault(hashlib.sha256(record["pcm"]).hexdigest(), set()).add(
            record["split"]
        )
    sessions: list[dict[str, Any]] = []
    ids: set[str] = set()
    for raw in raw_sessions:
        if not isinstance(raw, dict):
            _fail("session_invalid")
        session_id, source_id, speaker, split = (
            raw.get("session_id"),
            raw.get("source_id"),
            raw.get("speaker"),
            raw.get("split"),
        )
        if (
            not isinstance(session_id, str)
            or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._:-]{0,127}", session_id)
            or session_id in ids
        ):
            _fail("session_id_invalid")
        ids.add(session_id)
        if not isinstance(source_id, str) or not re.fullmatch(
            r"[A-Za-z0-9][A-Za-z0-9._:-]{0,127}", source_id
        ):
            _fail("session_source_id_missing")
        if not isinstance(speaker, str) or not speaker or len(speaker) > 128:
            _fail("session_speaker_invalid")
        if split not in SPLITS or raw.get("consent") is not True:
            _fail("session_contract_invalid")
        recording_kind = raw.get("recording_kind")
        if recording_kind not in ("real", "synthetic"):
            _fail("streaming_session_kind_invalid")
        source_splits.setdefault(source_id, set()).add(split)
        speaker_splits.setdefault(speaker, set()).add(split)
        timeline_start = _integer_ms(
            raw.get("timeline_start_ms"), "session_timeline_invalid"
        )
        session_end = _integer_ms(raw.get("session_end_ms"), "session_timeline_invalid")
        raw_segments = raw.get("segments")
        raw_events = raw.get("events")
        if (
            not isinstance(raw_segments, list)
            or not raw_segments
            or not isinstance(raw_events, list)
        ):
            _fail("session_contract_invalid")
        segments: list[dict[str, Any]] = []
        audio_ranges: list[tuple[int, int]] = []
        expected_start = timeline_start
        previous_kind: str | None = None
        for item in raw_segments:
            if not isinstance(item, dict) or item.get("kind") not in (
                "audio",
                "pause",
                "gap",
            ):
                _fail("session_segment_invalid")
            kind = item["kind"]
            start = _integer_ms(item.get("start_ms"), "session_timeline_invalid")
            if start != expected_start:
                _fail("session_timeline_gap_or_splice")
            if kind in ("pause", "gap"):
                end = _integer_ms(item.get("end_ms"), "session_timeline_invalid")
                if end <= start:
                    _fail("session_timeline_invalid")
                segments.append({"kind": kind, "start_ms": start, "end_ms": end})
            else:
                declared = item.get("sha256")
                if not isinstance(declared, str) or not re.fullmatch(
                    r"[0-9a-f]{64}", declared
                ):
                    _fail("session_hash_invalid")
                audio = _safe_audio(root, item.get("path"))
                if _sha256(audio) != declared:
                    _fail("session_hash_mismatch")
                pcm = _wav_pcm16(audio)
                samples = len(pcm) // 2
                if samples % 320:
                    _fail("session_frame_alignment_invalid")
                pcm_splits.setdefault(hashlib.sha256(pcm).hexdigest(), set()).add(split)
                end = start + samples // 16
                segments.append(
                    {
                        "kind": kind,
                        "start_ms": start,
                        "end_ms": end,
                        "sha256": declared,
                        "pcm": pcm,
                    }
                )
                audio_ranges.append((start, end))
            expected_start = end
            previous_kind = kind
        if expected_start != session_end or not audio_ranges:
            _fail("session_timeline_invalid")
        events: list[list[int]] = []
        previous_end = -1
        for event in raw_events:
            if (
                not isinstance(event, list)
                or len(event) != 2
                or _integer_ms(event[0], "session_event_invalid")
                >= _integer_ms(event[1], "session_event_invalid")
            ):
                _fail("session_event_invalid")
            start, end = event
            if start < previous_end or not any(
                start >= left and end <= right for left, right in audio_ranges
            ):
                _fail("session_event_invalid")
            events.append([start, end])
            previous_end = end
        sessions.append(
            {
                "session_id": session_id,
                "split": split,
                "source_id": source_id,
                "recording_kind": recording_kind,
                "segments": segments,
                "events": events,
                "session_end_ms": session_end,
            }
        )
    if any(len(value) > 1 for value in source_splits.values()):
        _fail("source_cross_split")
    if any(len(value) > 1 for value in speaker_splits.values()):
        _fail("speaker_cross_split")
    if any(len(value) > 1 for value in pcm_splits.values()):
        _fail("audio_cross_split")
    return sessions


def _kws_library() -> ctypes.CDLL:
    _workspace, tflm, kissfft, frontend_sources, _, _ = _frontend_inputs()
    repository = Path(__file__).resolve().parents[3]
    source = repository / "app/bk7258/bk7258_voice_kws.c"
    bridge = repository / "tests/host/bk7258/test_bk7258_voice_kws_bridge.c"
    if not source.is_file() or not bridge.is_file():
        _fail("kws_bridge_source_unavailable")
    with tempfile.TemporaryDirectory(prefix="bkvoice-kws-stream-") as temporary:
        temporary_path = Path(temporary)
        objects: list[Path] = []
        for index, item in enumerate((source, bridge, *frontend_sources)):
            object_file = temporary_path / f"stream-{index}.o"
            compiler = "c++" if item.suffix == ".cc" else "cc"
            standard = "-std=c++17" if item.suffix == ".cc" else "-std=c11"
            result = subprocess.run(
                [
                    compiler,
                    "-c",
                    "-fPIC",
                    standard,
                    "-O2",
                    f"-I{source.parent}",
                    f"-I{tflm}",
                    f"-I{kissfft}",
                    str(item),
                    "-o",
                    str(object_file),
                ],
                check=False,
                capture_output=True,
                timeout=30,
            )
            if result.returncode != 0:
                _fail("kws_bridge_compile_failed")
            objects.append(object_file)
        output = temporary_path / "stream.so"
        result = subprocess.run(
            [
                "c++",
                "-shared",
                "-o",
                str(output),
                *(str(item) for item in objects),
                "-lm",
            ],
            check=False,
            capture_output=True,
            timeout=30,
        )
        if result.returncode != 0:
            _fail("kws_bridge_compile_failed")
        library = ctypes.CDLL(str(output))
    callback = ctypes.CFUNCTYPE(
        ctypes.c_int,
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_float),
        ctypes.POINTER(ctypes.c_float),
    )
    library.bkvoice_kws_host_default_policy.argtypes = [ctypes.POINTER(ctypes.c_float)]
    library.bkvoice_kws_host_create.argtypes = [callback, ctypes.c_void_p]
    library.bkvoice_kws_host_create.restype = ctypes.c_void_p
    library.bkvoice_kws_host_feed.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_int16),
        ctypes.c_size_t,
        ctypes.c_uint64,
        ctypes.POINTER(ctypes.c_float),
    ]
    library.bkvoice_kws_host_feed.restype = ctypes.c_int
    library.bkvoice_kws_host_pause.argtypes = [ctypes.c_void_p]
    library.bkvoice_kws_host_destroy.argtypes = [ctypes.c_void_p]
    library._callback_type = callback  # retain the ctypes signature for callers
    return library


def _policy_from_c(library: ctypes.CDLL) -> dict[str, int | float]:
    values = (ctypes.c_float * 4)()
    library.bkvoice_kws_host_default_policy(values)
    return {
        "threshold": float(values[0]),
        "release_threshold": float(values[1]),
        "consecutive": int(values[2]),
        "cooldown_ms": int(values[3]),
    }


def _predict_int8(interpreter: Any, feature: Any, numpy: Any) -> tuple[int, Any]:
    details_in = interpreter.get_input_details()[0]
    details_out = interpreter.get_output_details()[0]
    scale, zero = details_in["quantization"]
    output_scale, output_zero = details_out["quantization"]
    if not scale or not output_scale:
        _fail("tflite_quantization_invalid")
    scaled = feature / scale
    rounded = numpy.copysign(numpy.floor(numpy.abs(scaled) + 0.5), scaled)
    value = numpy.clip(rounded + zero, -128, 127).astype(numpy.int8)[None, ...]
    interpreter.set_tensor(details_in["index"], value)
    interpreter.invoke()
    scores = (
        interpreter.get_tensor(details_out["index"])[0].astype(numpy.float32)
        - output_zero
    ) * output_scale
    return int(numpy.argmax(scores)), scores


def _overlaps(left: int, right: int, interval: list[int]) -> bool:
    return left < interval[1] and interval[0] < right


def _write_new_json(path: Path, value: dict[str, Any]) -> None:
    if path.exists() or not path.parent.is_dir():
        _fail("evaluation_output_invalid")
    path.write_text(
        json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def evaluate(
    manifest: Path, model: Path, output: Path, frozen_policy: Path, split_name: str
) -> dict[str, Any]:
    """Evaluate explicitly timed sessions through the product C stream path.

    Synthetic sessions remain candidate-only evidence; the TFLite interpreter
    is a host callback and TFLM board equivalence remains separate work.
    """
    records, report, wake_contract = _validate(manifest)
    wake_label, wake_phrase, class_labels = wake_contract
    document = _read_manifest(manifest)
    sessions = _validate_sessions(document, manifest, records)
    chosen_records = [record for record in records if record["split"] == split_name]
    chosen_sessions = [
        session for session in sessions if session["split"] == split_name
    ]
    if not chosen_sessions:
        _fail("streaming_split_missing")
    if output.exists() or not output.parent.is_dir() or output == frozen_policy:
        _fail("evaluation_output_invalid")
    if split_name == "validation" and (
        frozen_policy.exists() or not frozen_policy.parent.is_dir()
    ):
        _fail("frozen_policy_output_invalid")
    metadata, metadata_path = _read_candidate_metadata(model, wake_contract)
    try:
        import numpy as np
        import tensorflow as tf
    except ImportError as error:
        raise KwsError("training_dependencies_unavailable") from error
    interpreter = tf.lite.Interpreter(model_path=str(model))
    interpreter.allocate_tensors()
    details_in = interpreter.get_input_details()[0]
    details_out = interpreter.get_output_details()[0]
    if (
        list(details_in["shape"]) != [1, FEATURE_ROWS, 40, 1]
        or list(details_out["shape"]) != [1, len(class_labels)]
        or details_in["dtype"] != np.int8
        or details_out["dtype"] != np.int8
    ):
        _fail("tflite_shape_or_type_invalid")
    if (
        metadata.get("input_shape") != [1, FEATURE_ROWS, 40, 1]
        or metadata.get("output_shape") != [1, len(class_labels)]
        or metadata.get("input_quantization")
        != [float(details_in["quantization"][0]), int(details_in["quantization"][1])]
        or metadata.get("output_quantization")
        != [float(details_out["quantization"][0]), int(details_out["quantization"][1])]
    ):
        _fail("model_quantization_metadata_mismatch")
    library = _kws_library()
    policy = _policy_from_c(library)
    manifest_sha = _sha256(manifest)
    binding = {
        "schema": "bkvoice-kws-frozen-policy-v1",
        "selection_split": "validation",
        "policy": policy,
        "model_sha256": _sha256(model),
        "model_metadata_sha256": _sha256(metadata_path),
        "model_training_dataset_sha256": metadata.get("dataset_sha256"),
        "evaluation_manifest_sha256": manifest_sha,
        "validation_session_sha256": _session_digest(
            session for session in sessions if session["split"] == "validation"
        ),
    }
    # The target identity is part of all new policy bindings.  A legacy policy
    # can only be accepted for the unchanged default contract.
    binding.update(
        {
            "wake_label": wake_label,
            "wake_phrase": wake_phrase,
            "labels": list(class_labels),
        }
    )
    if split_name == "test":
        try:
            _regular_file(frozen_policy, "frozen_policy_unavailable")
            supplied = json.loads(frozen_policy.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, json.JSONDecodeError) as error:
            raise KwsError("frozen_policy_unavailable") from error
        if (
            not isinstance(supplied, dict)
            or any(
                supplied.get(key) != value
                for key, value in binding.items()
                if key in supplied or wake_label != DEFAULT_WAKE_LABEL
            )
            or supplied.get("validation_status") != "completed_candidate"
            or not isinstance(supplied.get("validation_result_sha256"), str)
            or not re.fullmatch(r"[0-9a-f]{64}", supplied["validation_result_sha256"])
        ):
            _fail("frozen_policy_binding_mismatch")
    # A test binding is checked above, before either slice or stream inference.
    features = _features(chosen_records, np)
    labels = np.asarray(
        [class_labels.index(record["label"]) for record in chosen_records],
        dtype=np.int32,
    )
    slices = _evaluate_int8(interpreter, features, labels, np, class_labels)
    slice_errors: Counter[str] = Counter()
    for record, feature, actual in zip(chosen_records, features, labels):
        predicted, _ = _predict_int8(interpreter, feature, np)
        if actual == class_labels.index(wake_label) and predicted != actual:
            slice_errors[f"target_false_negative:{record['category']}"] += 1
        elif actual == class_labels.index(
            "unknown"
        ) and predicted == class_labels.index(wake_label):
            slice_errors[f"unknown_false_positive:{record['category']}"] += 1
    events: list[tuple[str, int, int, float]] = []
    expected: list[tuple[str, list[int]]] = []
    listening_ms = 0
    paused_ms = 0
    gap_ms = 0
    callback_error: list[Exception] = []

    def infer(_context: Any, feature_pointer: Any, scores_pointer: Any) -> int:
        try:
            feature = np.ctypeslib.as_array(feature_pointer, shape=(FEATURES,)).copy()
            _, scores = _predict_int8(
                interpreter, feature.reshape(FEATURE_ROWS, 40, 1), np
            )
            for index, value in enumerate(scores):
                scores_pointer[index] = float(value)
            return 0
        except (
            Exception
        ) as error:  # C turns this into its normal inference failure path.
            callback_error.append(error)
            return -1

    callback = library._callback_type(infer)
    for session in chosen_sessions:
        # Each manifest session is a separate recording/clock epoch.  A
        # pause or gap within one session instead retains the C latch rules.
        host = library.bkvoice_kws_host_create(callback, None)
        if not host:
            _fail("kws_bridge_initialize_failed")
        try:
            expected.extend(
                (session["session_id"], event) for event in session["events"]
            )
            for segment in session["segments"]:
                if segment["kind"] == "pause":
                    paused_ms += segment["end_ms"] - segment["start_ms"]
                    library.bkvoice_kws_host_pause(host)
                    continue
                if segment["kind"] == "gap":
                    gap_ms += segment["end_ms"] - segment["start_ms"]
                    # Do not reset here: the product detects the real
                    # timestamp discontinuity on the next 20 ms frame.
                    continue
                pcm = (ctypes.c_int16 * (len(segment["pcm"]) // 2)).from_buffer_copy(
                    segment["pcm"]
                )
                frames = len(pcm) // 320
                listening_ms += frames * 20
                for frame in range(frames):
                    score = ctypes.c_float()
                    end_ms = segment["start_ms"] + (frame + 1) * 20
                    frame_pcm = ctypes.cast(
                        ctypes.byref(pcm, frame * 320 * ctypes.sizeof(ctypes.c_int16)),
                        ctypes.POINTER(ctypes.c_int16),
                    )
                    result = library.bkvoice_kws_host_feed(
                        host, frame_pcm, 320, end_ms, ctypes.byref(score)
                    )
                    if result < 0:
                        _fail("kws_stream_feed_failed")
                    if result == 1:
                        events.append(
                            (
                                session["session_id"],
                                end_ms - 20,
                                end_ms,
                                float(score.value),
                            )
                        )
            library.bkvoice_kws_host_pause(host)
        finally:
            library.bkvoice_kws_host_destroy(host)
    if callback_error:
        _fail("tflite_callback_failed")
    matched = [0] * len(expected)
    background_false_positives = 0
    event_details = []
    for session_id, start_ms, end_ms, score in events:
        overlaps = [
            index
            for index, (expected_id, interval) in enumerate(expected)
            if expected_id == session_id and _overlaps(start_ms, end_ms, interval)
        ]
        if not overlaps:
            background_false_positives += 1
            event_details.append(
                {
                    "session_id": session_id,
                    "start_ms": start_ms,
                    "end_ms": end_ms,
                    "wake_score": score,
                    "type": "background",
                }
            )
        else:
            matched[overlaps[0]] += 1
            event_details.append(
                {
                    "session_id": session_id,
                    "start_ms": start_ms,
                    "end_ms": end_ms,
                    "wake_score": score,
                    "type": "expected",
                }
            )
    misses = sum(count == 0 for count in matched)
    repeats = sum(max(0, count - 1) for count in matched)
    result = {
        "schema": "bkvoice-kws-stream-evaluation-v1",
        "status": "candidate",
        "split": split_name,
        "slice_metrics": slices,
        "slice_error_categories": dict(sorted(slice_errors.items())),
        "stream_metrics": {
            "expected_wakes": len(expected),
            "missed_wakes": misses,
            "repeated_wakes": repeats,
            "background_false_positives": background_false_positives,
            "wake_events": len(events),
            "listening_ms": listening_ms,
            "paused_ms": paused_ms,
            "gap_ms": gap_ms,
            "sessions": len(chosen_sessions),
            "events": event_details,
        },
        "policy": policy,
        "model_sha256": _sha256(model),
        "model_metadata_sha256": _sha256(metadata_path),
        "model_training_dataset_sha256": metadata["dataset_sha256"],
        "evaluation_manifest_sha256": manifest_sha,
        "session_dataset_sha256": _session_digest(chosen_sessions),
        "input_quantization": [
            float(details_in["quantization"][0]),
            int(details_in["quantization"][1]),
        ],
        "output_quantization": [
            float(details_out["quantization"][0]),
            int(details_out["quantization"][1]),
        ],
        "frontend_inputs_sha256": _frontend_provenance(),
        "runtime": {
            "policy_source": "bkvoice_kws_default_policy",
            "inference": "python_tflite_callback_not_tflm_board_equivalence",
        },
        "wake_label": wake_label,
        "wake_phrase": wake_phrase,
        "dataset": {
            "dataset_sha256": report["dataset_sha256"],
            "recording_kind_counts": report["recording_kind_counts"],
        },
    }
    if split_name == "validation":
        evidence = json.dumps(result, sort_keys=True, separators=(",", ":")).encode(
            "utf-8"
        )
        _write_new_json(
            frozen_policy,
            {
                **binding,
                "validation_status": "completed_candidate",
                "validation_result_sha256": hashlib.sha256(evidence).hexdigest(),
            },
        )
    _write_new_json(output, result)
    return result


def train(
    manifest: Path,
    output: Path,
    *,
    epochs: int,
    batch_size: int,
    seed: int,
    onset_hard_negatives: int = 0,
    channels: int = 32,
    unknown_shift_step_ms: int = 100,
    architecture: str = "ds-cnn",
    initial_frequency_stride: int = 2,
    pcm_level_augmentation: bool = False,
    room_augmentation: bool = False,
    positive_end_window_ms: int = 0,
) -> dict[str, Any]:
    """Train and export a full-INT8 candidate; imports ML packages only here."""
    if (
        epochs < 1
        or batch_size < 1
        or channels < 1
        or channels > 64
        or architecture not in ("ds-cnn", "temporal-ds-cnn")
        or initial_frequency_stride not in (1, 2, 4)
        or output.exists()
        or not output.parent.is_dir()
    ):
        _fail("training_arguments_invalid")
    if unknown_shift_step_ms not in (100, 200, 400, 800):
        _fail("unknown_shift_step_invalid")
    if positive_end_window_ms != 0 and (
        positive_end_window_ms < 100
        or positive_end_window_ms > 3000
        or positive_end_window_ms % 100 != 0
    ):
        _fail("positive_end_window_invalid")
    records, report, wake_contract = _validate(manifest)
    wake_label, wake_phrase, class_labels = wake_contract
    document = _read_manifest(manifest)
    if "sessions" in document:
        _validate_sessions(document, manifest, records)
    # Check the evaluation bridge dependency up front so a missing one is not
    # discovered only after training and quantization have finished.
    runtime_policy = _policy_from_c(_kws_library())
    try:
        import numpy as np
        import tensorflow as tf
    except ImportError as error:
        raise KwsError("training_dependencies_unavailable") from error
    tf.keras.utils.set_random_seed(seed)
    try:
        tf.config.experimental.enable_op_determinism()
    except (AttributeError, RuntimeError):
        pass
    positive_end_window_samples = positive_end_window_ms * 16
    original_train_positive_excluded = 0
    base_records = records
    if positive_end_window_samples:
        base_records = []
        for record in records:
            if record["split"] == "train" and record["label"] == wake_label:
                _, end = _speech_span(record["pcm"])
                if end < SAMPLES - positive_end_window_samples:
                    # Never relabel a complete phrase as unknown: it is simply
                    # outside this train-only fresh-end sampling policy.
                    original_train_positive_excluded += 1
                    continue
            base_records.append(record)
    augmented = _training_derivatives(
        records,
        wake_label,
        onset_hard_negatives=onset_hard_negatives,
        unknown_shift_step_ms=unknown_shift_step_ms,
        positive_end_window_ms=positive_end_window_ms,
    )
    train_records = [*base_records, *augmented]
    pcm_gains = (0.25, 0.1) if pcm_level_augmentation else ()
    amplitude_copies = 2 if pcm_level_augmentation else 1
    pcm_gain_counts: Counter[float] = Counter()
    source_windows: Counter[str] = Counter()
    quiet_records: list[dict[str, Any]] = []
    for record in train_records:
        if record["split"] != "train":
            continue
        pcm_gain_counts[1.0] += 1
        if not pcm_gains:
            continue
        source_id = record["source_id"]
        phase = int(hashlib.sha256(source_id.encode("utf-8")).hexdigest()[:8], 16)
        gain = pcm_gains[(phase + source_windows[source_id]) % len(pcm_gains)]
        source_windows[source_id] += 1
        pcm_gain_counts[gain] += 1
        # Keep every original rolling window, including confusable negatives.
        # Attenuate only after complete/partial window labels are fixed:
        # recomputing the voiced span on quiet audio could trim phonemes and
        # incorrectly retain a complete-target label. No split/ID changes.
        pcm = np.frombuffer(record["pcm"], dtype="<i2")
        quiet_records.append(
            {**record, "pcm": np.rint(pcm * gain).astype("<i2").tobytes()}
        )
    train_records.extend(quiet_records)
    room_records: list[dict[str, Any]] = []
    if room_augmentation:
        for record in train_records:
            if record["split"] != "train":
                continue
            # Keep the source, split and full/partial-word label fixed. These
            # are synthetic acoustic variations, not measured room responses.
            identity = (str(seed) + ":" + record["source_id"]).encode() + record["pcm"]
            rng = np.random.default_rng(
                int.from_bytes(hashlib.sha256(identity).digest()[:8], "big")
            )
            pcm = np.frombuffer(record["pcm"], dtype="<i2").astype(np.float64)
            reflected = pcm.copy()
            normalization = 1.0
            for low, high, amplitude in (
                (0.018, 0.045, 0.35),
                (0.045, 0.090, 0.22),
                (0.090, 0.180, 0.12),
            ):
                delay = int(rng.uniform(low, high) * 16000)
                reflected[delay:] += amplitude * pcm[:-delay]
                normalization += amplitude
            offsets = np.arange(-31, 32, dtype=np.float64)
            low_hz, high_hz = rng.uniform(90, 180), rng.uniform(3200, 6500)
            lowpass = (
                2
                * high_hz
                / 16000
                * np.sinc(2 * high_hz / 16000 * offsets)
                * np.hamming(63)
            )
            dc_band = (
                2
                * low_hz
                / 16000
                * np.sinc(2 * low_hz / 16000 * offsets)
                * np.hamming(63)
            )
            kernel = lowpass / lowpass.sum() - dc_band / dc_band.sum()
            filtered = np.convolve(reflected / normalization, kernel, mode="same")
            filtered *= rng.uniform(0.3, 1.0)
            filtered += rng.normal(0, rng.uniform(3, 15), len(filtered))
            room_records.append(
                {
                    **record,
                    "pcm": np.clip(np.rint(filtered), -32768, 32767)
                    .astype("<i2")
                    .tobytes(),
                }
            )
        train_records.extend(room_records)
        amplitude_copies *= 2
    features = _features(train_records, np)
    targets = np.asarray(
        [class_labels.index(record["label"]) for record in train_records],
        dtype=np.int32,
    )
    split = np.asarray([record["split"] for record in train_records])
    train_mask = split == "train"
    validation_mask = split == "validation"
    positive_by_source = Counter(
        record["source_id"]
        for record in train_records
        if record["split"] == "train" and record["label"] == wake_label
    )
    positive_total = sum(positive_by_source.values())
    source_count = len(positive_by_source)
    if not positive_by_source or positive_total <= 0 or source_count <= 0:
        _fail("training_positive_source_weight_invalid")
    # Extra amplitude coverage must not silently increase the class or
    # source weight relative to ordinary speech and background negatives.
    source_weight_total = float(positive_total) / amplitude_copies
    positive_weights = {
        source_id: source_weight_total / (source_count * count)
        for source_id, count in positive_by_source.items()
    }
    sample_weights = np.full(
        len(train_records), 1.0 / amplitude_copies, dtype=np.float32
    )
    ordinary_unknown_by_source = Counter(
        record["source_id"]
        for record in train_records
        if record["split"] == "train"
        and record["label"] == "unknown"
        and record.get("augmentation") in (None, "ordinary_speech_zero_padded_shift")
    )
    if not ordinary_unknown_by_source or any(
        count <= 0 for count in ordinary_unknown_by_source.values()
    ):
        _fail("training_unknown_source_weight_invalid")
    ordinary_unknown_categories: dict[str, str] = {}
    for record in train_records:
        if (
            record["split"] != "train"
            or record["label"] != "unknown"
            or record.get("augmentation")
            not in (None, "ordinary_speech_zero_padded_shift")
        ):
            continue
        source_id, category = record["source_id"], record["category"]
        previous = ordinary_unknown_categories.setdefault(source_id, category)
        if previous != category:
            _fail("training_unknown_source_category_invalid")
    ordinary_unknown_source_totals = {
        source_id: (
            source_weight_total / source_count if category == "near_homophone" else 5.0
        )
        for source_id, category in ordinary_unknown_categories.items()
    }
    ordinary_unknown_weights = {
        source_id: ordinary_unknown_source_totals[source_id] / count
        for source_id, count in ordinary_unknown_by_source.items()
    }
    for index, record in enumerate(train_records):
        if record["split"] == "train" and record["label"] == wake_label:
            sample_weights[index] = positive_weights[record["source_id"]]
        elif (
            record["split"] == "train"
            and record["label"] == "unknown"
            and record.get("augmentation")
            in (None, "ordinary_speech_zero_padded_shift")
        ):
            sample_weights[index] = ordinary_unknown_weights[record["source_id"]]
    # Feature extraction has consumed PCM. Retain source/label metadata, but
    # release the much larger augmented waveforms before TensorFlow fits.
    for record in train_records:
        record.pop("pcm", None)
    train_indices = np.flatnonzero(train_mask)
    shuffle_rng = np.random.default_rng(seed)

    def training_rows():
        # Avoid a full advanced-index copy and another complete TensorFlow
        # tensor of the same features. Only one batch is prefetched.
        for index in shuffle_rng.permutation(train_indices):
            yield features[index], targets[index], sample_weights[index]

    training_data = tf.data.Dataset.from_generator(
        training_rows,
        output_signature=(
            tf.TensorSpec((FEATURE_ROWS, 40, 1), tf.float32),
            tf.TensorSpec((), tf.int32),
            tf.TensorSpec((), tf.float32),
        ),
    )
    data_options = tf.data.Options()
    data_options.experimental_deterministic = True
    data_options.threading.private_threadpool_size = 1
    training_data = (
        training_data.with_options(data_options).batch(batch_size).prefetch(1)
    )
    # The temporal convolution projects the full 40 frequency bands first and
    # then extracts features along the time axis only, so no extra on-device
    # operator is required.
    temporal = architecture == "temporal-ds-cnn"
    frequency_kernel = 40 if temporal else 4
    frequency_stride = 40 if temporal else initial_frequency_stride
    frequency_positions = (40 + frequency_stride - 1) // frequency_stride
    depthwise_frequency = 1 if temporal else 3
    depthwise_blocks = (
        ((3, depthwise_frequency), (2, 1)),
        ((3, depthwise_frequency), (2, 1)),
        ((9, depthwise_frequency), (1, 1)),
        ((9, depthwise_frequency), (1, 1)),
    )
    model = tf.keras.Sequential(
        [
            tf.keras.layers.Input((FEATURE_ROWS, 40, 1)),
            tf.keras.layers.Conv2D(
                channels,
                (10, frequency_kernel),
                strides=(2, frequency_stride),
                padding="same",
                use_bias=False,
            ),
            tf.keras.layers.BatchNormalization(),
            tf.keras.layers.ReLU(),
            *[
                layer
                for kernel, strides in depthwise_blocks
                for layer in (
                    tf.keras.layers.DepthwiseConv2D(
                        kernel, strides=strides, padding="same", use_bias=False
                    ),
                    tf.keras.layers.BatchNormalization(),
                    tf.keras.layers.ReLU(),
                    tf.keras.layers.Conv2D(channels, (1, 1), use_bias=False),
                    tf.keras.layers.BatchNormalization(),
                    tf.keras.layers.ReLU(),
                )
            ],
            # 149 -> 75 -> 38 -> 19 time positions; pool the whole final map.
            tf.keras.layers.AveragePooling2D((19, frequency_positions)),
            tf.keras.layers.Flatten(),
            tf.keras.layers.Dense(3, activation="softmax"),
        ]
    )
    model.compile(
        optimizer="adam", loss="sparse_categorical_crossentropy", metrics=["accuracy"]
    )
    early = tf.keras.callbacks.EarlyStopping(
        monitor="val_loss", patience=20, restore_best_weights=True
    )
    # Report actual epochs for long runs; stdout stays reserved for the final
    # JSON result.
    progress = tf.keras.callbacks.LambdaCallback(
        on_epoch_end=lambda epoch, logs: print(
            json.dumps(
                {
                    "stage": "training_epoch",
                    "epoch": epoch + 1,
                    **{key: float(value) for key, value in (logs or {}).items()},
                }
            ),
            file=sys.stderr,
            flush=True,
        )
    )
    history = model.fit(
        training_data,
        epochs=epochs,
        validation_data=(features[validation_mask], targets[validation_mask]),
        verbose=0,
        callbacks=[early, progress],
    )
    # Keras restores best_weights only when its patience actually stops the
    # fit.  A bounded run can finish first, so export the observed best epoch.
    if early.best_weights is None:
        _fail("training_best_weights_unavailable")
    model.set_weights(early.best_weights)
    float_validation = _evaluate_float(
        model, features[validation_mask], targets[validation_mask], np, class_labels
    )
    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.representative_dataset = lambda: (
        [features[index : index + 1]] for index in np.where(train_mask)[0]
    )
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter.inference_input_type = tf.int8
    converter.inference_output_type = tf.int8
    candidate = converter.convert()
    if not 1 <= len(candidate) <= 65536:
        _fail("model_export_size_invalid")
    output.mkdir(mode=0o700)
    model_path = output / "model_int8.tflite"
    model_path.write_bytes(candidate)
    interpreter = tf.lite.Interpreter(model_path=str(model_path))
    interpreter.allocate_tensors()
    actual_input_shape = interpreter.get_input_details()[0]["shape"].tolist()
    actual_output_shape = interpreter.get_output_details()[0]["shape"].tolist()
    actual_operators = sorted(
        {
            detail["op_name"]
            for detail in interpreter._get_ops_details()
            if detail["op_name"] != "DELEGATE"
        }
    )
    expected_operators = [
        "AVERAGE_POOL_2D",
        "CONV_2D",
        "DEPTHWISE_CONV_2D",
        "FULLY_CONNECTED",
        "RESHAPE",
        "SOFTMAX",
    ]
    if (
        actual_input_shape != [1, FEATURE_ROWS, 40, 1]
        or actual_output_shape != [1, len(class_labels)]
        or actual_operators != expected_operators
    ):
        _fail("model_export_incompatible")
    # Test stays frozen until a separately bound ``kws evaluate --split test``.
    metrics = {
        "float_validation": float_validation,
        "validation": _evaluate_int8(
            interpreter,
            features[validation_mask],
            targets[validation_mask],
            np,
            class_labels,
        ),
    }
    in_q = interpreter.get_input_details()[0]["quantization"]
    out_q = interpreter.get_output_details()[0]["quantization"]
    metadata = {
        **report,
        "wake_label": wake_label,
        "wake_phrase": wake_phrase,
        "audio_seconds": 3,
        "input_pipeline": "source_preserving_tf_data_one_prefetched_batch",
        "architecture": f"{architecture}-conv{channels}-10x{frequency_kernel}-s2xf{frequency_stride}-dw3x{depthwise_frequency}-s2-dw3x{depthwise_frequency}-s2-dw9x{depthwise_frequency}-dw9x{depthwise_frequency}-pw{channels}-bn-relu-global-pool19x{frequency_positions}-flatten-dense3",
        "architecture_family": architecture,
        "channels": channels,
        "initial_frequency_kernel": frequency_kernel,
        "initial_frequency_stride": frequency_stride,
        "depthwise_blocks": [
            {"kernel": list(kernel), "strides": list(strides)}
            for kernel, strides in depthwise_blocks
        ],
        "theoretical_receptive_field_feature_frames": 150,
        "theoretical_receptive_field_ms": 3000,
        "theoretical_convolution_mac_per_inference": frequency_positions
        * (
            (750 * frequency_kernel + 513 * depthwise_frequency) * channels
            + 95 * channels * channels
        ),
        "theoretical_compute_note": "architecture estimate; not board latency or measured MAC evidence",
        "exported_operators": actual_operators,
        "seed": seed,
        "tensorflow_version": tf.__version__,
        "model_sha256": _sha256(model_path),
        "epochs": epochs,
        "epochs_completed": len(history.history["loss"]),
        "best_validation_epoch": early.best_epoch + 1,
        "best_validation_loss": float(history.history["val_loss"][early.best_epoch]),
        "batch_size": batch_size,
        "training_recipe": {
            "base": "trigger442 archived v30",
            "batch_normalization_momentum": 0.99,
            "optimizer": "keras Adam defaults",
            "room_augmentation": {
                "enabled": room_augmentation,
                "window_count": len(room_records),
                "response": "synthetic three reflections at 18-180 ms; not measured RIR",
                "bandwidth_hz": {"low": [90, 180], "high": [3200, 6500]},
                "gain": [0.3, 1.0],
                "noise_pcm_stddev": [3, 15],
                "stage": "train PCM before official frontend; original labels and source IDs retained",
                "validation_and_test": "unchanged",
            },
            "pcm_level_augmentation": {
                "gains": [1.0, *pcm_gains],
                "window_counts": dict(pcm_gain_counts),
                "preserve_full_level_windows": True,
                "assignment": "sha256(source_id) first 32 bits plus source window index modulo gain count",
                "stage": "train PCM after label-preserving window derivation, before official frontend",
                "validation_and_test": "unchanged",
            },
            "unknown_zero_padded_shifts": any(
                record.get("augmentation") == "ordinary_speech_zero_padded_shift"
                for record in augmented
            ),
            "unknown_shift_step_ms": unknown_shift_step_ms,
            "positive_end_window": {
                "milliseconds": positive_end_window_ms,
                "mode": (
                    "all_legal_complete_positions"
                    if not positive_end_window_ms
                    else "complete_phrase_end_within_final_window"
                ),
                "original_train_positive_excluded": original_train_positive_excluded,
            },
            "positive_source_weighting": {
                "rule": "unaugmented_positive_total/(positive_source_count*source_positive_windows)",
                "positive_windows": positive_total,
                "positive_source_count": source_count,
                "per_source_total_weight": source_weight_total / source_count,
                "weight_min": min(positive_weights.values()),
                "weight_max": max(positive_weights.values()),
                "positive_total_weight": sum(
                    positive_weights[record["source_id"]]
                    for record in train_records
                    if record["split"] == "train" and record["label"] == wake_label
                ),
                "non_positive_weight": 1.0 / amplitude_copies,
                "validation_weighting": "none",
            },
            "ordinary_unknown_source_weighting": {
                "eligible_records": "original_unknown_or_ordinary_speech_zero_padded_shift",
                "source_count": len(ordinary_unknown_by_source),
                "source_counts_by_category": dict(
                    Counter(ordinary_unknown_categories.values())
                ),
                "source_total_weight_by_category": {
                    "near_homophone": source_weight_total / source_count,
                    "other_unknown": 5.0,
                },
                "weight_min": min(ordinary_unknown_weights.values()),
                "weight_max": max(ordinary_unknown_weights.values()),
                "total_weight": sum(
                    ordinary_unknown_weights[record["source_id"]]
                    for record in train_records
                    if record["split"] == "train"
                    and record["label"] == "unknown"
                    and record.get("augmentation")
                    in (None, "ordinary_speech_zero_padded_shift")
                ),
                "other_derived_unknown_weight": 1.0 / amplitude_copies,
                "validation_weighting": "none",
            },
            "complete_target_sliding_windows": True,
        },
        "real_environment_train_entries": sum(
            record["split"] == "train"
            and record["label"] == "silence"
            and record["recording_kind"] == "real"
            for record in records
        ),
        "onset_hard_negatives": onset_hard_negatives,
        "training_derivatives": dict(
            Counter(record["augmentation"] for record in augmented)
        ),
        "training_curve": {
            key: [float(value) for value in values]
            for key, values in history.history.items()
        },
        "frontend_inputs_sha256": _frontend_provenance(),
        "training_source_sha256": _sha256(Path(__file__)),
        "input_shape": actual_input_shape,
        "output_shape": actual_output_shape,
        "input_quantization": [float(in_q[0]), int(in_q[1])],
        "output_quantization": [float(out_q[0]), int(out_q[1])],
        "model_authorization": {
            "status": "candidate",
            "accepted_for_board": False,
            "training_data_authorization": "manifest_entry_consent_true",
        },
        "runtime_policy": {"source": "bkvoice_kws_default_policy", **runtime_policy},
        "stream_evaluation": {
            "status": "not_run",
            "reason": "requires_real_timed_sessions_and_separate_evaluate_invocation",
        },
        "test_evaluation": {
            "status": "not_run",
            "reason": "requires_validation_frozen_policy_and_separate_evaluate_invocation",
        },
        "metrics": metrics,
    }
    (output / "metadata.json").write_text(
        json.dumps(metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return metadata


def run(args: argparse.Namespace) -> dict[str, Any]:
    """CLI dispatcher used by the parent ``voice`` command."""
    if args.kws_command == "audit":
        return audit(args.manifest)
    if args.kws_command == "train":
        return train(
            args.manifest,
            args.output,
            epochs=args.epochs,
            batch_size=args.batch_size,
            seed=args.seed,
            onset_hard_negatives=args.onset_hard_negatives,
            unknown_shift_step_ms=args.unknown_shift_step_ms,
            channels=args.channels,
            architecture=args.architecture,
            initial_frequency_stride=args.initial_frequency_stride,
            pcm_level_augmentation=args.pcm_level_augmentation,
            room_augmentation=args.room_augmentation,
            positive_end_window_ms=args.positive_end_window_ms,
        )
    if args.kws_command == "evaluate":
        return evaluate(
            args.manifest, args.model, args.output, args.frozen_policy, args.split
        )
    if args.kws_command == "package":
        return package(args.model, args.metadata, args.output)
    raise KwsError("command_invalid")
