#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Shared reader for src/edit.js and its time warp (same maths as src/timewarp.js).

Story time: what shots, eyes, screens and cue "at" values are written in.
Film time: the rendered film. EDIT["warp"] = [[story_t0, story_t1, factor], ...]
slows chosen ranges so a moment can breathe; audio keeps its real length and is
placed at film_from_story(at).
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def load_edit():
    text = (ROOT / "src/edit.js").read_text(encoding="utf-8")
    text = "\n".join(l for l in text.splitlines() if not l.strip().startswith("//"))
    return json.loads(text[text.index("{"): text.rindex("}") + 1])


def _warp(E):
    return sorted((tuple(w) for w in E.get("warp", [])), key=lambda w: w[0])


def film_from_story(E, ts):
    tf = ts
    for a, b, f in _warp(E):
        if ts <= a:
            break
        tf += (min(ts, b) - a) * (f - 1)
    return tf


def story_from_film(E, tf):
    shift = 0.0
    for a, b, f in _warp(E):
        fa = a + shift
        fb = fa + (b - a) * f
        if tf <= fa:
            return tf - shift
        if tf < fb:
            return a + (tf - fa) / f
        shift += (b - a) * (f - 1)
    return tf - shift


def film_duration(E):
    return film_from_story(E, E["duration"])
