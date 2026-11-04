#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Voice direction in post for 周屿's lines (narration and dialogue).

The synthetic voice reads ellipses and dashes straight through and runs at
~5.7 syllables/s. A restrained, intimate read needs room. For every line:
  1. align the take to the script (Whisper word timings → characters, mapped to
     script characters by a toneless-pinyin alignment);
  2. open pauses where the script asks for them (…… / —— / a mid-line full
     stop / a comma before a name), cutting at the quietest point within
     ±80 ms of the aligned boundary, with 10 ms fades;
  3. slow the line slightly with Rubber Band (formants preserved).
Inputs:  audio/voice/zhouyu/inputs/<id>.wav (SAMI originals, untouched)
Outputs: audio/voice/zhouyu/directed/<id>.wav + directed/edits.json
Usage:   python3 tools/direct_voice.py [ids...]
"""
import json, re, subprocess, sys
from pathlib import Path
import numpy as np, soundfile as sf
from pypinyin import lazy_pinyin

ROOT = Path(__file__).resolve().parents[1]
JOB = ROOT / 'audio/voice/zhouyu'
OUT = JOB / 'directed'
SR = 48000
PAUSE = {'……': 0.55, '——': 0.32, '。': 0.30, '，': 0.12, '：': 0.18, '？': 0.30, '；': 0.24}
TEMPO = {'N': 0.95, 'D': 1.0}
HAN = re.compile(r'[一-鿿]')


def char_times(model, path):
    segs, _ = model.transcribe(str(path), language='zh', beam_size=5, word_timestamps=True, initial_prompt='以下是普通话的句子。')
    out = []
    for s in segs:
        for w in s.words or []:
            chars = [c for c in w.word if HAN.match(c)]
            if not chars: continue
            step = (w.end - w.start) / len(chars)
            for i, c in enumerate(chars): out.append((c, w.start + i * step, w.start + (i + 1) * step))
    return out


def align(script_chars, asr):
    """Map each script char index to an ASR index (or None) by pinyin edit alignment."""
    a = lazy_pinyin(''.join(script_chars)); b = lazy_pinyin(''.join(c for c, _, _ in asr))
    n, m = len(a), len(b)
    D = np.zeros((n + 1, m + 1)); D[:, 0] = np.arange(n + 1); D[0, :] = np.arange(m + 1)
    for i in range(1, n + 1):
        for j in range(1, m + 1):
            D[i, j] = min(D[i - 1, j] + 1, D[i, j - 1] + 1, D[i - 1, j - 1] + (a[i - 1] != b[j - 1]))
    i, j, mp = n, m, [None] * n
    while i > 0 and j > 0:
        if D[i, j] == D[i - 1, j - 1] + (a[i - 1] != b[j - 1]): mp[i - 1] = j - 1; i -= 1; j -= 1
        elif D[i, j] == D[i - 1, j] + 1: i -= 1
        else: j -= 1
    return mp


def quietest(y, t, back=0.20, fwd=-0.03):
    """Cut point near t: low energy, and not inside a fricative/affricate onset
    (high zero-crossing rate), searching mostly back into the previous vowel tail."""
    a, b = max(0, int((t - back) * SR)), min(len(y), int((t + fwd) * SR))
    hop = int(0.005 * SR)
    if b - a < 2 * hop: return t
    best, bt = None, t
    for k in range(a, b - hop, hop):
        x = y[k:k + hop]
        rms = np.sqrt(np.mean(x ** 2)) + 1e-6
        zcr = np.mean(np.abs(np.diff(np.sign(x)))) / 2
        score = rms * (1 + 20 * max(0.0, zcr - 0.06))
        if best is None or score < best: best, bt = score, (k + hop // 2) / SR
    return bt


def direct(ident, text, model, log, level=2, asr=None):
    src = JOB / 'inputs' / f'{ident}.wav'
    y, sr = sf.read(src, always_2d=False)
    assert sr == SR
    if y.ndim > 1: y = y.mean(axis=1)
    asr = asr if asr is not None else char_times(model, src)
    # Script characters with the punctuation that follows each one.
    chars, after = [], []
    for k, ch in enumerate(text):
        if HAN.match(ch): chars.append(ch); after.append('')
        elif chars:
            after[-1] += ch
    mp = align(chars, asr)
    cuts = []
    if level == 0: mp = [None] * len(chars)
    for i, p in enumerate(after[:-1]):
        key = next((k for k in ('……', '——', '。', '？', '；', '：', '，') if k in p), None)
        if not key: continue
        # Short dialogue: commas keep their natural timing; only real breaks widen.
        if key == '，' and not (ident.startswith('N') or chars[i] == '晴'): continue
        j0, j1 = mp[i], mp[i + 1] if i + 1 < len(mp) else None
        if j0 is None or j1 is None: continue
        t = quietest(y, asr[j1][1])
        cuts.append((t, PAUSE[key], key))
    # A leading ellipsis (……晴 / ……你好) is a hesitation before the first word.
    lead = 0.35 if text.startswith('……') else 0.0
    parts, last = [np.zeros(int(lead * SR))], 0
    fade = int(0.02 * SR)
    for t, extra, key in sorted(cuts):
        k = int(t * SR); seg = y[last:k].copy()
        if len(seg) > 2 * fade: seg[-fade:] *= np.linspace(1, 0, fade)
        parts += [seg, np.zeros(int(extra * SR))]
        nxt = y[k:k + fade].copy(); last = k
        y[k:k + fade] = nxt * np.linspace(0, 1, len(nxt))
    parts.append(y[last:])
    z = np.concatenate(parts).astype(np.float32)
    tmp = OUT / f'{ident}.pre.wav'; sf.write(tmp, z, SR, subtype='PCM_24')
    tempo = TEMPO[ident[0]] if level == 2 else 1.0
    dst = OUT / f'{ident}.wav'
    if tempo == 1.0:
        tmp.replace(dst)
        d = sf.info(dst).duration
        log[ident] = {'level': level, 'text': text, 'pauses_added': [[round(t, 3), p, k] for t, p, k in sorted(cuts)], 'lead_s': lead,
                      'tempo': tempo, 'seconds_in': round(len(y) / SR, 3), 'seconds_out': round(d, 3)}
        print(ident, log[ident]['seconds_in'], '->', round(d, 2), [k for _, _, k in cuts], flush=True)
        return
    # 150 ms pre-roll so the stretcher never smears the first onset; trimmed after.
    pre = 0.15
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(tmp), '-af', f'adelay={int(pre * 1000)},apad=pad_dur=0.15,rubberband=tempo={tempo}:formant=preserved:pitchq=quality:transients=crisp,atrim=start={pre / tempo:.4f}',
                    '-ar', str(SR), '-ac', '1', '-c:a', 'pcm_s24le', str(dst)], check=True)
    tmp.unlink()
    d = sf.info(dst).duration
    log[ident] = {'level': level, 'text': text, 'pauses_added': [[round(t, 3), p, k] for t, p, k in sorted(cuts)], 'lead_s': lead,
                  'tempo': tempo, 'seconds_in': round(len(y) / SR, 3), 'seconds_out': round(d, 3)}
    print(ident, log[ident]['seconds_in'], '->', round(d, 2), [k for _, _, k in cuts], flush=True)


HOM = str.maketrans({'请': '晴', '情': '晴', '亲': '晴', '清': '晴', '他': '她', '它': '她'})


def per(model, path, text):
    h = ''.join(x.text for x in model.transcribe(str(path), language='zh', beam_size=5, initial_prompt='以下是普通话的句子。')[0])
    norm = lambda z: lazy_pinyin(re.sub(r'[^一-鿿0-9]', '', re.sub(r'open\s*v\S*|oppen\W*v\S*', '欧喷维拉', z.lower().replace('openvela', '欧喷维拉'))).translate(HOM))
    a, b = norm(text), norm(h)
    D = list(range(len(b) + 1))
    for i in range(1, len(a) + 1):
        pr, D[0] = D[0], i
        for j in range(1, len(b) + 1):
            c = D[j]; D[j] = min(D[j] + 1, D[j - 1] + 1, pr + (a[i - 1] != b[j - 1])); pr = c
    return round(D[len(b)] / max(1, len(a)), 3), h


def main():
    from faster_whisper import WhisperModel
    OUT.mkdir(parents=True, exist_ok=True)
    seg = {s['id']: s for s in json.loads((JOB / 'narration.json').read_text())['segments']}
    ids = sys.argv[1:] or list(seg)
    model = WhisperModel('medium', device='cpu', compute_type='int8', cpu_threads=8)
    logp = OUT / 'edits.json'
    log = json.loads(logp.read_text()) if logp.exists() else {}
    for i in ids:
        text = seg[i]['text']
        base, base_h = per(model, JOB / 'inputs' / f'{i}.wav', text)
        times = char_times(model, JOB / 'inputs' / f'{i}.wav')
        for level in (2, 1, 0):
            direct(i, text, model, log, level, times)
            got, h = per(model, OUT / f'{i}.wav', text)
            log[i]['asr_original'] = [base, base_h]; log[i]['asr_directed'] = [got, h]
            if got <= base or level == 0:
                break
            print('  level', level, 'heard worse:', h, '-> trying a lighter edit', flush=True)
    logp.write_text(json.dumps({'method': __doc__.strip().splitlines()[0], 'levels': '2 = pauses + stretch, 1 = pauses only, 0 = original take (lead pause only)', **{k: v for k, v in log.items() if k not in ('method', 'levels')}}, ensure_ascii=False, indent=1) + '\n')


if __name__ == '__main__':
    main()
