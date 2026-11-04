#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Subtitles synced to the actual audio: subtitles/wozai.zh-Hans.{srt,vtt,ass}.

Each line starts/ends where speech starts/ends inside its placed file
(script/voice-timing.json) at its film-time placement (edit warp). Lines
longer than MAX characters are split at punctuation; split points are timed
from Whisper word timestamps on the same file (fallback: by character count).
Narration is a softer grey (no faux italic for Chinese); Shaniu's lines are tinted; on-screen text (phone,
monitor) is not repeated as subtitles.

Usage: python3 tools/build_subtitles.py
"""
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from editlib import load_edit, film_from_story  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'subtitles'
MAX = 20
HAN = re.compile(r'[一-鿿A-Za-z]')


def texts():
    t = {s['id']: s['text'] for s in json.loads((ROOT / 'audio/voice/zhouyu/narration.json').read_text())['segments']}
    t.update({l['id']: l['text'] for l in json.loads((ROOT / 'audio/voice/shaniu/manifest.json').read_text())['lines']})
    t['W'] = '我在。'
    return t


def split(text):
    """Split at punctuation into balanced chunks of at most MAX visible characters."""
    vis = lambda z: len(re.sub(r'[，。；？！：—…“”‘’\s]', '', z))
    toks = re.findall(r'[^，。；？！：—…]+[，。；？！：—…]*', text) or [text]
    if vis(text) <= MAX:
        return [text]
    # Fewest chunks within MAX+2 visible characters, then the most even (DP over tokens).
    n = len(toks); lim = MAX + 2
    best = {0: (0, 0, [])}
    for j in range(1, n + 1):
        cands = []
        for i in range(j):
            if i not in best: continue
            c = ''.join(toks[i:j]); L = vis(c)
            if L > lim and j - i > 1: continue
            cnt, spread, path = best[i]
            cands.append((cnt + 1, max(spread, L), path + [c]))
        if cands: best[j] = min(cands, key=lambda x: (x[0], x[1]))
    return best[n][2] if n in best else toks


def char_times(model, path):
    segs, _ = model.transcribe(str(path), language='zh', beam_size=5, word_timestamps=True, initial_prompt='以下是普通话的句子。')
    out = []
    for s in segs:
        for w in s.words or []:
            chars = [c for c in w.word if HAN.match(c)]
            for i, c in enumerate(chars):
                out.append(w.start + (w.end - w.start) * (i + 0.5) / max(1, len(chars)))
    return out


def energy(path):
    import numpy as np, soundfile as sf
    x, sr = sf.read(path, always_2d=False)
    if x.ndim > 1: x = x.mean(axis=1)
    hop = int(0.02 * sr); n = len(x) // hop
    e = 20 * np.log10(np.sqrt(np.mean(x[: n * hop].reshape(n, hop) ** 2, axis=1)) + 1e-9)
    return [(k * hop / sr, float(v)) for k, v in enumerate(e)]


def ts(t, kind):
    t = max(0.0, t); h, m = int(t // 3600), int(t % 3600 // 60); s = t % 60
    if kind == 'srt': return f'{h:02d}:{m:02d}:{int(s):02d},{int(round((s % 1) * 1000)) % 1000:03d}'
    if kind == 'vtt': return f'{h:02d}:{m:02d}:{int(s):02d}.{int(round((s % 1) * 1000)) % 1000:03d}'
    return f'{h:d}:{m:02d}:{s:05.2f}'


def main():
    from faster_whisper import WhisperModel
    E = load_edit()
    T = texts()
    timing = json.loads((ROOT / 'script/voice-timing.json').read_text())
    model = WhisperModel('medium', device='cpu', compute_type='int8', cpu_threads=8)
    src = {'N': ROOT / 'audio/voice/zhouyu/directed', 'D': ROOT / 'audio/voice/zhouyu/directed', 'S': ROOT / 'audio/voice/shaniu', 'W': ROOT / 'audio/voice/shaniu'}
    events = []
    cache = {}
    for line in E['lines']:
        i = line['id']; kind = 'narr' if i[0] == 'N' else ('her' if i[0] in 'SW' else 'him')
        at = film_from_story(E, line['at']); on, off = timing[i]['on'], timing[i]['off']
        text = T[i]; chunks = split(text)
        if len(chunks) == 1:
            events.append((at + on, at + off, text, kind, i)); continue
        if i not in cache: cache[i] = char_times(model, src[i[0]] / f'{i}.wav')
        times = cache[i]
        n_vis = [len(HAN.findall(c)) for c in chunks]; total = sum(n_vis)
        starts, acc = [], 0
        for nv in n_vis:
            k = min(acc, len(times) - 1) if times and len(times) >= total * 0.8 else None
            starts.append((times[k] - 0.05) if k is not None else on + (off - on) * acc / total)
            acc += nv
        starts[0] = on
        # Snap each chunk to the actual speech onset after its estimated start
        # (the directed takes have pauses at these points), and end each chunk at
        # the last speech before the next one.
        env = energy(src[i[0]] / f'{i}.wav')
        snapped = [on] + [next((t for t, v in env if t >= starts[j] - 0.05 and v > -45), starts[j]) for j in range(1, len(starts))]
        for j, c in enumerate(chunks):
            a = snapped[j]
            if j + 1 < len(chunks):
                nxt = snapped[j + 1]
                last = max((t for t, v in env if a <= t < nxt - 0.02 and v > -45), default=nxt - 0.3)
                b = min(nxt - 0.02, last + 0.25)
            else:
                b = off
            events.append((at + a, at + b, c, kind, i))
    events.sort()
    # Screen position, decided on the speech itself (before any display hold):
    # her line goes to the top only while her speech really overlaps someone
    # else's (his narration over her lecture; him trying to cut in while she
    # cannot hear him). A tail overlap under 0.15 s is treated as sequential.
    def overlap(e, o):
        return min(e[1], o[1]) - max(e[0], o[0])
    top = [e[3] == 'her' and any(o[3] != 'her' and overlap(e, o) > 0.15 for o in events) for e in events]
    # Readability: at least 0.9 s on screen unless the next line needs the space.
    # A line never runs into the next line at the same screen position, or the
    # next line of the same kind (so no renderer has to stack two lines).
    fixed = []
    for k, (a, b, c, kind, i) in enumerate(events):
        nxt = min((e[0] for j, e in enumerate(events[k + 1:], k + 1) if e[3] == kind or top[j] == top[k]), default=b + 2)
        b = max(b, min(a + 0.9, nxt - 0.04)) + 0.12                # hold a touch past the last syllable
        b = min(b, nxt - 0.02)
        fixed.append((a, b, c.strip(), kind, i, top[k]))
    OUT.mkdir(parents=True, exist_ok=True)
    srt, vtt = [], ['WEBVTT', '']
    for n, (a, b, c, kind, i, _top) in enumerate(fixed, 1):
        body = c
        srt += [str(n), f'{ts(a, "srt")} --> {ts(b, "srt")}', body, '']
        vtt += [f'{ts(a, "vtt")} --> {ts(b, "vtt")}', body, '']
    (OUT / 'wozai.zh-Hans.srt').write_text('\n'.join(srt), encoding='utf-8')
    (OUT / 'wozai.zh-Hans.vtt').write_text('\n'.join(vtt), encoding='utf-8')
    head = ['[Script Info]', 'Title: 我在', 'ScriptType: v4.00+', 'PlayResX: 1920', 'PlayResY: 1080', 'WrapStyle: 0', 'ScaledBorderAndShadow: yes', '',
            '[V4+ Styles]',
            'Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding',
            'Style: him,Noto Sans SC,46,&H00F2F2F2,&H000000FF,&H00101010,&H78000000,0,0,0,0,100,100,1,0,1,2.2,1.2,2,160,160,64,1',
            'Style: narr,Noto Sans SC,46,&H00D2D2D2,&H000000FF,&H00101010,&H78000000,0,0,0,0,100,100,1,0,1,2.2,1.2,2,160,160,64,1',
            'Style: her,Noto Sans SC,46,&H00F5E9B8,&H000000FF,&H00101010,&H78000000,0,0,0,0,100,100,1,0,1,2.2,1.2,2,160,160,64,1',
            '', '[Events]', 'Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text']
    ev = []
    for a, b, c, kind, i, top in fixed:
        ev.append(f'Dialogue: 0,{ts(a, "ass")},{ts(b, "ass")},{kind},{i},0,0,0,,' + (r'{\an8}' if top else '') + c)
    (OUT / 'wozai.zh-Hans.ass').write_text('\n'.join(head + ev) + '\n', encoding='utf-8-sig')
    (OUT / 'subtitles.json').write_text(json.dumps([{'start': round(a, 3), 'end': round(b, 3), 'text': c, 'kind': kind, 'line': i, 'top': top} for a, b, c, kind, i, top in fixed], ensure_ascii=False, indent=1) + '\n')
    print(len(fixed), 'subtitle events')


if __name__ == '__main__':
    main()
