#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compose the finals demo and a JianYing draft without opening the editor."""

import argparse
import concurrent.futures
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from xml.sax.saxutils import escape

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
WORKSPACE = REPO.parent
SPEC = json.loads((HERE / "edit.json").read_text())
ASSETS = HERE / "assets"
CACHE = HERE / "render-cache"
LOGS = HERE / "verification"
FONT = HERE.parent / "素材/fonts/MiSans-Medium.ttf"
W, H, FPS = SPEC["width"], SPEC["height"], SPEC["fps"]


def command(args, log):
    with Path(log).open("wb") as stream:
        p = subprocess.run(args, stdout=stream, stderr=subprocess.STDOUT)
    if p.returncode:
        raise RuntimeError(f"Command failed ({p.returncode}); see {log}")


def probe(path):
    return json.loads(subprocess.check_output([
        "ffprobe", "-v", "error", "-show_format", "-show_streams",
        "-of", "json", str(path),
    ]))


def sha(path):
    with Path(path).open("rb") as stream:
        digest = hashlib.sha256()
        for data in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(data)
    return digest.hexdigest()


def root_for(source, videos):
    return WORKSPACE if source["root"] == "workspace" else videos


def video_path(key, videos):
    if key in ("approval", "workbench"):
        return ASSETS / f"{key}-normalized.mp4"
    source = SPEC["sources"][key]
    return root_for(source, videos) / source["path"]


def timeline():
    cursor = 0
    rows = []
    for source in SPEC["timeline"]:
        row = dict(source)
        duration = row.get("duration", row.get("out", 0) - row.get("in", 0))
        row["frames"] = round(duration * FPS)
        row["duration"] = row["frames"] / FPS
        row["start"] = cursor / FPS
        cursor += row["frames"]
        rows.append(row)
    return rows


def stamp(seconds, ass=False):
    if ass:
        value = round(seconds * 100)
        return f"{value // 360000}:{value // 6000 % 60:02}:{value // 100 % 60:02}.{value % 100:02}"
    value = round(seconds * 1000)
    return f"{value // 3600000:02}:{value // 60000 % 60:02}:{value // 1000 % 60:02},{value % 1000:03}"


def text_width(draw, text, font):
    return draw.textlength(text, font=font)


def card(name, data):
    from PIL import Image, ImageDraw, ImageFont
    image = Image.new("RGB", (W, H))
    draw = ImageDraw.Draw(image)
    for y in range(H):
        ratio = y / H
        color = tuple(round(a + (b - a) * ratio) for a, b in zip((11, 24, 32), (19, 43, 42)))
        draw.line((0, y, W, y), fill=color)
    mint, white, grey = "#C6F5DC", "#F5F8F5", "#A7BCB7"
    draw.rounded_rectangle((104, 140, 170, 148), radius=4, fill=mint)
    regular = lambda size: ImageFont.truetype(str(FONT), size)
    draw.text((104, 182), data["kicker"], font=regular(29), fill=mint)
    title_size = 132 if name == "intro" else 83
    draw.text((100, 365), data["title"], font=regular(title_size), fill=white)
    draw.text((104, 550 if name == "intro" else 510), data["subtitle"], font=regular(44), fill=mint)
    draw.line((104, 742, 1816, 742), fill="#33564E", width=2)
    labels = ["平台与实机", "手机认领", "电脑审批", "桌面工作台"]
    for index, label in enumerate(labels):
        x = 104 + index * 432
        color = mint if index == data["active"] else "#59736C"
        draw.text((x, 780), f"0{index + 1}", font=regular(25), fill=color)
        draw.text((x + 58, 773), label, font=regular(32), fill=color)
    draw.text((104, 969), data["footer"], font=regular(27), fill=grey)
    if name not in ("intro", "outro"):
        label = data["number"]
        draw.text((W - 130 - text_width(draw, label, regular(180)), 340),
                  label, font=regular(180), fill="#24463C")
    image.save(ASSETS / f"card-{name}.png")
    title_y = 482 if name == "intro" else 443
    subtitle_y = 590 if name == "intro" else 550
    steps = "".join(
        f'<text x="{104+i*432}" y="810" font-size="32" fill="{mint if i == data["active"] else "#59736C"}">0{i+1}  {escape(label)}</text>'
        for i, label in enumerate(labels)
    )
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="1920" height="1080" viewBox="0 0 1920 1080">
<defs><linearGradient id="bg" x2="0" y2="1"><stop stop-color="#0b1820"/><stop offset="1" stop-color="#132b2a"/></linearGradient></defs>
<rect width="1920" height="1080" fill="url(#bg)"/><g font-family="MiSans, sans-serif">
<rect x="104" y="140" width="66" height="8" rx="4" fill="{mint}"/>
<text x="104" y="212" font-size="29" fill="{mint}">{escape(data["kicker"])}</text>
<text x="100" y="{title_y}" font-size="{title_size}" fill="{white}">{escape(data["title"])}</text>
<text x="104" y="{subtitle_y}" font-size="44" fill="{mint}">{escape(data["subtitle"])}</text>
<path d="M104 742H1816" stroke="#33564e" stroke-width="2"/>{steps}
<text x="104" y="998" font-size="27" fill="{grey}">{escape(data["footer"])}</text></g></svg>'''
    (ASSETS / f"card-{name}.svg").write_text(svg)


def write_ass(row):
    header = """[Script Info]
ScriptType: v4.00+
PlayResX: 1920
PlayResY: 1080
WrapStyle: 2

[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Guide,MiSans,34,&H00DCF5C6,&H00FFFFFF,&H0020180B,&H0020180B,0,0,0,0,100,100,0,0,3,11,0,9,70,70,60,1

[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
"""
    lines = []
    for item in row.get("overlays", []):
        tags = r"{\an9\pos(1838," + str(item.get("y", 62)) + r")\fad(150,150)}"
        lines.append(f"Dialogue: 0,{stamp(item['start'], True)},{stamp(item['end'], True)},Guide,,0,0,0,,{tags}{item['text']}")
    path = ASSETS / (row["id"] + ".ass")
    path.write_text(header + "\n".join(lines) + "\n")
    return path


def captions(videos):
    output = []
    corrections = {
        "眼镜素材包": "眼睛素材包", "一个眼镜的": "一个眼睛的",
        "Windows的计算机": "Windows的计算器", "我们计算机就会": "计算器就会",
        "提示词（口误唤醒词）": "唤醒词", "当前提示词是你好": "当前唤醒词是你好",
    }
    for row in timeline():
        if "source" not in row:
            continue
        source = SPEC["sources"][row["source"]]
        path = root_for(source, videos) / source["srt"]
        for block in re.split(r"\n\s*\n", path.read_text(encoding="utf-8-sig").strip()):
            lines = block.splitlines()
            if len(lines) < 3 or "-->" not in lines[1]:
                continue
            def seconds(text):
                h, m, s, ms = map(int, re.split(r"[:,]", text.strip()))
                return h * 3600 + m * 60 + s + ms / 1000
            start, end = map(seconds, lines[1].split("-->"))
            start, end = max(start, row["in"]), min(end, row["out"])
            if end <= start:
                continue
            text = "\n".join(lines[2:])
            for old, new in corrections.items():
                text = text.replace(old, new)
            output.append((row["start"] + start - row["in"],
                           row["start"] + end - row["in"], text))
    text = "\n\n".join(f"{i}\n{stamp(a)} --> {stamp(b)}\n{t}"
                       for i, (a, b, t) in enumerate(output, 1)) + "\n"
    (HERE / "中文字幕-剪辑时间轴.srt").write_text(text, encoding="utf-8-sig")


def prepare(videos):
    for folder in (ASSETS, CACHE, LOGS):
        folder.mkdir(exist_ok=True)
    assert FONT.is_file(), "The existing deck's licensed MiSans font is required."
    manifest = {}
    for key, data in SPEC["sources"].items():
        source = root_for(data, videos) / data["path"]
        assert source.is_file(), source
        metadata = probe(source)
        manifest[key] = {"input": data["path"], "sha256": sha(source),
                         "bytes": source.stat().st_size,
                         "seconds": float(metadata["format"]["duration"]),
                         "period": data["period"]}
        if key in ("approval", "workbench"):
            target = video_path(key, videos)
            if target.exists():
                continue
            args = ["ffmpeg", "-hide_banner", "-loglevel", "warning", "-y"]
            if data.get("audio_decoder"):
                args += ["-c:a", data["audio_decoder"]]
            args += ["-i", str(source)]
            audio = root_for(data, videos) / data["audio"] if "audio" in data else None
            if audio:
                args += ["-i", str(audio)]
            args += ["-map", "0:v:0", "-map", f"{1 if audio else 0}:a:0",
                     "-c:v", "libx264", "-threads", "4", "-preset", "fast", "-crf", "18",
                     "-pix_fmt", "yuv420p", "-r", "30", "-c:a", "aac", "-b:a", "192k",
                     "-ar", "48000", "-ac", "2", "-movflags", "+faststart", str(target)]
            command(args, LOGS / f"normalize-{key}.log")
    for name, data in SPEC["cards"].items():
        card(name, data)
    for row in timeline():
        write_ass(row)
    captions(videos)
    (HERE / "sources.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
    (HERE / "timeline.json").write_text(json.dumps(timeline(), ensure_ascii=False, indent=2) + "\n")
    print("Prepared four original sources, five cards, guide tracks and the remapped SRT.")


def render_one(row, videos, preview=False):
    length = min(row["duration"], 10) if preview else row["duration"]
    target = CACHE / (row["id"] + ("-preview" if preview else "") + ".mp4")
    args = ["ffmpeg", "-hide_banner", "-loglevel", "warning", "-y"]
    if "card" in row:
        args += ["-loop", "1", "-framerate", str(FPS), "-i", str(ASSETS / f"card-{row['card']}.png"),
                 "-f", "lavfi", "-i", "anullsrc=r=48000:cl=stereo"]
        gain = 0
    else:
        args += ["-ss", str(row["in"]), "-i", str(video_path(row["source"], videos))]
        gain = SPEC["sources"][row["source"]]["gain_db"]
    vf = f"scale={W}:{H}:flags=lanczos,setsar=1,fps={FPS},format=yuv420p"
    if row.get("overlays"):
        vf += f",ass={ASSETS / (row['id'] + '.ass')}:fontsdir={FONT.parent}"
    af = f"volume={gain}dB,afade=t=in:d=0.045,afade=t=out:st={max(0,length-0.045)}:d=0.045"
    args += ["-map", "0:v:0", "-map", "1:a:0" if "card" in row else "0:a:0",
             "-vf", vf, "-af", af, "-t", f"{length:.6f}",
             "-c:v", "libx264", "-threads", "6", "-preset", "fast", "-crf", "20",
             "-profile:v", "high", "-pix_fmt", "yuv420p",
             "-color_primaries", "bt709", "-color_trc", "bt709", "-colorspace", "bt709",
             "-video_track_timescale", "30000", "-c:a", "aac", "-b:a", "160k",
             "-ar", "48000", "-ac", "2", "-movflags", "+faststart", str(target)]
    command(args, LOGS / (row["id"] + ("-preview" if preview else "") + ".log"))
    print("Rendered", row["id"], flush=True)
    return target


def render(videos, delivery):
    rows = timeline()
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        list(pool.map(lambda row: render_one(row, videos), rows))
    concat = CACHE / "concat.txt"
    concat.write_text("ffconcat version 1.0\n" + "".join(
        f"file '{(CACHE / (row['id'] + '.mp4')).as_posix()}'\nduration {row['duration']:.6f}\n"
        for row in rows))
    length = sum(row["duration"] for row in rows)
    chapters = [row for row in rows if "chapter" in row]
    metadata = ";FFMETADATA1\ntitle=傻妞 · 决赛作品演示\nartist=勇往直前\n"
    for index, row in enumerate(chapters):
        end = chapters[index + 1]["start"] if index + 1 < len(chapters) else length
        metadata += f"[CHAPTER]\nTIMEBASE=1/1000\nSTART={round(row['start']*1000)}\nEND={round(end*1000)}\ntitle={row['chapter']}\n"
    (CACHE / "chapters.txt").write_text(metadata)
    delivery.mkdir(parents=True, exist_ok=True)
    output = delivery / SPEC["output_name"]
    command(["ffmpeg", "-hide_banner", "-loglevel", "warning", "-y",
             "-f", "concat", "-safe", "0", "-i", str(concat),
             "-f", "ffmetadata", "-i", str(CACHE / "chapters.txt"),
             "-map", "0:v:0", "-map", "0:a:0", "-map_metadata", "1", "-map_chapters", "1",
             "-c:v", "copy", "-c:a", "aac", "-b:a", "160k", "-ar", "48000",
             "-af", "aresample=async=1:first_pts=0,alimiter=limit=0.794328:attack=5:release=60:level=0", "-t", str(length),
             "-movflags", "+faststart", str(output)], LOGS / "assemble.log")
    print("MP4:", output)


def windows_path(value):
    if value.startswith("/"):
        return subprocess.check_output(["wslpath", "-w", value], text=True).strip()
    return value


def make_draft(videos, draft_root):
    current_dir = str(HERE)
    candidates = [os.environ.get("JY_SKILL_ROOT", ""),
                  str(Path.home() / ".codex/skills/jianying-editor-skill"),
                  os.path.join(current_dir, ".agents/skills/jianying-editor"),
                  os.path.join(current_dir, ".agent/skills/jianying-editor"),
                  os.path.join(current_dir, "skills/jianying-editor")]
    skill_root = next((p for p in candidates if p and
                       os.path.isfile(os.path.join(p, "scripts/jy_wrapper.py"))), None)
    if not skill_root:
        raise ImportError("Set JY_SKILL_ROOT to the installed JianYing skill.")
    sys.path.insert(0, os.path.join(skill_root, "scripts"))
    from jy_wrapper import JyProject
    import pyJianYingDraft as draft
    name = SPEC["draft_name"]
    owned = HERE / "jianying-project.json"
    if (draft_root / name).exists() and not owned.exists():
        raise RuntimeError("An unrelated draft already uses this name.")
    project = JyProject(name, width=W, height=H, drafts_root=str(draft_root), overwrite=True)
    project._explicit_res = True
    project.script.fps = FPS
    for row in timeline():
        path = ASSETS / f"card-{row['card']}.png" if "card" in row else video_path(row["source"], videos)
        segment = project.add_clip(str(path), f"{row.get('in',0)}s", f"{row['duration']}s",
                                   target_start=f"{row['start']}s", track_name="完整演示")
        if segment is None:
            raise RuntimeError(f"Cannot import {path.name}")
        if "source" in row:
            segment.volume = 10 ** (SPEC["sources"][row["source"]]["gain_db"] / 20)
        for item in row.get("overlays", []):
            project.add_text_simple(
                item["text"], start_time=f"{row['start']+item['start']}s",
                duration=f"{item['end']-item['start']}s", track_name="章节与操作导读",
                style=draft.TextStyle(size=4.2, color=(0.776,0.961,0.863), align=2),
                clip_settings=draft.ClipSettings(transform_x=0.48, transform_y=1-item.get("y",62)/540),
                background=draft.TextBackground(color="#0B1820", alpha=1, round_radius=0.15),
                border=draft.TextBorder(alpha=0, width=0))
    result = project.save()
    root = draft_root / name
    def paths(value):
        if isinstance(value, dict):
            return {key: paths(item) for key, item in value.items()}
        if isinstance(value, list):
            return [paths(item) for item in value]
        if isinstance(value, str) and value.startswith("/") and Path(value).exists():
            return windows_path(value)
        return value
    for path in root.glob("*.json"):
        data = paths(json.loads(path.read_text()))
        path.write_text(json.dumps(data, ensure_ascii=False, separators=(",", ":")))
    snapshot = HERE / "jianying-draft"
    snapshot.mkdir(exist_ok=True)
    for path in root.glob("*.json"):
        shutil.copy2(path, snapshot / path.name)
    owned.write_text(json.dumps({"draft_name":name, "draft_root":str(draft_root),
                                "saved":result["status"]}, ensure_ascii=False, indent=2)+"\n")
    print("JianYing draft saved:", root)


def verify(delivery):
    path = delivery / SPEC["output_name"]
    metadata = probe(path)
    length = sum(row["duration"] for row in timeline())
    assert abs(float(metadata["format"]["duration"]) - length) < 0.2
    video = next(s for s in metadata["streams"] if s["codec_type"] == "video")
    audio = next(s for s in metadata["streams"] if s["codec_type"] == "audio")
    assert (video["width"], video["height"], video["codec_name"]) == (W, H, "h264")
    assert audio["codec_name"] == "aac" and audio["sample_rate"] == "48000"
    command(["ffmpeg", "-v", "error", "-i", str(path), "-f", "null", "-"],
            LOGS / "full-decode.log")
    report = {"path": str(path), "bytes":path.stat().st_size, "sha256":sha(path),
              "duration":metadata["format"]["duration"], "width":W, "height":H,
              "fps":video["r_frame_rate"], "audio":audio["codec_name"],
              "full_decode":"passed", "new_voiceover":False, "new_music":False,
              "footage_speed":1.0}
    (HERE / "delivery.json").write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n")
    print(json.dumps(report, ensure_ascii=False))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("stage", choices=["prepare","preview","render","draft","verify"])
    parser.add_argument("--videos", type=Path, required=True)
    parser.add_argument("--delivery", type=Path)
    parser.add_argument("--draft-root", type=Path)
    args = parser.parse_args()
    delivery = args.delivery or args.videos / "傻妞产品交付-20261104"
    if args.stage == "prepare":
        prepare(args.videos)
    elif args.stage == "preview":
        render_one(next(r for r in timeline() if r["id"] == "07-approval"), args.videos, True)
    elif args.stage == "render":
        render(args.videos, delivery)
    elif args.stage == "draft":
        root = args.draft_root or args.videos.parent / "AppData/Local/JianyingPro/User Data/Projects/com.lveditor.draft"
        make_draft(args.videos, root)
    else:
        verify(delivery)


if __name__ == "__main__":
    main()
