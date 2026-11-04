#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build the final defense deck from the original PPTX, including PC approval footage.

Story: one evening with Shaniu, from "你好，openvela" to "晚安". Each act pairs an
unedited iPhone clip from the 2026-09-19 shoot with the mechanism and evidence
behind it; post-preliminary work, platform engineering and evidence follow.

Inputs (all local, none committed):
  docs/2026 决赛答辩-勇往直前-傻妞.pptx            original deck on the official template
  docs/2026 首届 openvela AI 硬件全球开发者大赛答辩模板.pptx  official template (hash only)
  docs/contest/答辩材料/素材/                       photos, sprites, renders
  docs/contest/答辩材料/素材/实拍/clips.json        clip sources; cut by cut_clips.py
  docs/verification/bk7258/2026-11-03-feature-validation/  11-03 board recording
Outputs:
  docs/2026 决赛答辩-勇往直前-傻妞-修订版.pptx
  docs/contest/答辩材料/答辩时间与来源.json · 讲稿.md · 证据与来源.md
The PDF and slide PNGs are exported by render.ps1 through PowerPoint.
Run: python3 build.py --film <board-wake-subtitled-film.mp4>   (python-pptx 1.0.2)
"""

import argparse
import copy
import hashlib
import json
import re
import subprocess
from pathlib import Path

from lxml import etree
from pptx import Presentation
from pptx.dml.color import RGBColor
from pptx.enum.shapes import MSO_SHAPE
from pptx.enum.text import MSO_ANCHOR, PP_ALIGN
from pptx.oxml.ns import qn
from pptx.oxml.xmlchemy import OxmlElement
from pptx.util import Inches, Pt

HERE = Path(__file__).resolve().parent
DOCS = HERE.parents[1]
SOURCE = DOCS / "2026 决赛答辩-勇往直前-傻妞.pptx"
TEMPLATE = DOCS / "2026 首届 openvela AI 硬件全球开发者大赛答辩模板.pptx"
OUTPUT = DOCS / "2026 决赛答辩-勇往直前-傻妞-修订版.pptx"
MEDIA = HERE / "素材"
LIVE = MEDIA / "实拍"
CLIPS = {c["key"]: c for c in json.loads((LIVE / "clips.json").read_text(encoding="utf-8"))["clips"]}
VERIFICATION = DOCS / "verification/bk7258/2026-11-03-feature-validation"
BOARD_VIDEO = VERIFICATION / "videos/board/camera-capture-cancel-recapture.mp4"
BOARD_POSTER = VERIFICATION / "posters/camera-workbench.jpg"
INK, BODY, MUTED = "111827", "334155", "64748B"
CYAN, BLUE, PURPLE = "008FA8", "4D7CFE", "A855F7"
AMBER, GREEN = "B45309", "127A56"
PAPER, PALE, EDGE = "FFFFFF", "F5F7FA", "DFE5ED"
A_NS = "{http://schemas.openxmlformats.org/drawingml/2006/main}"

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument(
    "--film",
    type=Path,
    required=True,
    help="《我在》v3.1 板载应答音中文字幕版路径；交付时与 PPT 放在同一目录",
)
args = parser.parse_args()
film = args.film.expanduser().resolve(strict=True)

prs = Presentation(SOURCE)
slides = list(prs.slides)
header = [copy.deepcopy(s._element) for s in slides[3].shapes
          if s.name in ("AutoShape 2", "AutoShape 3", "AutoShape 4")]
content_layout = slides[3].slide_layout
timing = []
used_media = []


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def duration(path):
    out = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration",
                          "-of", "default=nw=1:nk=1", str(path)],
                         capture_output=True, text=True, check=True).stdout
    return round(float(out), 2)


# ---------------------------------------------------------------- text helpers
def style_paragraph(p, size, color, bold, align):
    p.alignment = align
    p.space_before = Pt(0)
    p.space_after = Pt(10)
    p.line_spacing = 1.18
    prop = p._p.get_or_add_pPr()
    for child in list(prop):
        if child.tag.rsplit("}", 1)[-1] in ("buChar", "buAutoNum", "buBlip"):
            prop.remove(child)
    if prop.find(A_NS + "buNone") is None:
        prop.append(OxmlElement("a:buNone"))
    for run in p.runs:
        run.font.name = "MiSans"
        run.font.size = Pt(size)
        run.font.bold = bold
        run.font.color.rgb = RGBColor.from_string(color)
        rpr = run._r.get_or_add_rPr()
        for tag in ("a:ea", "a:cs"):
            node = rpr.find(A_NS + tag[2:])
            if node is None:
                node = OxmlElement(tag)
                rpr.append(node)
            node.set("typeface", "MiSans")


def set_text(shape, value, size=30, color=BODY, bold=False, align=PP_ALIGN.LEFT,
             anchor=MSO_ANCHOR.TOP):
    tf = shape.text_frame
    tf.clear()
    tf.word_wrap = True
    tf.margin_left = tf.margin_right = Inches(0.03)
    tf.margin_top = tf.margin_bottom = Inches(0.02)
    tf.vertical_anchor = anchor
    for i, line in enumerate(value.split("\n")):
        p = tf.paragraphs[0] if i == 0 else tf.add_paragraph()
        p.text = line
        style_paragraph(p, size, color, bold, align)
    return shape


def text(target, value, x, y, w, h, size=30, color=BODY, bold=False,
         align=PP_ALIGN.LEFT, anchor=MSO_ANCHOR.TOP):
    shape = target.shapes.add_textbox(Inches(x), Inches(y), Inches(w), Inches(h))
    return set_text(shape, value, size, color, bold, align, anchor)


def box(target, x, y, w, h, fill=PALE, line=EDGE, rounded=True, radius=0.07):
    shape = target.shapes.add_shape(
        MSO_SHAPE.ROUNDED_RECTANGLE if rounded else MSO_SHAPE.RECTANGLE,
        Inches(x), Inches(y), Inches(w), Inches(h))
    if rounded:
        shape.adjustments[0] = radius
    shape.fill.solid()
    shape.fill.fore_color.rgb = RGBColor.from_string(fill)
    if line:
        shape.line.color.rgb = RGBColor.from_string(line)
        shape.line.width = Pt(1.25)
    else:
        shape.line.fill.background()
    shape.shadow.inherit = False
    return shape


def arrow(target, x, y, w, h, fill=MUTED, kind=MSO_SHAPE.RIGHT_ARROW):
    shape = target.shapes.add_shape(kind, Inches(x), Inches(y), Inches(w), Inches(h))
    shape.fill.solid()
    shape.fill.fore_color.rgb = RGBColor.from_string(fill)
    shape.line.fill.background()
    shape.shadow.inherit = False
    return shape


def chip(target, value, x, y, w, color=GREEN, fill="E9F8F1"):
    box(target, x, y, w, 0.62, fill, None, True, 0.5)
    return text(target, value, x + 0.2, y + 0.08, w - 0.4, 0.5, 20, color, True)


def group(slide):
    return slide.shapes.add_group_shape()


def picture(target, path, x, y, w=None, h=None):
    return target.shapes.add_picture(str(path), Inches(x), Inches(y),
                                     Inches(w) if w else None, Inches(h) if h else None)


def reset(slide, title):
    for shape in list(slide.shapes):
        shape._element.getparent().remove(shape._element)
    for element in header:
        slide.shapes._spTree.insert_element_before(copy.deepcopy(element), "p:extLst")
    heading = next(s for s in slide.shapes if s.name == "AutoShape 3")
    heading.width = Inches(22.6)
    heading.height = Inches(1.05)
    set_text(heading, title, 44, INK, True)
    return slide


def new_slide(title):
    slide = prs.slides.add_slide(content_layout)
    return reset(slide, title)


def footer(slide, value, size=24):
    text(slide, value, 1.95, 12.7, 22.7, 1.2, size, MUTED)


def card(target, x, y, w, h, title, body, accent=BLUE, badge=None, body_size=27):
    box(target, x, y, w, h)
    box(target, x, y, w, 0.09, accent, None, False)
    text(target, title, x + 0.4, y + 0.38, w - 0.8, 0.9, 32, accent, True)
    text(target, body, x + 0.4, y + 1.38, w - 0.8, h - (2.9 if badge else 1.6), body_size)
    if badge:
        text(target, badge, x + 0.4, y + h - 1.35, w - 0.8, 1.15, 21, MUTED, True)


def movie(slide, path, poster, x, y, w, name):
    h = w * 9 / 16
    m = slide.shapes.add_movie(str(path), Inches(x), Inches(y), Inches(w), Inches(h),
                               poster_frame_image=str(poster), mime_type="video/mp4")
    m.name = name
    return m


# ------------------------------------------------------- transitions + builds
P_NS = "http://schemas.openxmlformats.org/presentationml/2006/main"


def add_transition(slide):
    sld = slide._element
    old = sld.find(qn("p:transition"))
    if old is not None:
        sld.remove(old)
    tr = etree.SubElement(sld, qn("p:transition"))
    tr.set("spd", "med")
    etree.SubElement(tr, qn("p:fade"))
    # Schema order: cSld, clrMapOvr, transition, timing, extLst.
    anchor = sld.find(qn("p:clrMapOvr"))
    sld.remove(tr)
    if anchor is not None:
        anchor.addnext(tr)
    else:
        sld.find(qn("p:cSld")).addnext(tr)


def add_builds(slide, steps):
    """Fade each step in on a click; shapes inside a step follow 0.12 s apart."""
    ids = iter(range(3, 10000))
    pars = []
    for step in steps:
        outer, inner = next(ids), next(ids)
        effects = []
        for k, shape in enumerate(step):
            e, s1, s2 = next(ids), next(ids), next(ids)
            node = "clickEffect" if k == 0 else "withEffect"
            spid = shape.shape_id
            effects.append(
                f'<p:par><p:cTn id="{e}" presetID="10" presetClass="entr" presetSubtype="0" '
                f'fill="hold" nodeType="{node}"><p:stCondLst><p:cond delay="{k * 120}"/></p:stCondLst>'
                f'<p:childTnLst><p:set><p:cBhvr><p:cTn id="{s1}" dur="1" fill="hold"><p:stCondLst>'
                f'<p:cond delay="0"/></p:stCondLst></p:cTn><p:tgtEl><p:spTgt spid="{spid}"/></p:tgtEl>'
                f'<p:attrNameLst><p:attrName>style.visibility</p:attrName></p:attrNameLst></p:cBhvr>'
                f'<p:to><p:strVal val="visible"/></p:to></p:set><p:animEffect transition="in" filter="fade">'
                f'<p:cBhvr><p:cTn id="{s2}" dur="450"/><p:tgtEl><p:spTgt spid="{spid}"/></p:tgtEl>'
                f'</p:cBhvr></p:animEffect></p:childTnLst></p:cTn></p:par>')
        pars.append(
            f'<p:par><p:cTn id="{outer}" fill="hold"><p:stCondLst><p:cond delay="indefinite"/>'
            f'</p:stCondLst><p:childTnLst><p:par><p:cTn id="{inner}" fill="hold"><p:stCondLst>'
            f'<p:cond delay="0"/></p:stCondLst><p:childTnLst>{"".join(effects)}</p:childTnLst>'
            f'</p:cTn></p:par></p:childTnLst></p:cTn></p:par>')
    xml = (f'<p:timing xmlns:p="{P_NS}"><p:tnLst><p:par><p:cTn id="1" dur="indefinite" '
           f'restart="never" nodeType="tmRoot"><p:childTnLst><p:seq concurrent="1" nextAc="seek">'
           f'<p:cTn id="2" dur="indefinite" nodeType="mainSeq"><p:childTnLst>{"".join(pars)}'
           f'</p:childTnLst></p:cTn><p:prevCondLst><p:cond evt="onPrev" delay="0"><p:tgtEl>'
           f'<p:sldTgt/></p:tgtEl></p:cond></p:prevCondLst><p:nextCondLst><p:cond evt="onNext" '
           f'delay="0"><p:tgtEl><p:sldTgt/></p:tgtEl></p:cond></p:nextCondLst></p:seq>'
           f'</p:childTnLst></p:cTn></p:par></p:tnLst></p:timing>')
    sld = slide._element
    old = sld.find(qn("p:timing"))
    if old is not None:
        sld.remove(old)
    node = etree.fromstring(xml)
    tr = sld.find(qn("p:transition"))
    (tr if tr is not None else sld.find(qn("p:clrMapOvr"))).addnext(node)


def notes(slide, title, seconds, script, sources=(), cue="", video=0):
    item = {"title": title, "seconds": seconds, "video_seconds": video, "script": script,
            "cue": cue, "sources": list(sources)}
    timing.append(item)
    elapsed = sum(i["seconds"] for i in timing)
    meta = (f"主讲预算 {seconds} 秒（含视频 {video} 秒）；累计 {elapsed // 60}:{elapsed % 60:02d}。"
            if seconds else "隐藏备查页：不占 8 分钟主讲时间；评委提问时打开。")
    slide.notes_slide.notes_text_frame.text = "\n\n".join(
        part for part in (meta, script, cue,
                          "依据：\n" + "\n".join(sources) if sources else "") if part)


def live(slide, key, x, y, w, label_y=None, label=True):
    """Embed a clip and caption it with its source and disclosed editing scope."""
    c = CLIPS[key]
    path, poster = LIVE / c["file"], LIVE / c["poster"]
    seconds = duration(path)
    movie(slide, path, poster, x, y, w, f"{c['title']} · 点击播放")
    if label:
        ly = label_y if label_y is not None else y + w * 9 / 16 + 0.1
        cut = c.get("cut_note") or (f"略去 {c['omitted_seconds']:.0f} s 已在片中标注"
                                    if c.get("omitted_seconds") else "连续原速")
        origin = c.get("origin", "9/19 手机原片")
        text(slide, f"{origin} {c['source']} {c['start']:.1f}–{c['end']:.1f} s · {cut} · {seconds:.0f} 秒 · {c['scope']}",
             x, ly, w, 0.55, 16, MUTED)
    used_media.append({"key": key, "file": path.relative_to(DOCS.parent).as_posix(),
                       "seconds": seconds, "sha256": sha(path), "source": c["source"],
                       "start": c["start"], "end": c["end"], "segments": c["segments"],
                       "omitted_seconds": c.get("omitted_seconds", 0), "scope": c["scope"],
                       "embedded": True})
    return round(seconds)


def side_steps(slide, steps, x=17.5, y=3.4, w=7.15, h=None, size=21):
    """Right-hand annotations: (time, what happens, mechanism) for a clip."""
    n = len(steps)
    h = h or (8.9 / n)
    out = []
    for i, (t, what, how, accent) in enumerate(steps):
        g = group(slide)
        yy = y + i * h
        box(g, x, yy, w, h - 0.2)
        text(g, t, x + 0.25, yy + 0.18, 1.6, 0.6, 19, accent, True)
        text(g, what, x + 1.75, yy + 0.15, w - 2.0, 0.7, size + 2, INK, True)
        text(g, how, x + 1.75, yy + 0.82, w - 2.0, h - 1.15, size - 2, BODY)
        out.append([g])
    return out


def table(slide, rows, widths, y=3.4, size=21, head=BLUE, x=1.95, row_h=0.8):
    shape = slide.shapes.add_table(len(rows), len(rows[0]), Inches(x), Inches(y),
                                   Inches(sum(widths)), Inches(row_h * len(rows)))
    t = shape.table
    for column, width in zip(t.columns, widths):
        column.width = Inches(width)
    for k, row in enumerate(t.rows):
        row.height = Inches(min(row_h, 0.66) if k == 0 else row_h)
    for i, row in enumerate(rows):
        for j, value in enumerate(row):
            cell = t.cell(i, j)
            cell.fill.solid()
            cell.fill.fore_color.rgb = RGBColor.from_string(head if i == 0 else (PALE if i % 2 else PAPER))
            cell.margin_left = cell.margin_right = Inches(0.15)
            cell.margin_top = cell.margin_bottom = Inches(0.06)
            cell.text = value
            for p in cell.text_frame.paragraphs:
                style_paragraph(p, size + 2 if i == 0 else size, PAPER if i == 0 else BODY, i == 0, PP_ALIGN.LEFT)
                p.space_after = Pt(2)
    return shape


# Hidden appendices, in final order. Body text refers to them by key so the
# letters printed on main slides always match tail.py's relabelling.
APPX = ["matrix", "board-camera", "kws-research", "tts", "motion", "cards", "custom",
        "failures", "repro", "approval", "bom", "persona", "app-voice", "wakeword",
        "recording", "app", "film", "boards", "workbench-detail", "index"]


def ref(key):
    return f"备查 {chr(65 + APPX.index(key))}"


def add_appendix(key, slide):
    assert APPX[len(appendix)] == key, (key, len(appendix))
    appendix.append(slide)


# Spoken script, paced at about 4.5 Chinese characters per second of talk.
# The slides carry the detail; this is what the presenter actually says.
SPOKEN = {
    "封面": (8, "各位评委好，我是勇往直前的李健。今天讲一个晚上，和桌上的傻妞。"),
    "序幕": (13, "九月十九日的手机原片：“你好，openvela”，“我在”。今晚就把这一声拆开。"),
    "她是谁": (18, "她是官方 AIDK 主板，装在套件自带的透明外壳里，还是原型。三核 BK7258，两块圆屏当眼睛，摄像头、麦克风、扬声器，加速度计、NFC、马达和 SD NAND。"),
    "第一幕：对话与识物": (40, "第一幕，请她拍自拍再评价。唤醒在本地，“我在”是预置音频；之后上云识别，Agent 调相机拍一张交给多模态模型，再流式播出。中间那张卡：原片等了二十七秒。"),
    "一句话的旅程": (26, "一句话分五步：本地唤醒；录音整段上云识别；固定指令先在本地匹配，命中就不问大模型；回答分块进 PSRAM 队列，由唯一播放器播；播完才再听，所以能追问。底下是九月原片的真实等待，十一到二十七秒，这是初赛后时延工作的起点。"),
    "唤醒模型": (32, "唤醒模型是我们自己训的：正例用多音色合成并校正发音，负例有近音词、半句话和噪声，按来源先切分再增强。训练直接调用设备上那份 C 前端提特征，训练和上板一致。INT8 只有 23 KB，约 80 毫秒一次。之后又做了九轮研究，候选在合成测试上更好，但没有真人多人验证，产品没换。"),
    "第二幕：语音控制设备": (36, "第二幕：振动、换眼睛、调音量、读加速度，都只是一句话。她怎么做到的，下一页拆开。"),
    "Agent 与工具": (26, "大模型只能调用我们注册的八个工具，参数先过 tool_guard 校验，执行完把真实结果回填，失败也照实说。像“音量调小”这种固定指令，在本地就匹配执行，不用问大模型，更快也更省。"),
    "第三幕：记忆": (36, "第三幕。坦白说，这句我问了五次她才回应。初赛后我们把记忆真正做了出来：加密存储，手机可查可删。"),
    "电脑工作台": (24, "第四幕，动一动、采一帧。电脑执行任务，傻妞接收进度和结果。"),
    "三端分工与审批": (44, "手机是 owner：认领、配网、定制升级，一次性给电脑授权，日常对话不需要它。只有你想亲自把关的命令才走审批；拒绝分支还要补测。"),
    "三端安全": (24, "三端不靠同一个局域网互信：未认领设备自动开放有限时的 BLE 认领窗口，手机核对二维码证书指纹并证明一次性秘密；电脑用手机授予的独立密钥，在 USB 上跑 TLS；固件和小功能都要 P-256 签名，升级失败自动回退。"),
    "系统架构": (22, "第五幕，她背后的芯片。CPU0 跑 CP，管无线、存储和升级；CPU1、CPU2 组成 AP，跑 Agent、语音和外设。两套 openvela 用 RPMsg 协作，每个资源只有一个主人。"),
    "技术栈": (24, "用到的技术都钉在 manifest 里：NuttX，openvela 的 Agent、Media、TFLite Micro，加上 mbedTLS 和 MCUboot。对上游的修改都发布成 fork 的固定提交，构建时不打补丁。"),
    "平台攻坚": (24, "修过的坑：BL1 接 MCUboot，634 首次真实 OTA；串口能出不能进，查到三级中断门；SDK 标 320M 实测 160；RPMsg 第 53 轮内存耗尽，改常驻线程后 80 轮无失败。CPU1 心跳和 770 重连未解决。"),
    "三板复用": (20, "同一套适配跑在三块板上，这是 T5-Board 的 Dolphin。"),
    "证据与复现": (24, "每条结论标证据层级：主机近千个合同用例，App 345 个单测加 14 组模拟器场景，实板、真机分开记录。CI 从空目录按 manifest 冷构建、签名、再独立验签；PR #140 合入后的构建和交付复核均通过。"),
    "下一步": (8, "下一步：补齐实测，开放签名小功能，做重连长稳和多人唤醒，再做功耗和外壳。"),
    "尾声": (16, "谢谢各位评委老师，欢迎提问。"),
}
_notes = notes


def notes(slide, title, seconds, script, sources=(), cue="", video=0):
    if title in SPOKEN:
        seconds, spoken = SPOKEN[title]
        cue = (cue + "\n" if cue else "") + "详版讲解（问答或时间富余时用）：" + script
        script = spoken
        # ASCII words and numbers are read as a few syllables, not one per letter.
        estimate = video + len(re.sub(r"[A-Za-z0-9.\-]+", "xx", spoken)) / 4.5
        if estimate > seconds + 1:
            print(f"WARN timing {title}: estimate {estimate:.1f} s > {seconds} s")
    _notes(slide, title, seconds, script, sources, cue, video)


order = []
appendix = []

# ============================================================ 序幕
s = slides[0]
set_text(next(x for x in s.shapes if x.name == "文本框 9"),
         "傻妞：放在桌上的 openvela AI 伴侣", 64, PAPER, True, PP_ALIGN.CENTER)
set_text(next(x for x in s.shapes if x.name == "Text 3"),
         "一个晚上的故事：从“你好，openvela”到“晚安”  ｜  勇往直前 · 李健", 30, "DDE5EF", False,
         PP_ALIGN.CENTER)
add_transition(s)
notes(s, "封面", 8,
      "各位评委老师好，我是勇往直前的李健。今天用一个晚上的故事，讲傻妞：一台放在桌上的 openvela AI 伴侣。")
order.append(s)

s = reset(slides[3], "序幕 ·“你好，openvela。”——“我在。”")
v = live(s, "wake", 1.95, 3.4, 13.6)
g1 = group(s)
box(g1, 16.1, 3.4, 8.55, 7.65)
text(g1, "这一声“我在”，要走完：", 16.5, 3.7, 7.8, 0.75, 29, INK, True)
text(g1, "① 麦克风常开，AP 上的 INT8 模型\n    每 300 ms 判一次唤醒\n② 命中后播放预置应答“我在”\n③ 录音 → 云端识别 → Agent 理解\n④ 流式合成，边收边播\n⑤ 播完才重新收音，等你追问",
     16.5, 4.65, 7.9, 5.2, 23, BODY)
text(g1, "两套 openvela、三颗 CPU 核同时在工作", 16.5, 10.1, 7.9, 0.7, 21, CYAN, True)
footer(s, "今晚的顺序：她是谁 → 和她说话（拆唤醒模型）→ 让她动起来（拆 Agent 与工具）→ 她记得你 → 深夜的协作（拆三端安全）→ 她背后的芯片（技术栈）→ 怎么证明 → 晚安", 21)
add_transition(s)
add_builds(s, [[g1]])
notes(s, "序幕", 13,
      "这是九月十九日用手机实拍的原片：一句“你好，openvela”，她回答“我在”。看起来很简单，背后是本地唤醒、云端理解、流式播放和两套 openvela 同时协作。今天我按一个晚上的顺序，每一幕之后拆开一层技术。",
      [f"实拍原片 {CLIPS['wake']['source']}", "docs/verification/bk7258/2026-09-20-shaniu-638-full-image.md"],
      "点击画面播放（约 4 秒），播完点击出现右侧说明。", video=v)
order.append(s)

# ============================================================ 她是谁
s = reset(slides[5], "她是谁：一块装在透明外壳里的三核开发板")
box(s, 1.95, 3.4, 9.0, 8.6, "F4F1EC", None)
picture(s, MEDIA / "shaniu-device-top.jpg", 2.2, 3.65, w=8.5)
text(s, "实拍：AIDK AI Toy 主板 + 开发套件自带透明亚克力夹板外壳（原型）", 1.95, 12.05, 9.0, 0.6, 18, MUTED, False, PP_ALIGN.CENTER)
rows = [
    ("主控", "BK7258：3 × Cortex-M33，片上 SRAM 640 KiB；CPU0 = CP，CPU1+CPU2 = AP（SMP）"),
    ("双眼", "2 × GC9D01 160×160 圆屏，两路 QSPI；表情资源存 SD NAND"),
    ("看与听", "GC2145 摄像头 640×480 JPEG；麦克风采集 + 扬声器播放，由 Media 统一仲裁"),
    ("感知与触觉", "SC7A20H 加速度计（I2C0）；MFRC522 NFC（UART1）；振动马达 ≤100 ms"),
    ("存储", "8 MiB Flash 分区（BL1/BL2/CP/AP A/B/持久数据）；SD NAND 实测 121 MiB"),
    ("按键与连接", "K1/K3 音量、K2 电源；Wi-Fi + BLE；原生 USB 连电脑，CH340 调试口"),
]
row_groups = []
for i, (k, v_) in enumerate(rows):
    g = group(s)
    y = 3.4 + i * 1.47
    box(g, 11.45, y, 13.2, 1.3)
    text(g, k, 11.75, y + 0.3, 2.6, 0.7, 23, CYAN, True)
    text(g, v_, 14.3, y + 0.27, 10.2, 0.9, 20, BODY)
    row_groups.append(g)
add_transition(s)
add_builds(s, [row_groups[:3], row_groups[3:]])
notes(s, "她是谁", 18,
      "她就是这块官方 AIDK AI Toy 主板，装在开发套件自带的透明外壳里，目前是原型。主控 BK7258 有三个 Cortex-M33 核；两只眼睛是两块圆屏；有摄像头、麦克风和扬声器；还有加速度计、NFC、振动马达、SD NAND 和三颗按键。这些外设分别要在芯片层和板级层接起来，后面会讲。",
      ["docs/contest/技术报告-BK7258三核适配与傻妞AI伴侣.md#344-硬件设计接线与关键-bom", "boards/bk7258/aidk_ai_toy/"],
      f"两次点击，各出现三行。接线细节见{ref('bom')}。")
order.append(s)

# ============================================================ 第一幕：和她说话
s = reset(slides[1], "第一幕 · 叫醒她，让她看看我")
v = live(s, "camera", 1.95, 3.4, 15.0)
steps = side_steps(s, CLIPS["camera"]["beats"])
add_transition(s)
add_builds(s, steps)
notes(s, "第一幕：对话与识物", 40,
      "第一幕：叫醒她，请她拍张自拍再评价一下。这段是原速，等待时间都在里面。右边对应每一步：唤醒在本地，“我在”是预置音频；之后录音交给云端识别，Agent 判断需要看一眼，调用相机工具拍一张 640×480 的 JPEG，和问题一起交给多模态模型，回答以流式语音播出来。",
      [f"实拍原片 {CLIPS['camera']['source']}", "docs/verification/bk7258/2026-09-20-shaniu-639-full-image.md",
       "app/bk7258/bk7258_agent_vision.c"],
      "点击播放；播放结束后逐次点击右侧四步。", video=v)
order.append(s)

s = reset(slides[7], "一句话的旅程：本地唤醒，云端理解，播完再听")
flow = []
stages = [
    ("① 唤醒", "AP 上 microfrontend\n[1,149,40,1] 特征 · 20 ms 帧跳\nINT8 DS-CNN 23,640 B\n约 80 ms/次 · 每 300 ms 判定\n连续 2 次过阈值 · 冷却 1 s", CYAN),
    ("② 收音", "16 kHz 单声道\n端点检测后整段提交\nmimo-v2.5-asr 转文字\n连续块 Base64，少一次拷贝", BLUE),
    ("③ 理解", "官方 Agent Session\n固定指令先走本地意图\n（命中则不再请求大模型）\nmimo-v2.5 + 8 个受控工具", PURPLE),
    ("④ 播放", "mimo-v2.5-tts SSE PCM16\n256 KiB PSRAM 有界队列\n满队列等待 ~15 s → P50 1.0 s\n唯一 Media 播放器输出", CYAN),
    ("⑤ 再听", "播放真正排空后再收音\n免唤醒追问\n静默超时回待机\n失败如实返回并释放资源", BLUE),
]
for i, (t, body, accent) in enumerate(stages):
    g = group(s)
    x = 1.95 + i * 4.6
    box(g, x, 3.4, 4.3, 6.6)
    box(g, x, 3.4, 4.3, 0.09, accent, None, False)
    text(g, t, x + 0.3, 3.65, 3.8, 0.8, 30, accent, True)
    text(g, body, x + 0.3, 4.65, 3.85, 5.2, 20)
    flow.append([g])
tts = group(s)
box(tts, 1.95, 10.3, 22.7, 2.1, "FFF7ED", "F5D0A9")
text(tts, "9/19 原片里的真实等待（问完 → 开口，手机录像计时）：自拍识物 27.2 s · 振动 11.7 s · 眼睛 24.7 s · 音量 20.7 s · 加速度 13.9 s。\n初赛后：分阶段埋点定位等待 → 无工具问答直接复用正文 → 流式分句合成 + 256 KiB 队列；同板对照 TTS（MiMo 首段 PCM 1.23–1.34 s，腾讯云 1.61–1.68 s 且断供 3.9–7.6 s）→ 保留 MiMo。当前版本的真实云端时延尚未复测。",
     2.35, 10.45, 22.0, 1.9, 19, BODY)
add_transition(s)
add_builds(s, flow + [[tts]])
notes(s, "一句话的旅程", 26,
      "把刚才那一句拆开：第一步唤醒在 AP 上用 INT8 模型本地完成，每 300 毫秒判一次，连续两次才算；第二步录音整段交给云端识别；第三步进入官方 Agent，会先匹配音量、表情、停止这类固定指令，命中就不请求大模型；第四步合成的 PCM 分块进入 PSRAM 里的有界队列，由唯一的播放器输出；第五步真正播完才重新收音，所以能免唤醒追问。最下面是九月原片里的真实等待：问完到开口要 11 到 27 秒，这是初赛后我们做时延工作的起点——先分阶段埋点，发现大部分时间花在等网络和模型，再做流式分句和队列；TTS 供应商在同一块板上对照后保留 MiMo。当前版本的真实时延还要在补齐配置后复测。",
      ["app/bk7258/models/nihao_openvela.metadata.json", "docs/contest/技术报告-BK7258三核适配与傻妞AI伴侣.md#333-mimo-接入会话与资源调度",
       "docs/verification/bk7258/2026-10-29-voice-latency-r7/voice-latency-r7.md（本地研究记录）"],
      f"六次点击：五个阶段依次出现，最后出现真实等待与优化。TTS 对照表见{ref('tts')}。")
order.append(s)

# ------------------------------------------------------------ 技术细节 ① 唤醒模型
s = new_slide("技术细节 ① 唤醒模型：从数据到芯片上的 23 KB")
cols = [
    ("① 数据 406 条", CYAN,
     "正例 62：Kokoro v1.1 多音色合成（Apache-2.0）\n"
     "发现合成读成 /vɛlə/：按分词器实际 token 反解，改写为 open vila → /ˈviːlə/ 后全部重生成\n"
     "unknown 244：近音词、半句、普通指令、公开语音、噪声\n"
     "silence 100：含 72 条真实环境录音\n"
     "先按 source_id 切 train / val / test，再增强"),
    ("② 增强与难负例", BLUE,
     "完整目标滑窗 476\n半句难负例 120\n语音拼接难负例 166\n普通语音平移 5,312\n"
     "电平 ×1 / 0.25 / 0.1\n房间：三次反射 18–180 ms + 带限 + 噪声（合成，非实测 RIR）\n"
     "按来源加权；验证、测试集不增强"),
    ("③ 特征 = 设备代码", PURPLE,
     "训练时在主机编译设备上的 C 前端（TFLM microfrontend + kissfft），用 ctypes 调用\n"
     "16 kHz → 30 ms 窗 / 20 ms 帧跳 → 40 维滤波器组 + 降噪 + PCAN + log\n"
     "3 s 上下文 = [1,149,40,1]\n41 个前端源文件哈希写入元数据"),
    ("④ 模型与训练", CYAN,
     "DS-CNN 32 通道：\nConv 10×4（频率步长 4）\n→ 4 个深度可分离块\n→ 1×1 卷积 · BN · ReLU\n→ 全局池化 → Dense 3\n"
     "TensorFlow 2.15.1 · Adam\nbatch 32 · 24 epoch · 固定种子\n"
     "频率步长 4：理论 MAC\n4.85 M → 2.43 M / 次"),
    ("⑤ 量化与打包", BLUE,
     "全 INT8：输入、权重、输出\n6 种算子：CONV_2D · DEPTHWISE_CONV_2D · AVERAGE_POOL_2D · FULLY_CONNECTED · RESHAPE · SOFTMAX\n"
     "模型 23,640 B\nWKM1/2 包：136/140 B 头 + SHA-256 + 标签 + 短语 + 前端版本\nApp 可导入，以设备回读为准"),
    ("⑥ 上板运行", PURPLE,
     "AP 上 TFLite Micro\narena 实用 40,692 B\n约 80 ms / 次推理\n每 300 ms（15 帧）判一次\n连续 2 次过阈值才触发\n释放 0.20 · 冷却 1 s\n"
     "阈值：元数据 0.85、源码默认 0.60、App 50–90%\n配置期 + 运行期双重校验模型 SHA"),
]
cg = []
for i, (title, accent, body) in enumerate(cols):
    g = group(s)
    x = 1.95 + i * 3.82
    box(g, x, 3.4, 3.6, 6.5)
    box(g, x, 3.4, 3.6, 0.09, accent, None, False)
    text(g, title, x + 0.25, 3.6, 3.2, 0.7, 22, accent, True)
    text(g, body, x + 0.25, 4.4, 3.15, 5.4, 17.5)
    cg.append(g)
band = group(s)
box(band, 1.95, 10.1, 22.7, 2.4, "FFF7ED", "F5D0A9")
text(band, f"初赛后又做了 9 轮训练研究（本地记录，逐轮见{ref('kws-research')}）", 2.3, 10.2, 22.0, 0.6, 20, AMBER, True)
text(band, "数据：公开真人语料去重池 224 h / 17.2 万条（MobvoiHotwords、AISHELL、LibriSpeech、FLEURS 等），近重复指纹筛查，按来源与说话人隔离。"
           "方法：流式 TCN + PCAN-v2、真人语音预训练 + 分阶段解冻、困难负例挖掘、时间损失、两级验证器、Zipformer / Wav2Vec2 教师审计发音；Torch CUDA。\n"
           "结果：合成留出命中 127 → 318/400；20.99 h 真人负流 0 次误唤醒（曾用于选型）；但快语速、近音词有退步，且没有真人多人正例 → 未上板替换。",
     2.3, 10.8, 22.1, 1.7, 17)
footer(s, "产品模型 v46 · SHA-256 922eba91… · 元数据状态 candidate；目标正例全部为合成语音，没有真人多人唤醒率，所以不报唤醒率。", 19)
add_transition(s)
add_builds(s, [cg[:3], cg[3:], [band]])
notes(s, "唤醒模型", 32,
      "唤醒模型是团队用自己的工具链训练的。数据 406 条：正例 62 条用 Kokoro 多音色合成，我们发现合成引擎把 vela 读成了 vɛlə，于是用分词器实际输出的 token 反解，改写成 open vila 后全部重新生成；负例包括近音词、半句、普通指令、公开语音和噪声，还有 72 条真实环境静音。先按来源切分训练、验证和测试，再做滑窗、难负例、电平和合成房间增强。关键一点：训练时在主机上编译设备的那份 C 前端代码来提特征，所以训练和上板看到的是同一种特征。模型是 32 通道 DS-CNN，频率步长 4 把理论运算量从 4.85M 降到 2.43M MAC；全 INT8 只有 23,640 字节，在 AP 上约 80 毫秒一次，arena 用了 40,692 字节。初赛后又做了九轮研究：两百多小时公开真人语料、真人预训练、困难负例、教师模型审计发音，合成测试命中从 127 提到 318/400，20.99 小时真人负流里 0 次误唤醒；但快语速、近音词有退步，也没有真人多人正例，所以没有替换产品模型，也不报唤醒率。",
      ["app/bk7258/models/nihao_openvela.metadata.json", "tools/bk7258/_lib/voice_kws.py（audit / train / evaluate / package）",
       "app/bk7258/bk7258_voice_wake_package.h（WKM1/WKM2）",
       "docs/contest/技术报告-BK7258三核适配与傻妞AI伴侣.md#331-端侧-ai以公开-32-通道模型为基线",
       "docs/platforms/bk7258/shaniu-master-plan.md（v44–v46 训练记录）",
       "docs/verification/bk7258/2026-10-26-wakeword/（本地研究记录，未发布）"],
      "三次点击：数据/增强/特征 → 模型/量化/上板 → 9 轮研究。")
order.append(s)

# ============================================================ 第二幕：让她动起来
s = reset(slides[9], "第二幕 · 让她动起来：振动、眼睛、音量、读传感器")
v = live(s, "control", 1.95, 3.4, 15.0)
steps = side_steps(s, CLIPS["control"]["beats"])
add_transition(s)
add_builds(s, steps)
notes(s, "第二幕：语音控制设备", 36,
      "第二幕：让她动起来。我让她振一下马达、把眼睛调成困倦、把音量调小、读一下加速度。这些不是让大模型直接碰硬件：模型只能调用我们注册的设备工具，参数由 tool_guard 校验，比如振动只能 1 到 100 毫秒、音量 0 到 100；执行失败会如实返回，不会说“已经完成”。",
      [f"实拍原片 {CLIPS['control']['source']}", "app/bk7258/bk7258_agent_device_control.inc"],
      "点击播放；播放结束后逐次点击右侧说明。", video=v)
order.append(s)

# ------------------------------------------------------------ 技术细节 ② Agent 与工具
s = new_slide("技术细节 ② 大模型怎么安全地“动手”：Agent、工具与本地意图")
tools = table(s, [
    ("工具", "能做什么", "约束"),
    ("device_status", "读音量、人格、电池电压与充电状态", "不估算电量百分比"),
    ("device_motion", "读一次新鲜的三轴加速度（含重力）", "只有加速度计，没有陀螺仪"),
    ("device_control", "音量 0–100；5 种人格；振动 1–100 ms；9 种眼睛表情", "仅在用户要求时；每次重复都要新调用"),
    ("camera_capture", "拍一帧 640×480 JPEG，连同问题交给多模态模型", "唯一相机所有者；单帧，不是视频"),
    ("focus_timer", "专注计时：开始 / 暂停 / 继续 / 取消 / 状态", "“已受理”不等于已开始"),
    ("reply_expression", "本轮回答的情绪：neutral / happy / confused / thinking / sorry", "只影响播报期间的临时表情"),
    ("device_app", "列出、启动、停止、定时原生小功能", "仅启用小功能的配置"),
    ("read_file", "读取 /data/agent 下的 Skill 说明", "限定目录、只读"),
], (3.4, 6.1, 4.1), size=17, row_h=0.98)
flowg = group(s)
box(flowg, 15.85, 3.4, 8.8, 8.95)
text(flowg, "一轮对话怎么走", 16.2, 3.6, 8.1, 0.7, 24, PURPLE, True)
text(flowg,
     "① ASR 文本先过本地意图：只接受完整、明确的“音量 / 换表情 / 停止”，命中直接执行，不请求大模型\n\n"
     "② 未命中 → 官方 Agent Session（agent_loop）调用 mimo-v2.5，带上 8 个工具的 JSON Schema 与最近 3 轮加密记忆\n\n"
     "③ 模型发起工具调用 → tool_guard 校验参数与上限 → 执行 → 回填真实结果；忙或失败如实返回，OTA 中返回 EBUSY\n\n"
     "④ 回答流式合成；reply_expression 只决定播报时的临时表情\n\n"
     "⑤ Runtime Skill（专注助手 / 设备体检 / 识物解说）每轮锁定可用工具；外部 Skill 需 P-256 签名，装后默认停用",
     16.2, 4.4, 8.2, 7.9, 18.5)
footer(s, f"模型拿不到密钥、Shell 或裸 Flash；“受理”不等于“完成”，眼睛以请求 ID 回读为准。初赛后新增的拿起换表情、任务完成轻振见{ref('motion')}。", 19)
add_transition(s)
add_builds(s, [[tools], [flowg]])
notes(s, "Agent 与工具", 26,
      "刚才那一段是怎么做到的：大模型不直接碰硬件，只能调用我们注册的 8 个工具，每个工具都有 JSON Schema 和上限，比如振动 1 到 100 毫秒、音量 0 到 100。一轮对话里，ASR 文本先过本地意图：只接受完整、明确的音量、换表情、停止这类指令，命中就直接执行，不请求大模型；没命中才进官方 Agent Session，调用 mimo-v2.5。模型发起工具调用后先过 tool_guard 校验，执行完把真实结果回填，忙或失败如实返回。回答时 reply_expression 只决定播报期间的临时表情。Runtime Skill 每轮锁定能用的工具，外部 Skill 需要 P-256 签名。",
      ["app/bk7258/bk7258_agent_product.c（product_tools / product_tool_execute）", "app/bk7258/bk7258_local_intent.c",
       "app/bk7258/bk7258_runtime_skills.c", "docs/contest/技术报告-BK7258三核适配与傻妞AI伴侣.md#333-mimo-接入会话与资源调度"],
      "两次点击：工具表 → 一轮对话流程。")
order.append(s)

# ============================================================ 第三幕：她记得你
s = reset(slides[12], "第三幕 · 她记得你：从“我会记住”到真的记住")
v = live(s, "memory", 1.95, 3.4, 15.0)
mem = group(s)
box(mem, 17.5, 3.4, 7.15, 3.6, "FFF7ED", "F5D0A9")
text(mem, "视频里她说“我会写进记忆”", 17.85, 3.6, 6.5, 0.7, 22, AMBER, True)
text(mem, "这是模型自己说的；当时的记忆只是最近 3 轮对话的加密快照。原片里“我可以叫你冰冰吗”问了 5 次（238–325 s）才得到回答。", 17.85, 4.4, 6.5, 2.5, 18)
fact = group(s)
box(fact, 17.5, 7.25, 7.15, 5.0, "E9F8F1", None)
text(fact, "11 月：把它做成真的", 17.85, 7.45, 6.5, 0.7, 22, GREEN, True)
text(fact, "称呼 / 偏好 / 近期话题各 1 条，≤96 B\nmbedTLS AES-256-GCM 加密，绑定控制身份\nApp 可查看、纠正、删除；纠正时清空近期对话\n11-01 真机 + 实板：增、改、删、重启回读一致",
     17.85, 8.2, 6.6, 3.9, 18)
add_transition(s)
add_builds(s, [[mem], [fact]])
notes(s, "第三幕：记忆", 36,
      "第三幕：我问她可不可以叫她冰冰，她说能记住。但要诚实地讲：原片里这句话我问了五次才得到回答；而且“我会写进记忆”是模型自己说的，当时的记忆只是最近三轮对话的加密快照。所以初赛后我们把它真正做出来：称呼、偏好、近期话题各存一条，用 AES-256-GCM 加密保存，手机上可以查看、纠正、删除；十一月一日在真机和实板上验证了增删改和重启回读。",
      [f"实拍原片 {CLIPS['memory']['source']}", "docs/verification/bk7258/2026-11-01-agent-profile-skills-hil.md",
       "app/bk7258/bk7258_memory_facts.c", "app/bk7258/bk7258_agent_memory_codec.c"],
      "点击播放；之后两次点击：右上“模型口述”→ 右下“11 月实现”。", video=v)
order.append(s)

# ============================================================ 第四幕：深夜的协作
s = reset(slides[2], "第四幕 · 深夜十一点：电脑做电脑的事，把结果交给她")
v = live(s, "workbench", 1.95, 3.4, 14.6, label_y=11.7)
cg = []
for i, (t, b, a) in enumerate([
    ("动作联动", "拿起时眼睛变化；采样持续更新已有实板记录，动作与表情的关联仍待人工确认", CYAN),
    ("按需取景", "浏览器请求一帧，显示后再决定保存、取消或重拍；11-03 有两帧传输与哈希记录", BLUE),
    ("工作台边界", "电脑只用 owner 授予的 tasks / camera / resources / scenes 权限，经原生 USB + TLS", PURPLE),
]):
    g = group(s)
    y = 3.4 + i * 2.05
    box(g, 16.85, y, 7.8, 1.85)
    text(g, t, 17.15, y + 0.14, 7.2, 0.55, 21, a, True)
    text(g, b, 17.15, y + 0.73, 7.2, 1.0, 16)
    cg.append([g])
scope = group(s)
box(scope, 16.85, 9.55, 7.8, 2.75, "FFF7ED", "F5D0A9")
text(scope, "证据边界", 17.15, 9.7, 7.1, 0.55, 21, AMBER, True)
text(scope, "左侧是作者交付的粗剪录屏：中段连续截取，未额外加速或拼接；完整固件与 App 版本未记录。眼睛包导入只有口述，未作为安装成功证据。", 17.15, 10.35, 7.2, 1.75, 15.5)
add_transition(s)
add_builds(s, cg + [[scope]])
notes(s, "电脑工作台", 24,
      "第四幕，动一动、采一帧。左边是作者交付的工作台粗剪：拿起时眼睛变化，浏览器请求一帧并显示结果。电脑执行任务，傻妞接收进度和结果；电脑只使用手机 owner 授予的权限，经原生 USB 和 TLS 通信。视频未记录完整固件和 App 版本，因此不把它当作当前源码的整机验收；眼睛包导入只在口述中出现，不作为安装成功证据。",
      ["作者工作台录屏 傻妞电脑工作台.mp4（本地，11-04 接收；粗剪）",
       "docs/verification/bk7258/2026-11-03-feature-validation/README.md",
       "docs/verification/bk7258/2026-11-03-feature-validation/evidence/pc-task-success.json",
       "docs/verification/bk7258/2026-11-03-feature-validation/evidence/camera-events.json",
       "tools/bk7258/README.md#run-one-real-computer-task"],
      f"点击播放（约 16 秒，视频有作者口述，演讲者不说话）；之后四次点击：动作 → 取景 → 边界。协议与运行细节见{ref('workbench-detail')}。", video=v)
order.append(s)

s = reset(slides[4], "手机只在需要你决定时出场：一次真实的电脑审批")
v = live(s, "pc-approve", 1.95, 3.4, 14.6, label_y=11.7)
cg = []
for i, (t, b_, a_) in enumerate([
    ("傻妞（设备）", "自己唤醒、联网对话；表情、动作、专注、NFC；显示电脑任务结果", CYAN),
    ("手机 App（owner）", "离线扫码认领；配网与云服务、定制、升级；一次性给电脑授权", BLUE),
    ("电脑工作台", "跑真实任务、按需取景；只用被授予的权限；能请求审批，不能替你决定", PURPLE),
]):
    g = group(s)
    y = 3.4 + i * 1.75
    box(g, 16.85, y, 7.8, 1.6)
    text(g, t, 17.15, y + 0.1, 7.2, 0.55, 20, a_, True)
    text(g, b_, 17.15, y + 0.66, 7.25, 0.9, 16)
    cg.append([g])
seq = group(s)
box(seq, 16.85, 8.7, 7.8, 3.6, "FFF7ED", "F5D0A9")
text(seq, "可选的单次审批（左侧实录）", 17.15, 8.82, 7.2, 0.6, 21, AMBER, True)
text(seq, "电脑提交逐字命令、随机 ID、时限 → 经原生 USB 送到傻妞，亮“!”并轻振 → 只有已认证的手机 owner 能批准或拒绝 → 设备用该电脑密钥 HMAC 签回执，批准才执行\n"
          "证据：11-03 实板 + 真机，批准后执行（approved · authenticated=true · 返回码 0）；无人批准时过期不执行；拒绝待实测。语音不能发起电脑操作",
     17.15, 9.45, 7.3, 2.8, 15.5)
add_transition(s)
add_builds(s, cg + [[seq]])
notes(s, "三端分工与审批", 36,
      "这段是十一月三日的实录：电脑上用 workbench approval-run 请求打开计算器，请求经原生 USB 送到傻妞，傻妞通知手机，手机上核对逐字命令后批准，电脑拿到签名回执才执行，计算器打开；终端里能读到 decision=approved、authenticated=true、返回码 0，上面几条历史记录是无人批准时的 expired。手机在这里是 owner：开箱认领、配网和云服务、定制升级，并一次性给电脑授权；之后的日常对话不需要它。单次审批是可选的，只给你想亲自把关的命令，比如 AI 助手要推代码。放在手机上，是因为手机持有 owner 凭据，也能完整显示命令。拒绝分支还要补测。",
      ["11-03 作者录屏 电脑授权.mp4（本地，未发布；剪映导出，带口述字幕）", "tools/bk7258/README.md#phone-owner-approval-for-pc-agents",
       "app/bk7258/bk7258_pc_approval.c", "docs/verification/bk7258/2026-11-03-feature-validation/evidence/approval-expired.json"],
      f"点击播放（约 29 秒，视频里是作者口述，演讲者不说话）；之后四次点击：三端 → 审批。审批设计见{ref('approval')}；手机认领实拍见{ref('app')}。", video=v)
order.append(s)

# ------------------------------------------------------------ 技术细节 ③ 三端安全
s = new_slide("技术细节 ③ 三端怎么互相信任：通道、密钥与签名")
sec = table(s, [
    ("通道", "协议", "身份与密钥", "防什么 · 边界"),
    ("手机 ↔ 傻妞\n认领、配网、定制",
     "BLE GATT 只承载 TLS 1.2 密文：ECDHE-ECDSA + AES-128-GCM（板端 mbedTLS 3.4.0，手机 SSLEngine）；按 ATT MTU−3 分片、有界队列",
     "未认领且空闲时自动开放有限时认领窗口；开箱二维码：设备证书 SHA-256 指纹 + 256 位一次性 possession secret；手机凭据存 Android Keystore",
     "冒充设备、远程抢认领；secret 常量时间比较；配置事务原子提交，回读才算成功"),
    ("电脑 ↔ 傻妞\n工作台",
     "原生 USB CDC 上的 TLS + SDC1 帧协议；资源 4 KiB 块、32 B 帧追加，断点续传",
     "独立电脑密钥，由手机 owner 按 tasks / camera / resources / scenes 授予；Windows DPAPI 保存",
     "CH340 调试口不承载产品操作；单次审批回执用该电脑密钥 HMAC，绑定命令、ID、时限"),
    ("傻妞 ↔ 云",
     "HTTPS + Bearer；ASR 整段提交，TTS 为 SSE PCM16 流；同一配置复用 TLS 会话",
     "API Key 经认证 BLE 下发并保存在设备；评委自备账号与额度",
     "证书、主机名、有效期校验不因性能关闭；断网不伪造成功"),
    ("固件与资产",
     "自研 BL1 → MCUboot BL2 验签（官方 imgtool，ECDSA P-256 / SHA-256）→ CP/AP 同槽配对 A/B，试运行确认、失败回退",
     "小功能与 Skill：ECDSA P-256 签名；眼睛包 .bkep：CRC32 + 来源 SHA-256；唤醒模型 WKM：SHA-256",
     "篡改与降级；Flash 的 32 B + 2 B CRC 只保证传输完整，不替代签名"),
], (3.3, 7.0, 6.9, 5.5), size=18, row_h=1.9)
footer(s, "证据：BLE TLS 与认领有主机双端测试和真机认领（637/641）；USB TLS、审批批准后执行与过期不执行有 11-03 实板记录；拒绝待实测。私钥、设备身份与云凭据都不在仓库里。", 19)
add_transition(s)
add_builds(s, [[sec]])
notes(s, "三端安全", 24,
      "三端之间不靠“在同一个局域网”互信。未认领且空闲的设备会自动开放有限时的 BLE 认领窗口；手机先核对二维码里的设备证书指纹，再在 TLS 加密通道里证明持有一次性秘密。普通按键是 PTT，旧的八秒按住/松开仅用于只读回执恢复，并不打开首认领。电脑用手机授予的独立密钥，在原生 USB 上跑 TLS，只拿到被授予的权限，审批回执用这把密钥做 HMAC。云端走 HTTPS，证书校验不因性能关闭。固件由 MCUboot 用 ECDSA P-256 验签，CP 和 AP 成对试运行，失败自动回退；小功能和 Skill 也要签名。",
      ["docs/platforms/bk7258/shaniu-provision-security.md", "tools/bk7258/README.md#pc-workbench-control-client-development",
       "tools/bk7258/_lib/trust.py（ecdsa-p256-sha256, official imgtool）", "app/bk7258/bk7258_display_pack.h"],
      "一次点击出现表格。")
order.append(s)

# ============================================================ 第五幕：她背后的芯片
s = new_slide("第五幕 · 她背后：一颗 BK7258，两套 openvela")
cp = group(s)
box(cp, 1.95, 3.4, 9.0, 5.6, "E9F9FA", "9ED9E3")
text(cp, "CP · CPU0 · openvela", 2.35, 3.7, 8.2, 0.8, 32, CYAN, True)
text(cp, "Wi-Fi / BLE 控制器\nFlash · LittleFS · 签名 OTA\n健康监督 · AP 日志汇聚\n原生小功能加载（限定 CPU0）", 2.35, 4.75, 8.2, 3.9, 24)
apg = group(s)
box(apg, 15.65, 3.4, 9.0, 5.6, "EDF1FF", "B8C6FF")
text(apg, "AP · CPU1 + CPU2 SMP · openvela", 16.05, 3.7, 8.4, 0.8, 30, BLUE, True)
text(apg, "Agent · Session · Voice · Media\nTFLite Micro 唤醒 · 本地意图\n双屏 · 相机 · 动作 · NFC · 振动\n电脑工作台 · 原生小功能", 16.05, 4.75, 8.4, 3.9, 24)
link = group(s)
arrow(link, 11.2, 5.55, 4.2, 1.0, CYAN, MSO_SHAPE.LEFT_RIGHT_ARROW)
text(link, "RPTUN / RPMsg", 11.0, 4.5, 4.6, 0.7, 26, CYAN, True, PP_ALIGN.CENTER)
text(link, "MBOX0 通知 + 共享内存\nRPMsgFS 访问 CP 存储\n每个资源只有一个所有者", 11.0, 6.75, 4.6, 1.6, 19, MUTED, False, PP_ALIGN.CENTER)
ext = group(s)
for i, (name, how, accent) in enumerate([
    ("手机", "认证 BLE（TLS 1.2，证书 pin）· 局域网 HTTPS", BLUE),
    ("电脑", "原生 USB CDC（TLS，独立电脑密钥）", PURPLE),
    ("云服务", "Wi-Fi + TLS：ASR · LLM · TTS · 视觉", CYAN),
]):
    x = 1.95 + i * 7.75
    box(ext, x, 9.35, 7.2, 1.45)
    text(ext, name, x + 0.35, 9.55, 1.8, 0.7, 27, accent, True)
    text(ext, how, x + 2.0, 9.62, 5.05, 1.1, 18, BODY)
boot = group(s)
box(boot, 1.95, 11.1, 22.7, 1.2, "F5F7FA", None)
text(boot, "自研 BL1  →  MCUboot BL2 验签  →  同槽 CP/AP 镜像（A/B，试运行确认与回退）", 2.35, 11.33, 22.0, 0.75, 25, INK, True)
add_transition(s)
add_builds(s, [[cp], [apg], [link], [ext], [boot]])
notes(s, "系统架构", 22,
      "第五幕，她背后的芯片。BK7258 三个核：CPU0 运行 CP 实例，管无线、存储、升级和系统监督；CPU1 和 CPU2 组成 AP 的 SMP 域，运行 Agent、语音、媒体、唤醒、屏幕和外设。两套 openvela 通过 RPTUN 和 RPMsg 协作，每个资源只有一个所有者。外面三条通道连手机、电脑和云；最底下是我们自己的签名启动链。",
      ["chips/bk7258/", "boards/bk7258/CONFIGS.md", "docs/contest/技术报告-BK7258三核适配与傻妞AI伴侣.md#321-总体架构与数据流"],
      "五次点击：CP → AP → RPMsg → 外部通道 → 启动链。")
order.append(s)

# ------------------------------------------------------------ 技术栈
s = new_slide("用了什么技术：每一层都钉在 manifest 的固定提交上")
stack = table(s, [
    ("层", "用到的技术（manifest 固定提交）", "团队做的"),
    ("内核", "NuttX（团队 fork 892be7ba）· SMP · RPTUN / OpenAMP / RPMsg · VFS · LittleFS · RPMsgFS",
     "BK7258 芯片层：三核启动、IRQ、时钟、共享内存；Flash 与外设驱动"),
    ("AI 与语音", "openvela Agent（fork 76c65bc6，基线官方 e65550f）· Voice · TFLite Micro + microfrontend · speexdsp 1.2.1",
     "8 个设备工具、本地意图、流式 TTS 接入、记忆投影；唤醒训练工具链"),
    ("多媒体与显示", "Media（fork eee16420，录音正常停止）· audio / V4L2 · LVGL（Dolphin 界面）",
     "板级音频与相机、双 QSPI 圆屏眼睛服务、播放排空契约"),
    ("网络与安全", "mbedTLS 3.4.0 · webclient（fork d529ec81，keep-alive）· cJSON",
     "BLE 上的 TLS 服务端、USB CDC 上的 TLS、HTTPS 云客户端"),
    ("启动与升级", "官方 MCUboot（fefc398c）+ imgtool",
     "源码化 BL1、Flash 编码与打包器、CP/AP 配对 OTA、签名发布"),
    ("厂商 SDK", "Beken bk_avdk_smp v3.1.1.9（fork 修 SDIO 完成掩码）：只用无线与控制器组件",
     "适配层接合；设备不再运行另一套 FreeRTOS 应用"),
    ("云服务", "MiMo：mimo-v2.5-asr · mimo-v2.5 / pro · mimo-v2.5-tts（SSE PCM16）· 多模态",
     "接口封装、256 KiB PSRAM 流式队列、TTS 同板对照"),
    ("手机 · 电脑", "Kotlin 2.0 · Material Components 1.12 · Android 10–15 · BLE GATT · Keystore；Python 3 · pyserial 3.5 · DPAPI",
     "App 0.7.27：认领、配网、定制、升级、电脑授权；bk7258.py 工作台与浏览器界面"),
    ("工具链与构建", "Arm GNU 10.3-2021.10（SHA-256 锁定）· CMake · repo manifest · GitHub Actions",
     "一个 CLI：工具链 → SDK → 签名固件 → 工厂包验签；冷构建 CI"),
], (3.0, 11.2, 8.5), size=17, row_h=0.9)
footer(s, "对上游的修改都发布为 fork 的固定提交，构建时不打补丁；Agent 扩展尚未合入上游。云端模型 ID 由 App 配置，评委需自备账号。", 19)
add_transition(s)
add_builds(s, [[stack]])
notes(s, "技术栈", 24,
      "这一页回答“用了什么技术”。内核是 NuttX，跨核用 RPTUN、OpenAMP 和 RPMsg，存储是 LittleFS 和 RPMsgFS；AI 和语音用 openvela 官方的 Agent、Voice 和 TFLite Micro；多媒体用 Media；安全用 mbedTLS 3.4；启动用官方 MCUboot；厂商 SDK 只用无线和控制器部分。我们对 NuttX、apps、Agent 和 Media 的修改都发布成 fork 的固定提交，manifest 钉住每一个依赖，构建时不打补丁。云端是 MiMo 的 ASR、LLM 和 TTS。手机 App 是 Kotlin，电脑是 Python 命令行加浏览器界面，工具链是锁定哈希的 Arm GCC 10.3。",
      ["contest2026_135_yongwangzhiqian.xml", "openvela.xml", "tools/bk7258/toolchain.json",
       "android/shaniu-companion/app/build.gradle.kts", "SOURCE_PROVENANCE.md"],
      "一次点击出现表格。")
order.append(s)

s = new_slide("从裸板到能升级的产品：我们修过的坑")
cards = [
    ("启动与升级", "源码化 BL1 与打包器（32 B 数据 + 2 B CRC 的 Flash 编码）\n接入 MCUboot BL2：CP/AP 同槽配对、非活动槽写入、试运行确认与回退\n634：首次真实 App OTA，3,416,935 B，重启后 pair=confirmed", CYAN),
    ("串口与时钟", "UART 能出不能进：RX 位域、使能、三级中断门、FIFO 阈值逐层修复 → 交互式 NSH\nSDK 标“OPP 320M”，CPU0 实为 160 MHz → 按实测频率\n240 MHz CoreMark 561.58（T5-Board 性能配置，10 次）", BLUE),
    ("跨核资源", "RPMsg 临时线程第 53 轮 ENOMEM → 常驻 worker，80 轮无失败、堆首尾一致\n看门狗双键写交错、IRQ 中不能拿锁 → 任务侧串行，移到 LPWORK\nCPU1 文件/RPMsg 锁计数异常 → 加载生命周期限定 CPU0，两路径各 100 轮", BLUE),
    ("外设与存储", "双 QSPI 圆屏、GC2145 JPEG、SDIO SD NAND、I2S/AUD、SARADC、MFRC522\n639 显示卷 EIO → 641 修复，两套眼睛包跨复位保持\n未闭合：755/756/770 CPU1 心跳停止、770 升级后重连失败", PURPLE),
]
cg = []
for i, (t, b, a) in enumerate(cards):
    g = group(s)
    card(g, 1.95 + (i % 2) * 11.6, 3.4 + (i // 2) * 4.55, 11.1, 4.25, t, b, a, body_size=21)
    cg.append([g])
add_transition(s)
add_builds(s, cg)
notes(s, "平台攻坚", 24,
      "这一页是我们修过的坑。启动链：源码化 BL1，接入 MCUboot，CP 和 AP 成对升级、确认和回退，634 完成了第一次真实 App OTA。串口能输出不能输入，我们逐层查到位域、使能、中断门和 FIFO。SDK 标的 320M 在 CPU0 上其实只有 160 MHz。RPMsg 临时线程在第 53 轮内存耗尽，改成常驻 worker 后 80 轮无失败。还没解决的，比如 CPU1 心跳停止和 770 升级后重连失败，我们也写在这里。",
      ["docs/contest/技术报告-BK7258三核适配与傻妞AI伴侣.md#345-驱动适配由真实故障形成的工程解法",
       "docs/verification/bk7258/2026-08-27-bk7258-sdk-clock-240m-validation.md", "boards/bk7258/CONFIGS.md",
       "docs/verification/bk7258/2026-10-18-stability-and-phone-hil.md"],
      f"四张卡逐次出现。失败记录见{ref('failures')}。")
order.append(s)

s = reset(slides[8], "一套芯片，三块板：同一份适配，不同的样子")
v = live(s, "dolphin", 1.95, 3.4, 14.0)
bg = group(s)
for i, (name, role, accent) in enumerate([
    ("T5AI-Core", "最小系统与平台基线：启动、NSH、PSRAM、多核", CYAN),
    ("T5-Board", "Dolphin 图形工具（LVGL）：TF 文件、Wi-Fi 扫描与信道、录音存 WAV（0.1.0+13 用户确认）", BLUE),
    ("AIDK AI Toy", "傻妞：语音、双眼、相机、动作、NFC、电脑协作", PURPLE),
]):
    y = 3.4 + i * 2.5
    box(bg, 16.5, y, 8.15, 2.25)
    text(bg, name, 16.85, y + 0.25, 7.5, 0.7, 26, accent, True)
    text(bg, role, 16.85, y + 1.0, 7.5, 1.2, 18)
text(s, "芯片机制在 chips/，接线与分区在 boards/；新增板型只加板级描述（openvela.conf + CP/AP 配置），构建工具没有板名分支。三块板不能互刷固件。", 1.95, 12.15, 22.7, 0.8, 19, MUTED)
add_transition(s)
add_builds(s, [[bg]])
notes(s, "三板复用", 20,
      "同一套芯片适配复用到三块板：T5AI-Core 是最小系统基线，T5-Board 上跑 Dolphin 图形工具，这段实拍里能浏览 TF 卡、扫描 Wi-Fi、看信道；AIDK 就是傻妞。芯片机制和板级接线分开，新增板型只需要加板级描述。",
      [f"实拍原片 {CLIPS['dolphin']['source']}", "boards/bk7258/CONFIGS.md", "docs/platforms/bk7258/dolphin-master-plan.md"],
      f"点击播放；播放后点击出现右侧三板说明。三板同框照片在{ref('boards')}。", video=v)
order.append(s)

# ============================================================ 怎么证明
s = reset(slides[11], "我们怎么知道它真的能用：测试分层、冷构建与证据")
ladder = group(s)
levels = [("实现", "每项能力都能指到源文件；layers.py 静态所有权门禁 + 内核兼容检查"),
          ("主机测试", "CMocka + mock 隔离 MMIO / SDK / NuttX，带 sanitizer；pytest 996 个产品合同用例 · 104 个头文件 × CP/AP × C11/C++17"),
          ("模拟器", "Android JUnit 345 个不同用例；仪器化 14 组场景（含 11 个配网异常）"),
          ("实板", "11-03：电脑任务成功/超时、取景保存/重拍、审批批准/过期；634 首次 OTA"),
          ("真机手机", "11-03：审批批准（作者录屏）；11-01：记忆、Skill；637/641 认领"),
          ("人工确认", "振感、表情观感、语音体验——还要补")]
for i, (name, ev) in enumerate(levels):
    y = 3.4 + i * 1.5
    box(ladder, 1.95 + i * 0.2, y, 11.7, 1.32, "F5F7FA" if i < 5 else "FFF7ED", None)
    text(ladder, name, 2.25 + i * 0.2, y, 2.6, 1.32, 22, CYAN if i < 5 else AMBER, True,
         anchor=MSO_ANCHOR.MIDDLE)
    text(ladder, ev, 4.75 + i * 0.2, y + 0.04, 8.75, 1.24, 17, BODY, anchor=MSO_ANCHOR.MIDDLE)
repro = group(s)
box(repro, 15.0, 3.4, 9.65, 8.95)
text(repro, "冷构建 CI 与复现边界", 15.35, 3.65, 9.0, 0.75, 26, BLUE, True)
text(repro, "GitHub 托管 Ubuntu 24.04，从空目录：\n"
            "① repo 按 manifest 同步  ② 锁定工具链与 SDK\n"
            "③ 用临时开发身份签 BL1 / BL2 / CP / AP\n"
            "④ 构建 App 与单测  ⑤ 固件与配网回归\n"
            "⑥ 合同与采集自检  ⑦ 只发布公开证据（不含私钥）\n"
            "⑧ 下载后独立验签工厂包与 APK\n\n"
            "现状：PR #140 合入后的 b21fa804 构建与 verify-delivery 成功；最新公开分支头 9549e7ba 的官方 push 也成功。均为临时开发身份，不等于作者板可接受的发布包。\n\n"
            "同源码同配置可复现；签名镜像不逐字节相同（密钥不同）。失败也保留：770 重连失败、CPU1 心跳停止、11-03 缺网络配置",
     15.35, 4.5, 9.05, 7.8, 18)
add_transition(s)
add_builds(s, [[ladder], [repro]])
footer(s, "当前源码尚无作者签名发布与整机验收；11-03 实板的完整版本未能读回，不归为当前构建。", 21)
notes(s, "证据与复现", 24,
      "每条结论都带证据层级：从代码、主机测试、模拟器、实板、真机，到人工确认。主机测试用 CMocka 和 mock 隔离硬件寄存器和 SDK，pytest 有 996 个产品合同用例；App 有 345 个不同单测和 14 组模拟器场景；实板和真机分别有记录；人工观感还要补。复现上，CI 从空目录按 manifest 同步、锁定工具链、签名构建、跑回归，最后下载产物独立验签。PR #140 合入后，官方 b21fa804 的构建与独立交付复核均已成功；它仍只证明开发身份的可构建性，不替代作者签名发布或当前源码的实板验收。",
      ["docs/platforms/bk7258/shaniu-feature-inventory.md", "README.md#从源码复现", "tests/host/bk7258/README.md",
       ".github/workflows/shaniu-source-checks.yml",
       "https://github.com/open-vela/contest2026_135_yongwangzhiqian/actions/workflows/shaniu-source-checks.yml"],
      f"两次点击：证据阶梯 → CI 与复现。复现层次见{ref('repro')}。")
order.append(s)

s = reset(slides[13], "下一步：先把每天用得上的体验做扎实")
nxt = [
    ("1", "补齐网络与云配置，在当前版本上实测语音、审批拒绝、实卡、振感与 OTA 重连", CYAN),
    ("2", "确定第一方发布身份，开放签名小功能的真机安装与商店目录", BLUE),
    ("3", "OTA 后重连与长稳专项；采集真人多人唤醒数据，建立独立测试集", PURPLE),
    ("4", "功耗测量、外壳与生产供应；把多镜像启动与媒体排空整理为上游贡献", CYAN),
]
for i, (n, body, accent) in enumerate(nxt):
    y = 3.4 + i * 2.15
    box(s, 1.95, y, 22.7, 1.85)
    text(s, n, 2.35, y + 0.28, 1.0, 1.2, 46, accent, True)
    text(s, body, 3.6, y + 0.5, 20.6, 1.0, 25, INK)
footer(s, "首批用户：开发者与喜欢桌面互动的人；先验证使用频率与会话成本。功耗、续航、多人唤醒率、成本目前未测，不作承诺。", 21)
add_transition(s)
notes(s, "下一步", 8,
      "下一步：先在当前版本上补齐实机验证，再开放签名小功能，然后做升级后重连与长稳、真人多人唤醒，最后是功耗、外壳和生产。")
order.append(s)

# ============================================================ 尾声
s = slides[15]
next(x for x in s.shapes if x.name == "AutoShape 4").height = Inches(3.1)
set_text(next(x for x in s.shapes if x.name == "AutoShape 6"),
         "感谢聆听，欢迎提问", 34, PAPER, False, PP_ALIGN.CENTER)
for name, y, h, value, size in [
    ("AutoShape 7", 11.95, 0.8, "傻妞 · 勇往直前", 35),
    ("AutoShape 8", 12.95, 0.85, "“那你退下吧。”——“知道了啦，那我先走了哦，晚安。”", 27),
]:
    shape = next(x for x in s.shapes if x.name == name)
    shape.left, shape.width = Inches(2.0), Inches(22.6)
    shape.top, shape.height = Inches(y), Inches(h)
    set_text(shape, value, size, PAPER, False, PP_ALIGN.CENTER)
v = live(s, "goodnight", 19.2, 0.5, 6.9, label=False)
add_transition(s)
notes(s, "尾声", 16,
      "故事的最后，像那天晚上一样：她嘴硬说“才不想跟你多待一会儿”，我说“那你退下吧”，她说“知道了啦，晚安”。谢谢各位评委老师，欢迎提问。",
      [f"实拍原片 {CLIPS['goodnight']['source']}"], "右上角小窗点击播放（约 13 秒，含一张“略去 8 秒”说明卡）。", video=v)
order.append(s)


# ================================================================= appendix
s = new_slide("功能与证据矩阵")
table(s, [
    ("能力", "最高证据", "尚缺"),
    ("唤醒与连续问答", "实板 638（9/20）；11-03 真人唤醒本地命中", "当前版本补配置后复测；多人统计"),
    ("本地固定指令", "主机测试 + AIDK 构建", "实板验收"),
    ("拍照识物", "实板 639 用户确认；9/19 实拍", "准确率统计（有颜色错例）"),
    ("语音控制外设", "9/19 实拍：振动、眼睛、音量、加速度", "当前版本复测"),
    ("表情与资源包", "实板 641 跨复位保持；11-03 渲染回读", "当前双眼观感确认"),
    ("动作表情与触觉", "11-03 采样更新、驱动返回 0", "拿起关联与振感人工确认"),
    ("NFC 场景卡", "模拟器登记；无卡扫描 -110", "实卡登记与触发"),
    ("电脑任务 / 取景", "11-03 实板回执；55 秒录屏", "反馈观感；并发与长稳"),
    ("手机单次审批", "11-03 实板 + 真机：批准后执行（录屏）、过期不执行", "拒绝；长期使用"),
    ("记忆 / Skill", "11-01 真机 + 实板读回", "跨天使用"),
    ("App OTA", "634 首次；767 重连通过；770 重连失败", "OTA 后重连专项"),
    ("签名小功能", "主机测试；诊断配置测试签名实板安装", "产品发布者指纹"),
], (6.0, 9.8, 6.9), size=18)
notes(s, "功能与证据矩阵", 0, "被问“这个功能真的跑过吗”时打开。",
      ["docs/platforms/bk7258/shaniu-feature-inventory.md", "boards/bk7258/CONFIGS.md"])
add_appendix("matrix", s)

s = new_slide("11-03 实板：电脑按需取景（55 秒原速）")
movie(s, BOARD_VIDEO, BOARD_POSTER, 1.95, 3.45, 15.0, "11-03 实板取景录屏 · 点击播放")
used_media.append({"key": "board-camera", "file": BOARD_VIDEO.relative_to(DOCS.parent).as_posix(),
                   "seconds": duration(BOARD_VIDEO), "sha256": sha(BOARD_VIDEO), "source": "11-03 工作台录屏",
                   "start": 0, "end": duration(BOARD_VIDEO), "scope": "实板工作台原速录屏，无环境音；完整 CP/AP 版本未知", "embedded": True})
for i, (title, body, accent) in enumerate([
    ("两帧真实画面", "640×480 JPEG：8,583 / 11,767 B\n帧传输 1.413 / 2.020 s", CYAN),
    ("保存与重拍", "保存的 JPEG 哈希与回执一致\n取消后重拍，最后释放缓存", BLUE),
    ("边界", "画面偏暗、无环境音\n完整固件版本未读回", PURPLE),
]):
    y = 3.45 + i * 2.9
    box(s, 17.6, y, 7.05, 2.55)
    text(s, title, 18.0, y + 0.3, 6.3, 0.75, 27, accent, True)
    text(s, body, 18.0, y + 1.15, 6.3, 1.3, 20)
notes(s, "实板取景录像", 0, "后端取景与读取 4.429 / 5.067 秒，不是点击到显示的端到端时延。",
      ["docs/verification/bk7258/2026-11-03-feature-validation/README.md"])
add_appendix("board-camera", s)

s = new_slide("唤醒模型：初赛后 9 轮训练研究")
table(s, [
    ("轮次", "做了什么", "关键结果（固定 300 ms / 连续 2 次 / 冷却 1 s）", "结论"),
    ("R1 联合训练", "10 个候选 / 104 epoch；流式 TCN32 + PCAN-v2 前端", "合成留出命中 127 → 318/400；非目标触发 67 → 0", "选为研究基线 A"),
    ("R2 快语速", "4 组 × 12 epoch；变速增强、目标后监督区间", "快语速 21 → 55/100", "慢速退步，保留 A"),
    ("R3 开源迁移", "7 组 × 12 epoch；对照 openWakeWord / microWakeWord / micro_speech 源码迁移做法", "没有模型全面优于 A", "保留 A"),
    ("R4 时间损失", "6 组 36 epoch；时间损失 + 来源权重", "F4b 371/400；新测试 77/160（A 70/160）", "新增窗外触发，保留 A"),
    ("R5 真人预训练", "MobvoiHotwords 21.8 h 等真人语料预训练关键词编码器 + 新分类头 + 分阶段解冻", "新快语速集 0/40 → 31/40；真人负例 17 → 1 次 / 4.19 h", "选为研究候选 H3"),
    ("R6 增量真人", "新增 5.2 万条 / 57.3 h 公开真人录音；辅助任务与蒸馏", "部分误触发减少，新音色召回下降", "保留 H3"),
    ("R7 语料扩充", "去重池 224.4 h / 17.2 万条；8×8 时频指纹 + 相关 ≥0.995 近重复筛查", "7 组 28 epoch，无替换证据", "保留 H3"),
    ("R8 完整短语", "成对排序损失、两级验证器（逻辑回归）、CTC 原型", "20.99 h 真人连续负流 0 次（曾用于选型）", "保留 H3"),
    ("R9 双语教师", "Zipformer KWS 与 Wav2Vec2 CTC 审计 612 段发音；学生音素辅助", "103 次负触发浮点同样出现，非量化所致", "保留 H3；产品仍 v46"),
], (3.3, 8.3, 7.1, 4.0), size=16, row_h=0.8)
text(s, "每个候选都导出全 INT8 / WKM2，并用原生 TFLM 校验；目标词正例全部是合成语音，没有真人多人 FAR/FRR。研究记录与语料在本地，不随仓库发布；公开语料按各自许可使用。",
     1.95, 11.75, 22.7, 1.0, 18, MUTED)
notes(s, "唤醒研究", 0, "被问“唤醒模型怎么训的、为什么没换”时打开。11-03 真人唤醒两次本地命中（976/1000、859/1000），不据此计算识别率。",
      ["docs/verification/bk7258/2026-10-26-wakeword/（本地研究记录）", ".agents/skills/edge-wakeword-training/SKILL.md"])
add_appendix("kws-research", s)

s = new_slide("为什么保留 MiMo 流式 TTS")
table(s, [
    ("回合", "供应商", "完成", "首 PCM", "最大断供", "欠载"),
    ("Tencent v3-1", "腾讯云 WS", "是", "1,682 ms", "7,607 ms", "8"),
    ("Tencent v3-2", "腾讯云 WS", "否（超时）", "1,605 ms", "5,587 ms", "8"),
    ("Tencent v4-1", "腾讯云 WS", "是", "1,637 ms", "3,863 ms", "9"),
    ("MiMo v4-1", "MiMo SSE", "是", "1,344 ms", "531 ms", "3"),
    ("MiMo v4-2", "MiMo SSE", "是", "1,227 ms", "697 ms", "1"),
], (4.6, 3.8, 3.4, 3.6, 3.8, 3.5), size=22)
text(s, "同板、同输入、同 256 KiB 队列与 Media 路径。腾讯云最长 socket 可读等待 3,648 ms，板端 TLS 解密与锁均很小：等待在字节到达之前。\n5 次小样本只支持本轮取舍，不是 P95 或供应商排名；腾讯云实现保留为测试后端。",
     1.95, 9.0, 22.7, 2.2, 20, MUTED)
notes(s, "TTS 取舍", 0, "被问为什么不用更快的 TTS 时打开。",
      ["docs/verification/bk7258/2026-10-29-voice-latency-r7/voice-latency-r7.md（本地研究记录）"])
add_appendix("tts", s)

s = reset(slides[10], "初赛后：她不只听指令，也会自己有反应")
left = group(s)
box(left, 1.95, 3.4, 11.1, 5.6)
text(left, "拿起、倾斜时的表情", 2.35, 3.65, 10.3, 0.8, 29, CYAN, True)
text(left, "唯一 motion worker，采样间隔 ≥100 ms\n相邻变化 >3000 mm/s² 判“移动”；500 mm/s² 内驻留 400 ms 判“稳定”\n倾斜进入/退出余弦 0.80 / 0.95；事件冷却 2 s\n只发 ≤800 ms 的表情意图：语音、专注、认领优先，被抢占就作废",
     2.35, 4.55, 10.4, 4.3, 20)
right = group(s)
box(right, 13.55, 3.4, 11.1, 5.6)
text(right, "完成时轻振一下", 13.95, 3.65, 10.3, 0.8, 29, PURPLE, True)
text(right, "专注或电脑任务完成 → 40 ms 脉冲，冷却 2 s，不排队\n说话和收音时不振（防止自听）；振后 500 ms 丢弃动作参考\n电压 ≤3400 mV、OTA、电机故障时抑制\n动作本身不振动，避免形成加速度反馈环",
     13.95, 4.55, 10.4, 4.3, 20)
strip = group(s)
picture(strip, MEDIA / "eye-sprites-strip.png", 1.95, 9.35, w=13.2)
text(strip, "真实眼睛资源包 shaniu-cyan-v2：睡眠 · 聆听 · 思考 · 说话 · 惊讶 · 开心", 1.95, 11.65, 13.2, 0.55, 18, MUTED)
ev = group(s)
box(ev, 15.55, 9.35, 9.1, 2.85, "E9F8F1", None)
text(ev, "证据", 15.9, 9.5, 8.4, 0.6, 22, GREEN, True)
text(ev, "11-03 实板：加速度样本持续更新；任务反馈两次振动驱动返回 0，之后采集恢复\n待人工确认：拿起时的表情与实际振感", 15.9, 10.15, 8.5, 2.0, 18, BODY)
add_builds(s, [[left], [right], [strip, ev]])
notes(s, "动作与触觉", 0,
      "初赛之后，我们让她不只听指令。拿起、倾斜时她会短暂换表情；专注或电脑任务完成时会轻振一下。这里的难点是规则：采样多快、什么算移动、什么时候不能振——比如说话和收音时绝不振，否则麦克风会录进马达声。实板上采样和驱动都有记录，人眼和手感的确认还要补。",
      ["docs/platforms/bk7258/shaniu-desktop-companion.md#n1-输入与反馈",
       "docs/verification/bk7258/2026-11-03-feature-validation/README.md"],
      "三次点击：动作 → 触觉 → 眼睛与证据。")
add_appendix("motion", s)

s = reset(slides[6], "卡片、专注与小功能：把常用的事放到手边")
cards = [
    ("NFC 场景卡", "App 把卡登记到 8 个槽：\n1–4 开始/暂停/继续/取消专注\n5–6 播放/取消本地内容（原创 4 秒“四拍节律”）\n7 启动已安装小功能（App ID 的 FNV-1a 哈希）\n一次一张卡，移开再碰；UID 不等于 owner", CYAN,
     "模拟器登记通过；实板无卡 -110；实卡触发待测"),
    ("专注计时", "语音 / App / NFC / 电脑都能开始、暂停、继续、取消\n眼睛显示 32 点进度环与剩余时间\n完成：绿色对勾 + 轻振；重启不恢复运行中的计时", BLUE,
     "模拟器与主机测试；语音入口待实板验收"),
    ("签名原生小功能", "ABI 1/2/3：权限按 display→motion→haptic→sound→clock→storage\n每步 ≤20 ms；ELF ≤16 KiB，最多装 4 个、同时 1 个\n存档 ≤32 B、每日定时 ≤4 条；加载生命周期只在 CPU0\n协作式 ABI，不是安全沙箱", PURPLE,
     "主机测试；产品发布者指纹为空，正式固件暂拒安装"),
]
cg = []
for i, (t, b, a, e) in enumerate(cards):
    g = group(s)
    card(g, 1.95 + i * 7.65, 3.4, 7.3, 8.9, t, b, a, e, body_size=21)
    cg.append([g])
add_builds(s, cg)
notes(s, "卡片与小功能", 0,
      "放到手边的东西：NFC 卡可以登记成“开始专注”、播一段本地内容或启动一个小功能；专注计时可以从四个入口开始，眼睛上显示进度环；小功能是第一方签名的原生模块，权限要显式声明，每步不超过二十毫秒。这三项目前是模拟器和主机测试，实卡和签名安装还没在实板上完成。",
      ["docs/platforms/bk7258/shaniu-desktop-companion.md#n2-场景与本地内容", "docs/platforms/bk7258/shaniu-desktop-companion.md#n5-语音启动定时与保存进度abi-3",
       "tools/bk7258/README.md#first-party-native-feature-packages"],
      "三张卡逐次出现。")
add_appendix("cards", s)

s = new_slide("她可以被定制：人格、回答方式和场景技能")
cards = [
    ("人格与回答方式", "App 设置风格、回答长度（简洁/标准/详细）、\n思考开关、模型、音量、语速；设备保存，\n重启后保留；本轮明确要求优先于长期偏好", CYAN),
    ("回答时的表情", "LLM 可调 reply_expression 设定本轮表情：\nneutral / happy / confused / thinking / sorry；\n播报开始显示，结束或取消时撤销", BLUE),
    ("Runtime Skill", "“专注助手 / 设备体检 / 识物解说：……”\n每轮锁定工具权限，越权不重试；\n外部 Skill 需 P-256 签名，装后默认停用", PURPLE),
    ("唤醒词与外观", "App 内置 3 个唤醒模型（你好 openvela / 冰冰 / 傻妞），\n可导入 .wkm；眼睛资源包 .bkep 经 Wi-Fi 安装，\n以设备回读 pack_id / revision 为准", CYAN),
]
cg = []
for i, (t, b, a) in enumerate(cards):
    g = group(s)
    card(g, 1.95 + (i % 2) * 11.6, 3.4 + (i // 2) * 4.45, 11.1, 4.1, t, b, a, body_size=21)
    cg.append([g])
footer(s, f"证据：11-01 真机读回（Skill 启停、记忆）；9/19 实拍含“傲娇”人格对话（{ref('persona')}）；回答长度为主机测试，真实模型遵循度未统计。", 20)
add_builds(s, cg)
notes(s, "定制", 0,
      "她的性格和说话方式也可以定制：风格、回答长短、要不要深度思考，都由手机设置、设备保存。回答时她会配合表情；我们还做了三个场景技能，比如“专注助手”，每轮锁定能用的工具；唤醒词和眼睛外观也可以换，都以设备回读为准。",
      ["docs/verification/bk7258/2026-11-01-agent-profile-skills-hil.md", "tools/bk7258/README.md#runtime-skill-packages",
       "app/bk7258/assets/display/README.md"],
      f"四张卡逐次出现。被问人格时可打开{ref('persona')}的实拍。")
add_appendix("custom", s)

s = new_slide("失败与修复")
table(s, [
    ("时间 · 版本", "现象", "处理与状态"),
    ("8 月 bring-up", "UART 能出不能进；OPP 名称误导；RPMsg 53 轮 ENOMEM", "逐层修复；按实测频率；常驻 worker"),
    ("9/13 · 唤醒 v45", "合成正例发音为 /vɛlə/，与指定 /ˈviːlə/ 不符", "按实际 token 反解校正，62 条正例重生成，v46 重训"),
    ("9/20 · 639", "显示卷挂载/读包 EIO", "641 修复；身份写入 -2002 未通过"),
    ("10/18 · 755/756", "CPU1 心跳停止", "无共同根因，未闭合"),
    ("10/18 · 770", "升级成功但自动重连失败", "767 通过；OTA 重连专项"),
    ("11/02", "CPU1 上文件 I/O / RPMsg 锁计数异常", "加载限定 CPU0；不宣称底层修复"),
    ("11/03", "真人唤醒命中但对话 ENETDOWN", "设备缺网络与云配置；补齐后再验"),
    ("11/03", "模拟器配网异常组合超时", "图形层问题；软件渲染后完整通过"),
    ("11/03 · CI", "合同步骤 SETUP_ERROR", "历史 GCC 13 -Waddress + -Werror 问题；PR #140 合入后的 build 与 verify-delivery 已成功"),
], (5.0, 9.4, 8.3), size=19)
notes(s, "失败与修复", 0, "被问稳定性时打开。未闭合的直接说未闭合。",
      ["docs/platforms/bk7258/shaniu-feature-inventory.md#失败与修复", "docs/platforms/bk7258/shaniu-master-plan.md（v45/v46 发音更正）"])
add_appendix("failures", s)

s = new_slide("“同一份固件”指什么")
table(s, [
    ("层次", "是否保证", "说明"),
    ("相同源码与配置", "是", "团队 manifest 固定 NuttX、Agent、媒体、apps、SDK 与工具链"),
    ("等价行为", "预期是", "仍需在复现者自己的板上验收"),
    ("未签名载荷逐字节一致", "未验证", "尚未比对两个独立环境的原始镜像哈希"),
    ("签名镜像逐字节一致", "否", "签名密钥不同；私钥、设备身份与云凭据不是公开输入"),
], (6.0, 3.4, 13.3), size=21)
text(s, "外部 debug APK 签名不同，不能覆盖安装作者的 App（卸载会丢 Keystore 认领凭据）；CI 每次的临时签名根不被作者的板接受；\n作者签名发布出现后，以 Release 的 SHA256SUMS 与 source-inputs.json 核对。",
     1.95, 8.0, 22.7, 2.0, 21, BODY)
notes(s, "复现层次", 0, "被问“评委能不能编出同样的固件”时打开。", ["README.md#从源码复现"])
add_appendix("repro", s)

s = new_slide("电脑单次审批的安全设计")
card(s, 1.95, 3.4, 11.1, 8.9, "绑定什么",
     "请求方、目标、逐字命令、随机请求 ID\n有效期与单次使用\n该电脑授权密钥：回执用其 HMAC\n电脑只能请求；只有已认证手机 owner 能决定\n超时、内容改动、授权变化、断开 = 拒绝", AMBER, body_size=23)
card(s, 13.55, 3.4, 11.1, 8.9, "怎么用",
     "workbench approval-request：只请求不执行\nworkbench approval-run --exec …：批准后才执行\n（argv 直接执行，不经 shell；退出码 2 = 未批准）\napproval-hook：Claude Code PreToolUse\n普通已授权任务不需要审批", BLUE, body_size=23)
text(s, "11-03 实录：approval-run 请求打开 calc.exe → 手机批准 → decision=approved、authenticated=true、process_returncode=0；无人批准的几次为 expired、命令未执行。拒绝分支待实测。",
     1.95, 12.45, 22.7, 0.9, 18, MUTED)
notes(s, "审批设计", 0, "设备没有能逐字显示命令的屏幕，按键语义固定；owner 凭据在手机。第 11 页是批准分支的实录。",
      ["tools/bk7258/README.md#phone-owner-approval-for-pc-agents", "11-03 作者录屏 电脑授权.mp4（本地，未发布）"])
add_appendix("approval", s)

s = new_slide("AIDK 接线与 BOM（原型，非采购报价）")
table(s, [
    ("功能 / 数量", "器件或接口", "板级连接与约束"),
    ("主控 ×1", "BK7258，三核 Cortex-M33", "片内 SRAM 640 KiB；Flash 布局 8 MiB"),
    ("眼睛屏 ×2", "GC9D01，160×160", "/dev/fb0 → QSPI1；/dev/fb1 → QSPI0"),
    ("摄像头 ×1", "GC2145，DVP / SCCB", "I2C1 map mode1，P42/P43；640×480 JPEG"),
    ("音频", "MIC1；MIC2 为回环参考", "主麦克风采集；AEC 未验证"),
    ("扬声器控制", "MUTE / PA_SD", "P50；采集与播放由 Media 仲裁"),
    ("加速度计 ×1", "SC7A20H", "I2C0 P20/P21"),
    ("存储 ×1", "SD NAND", "SDIO 4-bit，P14—P19；实测 121 MiB"),
    ("NFC ×1", "MFRC522", "UART1 P0/P1"),
    ("马达", "CN10，高有效", "P9；1—100 ms"),
    ("按键 ×3", "K1 / K2 / K3，低有效", "P13 / P12 / P8；音量− / 电源 / 音量+"),
], (4.6, 7.0, 11.1), size=18)
notes(s, "接线与 BOM", 0, "被问硬件细节时打开。", ["docs/contest/技术报告-BK7258三核适配与傻妞AI伴侣.md#344-硬件设计接线与关键-bom"])
add_appendix("bom", s)

for key, title, desc in [
    ("persona", "更多实拍：切换“傲娇”人格，讲个笑话", "人格/风格可由手机设置或语音切换；回答内容由所配置的云端模型生成。原片中同一请求说了 3 次，此为第 3 次及回答。"),
    ("app-voice", "更多实拍：手机旁的一次对话", "真实手机与实板同框：“你好，openvela”→“我在”→“你在干什么？”。问完到开口 11.1 s，片中已标注略去部分。"),
    ("wakeword", "更多实拍：App 切换唤醒词", "App 内置三个唤醒模型：你好 openvela / 你好傻妞 / 你好冰冰；切换后以设备回读为准。手机屏幕曝光偏亮。"),
    ("recording", "更多实拍：T5-Board 录音写入 TF 卡", "Dolphin 录音：“你好 openvela，今天天气怎么样？”停止后在文件列表中出现。"),
    ("app", "更多实拍：真实手机认领傻妞", "9/19 真实手机扫码认领：认证成功、连接、认领完成。手机由此成为 owner，之后才能配网、定制、升级和给电脑授权。此为 App 0.5.x 时期界面。"),
]:
    s = new_slide(title)
    live(s, key, 1.95, 3.4, 15.0)
    text(s, desc, 17.5, 3.6, 7.15, 5.0, 20)
    notes(s, title.split("：")[-1], 0, "问答时按需播放。", [f"实拍原片 {CLIPS[key]['source']}"])
    add_appendix(key, s)

s = new_slide("产品动画《我在》（v3.1）")
film_seconds = duration(film)
film_length = f"{round(film_seconds) // 60} 分 {round(film_seconds) % 60:02d} 秒"
poster = picture(s, MEDIA / "wozai-v3.1-thumbnail.png", 1.95, 3.4, w=15.0)
poster.click_action.hyperlink.address = film.name
text(
    s,
    f"单独播放：{film.name} · {film_length}\n视频与 PPT 同目录；放映时点击封面打开",
    1.95,
    11.95,
    15.0,
    0.95,
    17,
    MUTED,
)
used_media.append(
    {
        "key": "film",
        "file": film.name,
        "seconds": film_seconds,
        "sha256": sha(film),
        "source": "docs/contest/答辩材料/产品影片/v3-wozai",
        "start": 0,
        "end": film_seconds,
        "scope": "《我在》v3.1 板载应答音中文字幕版：Claude 辅助制作动画，Codex 更新字幕与应答音；全片 9 处应答音替换，画面沿用原版；人物情节虚构、产品交互为动画演绎，片中等待不代表实测时延；未嵌入 PPT，与 PPT 同目录单独播放",
        "embedded": False,
    }
)
text(s, f"{film_length} · 三维叙事短片", 17.5, 3.6, 7.15, 0.6, 24, CYAN, True)
text(s, "中文字幕 · 板载应答音版", 17.5, 4.35, 7.15, 0.5, 19, MUTED)
text(
    s,
    "分手后的周屿收到一块开发板，亲手把它做成了会说“我在”的傻妞。\n故事串起对话、记忆、电脑审批与升级，在一次次回应中讲述陪伴。",
    17.5,
    5.3,
    7.15,
    3.5,
    22,
)
text(
    s,
    "人物与情节虚构；产品交互为动画演绎。\n片中等待经过剪辑，不代表实测时延。\n功能与镜头对照见 FEATURE_MAP.md。",
    17.5,
    9.15,
    7.15,
    2.3,
    18,
    MUTED,
)
notes(
    s,
    "产品动画《我在》v3.1（板载应答音·中文字幕版）",
    0,
    f"按需单独播放完整产品故事（{film_length}），不计入 8 分钟主讲。人物与情节虚构，产品交互为动画演绎，不能作为实机验收或时延证据。"
    "Claude 辅助制作原动画，Codex 更新字幕与应答音。全片 9 处“我在”替换为当前板载同源应答素材，沿用原片的空间音效处理；不是实板扬声器录音。"
    "画面与烧录中文字幕完整保留，编号 1203 未改。周屿旁白与对白仍为剪映 SAMI“温柔男主”，其余傻妞台词仍为 Kokoro 合成演绎。"
    "原工程 SOURCES.md 记载原始配音；本版替换与验证以成片同目录的《板载应答音版说明.txt》为准。",
    [
        "docs/contest/答辩材料/产品影片/v3-wozai/README.md",
        "docs/contest/答辩材料/产品影片/v3-wozai/FEATURE_MAP.md",
        "docs/contest/答辩材料/产品影片/v3-wozai/SOURCES.md",
        "外置成片同目录：板载应答音版说明.txt · SHA256SUMS-板载应答音.txt",
    ],
    cue=f"成片 {film.name} 与 PPT 保持同目录；放映时点击封面，或手动打开成片。",
)
add_appendix("film", s)

s = new_slide("三块板同框与 App 0.7.27 新界面")
picture(s, MEDIA / "boards-in-context.jpg", 1.95, 3.4, w=11.3)
text(s, "三板实拍：T5AI-Core · AIDK AI Toy（傻妞，透明外壳）· T5-Board（Dolphin）", 1.95, 9.0, 11.3, 0.6, 17, MUTED)
picture(s, MEDIA / "app-screens-emulator.jpg", 13.6, 3.4, w=11.05)
text(s, "App 0.7.27 模拟器截图（夹具状态）：设备页 · 设置 · 专注计时 · 场景卡片。\n设备 / 定制 / 更新 / 设置四页；状态只按设备回读显示；345 个不同 JVM 用例、14 组模拟器场景通过。\n模拟器不代表真实蓝牙；新版 App 尚无真机连板记录。", 13.6, 9.3, 11.05, 2.6, 18, BODY)
notes(s, "三板与新界面", 0, "被问“App 现在长什么样”或“三块板”时打开。",
      ["docs/verification/bk7258/2026-11-03-feature-validation/README.md#手机模拟器录像", "docs/contest/assets/report-v5/boards-in-context.jpg"])
add_appendix("boards", s)

s = new_slide("电脑工作台：任务、取景与资源作业的运行细节")
cards = [
    ("真实任务 task-run", "电脑执行 argv（不经 shell），退出码决定成败\n运行事件 ≤1 次/5 s、TTL 15 s；可选进度文件\nCtrl+C / 超时 → 取消，CLI 返回 130\n回执 device_result=confirmed 才算送达", CYAN,
     "11-03 实板：正常 0、超时取消，两次回执确认"),
    ("按需取景", "浏览器点“采一帧”：单帧 640×480 JPEG，512 B 分块\n保存的就是已显示的那一帧，不再拍第二次\n取消后可重拍，最后释放设备缓存", BLUE,
     "11-03 实板：8,583 / 11,767 B，帧传输 1.413 / 2.020 s，保存哈希一致"),
    ("资源与默认表情", "导入眼睛包、限时试用、设为默认、目录分页\n每次一个作业；4 KiB 块断点续传\n受理 ≠ 完成，以回读为准", PURPLE,
     "主机：HTTP → TLS → 原生安装作业；11-03 profile 无该权限（-13）"),
]
for i, (t, b, a, e) in enumerate(cards):
    card(s, 1.95 + i * 7.65, 3.4, 7.3, 7.8, t, b, a, e, body_size=21)
footer(s, "工作台录屏里的眼睛包导入是作者口述，未记录安装完成或设备回读，不能替代右侧资源作业的证据边界。", 18)
notes(s, "工作台运行细节", 0,
      "被问工作台怎样运行、采图是否会重复拍摄或资源安装怎样确认时打开。",
      ["tools/bk7258/README.md#run-one-real-computer-task",
       "docs/verification/bk7258/2026-11-03-feature-validation/evidence/pc-task-success.json",
       "docs/verification/bk7258/2026-11-03-feature-validation/evidence/camera-events.json"])
add_appendix("workbench-detail", s)

s = new_slide("证据索引")
table(s, [
    ("内容", "位置（仓库相对路径）"),
    ("当前板级结论", "boards/bk7258/CONFIGS.md"),
    ("功能与证据清单", "docs/platforms/bk7258/shaniu-feature-inventory.md"),
    ("唤醒模型与训练元数据", "app/bk7258/models/nihao_openvela.metadata.json · tools/bk7258/_lib/voice_kws.py"),
    ("11-03 实板与模拟器复测、录像", "docs/verification/bk7258/2026-11-03-feature-validation/"),
    ("11-01 记忆与 Skill 真机", "docs/verification/bk7258/2026-11-01-agent-profile-skills-hil.md"),
    ("10-18 稳定性与 OTA", "docs/verification/bk7258/2026-10-18-stability-and-phone-hil.md"),
    ("9/20 638 语音与 641 资源", "docs/verification/bk7258/2026-09-20-shaniu-638/641-full-image.md"),
    ("依赖固定提交", "contest2026_135_yongwangzhiqian.xml · openvela.xml · tools/bk7258/toolchain.json"),
    ("9/19 原始实拍", "作者 iPhone 原片 IMG_18xx.MOV（本地，未发布）"),
    ("构建与发布 SOP", "docs/platforms/bk7258/nuttx-port/bk7258-build-flash-debug-sop.md"),
], (8.2, 14.5), size=18)
notes(s, "证据索引", 0, "语音时延与唤醒研究记录、原始实拍均在本地，未随仓库发布。")
add_appendix("index", s)
assert len(appendix) == len(APPX)

# ---------------------------------------------------------------- assemble
final = order + appendix
ids = list(prs.slides._sldIdLst)
id_of = {sl.slide_id: el for el, sl in zip(ids, prs.slides)}
keep = [id_of[sl.slide_id] for sl in final]
for element in ids:
    prs.slides._sldIdLst.remove(element)
    if element not in keep:
        prs.part.drop_rel(element.rId)
for i, (element, slide) in enumerate(zip(keep, final)):
    prs.slides._sldIdLst.append(element)
    slide._element.set("show", "1" if i < len(order) else "0")
    if 0 < i < len(order) - 1:
        text(slide, f"{i + 1:02d}", 1.95, 14.28, 3.0, 0.4, 18, MUTED)
    elif i >= len(order):
        letter = chr(65 + i - len(order))
        text(slide, f"备查 {letter}", 1.95, 14.28, 3.0, 0.4, 18, MUTED)
        heading = next(x for x in slide.shapes if x.name == "AutoShape 3")
        title = re.sub(r"^备查( [A-Z])?｜", "", heading.text_frame.text)
        set_text(heading, f"备查 {letter}｜{title}", 44, INK, True)
        timing[i]["title"] = f"备查 {letter}｜{timing[i]['title']}"
    if slide._element.find(qn("p:transition")) is None:
        add_transition(slide)

prs.core_properties.title = "傻妞：放在桌上的 openvela AI 伴侣 — 决赛答辩修订版"
prs.core_properties.subject = f"{len(order)} 页主讲 + {len(appendix)} 页隐藏备查；官方模板；8 分钟时间预算"
prs.core_properties.author = "勇往直前"
prs.core_properties.comments = "一个晚上的故事结构；内嵌 9/19 原速实拍片段、11-03 实板录屏与示意动画；原稿与官方模板保留。"
prs.save(OUTPUT)

main = timing[:len(order)]
scheduled = sum(i["seconds"] for i in main)
video_main = sum(i["video_seconds"] for i in main)
manifest = {
    "output": OUTPUT.name, "source": SOURCE.name,
    "source_sha256": sha(SOURCE), "template_sha256": sha(TEMPLATE), "output_sha256": sha(OUTPUT),
    "main_slides": len(order), "hidden_appendices": len(appendix),
    "scheduled_seconds": scheduled, "budget_seconds": 480, "buffer_seconds": 480 - scheduled,
    "video_seconds_in_main": video_main, "timing_is_estimate": True,
    "media": used_media,
    "main_script_characters": sum(len(i["script"]) for i in main),
    "slides": timing,
}
(HERE / "答辩时间与来源.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")

lines = ["# 决赛答辩讲稿：一个晚上的故事（8 分钟主讲）", "",
         f"主讲 {len(order)} 页，预算合计 {scheduled // 60} 分 {scheduled % 60:02d} 秒（其中实拍视频 {video_main} 秒，"
         f"播放时不说话），余量 {480 - scheduled} 秒；隐藏备查 {len(appendix)} 页只在问答时打开。",
         "时间为估计值，请按实际朗读计时调整。9/19 iPhone 实拍片段内部原速、不加速；略去的等待在片中用卡片写明秒数。第 11 页使用 11-03 作者导出的电脑审批录屏（含口述字幕，可能已删减停顿），截取 8.8–38.1 秒，不额外加速或拼接。录屏未记录固件与 App 的完整版本，不能据此认定当前源码已完成实机验收。", ""]
elapsed = 0
for i, item in enumerate(main, 1):
    elapsed += item["seconds"]
    lines += [f"## {i:02d} {item['title']}（{item['seconds']} 秒，含视频 {item['video_seconds']} 秒；累计 {elapsed // 60}:{elapsed % 60:02d}）", "",
              item["script"], ""]
    if item["cue"]:
        lines += [f"> 操作：{item['cue']}", ""]
(HERE / "讲稿.md").write_text("\n".join(lines), encoding="utf-8")

src = [
    "# 每页证据与来源",
    "",
    "页码按最终顺序；备查页为隐藏页。路径默认相对仓库根目录；外置成片按文中的播放位置定位。“本地”表示未随仓库发布。",
    "",
]
for i, item in enumerate(timing, 1):
    label = f"{i:02d}" if i <= len(order) else f"备查 {chr(64 + i - len(order))}"
    src.append(f"## {label} {item['title']}")
    src += [f"- {x}" for x in item["sources"]] or ["- （无外部依据：标题或过渡页）"]
    src.append("")
src += ["## 媒体与播放方式", ""] + [
    f"- `{m['file']}`：{'已嵌入 PPT' if m['embedded'] else '未嵌入 PPT，单独播放'}；{m['seconds']} 秒，来源 {m['source']} {m['start']}–{m['end']} s，SHA-256 `{m['sha256']}`；{m['scope']}"
    for m in used_media]
(HERE / "证据与来源.md").write_text("\n".join(src) + "\n", encoding="utf-8")
print(json.dumps({k: v for k, v in manifest.items() if k not in ("slides", "media")}, ensure_ascii=False, indent=2))
