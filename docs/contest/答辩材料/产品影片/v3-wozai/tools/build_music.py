#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Original score for 《我在》, synthesised from scratch (no samples, seeded).

Motif "我在": F#→D, a falling major third, short–long. Its answer at the end
rises D–E–F#–A. Cues are anchored to story beats (src/edit.js) and placed in
film time through the edit's warp, so they stay on the picture.

Instruments are simple physical-ish models: additive piano, music box,
detuned-saw pads and strings, Karplus–Strong plucks, a soft arpeggio synth,
a low drone, and a stereo convolution reverb.

Music stays out where silence carries the scene: the first "我在", both card
knocks, the blackout calls and the door.

Usage: python3 tools/build_music.py   → audio/music/score.wav (48 kHz stereo) + cues.json
"""
import json
import sys
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy import signal

sys.path.insert(0, str(Path(__file__).resolve().parent))
from editlib import load_edit, film_from_story, film_duration  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
SR = 48_000
rng = np.random.default_rng(20261104)
E = load_edit()
F = lambda t: film_from_story(E, t)
TOTAL = film_duration(E) + 1.0
L = np.zeros(int(TOTAL * SR)); R = np.zeros_like(L)


def hz(n):
    """Note name like 'F#4' → Hz."""
    names = {'C': 0, 'C#': 1, 'D': 2, 'D#': 3, 'E': 4, 'F': 5, 'F#': 6, 'G': 7, 'G#': 8, 'A': 9, 'A#': 10, 'B': 11}
    name, octave = n[:-1], int(n[-1])
    return 440.0 * 2 ** ((names[name] + 12 * (octave + 1) - 69) / 12)


def tt(d):
    return np.arange(int(d * SR)) / SR


def put(x, at, gain=1.0, pan=0.0):
    i = int(at * SR)
    if i >= len(L) or len(x) == 0: return
    n = min(len(x), len(L) - i)
    gl, gr = np.cos((pan + 1) * np.pi / 4), np.sin((pan + 1) * np.pi / 4)
    L[i:i + n] += x[:n] * gain * gl * 1.4142
    R[i:i + n] += x[:n] * gain * gr * 1.4142


def lp(x, f, o=2):
    b, a = signal.butter(o, min(0.99, f / (SR / 2)), 'low'); return signal.lfilter(b, a, x)


def env(n, a, r):
    e = np.ones(n); ka, kr = int(a * SR), int(r * SR)
    if ka: e[:ka] = np.linspace(0, 1, ka) ** 1.5
    if kr: e[-kr:] *= np.linspace(1, 0, kr) ** 1.2
    return e


# ---------------------------------------------------------------- instruments
def piano(note, dur=2.5, vel=0.6):
    f0 = hz(note); x = tt(dur + 1.2); y = np.zeros_like(x)
    for k in range(1, 11):
        fk = k * f0 * np.sqrt(1 + 0.0004 * k * k)
        if fk > 12000: break
        tau = (2.8 if f0 < 300 else 1.8) / (1 + 0.55 * k)
        y += (1 / k ** 1.15) * np.sin(2 * np.pi * fk * x + rng.uniform(0, 6)) * np.exp(-x / tau)
    ham = lp(rng.standard_normal(len(x)), 2500) * np.exp(-x / 0.012) * 0.05
    y = (y + ham) * env(len(x), 0.004, 0.35) * vel
    return lp(y, 5200 * (0.5 + vel))


def musicbox(note, vel=0.5):
    f0 = hz(note); x = tt(2.6)
    y = sum(a * np.sin(2 * np.pi * f0 * m * x) * np.exp(-x / d) for m, a, d in ((1, 1.0, 0.9), (2.0, 0.35, 0.5), (3.0, 0.15, 0.3), (4.2, 0.08, 0.18)))
    y[:300] += rng.standard_normal(300) * np.exp(-np.arange(300) / 40) * 0.08
    return y * vel * env(len(x), 0.002, 0.5)


def saws(fs, dur, cutoff, attack, release, detune=0.006, vib=0.0):
    x = tt(dur); y = np.zeros_like(x)
    for f in fs:
        for d in (-detune, 0, detune):
            ph = 2 * np.pi * f * (1 + d) * x + (vib * np.sin(2 * np.pi * 5.1 * x) if vib else 0) + rng.uniform(0, 6)
            y += signal.sawtooth(ph) * 0.33
    y = lp(y / max(1, len(fs)), cutoff, 2)
    return y * env(len(x), attack, release)


def pad(chord, dur, vel=0.3, bright=1200):
    return saws([hz(n) for n in chord], dur, bright, min(2.5, dur * 0.35), min(3.0, dur * 0.4)) * vel


def strings(chord, dur, vel=0.3):
    return saws([hz(n) for n in chord], dur, 2600, min(3.0, dur * 0.4), min(3.0, dur * 0.4), 0.004, vib=0.003) * vel


def pluck(note, vel=0.4, dur=1.2):
    f0 = hz(note); n = int(dur * SR); N = max(2, int(SR / f0))
    buf = rng.uniform(-1, 1, N); out = np.zeros(n)
    for i in range(n):
        out[i] = buf[i % N]; buf[i % N] = 0.5 * (buf[i % N] + buf[(i + 1) % N]) * 0.996
    return lp(out, 3500) * vel * env(n, 0.001, 0.2)


def arp_note(note, dur=0.18, vel=0.25):
    f0 = hz(note); x = tt(dur + 0.25)
    y = sum(np.sin(2 * np.pi * f0 * k * x) / k for k in (1, 3, 5))
    return lp(y, 3000) * np.exp(-x / 0.18) * vel


def drone(notes, dur, vel=0.25):
    x = tt(dur); y = sum(np.sin(2 * np.pi * hz(n) * x) for n in notes)
    y = np.tanh(1.6 * y) * env(len(x), min(3, dur * 0.3), min(3, dur * 0.3))
    return lp(y, 600) * vel


# ---------------------------------------------------------------- helpers
def chord_seq(kind, t0, t1, chords, bar, vel=0.3, **kw):
    """Lay chords one per bar between story times t0..t1 (film time placement)."""
    a, b = F(t0), F(t1); t = a; i = 0
    while t < b - 0.2:
        d = min(bar, b - t) + (1.2 if kind != 'piano' else 0)
        c = chords[i % len(chords)]
        if kind == 'pad': put(pad(c, d, vel, **kw), t, 1, 0)
        elif kind == 'strings': put(strings(c, d, vel), t, 1, 0)
        elif kind == 'piano':
            for j, n in enumerate(c): put(piano(n, bar, vel * (0.9 if j else 1.0)), t + 0.012 * j, 1, -0.3 + 0.6 * j / max(1, len(c) - 1))
        t += bar; i += 1


def notes_at(seq, t0, beat, vel=0.5, inst=piano, pan=0.0):
    """seq: list of (note or None, beats). Starts at story time t0."""
    t = F(t0)
    for n, beats in seq:
        if n:
            for m in (n if isinstance(n, (list, tuple)) else [n]):
                put(inst(m, vel=vel) if inst is musicbox else inst(m, beat * beats + 0.6, vel), t, 1, pan)
        t += beat * beats


MOTIF = [('F#4', 1), ('D4', 3)]
ANSWER = [('D4', 1), ('E4', 1), ('F#4', 1), ('A4', 3)]
cues = []


def cue(name, t0, t1, note=''):
    cues.append({'cue': name, 'story': [t0, t1], 'film': [round(F(t0), 2), round(F(t1), 2)], 'note': note})


# ---------------------------------------------------------------- the score
# s01 rain night: sparse piano over a Bm pad.
cue('rain', 0, 19, 'sparse piano, Bm')
chord_seq('pad', 1.0, 19.0, [['B2', 'F#3', 'D4']], 18.0, 0.10, bright=900)
notes_at([('B4', 2), (None, 2), ('F#4', 2), (None, 3), ('D4', 3), (None, 2), ('A3', 4)], 3.0, 1.0, 0.22)
# s02 gift → decision: chords; the motif when he decides.
cue('gift', 19, 40.4, 'Bm–G–D–A; motif at the decision')
chord_seq('piano', 19.4, 34.4, [['B2', 'F#3', 'D4'], ['G2', 'D3', 'B3'], ['D3', 'A3', 'F#4'], ['A2', 'E3', 'C#4']], 3.75, 0.20)
chord_seq('pad', 33.6, 40.6, [['D3', 'A3', 'E4', 'F#4']], 7.0, 0.13)
notes_at(MOTIF, 36.6, 0.75, 0.38)
# s03–s04 making her: plucks, a pulse of intent; eyes open: music box.
cue('making', 40.4, 74.0, 'plucks at 92 BPM; music box when her eyes open')
beat = 60 / 92 / 2
seq = ['D4', 'A4', 'F#4', 'A4', 'B3', 'F#4', 'D4', 'F#4', 'G3', 'D4', 'B3', 'D4', 'A3', 'E4', 'C#4', 'E4']
t = F(43.6); end = F(71.6); i = 0
while t < end:
    put(pluck(seq[i % len(seq)], 0.16), t, 1, -0.4 if i % 2 else 0.4); t += beat; i += 1
chord_seq('pad', 43.0, 72.2, [['D3', 'A3', 'F#4'], ['B2', 'F#3', 'D4'], ['G2', 'D3', 'B3'], ['A2', 'E3', 'C#4']], 5.2, 0.08)
notes_at([('A5', 1), ('F#5', 1), ('D5', 2)], 72.4, 0.32, 0.3, musicbox, 0.2)
# s05: silence for the first 我在; a soft swell after; the curious cutaway.
cue('first-wozai', 74, 91.2, 'silence before and during 我在; pad after')
chord_seq('pad', 81.8, 90.6, [['D3', 'A3', 'E4', 'F#4']], 8.8, 0.10, bright=1000)
cue('cutaway-voice', 91.2, 103.4, 'arpeggio, curiosity')
t = F(91.4); end = F(103.0); i = 0
arp = ['D4', 'F#4', 'A4', 'E5', 'A4', 'F#4', 'B3', 'D4', 'F#4', 'C#5', 'F#4', 'D4']
while t < end:
    put(arp_note(arp[i % len(arp)], vel=0.13), t, 1, -0.5 + (i % 4) / 3); t += 60 / 96 / 2; i += 1
chord_seq('pad', 91.2, 103.6, [['D3', 'A3', 'F#4'], ['B2', 'F#3', 'D4']], 6.2, 0.08)
chord_seq('pad', 103.4, 107.2, [['D3', 'A3', 'E4', 'F#4']], 3.8, 0.07)
# s06 making her into 晴: tender, a little playful; out for the knocks.
cue('make-her-qing', 107, 145.6, 'G major staccato piano, low')
beat = 60 / 100
pat = [('G3', 1), ('D4', 1), ('B3', 1), ('D4', 1), ('C4', 1), ('E4', 1), ('A3', 1), ('E4', 1)]
for k in range(6):
    notes_at(pat, 107.4 + k * 8 * beat * 0.95, beat * 0.95, 0.085)
chord_seq('pad', 107.2, 145.6, [['G2', 'D3', 'B3'], ['E2', 'B2', 'G3'], ['C3', 'G3', 'E4'], ['D3', 'A3', 'F#4']], 6.4, 0.06)
notes_at([('A4', 1), ('G4', 1), ('E4', 2)], 141.8, 0.5, 0.18)
cue('knocks', 145.6, 153.0, 'music out: the card plays her four beats')
chord_seq('pad', 153.0, 157.8, [['E3', 'B3', 'F#4', 'G4']], 4.8, 0.08, bright=900)
# s07 days.
cue('days', 157.8, 248.0, '02:13 near-silence; day piano; plant; render to dawn')
chord_seq('pad', 158.0, 175.0, [['B2', 'F#3', 'D4']], 17.0, 0.05, bright=700)
chord_seq('piano', 175.6, 204.0, [['G2', 'D3', 'B3'], ['D3', 'A3', 'F#4'], ['E3', 'B3', 'G4'], ['C3', 'G3', 'E4']], 3.6, 0.10)
chord_seq('strings', 204.2, 231.0, [['G2', 'D3', 'B3'], ['D3', 'A3', 'F#4'], ['C3', 'G3', 'E4'], ['D3', 'A3', 'F#4']], 6.7, 0.07)
notes_at([('B4', 1), ('A4', 1), ('G4', 2), ('D5', 4)], 219.8, 0.6, 0.16)
t = F(231.4); end = F(240.4); i = 0
while t < end:
    put(arp_note(['A3', 'E4', 'C#5', 'E4'][i % 4], vel=0.08), t, 1, 0.3 * (1 if i % 2 else -1)); t += 0.3125; i += 1
chord_seq('pad', 240.6, 248.0, [['D3', 'A3', 'E4', 'F#4', 'A4']], 7.4, 0.09, bright=1600)
notes_at([('A5', 1), ('D6', 2)], 246.2, 0.25, 0.28, musicbox, 0.3)
cue('update', 248.0, 272.6, 'suspended chords; cutaway arpeggio; resolves on 她还是她')
chord_seq('pad', 248.0, 252.4, [['G2', 'D3', 'A3', 'D4']], 4.4, 0.08)
t = F(252.6); end = F(261.0); i = 0
while t < end:
    put(arp_note(['G3', 'D4', 'A4', 'D4'][i % 4], vel=0.09), t, 1, -0.3 + 0.2 * (i % 4)); t += 0.3125; i += 1
chord_seq('pad', 252.3, 266.6, [['G2', 'D3', 'A3', 'D4'], ['A2', 'E3', 'B3', 'D4']], 7.2, 0.07)
chord_seq('strings', 270.4, 274.0, [['D3', 'A3', 'F#4']], 3.6, 0.10)
# s08 the photo request: drone and a slow pulse; released when it expires.
cue('photos', 272.6, 306.4, 'drone + pulse while the ring runs out; silence after')
put(drone(['D2', 'A2'], F(298.2) - F(273.0), 0.055), F(273.0), 1, 0)
t = F(282.6); end = F(297.6); k = 0
while t < end:
    put(piano('D2', 1.0, 0.10 + 0.002 * k), t, 1, 0); t += 1.25; k += 1
chord_seq('pad', 289.0, 298.0, [['F#3', 'G3', 'D4']], 9.0, 0.05, bright=800)
# s09 3 a.m.: almost nothing; a lone chord after "我把她关了".
cue('3am', 306.4, 334.0, 'lone low notes; lone chord after N16')
notes_at([('B1', 4), (None, 4), ('B1', 4), (None, 4), ('F#1', 4)], 307.0, 1.2, 0.14)
chord_seq('pad', 314.6, 324.0, [['B2', 'D3', 'F#3', 'A3']], 9.4, 0.05, bright=700)
for j, n in enumerate(['B2', 'D3', 'F#3']): put(piano(n, 5.0, 0.16), F(331.4) + 0.02 * j, 1, -0.2 + 0.2 * j)
# s10 time-lapse: a little figure turning over and over, fading; then the storm alone.
cue('weeks', 334.0, 342.0, 'repeating figure fading')
fig = [('D4', 1), ('F#4', 1), ('A4', 1), ('F#4', 1), ('E4', 1)]
for k in range(4): notes_at(fig, 334.4 + k * 1.9, 0.38, 0.10 * (1 - k * 0.2))
cue('blackout-silence', 342.0, 370.8, 'no music: the calls, the knocks, the door')
cue('her-glance', 370.8, 404.0, 'music box motif at her glance; pad for the card; piano for the choice; resolves with N18')
notes_at([('F#5', 1), ('D5', 3)], 371.3, 0.45, 0.22, musicbox, 0.1)
chord_seq('pad', 377.4, 384.0, [['D3', 'A3', 'E4']], 6.6, 0.06, bright=900)
chord_seq('strings', 379.6, 394.0, [['B2', 'F#3', 'D4'], ['G2', 'D3', 'B3']], 7.2, 0.05)
notes_at(MOTIF + [(None, 2)] + ANSWER, 384.2, 0.8, 0.20)
chord_seq('piano', 394.6, 403.0, [['G2', 'D3', 'B3'], ['A2', 'E3', 'C#4'], ['D3', 'A3', 'F#4']], 2.8, 0.12)
chord_seq('pad', 394.6, 404.2, [['D3', 'A3', 'F#4']], 9.6, 0.06)
# s11 dawn: warm D major; new eyes on a music box.
cue('dawn', 404.0, 476.4, 'D major piano and strings; music box for the new eyes')
chord_seq('strings', 405.0, 476.4, [['D3', 'A3', 'F#4'], ['B2', 'F#3', 'D4'], ['G2', 'D3', 'B3'], ['A2', 'E3', 'C#4']], 7.6, 0.06)
chord_seq('piano', 409.0, 452.0, [['D3', 'A3', 'F#4'], ['B2', 'F#3', 'D4'], ['G2', 'D3', 'B3'], ['A2', 'E3', 'C#4']], 3.8, 0.09)
notes_at(MOTIF + [(None, 1)] + ANSWER, 435.6, 0.62, 0.14)
for k in range(5): notes_at([('A5', 1), ('F#5', 1), ('D5', 1), ('E5', 1)], 456.4 + k * 1.6, 0.4, 0.13, musicbox, 0.25)
notes_at([('D5', 1), ('E5', 1), ('F#5', 1), ('A5', 3)], 468.4, 0.42, 0.24, musicbox, 0.2)
# s12 lighthouse: a steady build; thins for the delete request; warm on the archive.
cue('lighthouse', 476.4, 518.0, 'steady 8ths; thin for the request; resolve on the archive')
beat = 60 / 84 / 2
t = F(477.4); end = F(488.0); i = 0
line = ['D4', 'A4', 'F#4', 'A4', 'G3', 'D4', 'B3', 'D4', 'B3', 'F#4', 'D4', 'F#4', 'A3', 'E4', 'C#4', 'E4']
while t < end:
    put(piano(line[i % len(line)], 0.6, 0.09), t, 1, -0.3 if i % 2 else 0.3); t += beat; i += 1
chord_seq('strings', 477.0, 488.4, [['D3', 'A3', 'F#4'], ['G2', 'D3', 'B3'], ['B2', 'F#3', 'D4'], ['A2', 'E3', 'C#4']], 2.9, 0.06)
chord_seq('pad', 488.2, 492.8, [['E3', 'B3', 'D4']], 4.6, 0.06, bright=800)
chord_seq('pad', 492.8, 503.0, [['G2', 'D3', 'A3']], 10.2, 0.05)
chord_seq('strings', 503.0, 518.0, [['D3', 'A3', 'F#4'], ['G2', 'D3', 'B3'], ['D3', 'A3', 'F#4']], 5.0, 0.08)
notes_at([('F#4', 1), ('A4', 1), ('D5', 2)], 503.2, 0.5, 0.16)
# s13 "我在。来得及。" — silence while he types; the motif answers; title; end card.
cue('ending', 518.0, 557.0, 'light morning piano; out while he types; motif and answer; title chord; reprise')
chord_seq('piano', 518.2, 524.0, [['D3', 'A3', 'F#4'], ['G2', 'D3', 'B3']], 2.9, 0.09)
notes_at(MOTIF, 529.6, 0.7, 0.24)
chord_seq('strings', 529.4, 541.6, [['B2', 'F#3', 'D4'], ['G2', 'D3', 'B3'], ['D3', 'A3', 'F#4']], 4.1, 0.09)
notes_at(ANSWER + [(None, 1)] + MOTIF + [(None, 1)] + ANSWER, 534.2, 0.7, 0.22)
chord_seq('pad', 535.4, 547.6, [['D3', 'A3', 'E4', 'F#4', 'A4']], 12.2, 0.08, bright=1500)
for j, n in enumerate(['D2', 'A2', 'F#3', 'D4', 'E4', 'A4']): put(piano(n, 6.0, 0.20), F(541.8) + 0.03 * j, 1, -0.4 + 0.16 * j)
notes_at([('F#4', 1), ('D4', 2), (None, 1), ('A3', 1), ('D4', 3)], 547.6, 0.9, 0.14)
chord_seq('pad', 547.4, 556.6, [['D3', 'A3', 'F#4']], 9.2, 0.06)


# ---------------------------------------------------------------- reverb and write
def reverb(x, seed, decay=2.6):
    n = int(decay * SR); r = np.random.default_rng(seed)
    ir = r.standard_normal(n) * np.exp(-np.arange(n) / (decay * SR / 6.5))
    ir = lp(ir, 5000); ir[0] = 0; ir /= np.sqrt(np.sum(ir ** 2))
    return signal.fftconvolve(x, ir)[: len(x)]


wetL, wetR = reverb(L, 1), reverb(R, 2)
outL, outR = L + 0.35 * wetL, R + 0.35 * wetR
fade = int(3.0 * SR)
for ch in (outL, outR): ch[-fade:] *= np.linspace(1, 0, fade)
out = np.stack([outL, outR], axis=1)
peak = np.max(np.abs(out)) or 1
out *= 0.5 / peak                      # leave headroom; mix.py sets the final level
dst = ROOT / 'audio/music'; dst.mkdir(parents=True, exist_ok=True)
sf.write(dst / 'score.wav', out.astype(np.float32), SR, subtype='PCM_24')
(dst / 'cues.json').write_text(json.dumps({'generator': 'tools/build_music.py (seed 20261104)', 'rate': SR, 'motif': 'F#4→D4 (我在), answer D–E–F#–A',
                                           'cues': cues, 'audio_subjective': 'not_checked'}, ensure_ascii=False, indent=1) + '\n')
print('score', round(len(out) / SR, 2), 's;', len(cues), 'cues')
