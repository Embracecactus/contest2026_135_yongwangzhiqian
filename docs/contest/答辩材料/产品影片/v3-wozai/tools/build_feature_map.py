#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""FEATURE_MAP.md: every capability in script/features.json against where the
film shows it (src/edit.js "features", converted to film time through the
warp), with its availability and evidence level kept separate from what the
film animates. Fails if a feature is neither shown nor explained as not shown.

Usage: python3 tools/build_feature_map.py
"""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from editlib import load_edit, film_from_story  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
NOT_SHOWN = {
    'F13': '实现了 App“停止这次对话”，但面向用户的取消没有实机验收；片中不演（半双工的局限由 F12 表现）。',
    'F18': '唤醒模型导入 / 恢复上一模型属于工程能力；片中只回读当前唤醒词（F17）。',
    'F48': '移除本机连接资料 / 断开手机连接与故事无关；片中改 Wi-Fi 时连接保持（F06）。',
    'F51': 'Wi-Fi CSI 诊断采集是研究功能，不在产品用户路径里。',
    'F52': '三板复用（T5-Board Dolphin、T5AI-Core）是平台工作，片中只出现 AIDK 开发板。',
}
AVAIL = {'shipping': '已实现', 'shipping-limit': '已实现（有局限）', 'unverified': '实现未验收', 'disabled': '产品固件禁用',
         'not-implemented': '未实现', 'research': '研究', 'platform': '平台 / 开发流程'}


def fmt(t):
    return f'{int(t // 60)}:{t % 60:04.1f}'


def main():
    E = load_edit()
    F = json.loads((ROOT / 'script/features.json').read_text())
    L = F if isinstance(F, list) else F['features']
    scenes = E['scenes']
    def scene_of(t):
        return next((s for s in scenes if s['t0'] <= t < s['t1']), scenes[-1])
    shown = {}
    for p in E['features']:
        shown.setdefault(p['id'], []).append(p)
    missing = [f['id'] for f in L if f['id'] not in shown and f['id'] not in NOT_SHOWN]
    if missing:
        raise SystemExit(f'features neither shown nor explained: {missing}')
    out = ['# 功能与镜头对照（FEATURE_MAP）', '',
           '> 由 `tools/build_feature_map.py` 从 `script/features.json`（功能清单与证据）和 `src/edit.js`（片中位置）生成；时间为成片时间（已计入剪辑的放慢段）。',
           '> **三层分开看：** “片中如何出现”是动画演绎；“实现”列出源码位置；“证据”只转述仓库里已有的验证记录，片中画面本身不是证据。片中等待经过剪辑，不代表实测时延。', '',
           '| ID | 功能 | 可用性 | 片中位置（场景 · 成片时间） | 片中如何出现 | 实现 | 证据（来自仓库记录） |',
           '| --- | --- | --- | --- | --- | --- | --- |']
    for f in L:
        i = f['id']
        refs = '<br>'.join(f.get('refs', []))
        status = f.get('status', '')
        if i in shown:
            locs, notes = [], []
            for p in shown[i]:
                a, b = film_from_story(E, p['t0']), film_from_story(E, p['t1'])
                sc = scene_of(p['t0'])
                locs.append(f"{sc['id']} {sc['title']} · {fmt(a)}–{fmt(b)}")
                notes.append(p['note'])
            out.append(f"| {i} | {f['name']} | {AVAIL.get(f['availability'], f['availability'])} | {'<br>'.join(locs)} | {'<br>'.join(notes)} | {refs} | {status} |")
        else:
            out.append(f"| {i} | {f['name']} | {AVAIL.get(f['availability'], f['availability'])} | 不出现 | {NOT_SHOWN[i]} | {refs} | {status} |")
    out += ['', '## 刻意不演的', '',
            '- 不让傻妞用语音发起电脑操作（F44 未实现）：命令都由电脑上的人或 AI 助手发起，傻妞只提醒，手机决定。',
            '- 断网时不让她“假装回答”，也不给唤醒加任何反应：固件在服务未就绪时消费本地命中，不播放“我在”、不改变双眼。',
            '- 不演签名原生小功能（F29 产品固件禁用），卡片操作里只显示为不可用。',
            '- 不出现指定唤醒词以外的唤醒词；不克隆、不模仿任何真实的人或前任的声音。', '',
            '## 声音的来源', '',
            '- “我在”：旧版工程使用项目公开内置素材 `app/bk7258/assets/wake_reply_public.pcm`（Kokoro-82M v1.1-zh，sid 0，非克隆）。最新交付的 9 处“我在”替换为授权板载同源 PCM（SHA-256 `772a8aa9…`，该短 PCM 已按比赛期间授权入库）；两者都不是板子扬声器录音，也不构成新的实机验收。',
            '- 傻妞的其他台词：沿用原版 Kokoro 合成（0.8·sid0 + 0.2·sid8），与公开默认“我在”配套；不是从替换后的授权 PCM 克隆或重生成。真实设备上的回答来自用户配置的云端语音合成，音色可能不同。',
            '- 周屿（旁白与对白）：剪映 SAMI 配音“温柔男主”（zh_male_iclvop_chenxuanqinggan），见 `audio/voice/audition/REPORT.md`。', '']
    (ROOT / 'FEATURE_MAP.md').write_text('\n'.join(out), encoding='utf-8')
    print('FEATURE_MAP.md:', len(L), 'features,', sum(1 for f in L if f['id'] in shown), 'shown,', len(NOT_SHOWN), 'explained as not shown')


if __name__ == '__main__':
    main()
