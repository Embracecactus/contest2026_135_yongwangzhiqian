#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Cut the deck's live clips from the author's unedited iPhone originals.

素材/实拍/clips.json lists, per clip, the original MOV and one or more
continuous [start, end] ranges at original speed. Nothing is sped up. When a
clip joins two ranges of the same take, a 1.6 s card is inserted that states
exactly how many seconds of the original were left out and why, so the
waiting time is disclosed instead of hidden. The only audio processing is a
dynamic level normalizer, because the device speaker sits ~17 dB below the
presenter's lapel microphone in the originals. Output: 1280x720 30 fps H.264 +
AAC for offline PowerPoint playback, plus a poster frame from the original.
Originals stay in the author's private folder and are not published.

A clip may instead name an author-exported SDR screen recording with
"source_path", "hdr": false, an optional separate "audio_source" on the same
timeline and its own "audio_filter"; its "origin" label is shown on the slide.
If an exported recording carries an unrecognised audio tag, "audio_decoder"
selects the decoder for the first input before FFmpeg opens it.

Usage: python3 cut_clips.py [--source-dir <dir with IMG_xxxx.MOV>] [--only KEY ...]
"""

import argparse
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
LIVE = HERE / "素材" / "实拍"
FONT = HERE / "素材" / "fonts" / "MiSans-Medium.ttf"

# The iPhone originals are HLG (ARIB STD-B67) BT.2020 10-bit HDR. Tone-map to
# BT.709 SDR with Hable; treating them as SDR desaturates skin tones.
HDR_TO_SDR = ("zscale=tin=arib-std-b67:min=bt2020nc:pin=bt2020:rin=tv:t=linear:npl=100,"
              "format=gbrpf32le,zscale=p=bt709,tonemap=tonemap=hable:desat=0,"
              "zscale=t=bt709:m=bt709:r=tv,format=yuv420p")
ENCODE = ["-c:v", "libx264", "-profile:v", "high", "-crf", "20", "-r", "30",
          "-color_primaries", "bt709", "-color_trc", "bt709", "-colorspace", "bt709"]
LEVEL = "dynaudnorm=f=150:g=11:m=12"
CARD = ["-vf", "scale=1280:720,fps=30,format=yuv420p", "-c:v", "libx264", "-profile:v", "high",
        "-crf", "20", "-r", "30", "-color_primaries", "bt709", "-color_trc", "bt709",
        "-colorspace", "bt709"]
AUDIO = ["-c:a", "aac", "-b:a", "160k", "-ar", "48000", "-ac", "2"]


def run(cmd):
    subprocess.run(cmd, check=True)


def video_filter(c, scale="scale=1280:720"):
    tone = f"{HDR_TO_SDR}," if c.get("hdr", True) else ""
    return f"{tone}{scale}"


def segment(c, src, audio, start, end, out):
    length = end - start
    window = ["-ss", f"{start:.3f}", "-t", f"{length:.3f}"]
    decoder = ["-c:a", c["audio_decoder"]] if c.get("audio_decoder") else []
    inputs = [*window, *decoder, "-i", str(src)]
    if audio:
        inputs += [*window, "-i", str(audio)]
    level = c.get("audio_filter", LEVEL)
    run(["ffmpeg", "-v", "error", "-y", *inputs,
         "-map", "0:v:0", "-map", f"{1 if audio else 0}:a:0",
         "-vf", f"{video_filter(c)},fps=30,format=yuv420p", *ENCODE, *AUDIO,
         "-af", f"{level},afade=t=in:d=0.15,afade=t=out:st={length - 0.15:.3f}:d=0.15", str(out)])


def source_of(c, source_dir, videos):
    src = videos / c["source_path"] if c.get("source_path") else source_dir / c["source"]
    audio = videos / c["audio_source"] if c.get("audio_source") else None
    return src, audio


def card(text, out, tmp):
    image = Image.new("RGB", (1280, 720), (17, 24, 39))
    draw = ImageDraw.Draw(image)
    big = ImageFont.truetype(str(FONT), 44)
    small = ImageFont.truetype(str(FONT), 28)
    lines = text.split("\n")
    y = 300 - 30 * len(lines)
    for i, line in enumerate(lines):
        font = big if i == 0 else small
        w = draw.textlength(line, font=font)
        draw.text(((1280 - w) / 2, y), line, font=font, fill=(240, 244, 248) if i == 0 else (148, 163, 184))
        y += 70 if i == 0 else 46
    png = Path(tmp) / "card.png"
    image.save(png)
    run(["ffmpeg", "-v", "error", "-y", "-loop", "1", "-t", "1.6", "-i", str(png),
         "-f", "lavfi", "-t", "1.6", "-i", "anullsrc=r=48000:cl=stereo", *CARD, *AUDIO,
         "-shortest", str(out)])


def poster(src, c):
    """Poster frame from the original, tone-mapped like the clip itself."""
    run(["ffmpeg", "-v", "error", "-y", "-ss", f"{c['poster_at']:.3f}", "-i", str(src),
         "-frames:v", "1", "-vf", video_filter(c), "-q:v", "2", str(LIVE / c["poster"])])


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--source-dir", type=Path, required=True)
    ap.add_argument("--videos", type=Path, required=True, help="Root for the PC recordings")
    ap.add_argument("--posters-only", action="store_true",
                    help="regenerate poster frames without re-cutting the clips")
    ap.add_argument("--only", nargs="+", metavar="KEY", help="process only these clip keys")
    args = ap.parse_args()
    spec = json.loads((LIVE / "clips.json").read_text(encoding="utf-8"))
    selected = [c for c in spec["clips"] if not args.only or c["key"] in args.only]
    if args.posters_only:
        for c in selected:
            poster(source_of(c, args.source_dir, args.videos)[0], c)
            print(c["key"], "poster", c["poster"])
        return
    for c in selected:
        src, audio = source_of(c, args.source_dir, args.videos)
        out = LIVE / c["file"]
        with tempfile.TemporaryDirectory() as tmp:
            parts = []
            ranges = c["segments"]
            for i, (start, end) in enumerate(ranges):
                part = Path(tmp) / f"p{i}.mp4"
                segment(c, src, audio, start, end, part)
                parts.append(part)
                if i + 1 < len(ranges):
                    gap = ranges[i + 1][0] - end
                    note = c.get("gap_notes", [""] * len(ranges))[i]
                    cpart = Path(tmp) / f"c{i}.mp4"
                    card(f"原片此处略去 {gap:.0f} 秒\n{note}\n未加速；前后均为原速", cpart, tmp)
                    parts.append(cpart)
            listing = Path(tmp) / "list.txt"
            listing.write_text("".join(f"file '{p}'\n" for p in parts))
            run(["ffmpeg", "-v", "error", "-y", "-f", "concat", "-safe", "0", "-i", str(listing),
                 "-c", "copy", "-movflags", "+faststart", str(out)])
        poster(src, c)
        c["start"], c["end"] = ranges[0][0], ranges[-1][1]
        c["omitted_seconds"] = round(sum(ranges[i + 1][0] - ranges[i][1] for i in range(len(ranges) - 1)), 2)
        c["sha256"] = hashlib.sha256(out.read_bytes()).hexdigest()
        print(c["key"], "segments", len(ranges), "omitted", c["omitted_seconds"], c["sha256"][:12])
    (LIVE / "clips.json").write_text(json.dumps(spec, ensure_ascii=False, indent=2) + "\n",
                                     encoding="utf-8")


if __name__ == "__main__":
    main()
