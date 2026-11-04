#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build Shaniu's film voice: the public project "我在" asset plus her answers,
all in the same synthetic voice.

- "我在" (W) is the public default wake acknowledgement
  app/bk7258/assets/wake_reply_public.pcm (16 kHz mono PCM16LE, SHA-256
  62091092…, provenance in app/bk7258/assets/WAKE_REPLY_PUBLIC.md: Kokoro-82M
  v1.1-zh int8, sid 0). It is used byte-for-byte, only resampled for the mix.
  It is NOT a recording of the board's speaker. The private personalised
  asset (wake_reply.pcm) is deliberately not used: Kconfig reserves it for
  operator-authorised builds, not publication.
- Her other lines use the same pinned model with a blended voice: 80 % of sid 0
  (the voice of the public "我在") and 20 % of sid 8 (a native Mandarin voice),
  mixed in Kokoro's style-vector space. Chosen in audio/voice/audition
  (REPORT.md): speaker similarity to the 我在 voice 0.81–0.89 (pure sid 0
  0.85–0.90), while 傻妞/小鱼/两勺 come out cleaner than pure sid 0. They take
  the asset's own path: 24 kHz → linear resample to 16 kHz PCM16 → 48 kHz, and
  are loudness-matched to W. Not a human listening test.
- On a real device her answers come from the user-configured cloud TTS and
  can sound different from the local "我在". These are dramatisations.

Usage:
  python3 tools/build_shaniu_voice.py --kokoro <dir with kokoro/, kokoro-deps/, python/> \
      --repo <contest repository checkout> [--force]
Outputs audio/voice/shaniu/*.wav (48 kHz, 24-bit) and manifest.json.
"""
import argparse
import hashlib
import json
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "audio/voice/shaniu"
RATE = 48_000
SID, SPEED = 1, 0.95          # slot 1 of the blended voices file (see blend_voices)
BLEND = (0, 8, 0.80)          # sid a, sid b, weight of a
PUBLIC_SHA = "62091092ee2cde223e43a8a5364a5769620dbb2be903801ebcdfe16975b351ae"
LINES = [
    ("S01", "你好呀。听得见，你想聊点什么？"),
    ("S02", "行吧。小鱼，记住了。"),
    ("S03", "两勺？好吧，记下了。"),
    ("S04a", "灯塔最早可以追溯到两千多年前，亚历山大港的法罗斯灯塔，高一百多米。古人在塔顶点火，用镜子把光照到很远的海面上。"),
    ("S04b", "这就是灯塔的故事。"),
    ("S05", "这是绿萝。叶子发黄，土也干了，该浇水了。"),
    ("S06", "我是傻妞啊。小鱼，你是不是，又想她了？"),
    ("S07", "网络正常，屏幕正常，加速度计也正常。小鱼，你呢？"),
]


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def trim(y, rate, floor=0.008):
    idx = np.flatnonzero(np.abs(y) > floor)
    if idx.size == 0:
        raise RuntimeError("silent synthesis")
    pad = int(0.03 * rate)
    return y[max(0, idx[0] - pad): idx[-1] + pad]


def to_16k_pcm(x, sr):
    """The asset's documented path: linear resample to 16 kHz, PCM16."""
    n = int(round(len(x) * 16000 / sr))
    y = np.interp(np.arange(n) * (sr / 16000), np.arange(len(x)), x)
    return np.clip(np.round(y * 32767), -32768, 32767).astype(np.int16).astype(np.float32) / 32768


def up48(y16):
    import librosa
    return librosa.resample(y16.astype(np.float32), orig_sr=16000, target_sr=RATE, res_type="soxr_hq")


def speech_rms(y):
    """RMS over the louder half of 20 ms frames (speech, not pauses)."""
    hop = int(0.02 * RATE)
    n = len(y) // hop
    e = np.sqrt(np.mean(y[: n * hop].reshape(n, hop) ** 2, axis=1))
    e = np.sort(e)[n // 2:]
    return float(np.sqrt(np.mean(e ** 2)))


def blend_voices(kokoro):
    """A copy of the pinned voices file with slot 1 = BLEND (pinned file untouched)."""
    a, b, w = BLEND
    v = np.fromfile(kokoro / "kokoro" / "voices-pinned.bin", dtype=np.float32).reshape(-1, 510, 256)
    v = v.copy(); v[SID] = w * v[a] + (1 - w) * v[b]
    import tempfile
    path = Path(tempfile.mkdtemp(prefix="kokoro-blend-")) / "voices-blend-80-sid0-sid8.bin"
    v.tofile(path)                          # ~54 MB scratch copy; never written into the project
    return path


def make_tts(kokoro):
    sys.path.insert(0, str(kokoro / "python"))
    import sherpa_onnx  # noqa: E402
    model = sherpa_onnx.OfflineTtsKokoroModelConfig(
        model=str(kokoro / "kokoro" / "model.int8.onnx"),
        voices=str(blend_voices(kokoro)),
        tokens=str(kokoro / "kokoro" / "tokens.txt"),
        lexicon=str(kokoro / "kokoro" / "lexicon-zh.txt"),
        data_dir=str(kokoro / "kokoro-deps" / "espeak-ng-data"),
        lang="en-us",
    )
    cfg = sherpa_onnx.OfflineTtsConfig(
        model=sherpa_onnx.OfflineTtsModelConfig(kokoro=model, num_threads=1, provider="cpu"),
        max_num_sentences=1,
    )
    return sherpa_onnx.OfflineTts(cfg)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--kokoro", type=Path, required=True)
    ap.add_argument("--repo", type=Path, required=True)
    ap.add_argument("--force", action="store_true")
    args = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    manifest_path = OUT / "manifest.json"
    if manifest_path.exists() and not args.force:
        raise SystemExit("manifest exists; refusing to overwrite (use --force for a new version)")
    wake = args.repo / "app/bk7258/assets/wake_reply_public.pcm"
    if sha256(wake) != PUBLIC_SHA:
        raise SystemExit("wake_reply_public.pcm does not match the reviewed public asset")
    raw = np.frombuffer(wake.read_bytes(), dtype="<i2").astype(np.float32) / 32768.0
    w = up48(raw)
    ref = speech_rms(w)
    peak = float(np.max(np.abs(w)))
    g = min(10 ** (-3 / 20) / peak, 1.0 / ref * 0.12)       # same speech level for every line
    w = w * g
    sf.write(OUT / "W.wav", w, RATE, subtype="PCM_24")
    entries = [{"id": "W", "text": "我在。", "file": "W.wav", "duration_seconds": round(len(w) / RATE, 3),
                "sha256": sha256(OUT / "W.wav"), "asset": "app/bk7258/assets/wake_reply_public.pcm",
                "asset_sha256": PUBLIC_SHA, "gain_db": round(20 * np.log10(g), 2),
                "note": "public non-cloned project asset, resampled 16k→48k; not a board-speaker recording"}]
    target_rms = speech_rms(w)
    tts = make_tts(args.kokoro)
    for ident, text in LINES:
        audio = tts.generate(text, sid=SID, speed=SPEED)
        seg = trim(np.asarray(audio.samples, dtype=np.float32), audio.sample_rate)
        y = up48(to_16k_pcm(seg, audio.sample_rate))
        gain = target_rms / speech_rms(y)
        if np.max(np.abs(y)) * gain > 0.89:
            gain = 0.89 / np.max(np.abs(y))
        y = y * gain
        target = OUT / f"{ident}.wav"
        sf.write(target, y, RATE, subtype="PCM_24")
        entries.append({"id": ident, "text": text, "file": target.name,
                        "duration_seconds": round(len(y) / RATE, 3), "sha256": sha256(target),
                        "engine": "kokoro-int8-multi-lang-v1_1 (same pinned files as the public asset)",
                        "voice": "blend 0.80*sid0 + 0.20*sid8", "speed": SPEED, "path": "24k → 16k PCM16 (linear) → 48k"})
        print(ident, entries[-1]["duration_seconds"], flush=True)
    manifest_path.write_text(json.dumps({"rate": RATE, "voice": "Kokoro-82M v1.1-zh int8, blend 0.80*sid0 (the public 我在 voice) + 0.20*sid8",
                                         "lines": entries, "audio_subjective": "not_checked"},
                                        ensure_ascii=False, indent=2) + "\n")


if __name__ == "__main__":
    main()
