#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""STORYBOARD.md: every shot with its film time, the line that plays over it,
and what it shows. Shot ids/times come from the film itself (script/shots.json,
exported from src/scenes*.js in the browser); descriptions live here.

Usage: python3 tools/build_storyboard.py
"""
import json, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from editlib import load_edit, film_from_story  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
DESC = {
 's01a': '雨水爬过窗玻璃，镜头退回室内（角标：人物与情节纯属虚构｜产品交互为动画演绎）', 's01b': '空着的小鱼杯 → 显示器上停在第 1024 帧的灯塔，便签“按时吃饭”',
 's01c': '手机：与晴的最后几条消息；“在吗？”打了又删，两次', 's01d': '手机扣在桌上',
 's02a': '门边快递盒：AI 开发板套件 · 预售 · 7 月下单 · 11 月 14 日送达', 's02b': '他把盒子抱上桌', 's02c': '俯拍开箱：裸板、亚克力、螺柱、线、卡片',
 's02d': '主观：双手捧着卡片“生日快乐。以后有人提醒你按时睡觉了。——晴”', 's02e': '过肩：他把裸板举到脸前，两块黑着的圆屏像一双眼睛——决定',
 's02f': '他的脸：看了很久，很小的点头', 's03a': '抽屉里的门禁卡 1203，他停了一下', 's03b': '拧螺柱、夹亚克力（手指在板角）',
 's03c': '终端：bk7258.py build --board aidk_ai_toy …（构建清单）', 's03d': '插上 Type-C', 's03e': '两块圆屏亮起认领码',
 's04a': 'App：让傻妞，回到你身边 → 扫码连接', 's04b': '手机扫描圆屏上的认领码', 's04c': '正在连接并验证设备 → 已验证连接傻妞',
 's04d': '设置网络：屿和晴的家，八个点的密码', 's04e': '云服务与模型：听/想/说，等待回读 → 就绪', 's04f': '连接链亮起', 's04g': '认领码淡去，眼睛睁开、眨眼',
 's05a': '他把她立到晴的电脑原来的位置（放稳：半眨眼）', 's05b': '过肩：你好，openvela → 她看向一侧 → 我在', 's05c': '他愣住', 's05d': '……你好',
 's05e': '她说话（speaking）', 's05f': '他问：你……怎么听见我的？', 's05g': '镜头穿过她的眼睛', 's05h': '插段：本地唤醒、我在、RPMsg、CP 出门、云端听想说、流回来、断网时 AP 还醒着',
 's05i': '回到书桌，他嘴角动了一下', 's06a': 'App：轻轻嘴硬', 's06b': 'App：开启跨重启记忆', 's06c': '过肩：你好，openvela → 我在', 's06d': '记住，我叫小鱼',
 's06e': '她害羞地回答', 's06f': '小鱼杯里加两勺糖', 's06g': '还有，我喜欢加两勺糖的咖啡', 's06h': '两勺？好吧，记下了', 's06i': '他喝一口，皱眉，又喝一口',
 's06j': 'App：不能修改唤醒应答语', 's06k': 'App：场景卡片 → 播放四拍节律', 's06l': '门禁卡碰她：登记；再碰：四下', 's06m': '他看向门',
 's07a1': '02:13，他低声叫她，没反应', 's07a2': 'App：唤醒灵敏度拨一格', 's07a3': '再叫：我在', 's07a4': '他什么也没说', 's07a5': 'K1 按两下调小音量', 's07a6': '夜里的房间',
 's07b1': '白天：你好，openvela → 我在', 's07b2': '你给我讲讲灯塔吧', 's07b3': '她讲起来', 's07b4': '好了好了……', 's07b5': '托着下巴等她讲完（半双工）', 's07b6': '这就是灯塔的故事 / 结束对话',
 's07b7': 'App：回答简洁、快速对话', 's07c1': '他把绿萝搬到她面前', 's07c2': '你好，openvela → 我在', 's07c3': '看看这个，这是什么？', 's07c4': '她的相机：一帧 640×480',
 's07c5': '这是绿萝……该浇水了', 's07c6': '他浇水', 's07c7': '拿起她擦桌子：她看向一侧；……抱歉', 's07d1': '终端 pair-start', 's07d2': 'App：电脑授权，勾选三项，导出',
 's07d3': 'pair-finish；task-run 渲染', 's07d4': '关灯，夜到清晨', 's07d5': '清晨：绿色对勾，轻轻一震', 's07e1': 'App：开始固件更新——拇指停住',
 's07e2': '插段：B 槽、A 槽保留、数据区不动、验签', 's07e3': '下载、校验', 's07e4': '重启：眼睛暗下又亮', 's07e5': '已核对新版本', 's07e6': '你好，openvela → 我在', 's07e7': '她还是她',
 's08a': '渲染已停止：D 盘空间不足', 's08b': '他请 AI 助手清理；AI 助手要删 2024 · 和晴', 's08c': '她的眼睛：琥珀色“!”和倒计时环', 's08d': '手机：命令原文 rm -rf，拇指悬着',
 's08e': '他的脸', 's08f': '环一格格暗下去', 's08g': '剩余 2 秒 → 已过期 · 未执行', 's08h': '资源管理器：文件夹还在', 's08i': '他关掉显示器',
 's09a': '凌晨三点的房间', 's09b': '……晴，你在吗？', 's09c': '她难过地回答', 's09d': 'App：转交或恢复出厂——他退出', 's09e': '按住 K2 三秒', 's09f': '双眼合上，暗了', 's09g': '那天晚上，我把她关了',
 's10a': '几个星期：她落灰的圆屏，日光来回', 's10b': '雷雨，灯闪，全黑', 's10b2': '手机：Wi-Fi 已断开 · 无网络', 's10c1': '手电下把充电宝插进她的 Type-C',
 's10c2': '按 K2', 's10c3': '她的眼睛亮起——照在他脸上', 's10d1': '你好，openvela？——没有回答', 's10d2': '他等', 's10d3': '……你好，openvela——还是没有', 's10d4': '他垂下眼睛，看见门禁卡',
 's10e1': '门禁卡碰她：四下', 's10e2': '他看着门。门口没有人', 's10e3': '他转回来看她', 's10f1': '捧起她：她看向一侧；手指抹掉灰', 's10f2': '他的侧脸，镜片里是她的眼睛',
 's10g1': '她的光移向桌角', 's10g2': '卡片：以后有人提醒你按时睡觉了', 's10h1': '俯拍：挪开鼠标，把她放到自己键盘旁边 → 她半眨眼', 's10i1': '闪电照见窗台上耷拉的绿萝',
 's10i2': '他把绿萝搬进她的光里，浇水', 's10j1': '他看着她，把头枕下去', 's10j2': '她的眼睛', 's10j3': '他睡着了，她的光在旁边（旁白 N18）',
 's11a': '来电：路由器的灯一格格亮起', 's11b': '他醒来', 's11c': '路由器：屿和晴的家 → 屿的家，新密码', 's11d': 'App：设备未联网 · 连接还在，不必重新认领',
 's11e': '更换 Wi-Fi：屿的家', 's11f': '已联网', 's11g': '过肩（她在他右手边）：你好，openvela → 我在', 's11h': '设备体检：昨晚停电，你还好吗？', 's11i': '她：都正常……小鱼，你呢？',
 's11j': '他停住', 's11k': 'App：小鱼 → 阿屿；不加糖的美式', 's11l': '清空近期对话', 's11m': '聊天风格：活泼俏皮', 's11n': '他用自己的杯子喝黑咖啡',
 's11o': '剪下的绿萝枝插进小鱼杯', 's11p': '门禁卡改登记：开始专注', 's11q': '速写本：一双晨光色的新眼睛', 's11r': '电脑工作台：导入、试用', 's11s': '你好，openvela → 我在',
 's11t': '换成开心表情', 's11u': '新眼睛弯起来', 's11v': '他回去工作，她在旁边', 's12a': '碰卡：专注 25:00', 's12b': '他做最后几个镜头（剪辑压缩）', 's12c': '专注完成：对勾',
 's12d': 'task-run：灯塔 2400 帧', 's12e': '渲染完成：对勾', 's12f': 'AI 助手：要删除 2024 · 和晴 吗？', 's12g': '“!”', 's12h': '手机：拒绝 → 已拒绝 · 未执行',
 's12i': '资源管理器：文件夹原样保留', 's12j': '他打字：不删。挪到移动硬盘，归档', 's12k': '“!”', 's12l': '手机：mv … /e/归档/ → 批准', 's12m': 'E:\\归档：一张照片也没有删（旁白 N23）',
 's12n': '他举起插着新枝的小鱼杯，对她的相机笑', 's12o': '工作台：采到的那一帧，保存到本机', 's12p': '阳光里的房间', 's13a': '晴天，书桌恢复了样子',
 's13b': '阿川：在吗？……来得及吗？他回：我在。来得及。', 's13c': '他的脸，转向她', 's13d': '早啊。她眨了眨眼', 's13e': '亮着的灯塔、新叶、小鱼杯里的枝条', 's13f': '片名“我在”，片尾',
}


def main():
    E = load_edit(); shots = json.loads((ROOT / 'script/shots.json').read_text())
    T = {s['id']: s['text'] for s in json.loads((ROOT / 'audio/voice/zhouyu/narration.json').read_text())['segments']}
    T.update({l['id']: l['text'] for l in json.loads((ROOT / 'audio/voice/shaniu/manifest.json').read_text())['lines']}); T['W'] = '我在。'
    fmt = lambda t: f'{int(t // 60)}:{t % 60:04.1f}'
    out = ['# 分镜表（STORYBOARD）', '', '> 由 `tools/build_storyboard.py` 生成；镜头与时间来自影片本身（`script/shots.json`），时间为成片时间。', '']
    for sc in E['scenes']:
        out += [f"## {sc['id']} {sc['title']}（{fmt(film_from_story(E, sc['t0']))}–{fmt(film_from_story(E, sc['t1']))}）", '', '| 镜头 | 成片时间 | 画面 | 声音 |', '| --- | --- | --- | --- |']
        for sid, t0, t1, cut, ov in shots:
            if not (sc['t0'] <= t0 < sc['t1']): continue
            lines = [L for L in E['lines'] if t0 <= L['at'] < t1]
            snd = ' / '.join(('旁白 ' if L['id'][0] == 'N' else ('傻妞 ' if L['id'][0] in 'SW' else '周屿 ')) + T[L['id']] for L in lines)
            kind = '（插段）' if cut else ('（手机/画面特写）' if ov else '')
            out.append(f"| {sid} | {fmt(film_from_story(E, t0))}–{fmt(film_from_story(E, t1))} | {DESC.get(sid, '')}{kind} | {snd} |")
        out.append('')
    (ROOT / 'STORYBOARD.md').write_text('\n'.join(out), encoding='utf-8')
    print(len(shots), 'shots;', sum(1 for s in shots if s[0] not in DESC), 'without description')


if __name__ == '__main__':
    main()
