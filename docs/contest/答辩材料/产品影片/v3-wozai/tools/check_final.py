#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Machine checks of the finished film (writes <out>/final-check.json and contact sheets).

- streams, frame counts and durations of the delivery, clean and picture files;
- a full decode of each (any decoder error is reported);
- loudness and true peak of the delivered audio (FFmpeg ebur128);
- audio alignment: the muxed AAC against audio/master/master.wav by
  cross-correlation (the master is placed from film time 0, the same origin as
  the picture, so a zero offset means the mux kept picture and sound in sync);
- optional splice continuity: PSNR across the cut;
- contact sheets: one frame per scene (scene midpoints) and one per sampled
  subtitle event (event midpoints), both taken from the delivery file.

Usage: python3 tools/check_final.py --final F.mp4 --clean C.mp4 --picture P.mp4 --out DIR
       [--splice-main M.mp4 --splice-tail T.mp4 --splice-at 566.0]
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))
from editlib import load_edit, film_from_story  # noqa: E402

FONT = str(ROOT / 'assets/fonts/NotoSansSC-VF.ttf')
FPS = 30


def run(cmd, **kw):
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, **kw)


def probe(path):
    r = run(['ffprobe', '-v', 'error', '-count_packets', '-show_entries',
             'stream=index,codec_type,codec_name,profile,width,height,pix_fmt,r_frame_rate,nb_read_packets,sample_rate,channels,bit_rate,duration,start_time,color_space,color_transfer,color_primaries'
             ':format=duration,size,bit_rate', '-of', 'json', str(path)])
    d = json.loads(r.stdout)
    return {'format': d['format'], 'streams': d['streams']}


def decode_errors(path):
    r = run(['ffmpeg', '-v', 'error', '-i', str(path), '-f', 'null', '-'])
    lines = [l for l in r.stderr.splitlines() if l.strip()]
    return {'returncode': r.returncode, 'error_lines': len(lines), 'first': lines[:5]}


def loudness(path):
    r = run(['ffmpeg', '-nostats', '-i', str(path), '-map', '0:a:0', '-af', 'ebur128=peak=true', '-f', 'null', '-'])
    tail = r.stderr[r.stderr.rfind('Summary:'):]
    grab = lambda k: float(re.search(k + r':\s+(-?[\d.]+|-inf)', tail).group(1))
    return {'I_LUFS': grab('I'), 'LRA_LU': grab('LRA'), 'TP_dBTP': grab('Peak')}


def pcm(path, sr=48000, seconds=None):
    cmd = ['ffmpeg', '-v', 'error', '-i', str(path), '-map', '0:a:0', '-ac', '1', '-ar', str(sr)]
    if seconds:
        cmd += ['-t', str(seconds)]
    cmd += ['-f', 'f32le', '-']
    raw = subprocess.run(cmd, stdout=subprocess.PIPE, check=True).stdout
    return np.frombuffer(raw, dtype=np.float32)


def alignment(final, master):
    """Offset (ms) of the muxed audio against the master over a speech-dense
    window, plus the residual after alignment (dB, relative to the master)."""
    sr = 48000
    a = pcm(final, sr); m = pcm(master, sr)
    n = min(len(a), len(m))
    best = None
    for t0 in (60.0, 300.0, 480.0):  # three windows: early, blackout, ending
        i0, i1 = int(t0 * sr), int((t0 + 20.0) * sr)
        x, y = a[i0:i1], m[i0:i1]
        L = 1 << int(np.ceil(np.log2(len(x) * 2)))
        c = np.fft.irfft(np.fft.rfft(x, L) * np.conj(np.fft.rfft(y, L)), L)
        lag = int(np.argmax(np.abs(np.concatenate([c[-2400:], c[:2401]])))) - 2400  # search ±50 ms
        best = (best or []) + [lag]
    lag = int(np.median(best))
    if lag >= 0:
        x, y = a[lag:n], m[:n - lag]
    else:
        x, y = a[:n + lag], m[-lag:n]
    res = 10 * np.log10(np.mean((x - y) ** 2) / max(np.mean(y ** 2), 1e-12))
    return {'offset_samples_48k': lag, 'offset_ms': round(lag / sr * 1000, 3), 'window_lags': best,
            'residual_vs_master_db': round(float(res), 1), 'duration_s': {'final': round(len(a) / sr, 3), 'master': round(len(m) / sr, 3)}}


def frame_at(path, t, out):
    # Input seek to just before the frame boundary picks frame round(t*30).
    run(['ffmpeg', '-v', 'error', '-y', '-ss', f'{max(0.0, t - 0.4 / FPS):.4f}', '-i', str(path), '-frames:v', '1', str(out)])
    return Image.open(out).convert('RGB')


def frame_n(path, n):
    # Accurate input seek (decodes from the previous keyframe): first frame with pts >= (n - 0.4)/fps is frame n.
    r = subprocess.run(['ffmpeg', '-v', 'error', '-ss', f'{max(0.0, (n - 0.4) / FPS):.4f}', '-i', str(path), '-frames:v', '1',
                        '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-'], stdout=subprocess.PIPE, check=True)
    a = np.frombuffer(r.stdout, dtype=np.uint8).astype(np.float64)
    assert a.size == 1920 * 1080 * 3, f'frame {n} of {path}: got {a.size} bytes'
    return a


def psnr(a, b):
    mse = np.mean((a - b) ** 2)
    return float('inf') if mse == 0 else round(float(10 * np.log10(255 ** 2 / mse)), 2)


def sheet(tiles, cols, out, tw=480):
    th = tw * 9 // 16; rows = (len(tiles) + cols - 1) // cols
    f = ImageFont.truetype(FONT, 18)
    S = Image.new('RGB', (cols * tw, rows * (th + 26)), (16, 16, 16)); d = ImageDraw.Draw(S)
    for k, (img, label) in enumerate(tiles):
        x, y = (k % cols) * tw, (k // cols) * (th + 26)
        S.paste(img.resize((tw, th), Image.LANCZOS), (x, y)); d.text((x + 6, y + th + 2), label, font=f, fill=(220, 220, 220))
    S.save(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--final', type=Path, required=True); ap.add_argument('--clean', type=Path, required=True)
    ap.add_argument('--picture', type=Path, required=True); ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--splice-main', type=Path); ap.add_argument('--splice-tail', type=Path); ap.add_argument('--splice-at', type=float)
    a = ap.parse_args(); a.out.mkdir(parents=True, exist_ok=True)
    E = load_edit()
    rep = {'files': {}}
    for k in ('final', 'clean', 'picture'):
        p = getattr(a, k)
        rep['files'][k] = {'name': p.name, 'probe': probe(p), 'decode': decode_errors(p)}
        print(k, 'probed and decoded', flush=True)
    rep['loudness_final'] = loudness(a.final)
    rep['loudness_clean'] = loudness(a.clean)
    rep['audio_alignment_final'] = alignment(a.final, ROOT / 'audio/master/master.wav')
    rep['audio_alignment_clean'] = alignment(a.clean, ROOT / 'audio/master/master.wav')
    print('audio checked', flush=True)
    if a.splice_main and a.splice_tail and a.splice_at is not None:
        n = round(a.splice_at * FPS)
        rep['splice'] = {'at_s': a.splice_at, 'frame': n,
                         'psnr_main_n_vs_tail_0': psnr(frame_n(a.splice_main, n), frame_n(a.splice_tail, 0)),
                         'psnr_picture_n-1_vs_n': psnr(frame_n(a.picture, n - 1), frame_n(a.picture, n)),
                         'psnr_main_n-1_vs_picture_n-1': psnr(frame_n(a.splice_main, n - 1), frame_n(a.picture, n - 1)),
                         'psnr_tail_0_vs_picture_n': psnr(frame_n(a.splice_tail, 0), frame_n(a.picture, n))}
        print('splice checked', flush=True)
    # Contact sheets from the delivery file.
    tmp = a.out / '_frames'; tmp.mkdir(exist_ok=True)
    tiles = []
    for sc in E['scenes']:
        t0, t1 = film_from_story(E, sc['t0']), film_from_story(E, sc['t1'])
        tm = (t0 + t1) / 2
        tiles.append((frame_at(a.final, tm, tmp / f"{sc['id']}.png"), f"{sc['id']} {sc['title']}  {tm:.1f}s"))
    sheet(tiles, 4, a.out / 'scenes.png')
    subs = json.loads((ROOT / 'subtitles/subtitles.json').read_text())
    pick = [subs[i] for i in np.linspace(0, len(subs) - 1, 16).round().astype(int)]
    tiles = [(frame_at(a.final, (ev['start'] + ev['end']) / 2, tmp / f"sub-{k:02d}.png"), f"{ev['line']} {(ev['start'] + ev['end']) / 2:.1f}s {ev['text'][:14]}")
             for k, ev in enumerate(pick)]
    sheet(tiles, 4, a.out / 'subtitles.png')
    rep['sheets'] = ['scenes.png', 'subtitles.png']
    (a.out / 'final-check.json').write_text(json.dumps(rep, ensure_ascii=False, indent=1) + '\n')
    print(json.dumps({k: v for k, v in rep.items() if k != 'files'}, ensure_ascii=False, indent=1))
    for k, v in rep['files'].items():
        vs = [s for s in v['probe']['streams'] if s['codec_type'] == 'video'][0]
        print(k, vs['width'], vs['height'], vs['r_frame_rate'], vs['nb_read_packets'], 'frames', v['probe']['format']['duration'], 's', 'decode errors', v['decode']['error_lines'])


if __name__ == '__main__':
    main()
