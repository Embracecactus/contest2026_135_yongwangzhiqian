// SPDX-License-Identifier: Apache-2.0
// What the phone (Shaniu App, chat) and the monitor (3D software, terminal,
// AI assistant, Explorer, router page, browser workbench) show at time t.
// One timeline drives both the 3D screens and the readable 2D inserts.
// App screens follow android/shaniu-companion; terminal lines quote the real
// CLI (tools/bk7258/bk7258.py) and its JSON receipts; waits are edited.
import { clamp, range } from './util.js';

const typed = (s, lt, cps = 6) => s.slice(0, Math.max(0, Math.floor(lt * cps)));
const D = (theme, time, extra) => ({ theme, time, ...extra });

// ---------------------------------------------------------------- phone
const CHAT_QING = [{ date: '8月12日 21:07' }, { text: '我明天上午的车。' }, { text: '我送你。', me: true }, { text: '不用了。' }, { text: '照顾好自己。' }];
const DELETE_CMD = 'Bash: rm -rf "/d/照片/2024 · 和晴"';
const MOVE_CMD = 'Bash: mv "/d/照片/2024 · 和晴" "/e/归档/"';
export const COMMANDS = { DELETE_CMD, MOVE_CMD };

const PHONE = [
  // s01: "在吗？" typed and deleted, twice.
  [0, (t) => {
    let draft = '';
    if (t >= 10.4 && t < 12.3) draft = typed('在吗？', t - 10.4, 4);
    else if (t >= 12.3 && t < 12.8) draft = '在吗？'.slice(0, Math.max(0, 3 - Math.floor((t - 12.3) * 7)));
    else if (t >= 13.3 && t < 15.3) draft = typed('在吗？', t - 13.3, 4);
    else if (t >= 15.3 && t < 15.8) draft = '在吗？'.slice(0, Math.max(0, 3 - Math.floor((t - 15.3) * 7)));
    return { screen: 'chat', ...D('dark', '23:41'), contact: '晴', messages: CHAT_QING, draft, keyboard: true, lt: t };
  }],
  [17.6, () => ({ off: true })],
  // s04: claim and provisioning (dark theme at night, F36).
  [52.0, (t) => ({ screen: 'home', ...D('dark', '00:06'), claimed: false, tapK: clamp((t - 53.8) / 0.4) })],
  [54.4, (t) => ({ screen: 'scan', ...D('dark', '00:06'), lt: t - 54.4 })],
  [57.0, (t) => ({ screen: 'verify', ...D('dark', '00:07'), state: t < 60.0 ? 'pending' : 'ok', lt: t - 57.0, anim: t < 60.0 })],
  [61.6, (t) => ({ screen: 'wifi', ...D('dark', '00:07'), picked: t >= 62.6, dots: Math.min(8, Math.max(0, Math.floor((t - 63.2) * 5.5))), tapK: clamp((t - 66.2) / 0.4) })],
  [67.0, (t) => ({ screen: 'cloud', ...D('dark', '00:08'), state: t < 68.7 ? null : (t < 69.8 ? 'saving' : 'ok'), tapK: clamp((t - 68.4) / 0.4), lt: t - 68.7, anim: t >= 68.7 && t < 69.8 })],
  [71.0, () => ({ screen: 'home', ...D('dark', '00:08'), claimed: true, mood: 'awake' })],
  [74.0, () => ({ off: true })],
  // s06: persona, memory, the wake reply that cannot change, the door card.
  [107.0, (t) => ({ screen: 'persona', ...D('dark', '21:12'), sel: t >= 110.2 ? 4 : 0 })],
  [112.0, (t) => ({ screen: 'memory', ...D('dark', '21:12'), on: t >= 113.0 })],
  [115.4, () => ({ off: true })],
  [136.0, () => ({ screen: 'wakeReply', ...D('dark', '21:20') })],
  [143.6, (t) => ({ screen: 'scenes', ...D('dark', '21:21'), sel: t >= 145.0 ? 4 : null, state: t < 145.2 ? null : (t < 147.6 ? 'wait' : 'ok'), label: '门禁卡 1203 · 播放四拍节律' })],
  [149.0, () => ({ off: true })],
  // s07: sensitivity at 02:13; answer settings by day; PC authorisation; update.
  [161.0, (t) => ({ screen: 'sensitivity', ...D('dark', '02:13'), value: t < 162.6 ? 0.85 : 0.85 - 0.05 * clamp((t - 162.6) / 0.5) })],
  [164.6, () => ({ off: true })],
  [199.0, (t) => ({ screen: 'answer', ...D('light', '16:05'), len: t >= 200.6 ? 0 : 1, fast: t >= 202.0 })],
  [204.0, () => ({ off: true })],
  [232.6, (t) => ({ screen: 'pcAuth', ...D('dark', '23:20'), checked: t < 234.6 ? 0 : (t < 235.0 ? 1 : (t < 235.4 ? 2 : 3)), exported: t >= 235.6, tapK: clamp((t - 235.4) / 0.3) })],
  [236.4, () => ({ off: true })],
  [248.0, (t) => ({ screen: 'update', ...D('dark', '19:42'), current: '0.7.128 · 774', phase: 0, tapK: clamp((t - 252.0) / 0.3) })],
  [252.3, () => ({ off: true })],
  [261.3, (t) => ({ screen: 'update', ...D('dark', '19:46'), current: '0.7.128 · 774', phase: t < 262.6 ? 1 : (t < 265.6 ? 2 : 3), lt: t, anim: t < 265.6 })],
  [267.0, () => ({ off: true })],
  // s08: the delete request, left to run out.
  [285.6, (t) => ({ screen: 'approval', ...D('dark', '01:37'), target: 'D:\\照片\\2024 · 和晴（38.2 GB）', command: DELETE_CMD, state: 'pending', left: 118 - (t - 285.6) * 0.6 })],
  [290.0, () => ({ off: true })],
  [294.4, (t) => ({ screen: 'approval', ...D('dark', '01:39'), target: 'D:\\照片\\2024 · 和晴（38.2 GB）', command: DELETE_CMD, state: t < 297.8 ? 'pending' : 'expired', left: Math.max(0, 3.4 - (t - 294.4)) })],
  [299.6, () => ({ off: true })],
  // s09: transfer / factory reset, not confirmed.
  [318.6, (t) => ({ screen: 'reset', ...D('dark', '03:04'), tap: t >= 323.2 ? 'cancel' : null, tapK: clamp((t - 323.2) / 0.3) })],
  [324.6, () => ({ off: true })],
  // s10: no network; the torch.
  [345.6, () => ({ screen: 'lock', ...D('dark', '23:52'), date: '2月20日 星期六' })],
  [347.6, () => ({ off: true })],
  // s11: Wi-Fi renamed → device offline → change Wi-Fi without re-claiming.
  [412.4, (t) => ({ screen: 'home', ...D('light', '06:44'), claimed: true, mood: 'asleep', wifi: false, tapWifi: t >= 414.2 })],
  [414.6, (t) => ({ screen: 'wifi', ...D('light', '06:44'), nets: ['屿的家', 'ChinaNet-5G2', 'TP-LINK_3F12', 'MiWiFi-1203'], picked: t >= 415.0, dots: Math.min(10, Math.max(0, Math.floor((t - 415.4) * 8))), tapK: clamp((t - 417.0) / 0.4) })],
  [417.6, () => ({ screen: 'home', ...D('light', '06:45'), claimed: true, mood: 'awake' })],
  [419.0, () => ({ off: true })],
  [433.6, (t) => {
    const f = [['称呼', t < 435.0 ? '小鱼' : '阿屿'], ['偏好', t < 436.4 ? '加两勺糖的咖啡' : '不加糖的美式'], ['近期话题', '灯塔']];
    const editing = t < 435.0 ? 0 : (t < 436.4 ? 1 : null);
    return { screen: 'facts', ...D('light', '07:05'), facts: f, editing, draft: editing === 0 ? '阿屿' : (editing === 1 ? '不加糖的美式' : undefined), state: t < 436.8 ? null : (t < 437.4 ? 'saving' : 'ok'), lt: t, anim: t >= 436.8 && t < 437.4 };
  }],
  [439.6, (t) => ({ screen: 'clearRecent', ...D('light', '07:06'), done: t >= 440.2 })],
  [441.2, (t) => ({ screen: 'persona', ...D('light', '07:06'), sel: t >= 442.4 ? 1 : 4 })],
  [443.6, () => ({ off: true })],
  [452.6, (t) => ({ screen: 'scenes', ...D('light', '07:31'), sel: t >= 453.4 ? 0 : 4, state: t < 453.6 ? null : (t < 455.0 ? 'wait' : 'ok'), label: '门禁卡 1203 · 开始专注' })],
  [456.0, () => ({ off: true })],
  // s12: reject the delete, approve the archive (phone decides; F40/F41).
  [489.8, (t) => ({ screen: 'approval', ...D('light', '10:20'), target: 'D:\\照片\\2024 · 和晴（38.2 GB）', command: DELETE_CMD, state: t < 492.6 ? 'pending' : 'rejected', left: 119 - (t - 489.8), tap: 'reject', tapK: clamp((t - 492.3) / 0.3) })],
  [494.0, () => ({ off: true })],
  [500.2, (t) => ({ screen: 'approval', ...D('light', '10:22'), target: 'D:\\照片\\2024 · 和晴 → E:\\归档\\', command: MOVE_CMD, state: t < 503.0 ? 'pending' : 'approved', left: 119 - (t - 500.2), tap: 'approve', tapK: clamp((t - 502.7) / 0.3) })],
  [504.4, () => ({ off: true })],
  // s13: 阿川 asks; he answers.
  [524.2, (t) => ({ screen: 'chat', ...D('light', '08:30'), contact: '阿川', avatar: '川',
    messages: [{ date: '上午 8:30' }, { text: '在吗？下周的独立动画放映，《灯塔》还来得及吗？' }, ...(t >= 529.2 ? [{ text: '我在。来得及。', me: true }] : [])],
    draft: t < 529.2 ? typed('我在。来得及。', t - 526.6, 3.2) : '', keyboard: t >= 525.8 && t < 529.4, lt: t })],
  [531.0, () => ({ off: true })],
];

export function phoneAt(t, shot, w) {
  let k = null;
  for (const e of PHONE) { if (e[0] <= t) k = e; else break; }
  if (!k) return { off: true };
  return k[1](t);
}

// ---------------------------------------------------------------- monitor
const PROMPT = (text, at, typing = true) => ({ text, at, prompt: true, typing });
const OUT = (text, at, color) => ({ text, at, color });
const BUILD = 'python3 tools/bk7258/bk7258.py build --board aidk_ai_toy --boot mcuboot --development-identity --rollback-floor 1 --jobs 4';
const PAIR_START = 'bk7258.py workbench pair-start --request pc-request.json --pending pc-pending.bin --allow resources tasks camera';
const PAIR_FINISH = 'bk7258.py workbench pair-finish --pending pc-pending.bin --response pc-response.bin --profile 屿的电脑.json --confirm-device-sha256 3f2a…91c4';
const TASK = (args) => `bk7258.py workbench task-run --profile 屿的电脑.json --task-id 5f1c…e2 --exec blender -b 灯塔.blend ${args}`;

const MON = [
  [0, () => ({ app: 'lighthouse', frame: 1024, status: '已停止' })],
  [46.6, (t) => ({ app: 'terminal', lt: t - 46.6, lines: [PROMPT(BUILD, 0.1), OUT('build manifest=out/bk7258/aidk_ai_toy/app__openvela_ap/…/build-manifest.json', 1.4, '#7cf0bf')] })],
  [48.6, () => ({ app: 'lighthouse', frame: 1024, status: '已停止' })],
  [231.0, (t) => ({ app: 'terminal', lt: t - 231.0, lines: [PROMPT(PAIR_START, 0.4), OUT('{ "request_sha256": "3f2a91c47d0e…", "expires_at_ms": 1795449600000, "device_authorization_verified": false }', 1.6)] })],
  [236.4, (t) => ({ app: 'terminal', lt: t - 236.4, lines: [PROMPT(PAIR_FINISH, 0.0, false), OUT('{ "profile_saved": true, "device_authorization_verified": false }', 0.1, '#7cf0bf'),
    PROMPT(TASK('-s 1 -e 1024 -a'), 1.0), OUT('Fra:1 Mem:812M | 灯塔 | 渲染中…', 2.4), OUT('Fra:286 Mem:830M | 灯塔 | 渲染中…', 3.2)] })],
  [240.6, () => ({ app: 'off' })],
  [244.0, (t) => ({ app: 'terminal', lt: t - 244.0, lines: [PROMPT(TASK('-s 1 -e 1024 -a'), 0, false), OUT('Fra:1024 | 灯塔 | 完成', 0.0),
    OUT('{ "process_state": "success", "process_returncode": 0, "device_result": "confirmed" }', 1.8, '#7cf0bf')] })],
  [248.0, () => ({ app: 'lighthouse', frame: 1024, status: '1–1024 已渲染' })],
  // s08: the disk is full; the assistant asks; the phone must decide.
  [272.6, () => ({ app: 'dialog', frame: 1024 })],
  [276.6, (t) => ({ app: 'assistant', lt: t - 276.6, wait: 5.6, messages: [
    { who: 'me', text: '帮我清理一下硬盘，腾出 40 GB。', at: 0.2, typing: true },
    { who: 'ai', text: '最大的文件夹是 D:\\照片\\2024 · 和晴（38.2 GB）。删除命令需要你在手机上批准。', at: 2.4, typing: true }] })],
  [297.8, (t) => ({ app: 'assistant', lt: 10, messages: [
    { who: 'me', text: '帮我清理一下硬盘，腾出 40 GB。' },
    { who: 'ai', text: '审批已过期，命令未执行。文件夹还在。' }] })],
  [300.0, () => ({ app: 'explorer', drive: 'D:', path: 'D:\\照片', dFree: '1.6 GB 可用', rows: [{ name: '2024 · 和晴', size: '38.2 GB', hl: true }, { name: '2023', size: '6.1 GB' }, { name: '截图', size: '0.4 GB' }] })],
  [304.0, () => ({ app: 'off' })],
  [334.0, () => ({ app: 'lighthouse', frame: 1024, status: '已停止' })],
  [344.6, () => ({ app: 'off' })],
  // s11: router renamed and re-keyed; new eyes in the workbench.
  [409.2, (t) => ({ app: 'router', lt: t - 409.6 })],
  [413.0, () => ({ app: 'lighthouse', frame: 1024, status: '已停止' })],
  [456.6, (t) => ({ app: 'workbench', panel: 'eyes', step: t < 461.6 ? 1 : (t < 468.6 ? 2 : 3), lt: t < 461.6 ? t - 459.8 : t - 461.6 })],
  [471.6, () => ({ app: 'lighthouse', frame: 2210, status: '继续制作', lit: true })],
  // s12: the last shots, the final render, then the photos: reject, archive.
  [477.0, (t) => ({ app: 'lighthouse', frame: Math.min(2400, Math.round(2210 + (t - 477.0) * 44)), status: '制作中', lit: true })],
  [482.8, (t) => ({ app: 'terminal', lt: t - 482.8, lines: [PROMPT(TASK('-a'), 0.2), OUT('Fra:2400 | 灯塔 | 完成', 2.6),
    OUT('{ "process_state": "success", "process_returncode": 0, "device_result": "confirmed" }', 4.0, '#7cf0bf')] })],
  [488.0, (t) => ({ app: 'assistant', lt: t - 488.0, wait: 1.4, messages: [
    { who: 'me', text: '导出成片还差 40 GB。', at: 0, typing: false },
    { who: 'ai', text: '要删除 D:\\照片\\2024 · 和晴（38.2 GB）吗？需要你在手机上批准。', at: 0.2, typing: true }] })],
  [492.6, () => ({ app: 'assistant', lt: 10, messages: [
    { who: 'ai', text: '要删除 D:\\照片\\2024 · 和晴（38.2 GB）吗？' },
    { who: 'ai', text: '已拒绝，命令未执行（退出码 2）。' }] })],
  [495.2, () => ({ app: 'explorer', drive: 'D:', path: 'D:\\照片', dFree: '1.6 GB 可用', rows: [{ name: '2024 · 和晴', size: '38.2 GB', hl: true }, { name: '2023', size: '6.1 GB' }, { name: '截图', size: '0.4 GB' }], note: '已拒绝删除 · 文件夹原样保留' })],
  [496.6, (t) => ({ app: 'assistant', lt: t - 496.6, wait: 3.4, messages: [
    { who: 'me', text: '不删。挪到移动硬盘，归档。', at: 0.2, typing: true },
    { who: 'ai', text: '好，移动到 E:\\归档\\。需要你在手机上批准。', at: 2.2, typing: true }] })],
  [503.0, (t) => ({ app: 'assistant', lt: 10, messages: [
    { who: 'me', text: '不删。挪到移动硬盘，归档。' },
    { who: 'ai', text: '已批准，执行完成（退出码 0）。' }] })],
  [504.4, () => ({ app: 'explorer', drive: 'E:', path: 'E:\\归档', dFree: '39.8 GB 可用', dLow: false, eFree: '774 GB 可用', rows: [{ name: '2024 · 和晴', size: '38.2 GB', hl: true }], note: '已归档 · 一张照片也没有删', noteOk: true })],
  [508.6, (t) => ({ app: 'workbench', panel: 'camera', waiting: t >= 509.4 && t < 510.6, frame: t >= 510.6, saved: t >= 514.0, lt: t - 508.6 })],
  [516.4, () => ({ app: 'lighthouse', frame: 2400, status: '渲染完成', statusColor: '#7cf0bf', lit: true })],
];

export function monitorAt(t, w) {
  let k = null;
  for (const e of MON) { if (e[0] <= t) k = e; else break; }
  return k ? k[1](t) : { app: 'off' };
}
