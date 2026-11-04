#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Editable JianYing draft of 《我在》 via the creator-flow jianying-editor skill
(JyProject; no GUI is opened or driven).

Tracks: 画面 (the video-only picture cut at the 13 scene boundaries, clip volume 0
so a picture file that carries audio can never double the stems), 旁白,
对白与设备声 (周屿 on screen, Shaniu, the "我在" asset and her local four-beat
sound), 配乐, 音效 (each a full-length stem from tools/mix.py, at the pre-master
level), 母带（参考，静音） (the mastered mix, volume 0: unmute it and mute the
four stems to hear or export exactly the film's audio), and 字幕 (one text clip
per subtitle event; her lines that play under his speech on 字幕（上）, as in
the ASS). JyProject copies media into the draft and refuses to replace an
existing draft of the same name.

Usage:
  JY_SKILL_ROOT=/path/to/jianying-editor-skill \\
  JY_PROJECTS_ROOT=/path/to/JianYing/drafts \\
  python3 tools/build_jianying.py --picture renders/wozai-clean.mp4 --name 傻妞影片-我在-v3.1-20261104
"""
import argparse
import json
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))
from editlib import load_edit, film_from_story  # noqa: E402

SKILL_ROOT = os.getenv('JY_SKILL_ROOT')
PROJECTS_ROOT = os.getenv('JY_PROJECTS_ROOT')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--picture', type=Path, required=True, help='clean picture MP4 (no burned subtitles)')
    ap.add_argument('--name', required=True)
    args = ap.parse_args()
    if not SKILL_ROOT:
        raise SystemExit('set JY_SKILL_ROOT to the JianYing editor Skill directory')
    if not PROJECTS_ROOT:
        raise SystemExit('set JY_PROJECTS_ROOT to the local JianYing drafts root')
    skill = Path(SKILL_ROOT)
    projects_root = Path(PROJECTS_ROOT)
    if not (skill / 'scripts/jy_wrapper.py').exists():
        raise SystemExit(f'jianying-editor skill not found at {skill}')
    if not projects_root.is_dir():
        raise SystemExit(f'JianYing drafts root not found at {projects_root}')
    sys.path.insert(0, str(skill / 'scripts'))
    from jy_wrapper import JyProject  # noqa: E402
    import pyJianYingDraft as draft  # noqa: E402

    E = load_edit()
    us = lambda s: int(round(s * 1_000_000))
    project = JyProject(args.name, width=1920, height=1080, drafts_root=str(projects_root), overwrite=False)
    if project.df.has_draft(project.name) and getattr(project, 'script', None) and project.get_track_duration('画面') > 0:
        raise SystemExit(f'draft {project.name} already has content; choose a new --name')
    # Picture: one clip per scene, cut points at the scene boundaries in film time.
    picture = str(args.picture.resolve())

    def placed(seg, what):
        if seg is None:
            raise SystemExit(f'JyProject could not import {what}')
        return seg
    for sc in E['scenes']:
        a, b = film_from_story(E, sc['t0']), film_from_story(E, sc['t1'])
        placed(project.add_clip(picture, source_start=us(a), duration=us(b - a), target_start=us(a), track_name='画面'), sc['id']).volume = 0.0
    # Audio stems on their own tracks, at the pre-master level they sum to.
    stems = ROOT / 'audio/stems'
    for track, f in (('旁白', 'narration.wav'), ('对白与设备声', 'dialogue.wav'), ('配乐', 'music.wav'), ('音效', 'sfx.wav')):
        placed(project.add_media_safe(str(stems / f), 0, track_name=track), f)
    # The mastered mix as a muted reference.
    placed(project.add_media_safe(str(ROOT / 'audio/master/master.wav'), 0, track_name='母带（参考，静音）'), 'master.wav').volume = 0.0
    # Subtitles as editable text clips: bottom track, plus a top track for her
    # lines that play under his speech (the same placement as {\an8} in the ASS).
    subs = json.loads((ROOT / 'subtitles/subtitles.json').read_text())
    for pos in (False, True):
        evs = sorted((e for e in subs if bool(e.get('top')) == pos), key=lambda e: e['start'])
        clash = [(a['line'], b['line']) for a, b in zip(evs, evs[1:]) if b['start'] < a['end']]
        if clash:
            raise SystemExit(f'overlapping subtitle events on one track: {clash}')
    for ev in subs:
        style = draft.TextStyle(size=6.0, color=(0.96, 0.91, 0.72) if ev['kind'] == 'her' else ((0.82, 0.82, 0.82) if ev['kind'] == 'narr' else (0.95, 0.95, 0.95)))
        top = bool(ev.get('top'))
        project.add_text_simple(ev['text'], start_time=us(ev['start']), duration=us(ev['end'] - ev['start']), track_name='字幕（上）' if top else '字幕',
                                style=style, clip_settings=draft.ClipSettings(transform_y=0.82 if top else -0.82))
    res = project.save()
    master = json.loads((ROOT / 'audio/master/master.json').read_text())
    record = {'draft': res.get('draft_path'), 'name': project.name, 'picture': Path(picture).name, 'scenes': len(E['scenes']), 'subtitles': len(subs),
              'tracks': ['画面', '旁白', '对白与设备声', '配乐', '音效', '母带（参考，静音）', '字幕', '字幕（上）'],
              'subtitles_top': sum(1 for e in subs if e.get('top')),
              'picture_clip_volume': 0.0, 'stem_volume': 1.0, 'master_reference_volume': 0.0,
              'stem_sum_loudness': master['input'], 'master_gain_db': master['result']['gain_db'], 'gui_opened': False}
    (ROOT / 'jianying.json').write_text(json.dumps(record, ensure_ascii=False, indent=1) + '\n')
    print(json.dumps(record, ensure_ascii=False))


if __name__ == '__main__':
    main()
