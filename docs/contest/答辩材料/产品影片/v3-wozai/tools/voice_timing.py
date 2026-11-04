#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Measure speech onset/offset of every voice file → script/voice-timing.json.

The edit places files by their start time; subtitles and lip flaps use the
measured speech span inside each file (energy above -45 dBFS, 20 ms frames).

Usage: python3 tools/voice_timing.py
"""
import json
from pathlib import Path

import numpy as np
import soundfile as sf

ROOT = Path(__file__).resolve().parents[1]


def span(path):
    x, sr = sf.read(path, always_2d=False)
    if x.ndim > 1:
        x = x.mean(axis=1)
    hop = int(0.02 * sr)
    n = len(x) // hop
    e = 20 * np.log10(np.sqrt(np.mean(x[: n * hop].reshape(n, hop) ** 2, axis=1)) + 1e-9)
    on = np.flatnonzero(e > -45)
    if on.size == 0:
        return {"dur": round(len(x) / sr, 3), "on": 0.0, "off": round(len(x) / sr, 3)}
    return {"dur": round(len(x) / sr, 3), "on": round(on[0] * hop / sr, 3), "off": round(min(len(x), (on[-1] + 1) * hop) / sr, 3)}


def main():
    out = {}
    for d in (ROOT / "audio/voice/zhouyu/directed", ROOT / "audio/voice/shaniu"):
        for p in sorted(d.glob("*.wav")):
            out[p.stem] = span(p)
    (ROOT / "script/voice-timing.json").write_text(json.dumps(out, indent=1, ensure_ascii=False) + "\n")
    print(len(out), "files")


if __name__ == "__main__":
    main()
