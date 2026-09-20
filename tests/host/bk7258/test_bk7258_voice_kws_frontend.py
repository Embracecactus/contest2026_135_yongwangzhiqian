#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""TFLM microfrontend host checks; no speech corpus is used."""

from __future__ import annotations

import ctypes
import subprocess
import tempfile
from pathlib import Path

import numpy as np


ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "app/bk7258/bk7258_voice_kws_frontend.c"
WORKSPACE = ROOT.parent
TFLM = WORKSPACE / "apps/mlearning/tflite-micro/tflite-micro"
LIB = TFLM / "tensorflow/lite/experimental/microfrontend/lib"
KISSFFT = WORKSPACE / "apps/math/kissfft/kissfft"
SAMPLES, WINDOW, HOP, BINS, ROWS = 32000, 480, 320, 40, 99

UPSTREAM_SOURCES = (
    "frontend.c",
    "frontend_util.c",
    "fft.cc",
    "fft_util.cc",
    "kiss_fft_int16.cc",
    "filterbank.c",
    "filterbank_util.c",
    "log_lut.c",
    "log_scale.c",
    "log_scale_util.c",
    "noise_reduction.c",
    "noise_reduction_util.c",
    "pcan_gain_control.c",
    "pcan_gain_control_util.c",
    "window.c",
    "window_util.c",
)


def _library(directory: Path) -> ctypes.CDLL:
    output = directory / "frontend.so"
    sources = [SOURCE, *(LIB / item for item in UPSTREAM_SOURCES)]
    assert KISSFFT.joinpath("kiss_fft.c").is_file()
    assert KISSFFT.joinpath("tools/kiss_fftr.c").is_file()
    objects = []
    for index, source in enumerate(sources):
        object_file = directory / f"frontend-{index}.o"
        compiler = "c++" if source.suffix == ".cc" else "cc"
        standard = "-std=c++17" if source.suffix == ".cc" else "-std=c11"
        result = subprocess.run(
            [
                compiler,
                "-c",
                "-fPIC",
                standard,
                "-O2",
                f"-I{TFLM}",
                f"-I{KISSFFT}",
                str(source),
                "-o",
                str(object_file),
            ],
            check=False,
            capture_output=True,
        )
        assert result.returncode == 0, result.stderr.decode(errors="replace")
        objects.append(object_file)
    result = subprocess.run(
        ["c++", "-shared", "-o", str(output), *(str(item) for item in objects), "-lm"],
        check=False,
        capture_output=True,
    )
    assert result.returncode == 0, result.stderr.decode(errors="replace")
    library = ctypes.CDLL(str(output))
    library.bkvoice_kws_features.argtypes = [
        ctypes.POINTER(ctypes.c_int16),
        ctypes.c_size_t,
        ctypes.POINTER(ctypes.c_float),
        ctypes.c_size_t,
    ]
    library.bkvoice_kws_features.restype = ctypes.c_int
    return library


def test_c_frontend_returns_raw_microfrontend_values() -> None:
    samples = np.asarray(
        ((np.arange(SAMPLES, dtype=np.int64) * 173) % 65536) - 32768, dtype=np.int16
    )
    with tempfile.TemporaryDirectory(prefix="bkvoice-kws-frontend-") as name:
        library = _library(Path(name))
        result = np.empty((ROWS, BINS), dtype=np.float32)
        assert (
            library.bkvoice_kws_features(
                samples.ctypes.data_as(ctypes.POINTER(ctypes.c_int16)),
                SAMPLES,
                result.ctypes.data_as(ctypes.POINTER(ctypes.c_float)),
                result.size,
            )
            == 0
        )
    assert np.all(np.isfinite(result))
    assert np.all(result >= 0.0)
    assert np.all(result == np.floor(result))


def test_frontend_rejects_wrong_window_or_output_count() -> None:
    samples = np.zeros(SAMPLES, dtype=np.int16)
    result = np.empty(ROWS * BINS, dtype=np.float32)
    with tempfile.TemporaryDirectory(prefix="bkvoice-kws-frontend-") as name:
        library = _library(Path(name))
        pointer = samples.ctypes.data_as(ctypes.POINTER(ctypes.c_int16))
        output = result.ctypes.data_as(ctypes.POINTER(ctypes.c_float))
        assert (
            library.bkvoice_kws_features(pointer, SAMPLES - 1, output, result.size) < 0
        )
        assert (
            library.bkvoice_kws_features(pointer, SAMPLES, output, result.size - 1) < 0
        )
