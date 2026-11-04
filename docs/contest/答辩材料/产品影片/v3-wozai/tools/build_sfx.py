#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Synthesize the film's sound effects (original, seeded, no samples).

Only sounds with a cause on screen. Shaniu makes no sound the real device
does not make: no NFC beep, no camera shutter, no power chime. Her two real
sounds are the haptic motor pulse (40 ms, bk7258_haptic_service) and the local
"四拍节律" PCM, which the mixer takes from app/bk7258/assets/local_rhythm.pcm.

Usage: python3 tools/build_sfx.py   (writes audio/sfx/*.wav, 48 kHz mono)
"""
import json
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy import signal

SR = 48_000
OUT = Path(__file__).resolve().parents[1] / "audio/sfx"
rng = np.random.default_rng(1203)


def t(d):
    return np.arange(int(d * SR)) / SR


def env(n, a=0.002, r=0.05):
    x = np.arange(n) / SR
    return np.minimum(1, x / max(a, 1e-4)) * np.exp(-np.maximum(0, x - a) / max(r, 1e-4))


def bp(x, lo, hi, order=2):
    b, a = signal.butter(order, [lo / (SR / 2), hi / (SR / 2)], "band")
    return signal.lfilter(b, a, x)


def lp(x, f, order=2):
    b, a = signal.butter(order, f / (SR / 2), "low")
    return signal.lfilter(b, a, x)


def hp(x, f, order=2):
    b, a = signal.butter(order, f / (SR / 2), "high")
    return signal.lfilter(b, a, x)


def noise(d):
    return rng.standard_normal(int(d * SR))


def norm(x, peak=0.8):
    m = np.max(np.abs(x)) or 1
    return x / m * peak


def click(d=0.012, lo=1500, hi=7000, r=0.004):
    n = int(d * SR)
    return bp(noise(d), lo, hi) * env(n, 0.0005, r)


def place(total, events):
    out = np.zeros(int(total * SR))
    for at, x in events:
        i = int(at * SR)
        out[i:i + len(x)] += x[: max(0, len(out) - i)]
    return out


def rain(d=12.0, heavy=1.0):
    base = lp(hp(noise(d), 400), 5000) * 0.35 * heavy
    drops = np.zeros(int(d * SR))
    for _ in range(int(220 * d * heavy)):
        i = rng.integers(0, len(drops) - 400)
        drops[i:i + 300] += click(300 / SR, 1800, 9000, 0.0015)[:300] * rng.uniform(0.2, 1.0)
    x = base + drops * 0.5
    # Loopable: crossfade tail into head.
    f = int(0.5 * SR)
    x[:f] = x[:f] * np.linspace(0, 1, f) + x[-f:] * np.linspace(1, 0, f)
    return norm(x[:-f], 0.5)


def storm(d=14.0):
    r = rain(d, 1.5)
    wind = lp(noise(len(r) / SR), 380) * (0.6 + 0.4 * np.sin(2 * np.pi * 0.11 * t(len(r) / SR)))
    return norm(r + norm(wind, 0.35), 0.6)


def thunder(d=5.5):
    x = t(d)
    crack = hp(noise(0.25), 800) * env(int(0.25 * SR), 0.003, 0.06)
    rumble = lp(noise(d), 140, 4) * (np.exp(-x / 1.6) * (1 + 0.6 * np.sin(2 * np.pi * 1.7 * x) ** 2))
    out = norm(rumble, 0.9)
    out[: len(crack)] += crack * 0.5
    return norm(out, 0.95)


def typing(n=4, gap=0.13):
    return norm(place(n * gap + 0.1, [(i * gap + rng.uniform(0, 0.03), click(0.02, 900, 5000, 0.006) * rng.uniform(0.6, 1)) for i in range(n)]), 0.5)


def thud(f=85, d=0.25):
    x = t(d)
    return norm(np.sin(2 * np.pi * f * x) * np.exp(-x / 0.06) + lp(noise(d), 600) * np.exp(-x / 0.03) * 0.5, 0.8)


def slide(d=0.7, lo=300, hi=1600):
    x = noise(d)
    y = np.zeros_like(x)
    seg = int(0.05 * SR)
    for k in range(0, len(x) - seg, seg):
        c = lo + (hi - lo) * k / len(x)
        y[k:k + seg] = bp(x[k:k + seg], c * 0.7, c * 1.3)
    e = np.minimum(1, np.linspace(0, 6, len(x))) * np.minimum(1, np.linspace(6, 0, len(x)))
    return norm(y * e, 0.5)


def metal(freqs=(2180, 3420, 5210, 7350), d=0.6, decay=0.12):
    x = t(d)
    y = sum(np.sin(2 * np.pi * f * x + rng.uniform(0, 6)) * np.exp(-x / (decay * (1 - i * 0.15))) for i, f in enumerate(freqs))
    c = click(0.01)
    y[: len(c)] += c * 0.6
    return norm(y, 0.6)


def pour(d=2.0):
    x = bp(noise(d), 500, 3200) * (0.6 + 0.4 * np.abs(np.sin(2 * np.pi * 7 * t(d))))
    for _ in range(int(25 * d)):
        i = rng.integers(0, len(x) - 2000)
        f = rng.uniform(500, 1400)
        tt = t(0.04)
        x[i:i + len(tt)] += np.sin(2 * np.pi * f * (1 + 2 * tt) * tt) * np.exp(-tt / 0.012) * 0.8
    e = np.minimum(1, np.linspace(0, 8, len(x))) * np.minimum(1, np.linspace(5, 0, len(x)))
    return norm(x * e, 0.5)


def whoosh(d=0.9, up=True):
    x = noise(d)
    y = np.zeros_like(x)
    seg = int(0.03 * SR)
    for k in range(0, len(x) - seg, seg):
        p = k / len(x)
        c = 300 + 2500 * (p if up else 1 - p)
        y[k:k + seg] = bp(x[k:k + seg], c * 0.6, c * 1.4)
    e = np.sin(np.pi * np.linspace(0, 1, len(x))) ** 2
    return norm(y * e, 0.45)


def haptic(on=0.04):
    # Motor pulse heard on a desk: 40 ms completion pulse, 60 ms approval alert
    # (product_companion_pulse(40) / (60) in bk7258_agent_companion.inc).
    x = t(on + 0.05)
    buzz = np.sign(np.sin(2 * np.pi * 172 * x)) * 0.5 + np.sin(2 * np.pi * 344 * x) * 0.3
    e = (x < on) * 1.0 + (x >= on) * np.exp(-(x - on) / 0.012)
    return norm(lp(buzz * e, 2500), 0.7)


def birds(d=10.0):
    out = np.zeros(int(d * SR))
    for _ in range(int(2.2 * d)):
        at = rng.uniform(0, d - 0.5)
        n = rng.integers(2, 5)
        for j in range(n):
            tt = t(rng.uniform(0.05, 0.11))
            f0 = rng.uniform(2800, 4600)
            ch = np.sin(2 * np.pi * (f0 + 900 * np.sin(2 * np.pi * 18 * tt)) * tt) * np.sin(np.pi * tt / tt[-1])
            i = int((at + j * 0.13) * SR)
            out[i:i + len(ch)] += ch * rng.uniform(0.3, 0.8)
    return norm(out, 0.4)


def hum(d=4.0, rise=True):
    x = t(d)
    h = np.sin(2 * np.pi * 50 * x) * 0.5 + np.sin(2 * np.pi * 100 * x) * 0.3 + lp(noise(d), 300) * 0.1
    e = np.minimum(1, x / 1.5) if rise else np.maximum(0, 1 - x / 0.4)
    return norm(h * e, 0.4)


def breath(d=1.2):
    x = bp(noise(d), 300, 2500) * np.sin(np.pi * np.linspace(0, 1, int(d * SR))) ** 2
    return norm(x, 0.35)


def clock(d=8.0):
    return norm(place(d, [(i * 0.5, click(0.02, 2000, 6000, 0.004) * (1 if i % 2 else 0.7)) for i in range(int(d * 2))]), 0.35)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    lib = {
        "rain": rain(), "storm": storm(), "thunder": thunder(),
        "typing": typing(5), "backspace": typing(3, 0.07), "phone_down": thud(95, 0.3),
        "box_open": norm(place(1.4, [(0.0, slide(0.5, 900, 2600)), (0.55, slide(0.6, 700, 2200))]), 0.5),
        "drawer": norm(place(0.9, [(0.0, slide(0.6, 250, 900)), (0.62, thud(110, 0.2) * 0.5)]), 0.6),
        "screw": norm(place(1.8, [(i * 0.17, click(0.01, 2500, 8000, 0.002)) for i in range(10)]), 0.5),
        "click": norm(click(0.03, 1200, 6000, 0.006), 0.6), "usb": norm(place(0.4, [(0.0, click(0.02, 1500, 7000, 0.005)), (0.01, metal((3900, 6100), 0.2, 0.03) * 0.4)]), 0.6),
        "tap": norm(click(0.02, 800, 3500, 0.004), 0.35), "confirm": norm(place(0.3, [(0, click(0.02, 900, 3000, 0.004)), (0.08, click(0.02, 1100, 3600, 0.004) * 0.7)]), 0.35),
        "set_down": thud(120, 0.22), "lift": norm(slide(0.35, 600, 2400), 0.3),
        "whoosh_in": whoosh(0.9, True), "whoosh_out": whoosh(0.9, False), "send": whoosh(0.5, True),
        "spoon": metal(), "card_place": norm(click(0.03, 1200, 5000, 0.008), 0.5), "slider": norm(place(0.5, [(i * 0.06, click(0.01, 1500, 5000, 0.002) * 0.6) for i in range(7)]), 0.3),
        "key": norm(place(0.12, [(0, click(0.015, 2000, 8000, 0.003)), (0.05, click(0.012, 1500, 6000, 0.003) * 0.6)]), 0.6),
        "pour": pour(), "switch": norm(click(0.03, 800, 5000, 0.01), 0.7), "morning": birds(6.0), "birds": birds(12.0),
        "haptic": haptic(), "error": thud(70, 0.35) * 0.5, "expire": thud(60, 0.4) * 0.4,
        "power_cut": norm(place(1.0, [(0, click(0.05, 200, 4000, 0.02)), (0.02, hum(0.8, False))]), 0.7), "power_on": hum(3.0, True),
        "breath": breath(), "snip": norm(place(0.3, [(0, metal((4200, 6800), 0.08, 0.01)), (0.12, metal((3900, 6300), 0.08, 0.01))]), 0.5),
        "pencil": norm(bp(noise(3.0), 2500, 9000) * (0.5 + 0.5 * np.abs(np.sin(2 * np.pi * 3.3 * t(3.0)))), 0.25),
        "enter": norm(click(0.03, 700, 4000, 0.008), 0.6), "phone_buzz": norm(place(1.0, [(i * 0.05, haptic() * 0.6) for i in range(8)] + [(0.5 + i * 0.05, haptic() * 0.6) for i in range(5)]), 0.6),
        "clock": clock(),
        "haptic_alert": haptic(0.06),
    }
    manifest = {}
    for name, x in lib.items():
        x = np.asarray(x, dtype=np.float64)
        path = OUT / f"{name}.wav"
        sf.write(path, x.astype(np.float32), SR, subtype="PCM_24")
        manifest[name] = {"seconds": round(len(x) / SR, 3), "peak": round(float(np.max(np.abs(x))), 3)}
    (OUT / "manifest.json").write_text(json.dumps({"rate": SR, "generator": "tools/build_sfx.py (seed 1203)", "sounds": manifest}, indent=1) + "\n")
    print(len(lib), "sounds")


if __name__ == "__main__":
    main()
