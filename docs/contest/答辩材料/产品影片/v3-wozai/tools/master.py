#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Master audio/stems/mix.wav to -16 LUFS integrated, ≤ -1 dBTP.

A static gain to the loudness target followed by a 4x-oversampled look-ahead
peak limiter (no loudness AGC, so quiet scenes stay quiet). The result is
measured with FFmpeg ebur128 (true peak); gain and ceiling are adjusted until
I = -16 ± 0.3 LUFS and true peak ≤ -1 dBTP.
Writes audio/master/master.wav and master.json.

Usage: python3 tools/master.py
"""
import json
import re
import subprocess
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy import signal
from scipy.ndimage import minimum_filter1d

ROOT = Path(__file__).resolve().parents[1]
SRC, DST = ROOT / 'audio/stems/mix.wav', ROOT / 'audio/master'
TARGET_I, TARGET_TP = -16.0, -1.0


def measure(path):
    e = subprocess.run(['ffmpeg', '-hide_banner', '-nostats', '-i', str(path), '-af', 'ebur128=peak=true', '-f', 'null', '-'],
                       capture_output=True, text=True).stderr
    I = float(re.findall(r'I:\s+(-?[\d.]+) LUFS', e)[-1]); TP = float(re.findall(r'Peak:\s+(-?[\d.]+) dBFS', e)[-1])
    LRA = float(re.findall(r'LRA:\s+(-?[\d.]+) LU', e)[-1])
    return I, TP, LRA


def true_peak_limit(x, sr, ceiling_db, look_ms=3.0, release_ms=90.0):
    """Look-ahead limiter on 4x-oversampled peaks (approximates true peak)."""
    up = signal.resample_poly(x, 4, 1, axis=0)
    pk = np.abs(up).max(axis=1)
    n = len(x)
    pk = pk[: n * 4].reshape(n, 4).max(axis=1)
    thr = 10 ** (ceiling_db / 20)
    want = np.minimum(1.0, thr / np.maximum(pk, 1e-9))
    look = max(1, int(look_ms * sr / 1000))
    want = minimum_filter1d(want, size=2 * look + 1, origin=0)
    a = np.exp(-1.0 / (release_ms * sr / 1000))
    g = signal.lfilter([1 - a], [1, -a], want - 1.0) + 1.0      # smooth recovery
    g = np.minimum(g, want)                                         # never above the needed reduction
    return x * g[:, None]


def main():
    DST.mkdir(parents=True, exist_ok=True)
    I0, TP0, LRA0 = measure(SRC)
    x, sr = sf.read(SRC, always_2d=True)
    gain = TARGET_I - I0
    ceiling, out, log = -1.5, DST / 'master.wav', []
    for _ in range(6):
        y = true_peak_limit(x * 10 ** (gain / 20), sr, ceiling)
        sf.write(out, y.astype(np.float32), sr, subtype='PCM_24')
        I, TP, LRA = measure(out)
        log.append({'gain_db': round(gain, 2), 'ceiling_db': ceiling, 'I': I, 'TP': TP, 'LRA': LRA})
        if TP <= TARGET_TP and abs(I - TARGET_I) <= 0.3:
            break
        if TP > TARGET_TP: ceiling -= 0.3
        if abs(I - TARGET_I) > 0.3: gain += TARGET_I - I
    (DST / 'master.json').write_text(json.dumps({'source': 'audio/stems/mix.wav', 'input': {'I': I0, 'TP': TP0, 'LRA': LRA0},
                                                 'target': {'I': TARGET_I, 'TP_max': TARGET_TP}, 'passes': log,
                                                 'result': log[-1], 'method': 'static gain + 4x-oversampled look-ahead peak limiter (numpy), verified by FFmpeg ebur128 true peak'}, indent=1) + '\n')
    print('master', log[-1])


if __name__ == '__main__':
    main()
