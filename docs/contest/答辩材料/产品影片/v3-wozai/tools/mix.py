#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Place every voice, effect and the score from src/edit.js into editable stems.

Stems (48 kHz, 24-bit, stereo): narration.wav (周屿's voice-over), dialogue.wav
(周屿 on screen, Shaniu's answers and the project "我在" asset through a small
desk-speaker colour), music.wav (ducked under speech), sfx.wav, and mix.wav.
The master (-16 LUFS, -1 dBTP) is made by tools/master.sh with FFmpeg.

Usage: python3 tools/mix.py [--range START END] [--out DIR]
"""
import argparse
import json
import re
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy import signal

ROOT = Path(__file__).resolve().parents[1]
SR = 48_000


import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from editlib import load_edit, film_from_story, film_duration  # noqa: E402


def read(path):
    x, sr = sf.read(path, always_2d=False)
    if x.ndim > 1:
        x = x.mean(axis=1)
    if sr != SR:
        import librosa
        x = librosa.resample(x.astype(np.float32), orig_sr=sr, target_sr=SR, res_type="soxr_hq")
    return x.astype(np.float64)


def db(g):
    return 10 ** (g / 20)


def speaker(x):
    """A small desk speaker heard from ~60 cm: band-limited, a touch of room."""
    b, a = signal.butter(2, [260 / (SR / 2), 7200 / (SR / 2)], "band")
    y = signal.lfilter(b, a, x)
    b2, a2 = signal.butter(1, 2400 / (SR / 2), "high")
    y = y + 0.18 * signal.lfilter(b2, a2, y)
    return room(y, 0.10, 0.08)


def room(x, wet=0.12, decay=0.25, seed=7):
    n = int(decay * SR)
    rng = np.random.default_rng(seed)
    ir = rng.standard_normal(n) * np.exp(-np.arange(n) / (decay * SR / 5))
    ir[0] = 0
    ir /= np.sqrt(np.sum(ir ** 2))
    return x + wet * signal.fftconvolve(x, ir)[: len(x)]


def add(buf, x, at, gain=1.0):
    i = int(round(at * SR))
    if i >= len(buf):
        return
    if i < 0:
        x = x[-i:]
        i = 0
    n = min(len(x), len(buf) - i)
    buf[i:i + n] += x[:n] * gain


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--range", nargs=2, type=float)
    ap.add_argument("--out", type=Path, default=ROOT / "audio/stems")
    args = ap.parse_args()
    E = load_edit()
    F = lambda t: film_from_story(E, t)          # cue times are story time; place in film time
    total = film_duration(E)
    narr = np.zeros(int(total * SR))
    dlg = np.zeros_like(narr)
    sfx = np.zeros_like(narr)
    gg = ROOT / "audio/voice/zhouyu/directed"
    sh = ROOT / "audio/voice/shaniu"
    duck_spans = []
    placements = []
    for line in E["lines"]:
        i = line["id"]
        g = db(line.get("gain_db", 0))
        if i.startswith("N"):
            x = read(gg / f"{i}.wav")
            add(narr, x, F(line["at"]), g * 0.95)
        elif i.startswith("D"):
            x = room(read(gg / f"{i}.wav"), 0.10, 0.22)
            if line.get("tag", "").startswith("whisper"):
                b, a = signal.butter(2, 380 / (SR / 2), "high")
                x = signal.lfilter(b, a, x)
            add(dlg, x, F(line["at"]), g * 0.9)
        else:
            x = speaker(read(sh / f"{i}.wav"))
            add(dlg, x, F(line["at"]), g * 0.85)
        placements.append({"id": i, "at": line["at"], "film_at": round(F(line["at"]), 3), "seconds": round(len(x) / SR, 3), "tag": line.get("tag")})
        if line.get("duck_under"):
            for other in E["lines"]:
                if other["id"] in line["duck_under"]:
                    duck_spans.append((F(other["at"]) - 0.2, F(other["at"]) + len(read(gg / f"{other['id']}.wav")) / SR + 0.1, line["at"]))
    # Shaniu's lecture sits under the narration that talks over it.
    for a, b, owner in duck_spans:
        ia, ib = int(a * SR), int(b * SR)
        ramp = int(0.2 * SR)
        gain = np.ones(ib - ia) * db(-11)
        gain[:ramp] = np.linspace(1, db(-11), ramp)
        gain[-ramp:] = np.linspace(db(-11), 1, ramp)
        dlg[ia:ib] *= gain
    lib = ROOT / "audio/sfx"
    rhythm = np.frombuffer((lib / "local_rhythm.pcm").read_bytes(), dtype="<i2").astype(np.float64) / 32768
    import librosa
    rhythm = librosa.resample(rhythm.astype(np.float32), orig_sr=16_000, target_sr=SR, res_type="soxr_hq").astype(np.float64)
    for cue in E["sfx"]:
        g = db(cue.get("gain_db", -18))
        if cue["id"] == "local_rhythm":
            x = speaker(rhythm / (np.max(np.abs(rhythm)) or 1) * 0.8)
            add(dlg, x, F(cue["at"]), g)          # device sound: lives with her voice
            continue
        x = read(lib / f"{cue['id']}.wav")
        if cue.get("loop"):
            until = F(cue["until"])
            n = int((until - F(cue["at"])) * SR)
            reps = int(np.ceil(n / len(x))) + 1
            y = np.tile(x, reps)[:n]
            f = int(1.2 * SR)
            if n > 2 * f:
                y[:f] *= np.linspace(0, 1, f)
                y[-f:] *= np.linspace(1, 0, f)
            add(sfx, y, F(cue["at"]), g)
        else:
            add(sfx, x, F(cue["at"]), g)
    music_path = ROOT / "audio/music/score.wav"
    music = np.zeros_like(narr)
    if music_path.exists():
        m = read(music_path)
        add(music, m, 0.0, 1.0)
        # Duck under any speech.
        speech = np.abs(narr) + np.abs(dlg)
        win = int(0.25 * SR)
        envl = np.sqrt(np.convolve(speech ** 2, np.ones(win) / win, "same"))
        target = np.where(envl > 0.01, db(-7), 1.0)
        b, a = signal.butter(1, 1.5 / (SR / 2), "low")
        music *= signal.filtfilt(b, a, target)
    lo, hi = (0, total) if not args.range else args.range
    ia, ib = int(lo * SR), int(hi * SR)
    out = args.out
    out.mkdir(parents=True, exist_ok=True)

    def st(x):
        x = x[ia:ib]
        return np.stack([x, x], axis=1).astype(np.float32)
    stems = {"narration": narr, "dialogue": dlg, "music": music, "sfx": sfx}
    for k, v in stems.items():
        sf.write(out / f"{k}.wav", st(v), SR, subtype="PCM_24")
    mix = narr + dlg + music * 0.9 + sfx
    peak = np.max(np.abs(mix[ia:ib])) or 1
    if peak > 0.98:
        mix *= 0.98 / peak
    sf.write(out / "mix.wav", st(mix), SR, subtype="PCM_24")
    (out / "mix.json").write_text(json.dumps({"range": [lo, hi], "rate": SR, "placements": placements,
                                              "stems": list(stems) + ["mix"], "music": music_path.exists()}, ensure_ascii=False, indent=1) + "\n")
    print("stems written", out, "range", lo, hi)


if __name__ == "__main__":
    main()
