// SPDX-License-Identifier: Apache-2.0
// The monitor: 周屿's 3D software with the unfinished short 《灯塔》, a terminal
// running the real workbench CLI (tools/bk7258/bk7258.py workbench …, JSON
// receipts as printed by workbench_task_runner.py / workbench_approval.py),
// an AI assistant pane, the router page, and the browser workbench
// (tools/bk7258/_lib/workbench_web: "傻妞 · 电脑工作台").
import { clamp, easeOut, smooth } from './util.js';

const SANS = '"Noto Sans SC", sans-serif';
const MONO = 'monospace';

function rr(g, x, y, w, h, r, fill, stroke, lw = 2) {
  g.beginPath(); g.roundRect(x, y, w, h, r);
  if (fill) { g.fillStyle = fill; g.fill(); }
  if (stroke) { g.strokeStyle = stroke; g.lineWidth = lw; g.stroke(); }
}
function txt(g, s, x, y, size, color, weight = 400, font = SANS, align = 'left') {
  g.font = `${weight} ${size}px ${font}`; g.fillStyle = color; g.textAlign = align; g.textBaseline = 'alphabetic'; g.fillText(s, x, y);
}
function wrap(g, s, x, y, size, color, maxW, lh = 1.5, weight = 400, font = SANS) {
  g.font = `${weight} ${size}px ${font}`; g.fillStyle = color; g.textAlign = 'left';
  let line = '', yy = y;
  for (const ch of s) {
    if (ch === '\n') { g.fillText(line, x, yy); line = ''; yy += size * lh; continue; }
    if (g.measureText(line + ch).width > maxW && line) { g.fillText(line, x, yy); line = ch; yy += size * lh; } else line += ch;
  }
  if (line) g.fillText(line, x, yy);
  return yy + size * lh;
}

// Stylised viewport of his short film: a lighthouse on a rock at dusk.
export function drawLighthouse(g, x, y, w, h, lit, t) {
  g.save(); g.beginPath(); g.rect(x, y, w, h); g.clip();
  const sky = g.createLinearGradient(0, y, 0, y + h);
  sky.addColorStop(0, lit ? '#1b2847' : '#202838'); sky.addColorStop(0.62, lit ? '#e39a72' : '#5c6474'); sky.addColorStop(1, lit ? '#2a3a5c' : '#2b3240');
  g.fillStyle = sky; g.fillRect(x, y, w, h);
  g.fillStyle = lit ? '#24375a' : '#273040'; g.fillRect(x, y + h * 0.68, w, h * 0.32);
  for (let i = 0; i < 14; i++) { g.strokeStyle = lit ? 'rgba(255,200,150,0.25)' : 'rgba(180,190,200,0.12)'; g.lineWidth = 2; g.beginPath(); const yy = y + h * (0.72 + i * 0.02); g.moveTo(x + w * 0.1 + i * 9, yy); g.lineTo(x + w * 0.9 - i * 13, yy); g.stroke(); }
  g.fillStyle = '#151a22'; g.beginPath(); g.moveTo(x + w * 0.52, y + h * 0.7); g.quadraticCurveTo(x + w * 0.62, y + h * 0.58, x + w * 0.76, y + h * 0.62); g.lineTo(x + w * 0.86, y + h * 0.72); g.closePath(); g.fill();
  const tx = x + w * 0.67, ty = y + h * 0.3, tw = w * 0.04, th = h * 0.31;
  g.fillStyle = '#e9e3d8'; g.beginPath(); g.moveTo(tx - tw * 0.6, ty + th); g.lineTo(tx - tw * 0.42, ty); g.lineTo(tx + tw * 0.42, ty); g.lineTo(tx + tw * 0.6, ty + th); g.closePath(); g.fill();
  g.fillStyle = '#b8473a'; for (let i = 0; i < 3; i++) g.fillRect(tx - tw * 0.5, ty + th * (0.2 + i * 0.27), tw, th * 0.09);
  g.fillStyle = '#2a2d33'; g.fillRect(tx - tw * 0.5, ty - h * 0.045, tw, h * 0.045);
  if (lit) {
    const a = Math.sin(t * 0.9) * 0.5;
    g.fillStyle = 'rgba(255,236,170,0.95)'; g.beginPath(); g.arc(tx, ty - h * 0.022, h * 0.016, 0, 7); g.fill();
    const beam = g.createLinearGradient(tx, ty, tx - w * 0.6, ty);
    beam.addColorStop(0, 'rgba(255,236,170,0.55)'); beam.addColorStop(1, 'rgba(255,236,170,0)');
    g.fillStyle = beam; g.beginPath(); g.moveTo(tx, ty - h * 0.022); g.lineTo(tx - w * 0.7, ty - h * 0.12 + a * h * 0.1); g.lineTo(tx - w * 0.7, ty + h * 0.08 + a * h * 0.1); g.closePath(); g.fill();
  } else { g.fillStyle = '#3a3d44'; g.beginPath(); g.arc(tx, ty - h * 0.022, h * 0.014, 0, 7); g.fill(); }
  g.restore();
}

function chrome(g, W, H, title, dark = true) {
  g.fillStyle = dark ? '#16181d' : '#e9ecef'; g.fillRect(0, 0, W, H);
  g.fillStyle = dark ? '#0f1014' : '#d6dade'; g.fillRect(0, H - 54, W, 54);
  txt(g, title, 28, H - 18, 22, dark ? '#9aa3ad' : '#4a5258', 500);
}

const APPS = {};
APPS.lighthouse = (g, W, H, m, t) => {
  chrome(g, W, H, '灯塔.blend — 三维动画');
  rr(g, 24, 24, W - 48, H - 210, 10, '#1d2027');
  drawLighthouse(g, 40, 40, W - 80, H - 250, m.lit, t);
  // Timeline with the stuck frame.
  rr(g, 24, H - 170, W - 48, 100, 10, '#20232a');
  const f = m.frame ?? 1024, total = 2400;
  g.fillStyle = '#2f343d'; g.fillRect(60, H - 115, W - 120, 10);
  g.fillStyle = '#5aa0d8'; g.fillRect(60, H - 115, (W - 120) * f / total, 10);
  txt(g, `第 ${f} / ${total} 帧`, 60, H - 132, 30, '#cfd6de', 600);
  if (m.status) txt(g, m.status, W - 60, H - 132, 28, m.statusColor || '#9aa3ad', 500, SANS, 'right');
};
APPS.terminal = (g, W, H, m, t) => {
  chrome(g, W, H, '终端 — 屿的电脑');
  rr(g, 24, 24, W - 48, H - 102, 10, '#0d0f12');
  let y = 84;
  const lt = m.lt ?? 0;
  for (const line of m.lines || []) {
    if (line.at != null && lt < line.at) continue;
    let s = line.text;
    if (line.typing && line.at != null) s = s.slice(0, Math.floor((lt - line.at) * 34));
    const col = line.color || (line.prompt ? '#e8edf2' : '#9fb3c8');
    if (line.prompt) txt(g, '$', 60, y, 38, '#7cf0bf', 600, MONO);
    y = wrap(g, s, line.prompt ? 104 : 60, y, line.size || 36, col, W - 170, 1.4, 500, line.mono === false ? SANS : MONO) + 10;
  }
};
APPS.assistant = (g, W, H, m, t) => {
  chrome(g, W, H, 'AI 助手 — 终端会话');
  rr(g, 24, 24, W - 48, H - 102, 10, '#111318');
  let y = 90;
  const lt = m.lt ?? 0;
  for (const msg of m.messages || []) {
    if (msg.at != null && lt < msg.at) continue;
    let s = msg.text;
    if (msg.typing) s = s.slice(0, Math.floor((lt - msg.at) * 18));
    const me = msg.who === 'me';
    txt(g, me ? '屿' : 'AI 助手', 60, y, 24, me ? '#7cf0bf' : '#f2c27b', 600);
    y = wrap(g, s, 60, y + 44, 34, me ? '#f2efea' : '#d8dee6', W - 180, 1.45) + 20;
  }
  if (m.wait && lt >= m.wait) {
    rr(g, 60, y, W - 140, 74, 14, '#2e2416');
    txt(g, '等待手机批准… 傻妞已提醒', 90, y + 48, 30, '#f2c27b', 600);
  }
};
APPS.dialog = (g, W, H, m, t) => {
  APPS.lighthouse(g, W, H, { frame: m.frame, lit: false }, t);
  g.fillStyle = 'rgba(0,0,0,0.45)'; g.fillRect(0, 0, W, H);
  rr(g, W / 2 - 430, H / 2 - 150, 860, 280, 18, '#23262d', '#3a3f48');
  txt(g, '渲染已停止', W / 2 - 380, H / 2 - 70, 40, '#f2efea', 700);
  txt(g, 'D 盘空间不足，无法继续渲染。', W / 2 - 380, H / 2 - 4, 34, '#ffb4ab', 500);
  txt(g, '需要约 40 GB 可用空间', W / 2 - 380, H / 2 + 52, 28, '#9aa3ad', 400);
};
APPS.router = (g, W, H, m, t) => {
  chrome(g, W, H, '浏览器 — 路由器设置', false);
  rr(g, 24, 24, W - 48, H - 102, 10, '#ffffff', '#d6dade');
  txt(g, '无线设置', 80, 120, 46, '#1d2228', 700);
  const lt = m.lt ?? 0;
  const name = lt < 1.0 ? '屿和晴的家' : '屿的家';
  txt(g, 'Wi-Fi 名称', 80, 220, 28, '#5a636c', 500); rr(g, 80, 240, 760, 76, 10, '#f5f7f9', '#c9d1d8'); txt(g, name, 104, 292, 34, '#1d2228', 500);
  txt(g, 'Wi-Fi 密码', 80, 380, 28, '#5a636c', 500); rr(g, 80, 400, 760, 76, 10, '#f5f7f9', '#c9d1d8');
  const dots = lt < 1.6 ? 8 : Math.min(10, Math.floor((lt - 1.6) * 8));
  txt(g, '•'.repeat(dots), 104, 452, 34, '#1d2228', 500);
  rr(g, 80, 530, 220, 74, 37, lt > 2.8 ? '#2b8a57' : '#1d2228'); txt(g, lt > 2.8 ? '已保存' : '保存', 190, 578, 30, '#ffffff', 600, SANS, 'center');
};
APPS.workbench = (g, W, H, m, t) => {
  // Light theme of tools/bk7258/_lib/workbench_web/style.css.
  const C = { bg: '#f2f6f3', card: '#ffffff', ink: '#16352e', green: '#24775e', muted: '#526c63', line: '#cadbd2', soft: '#e0efe7' };
  chrome(g, W, H, '浏览器 — 傻妞 · 电脑工作台', false);
  g.fillStyle = C.bg; g.fillRect(24, 24, W - 48, H - 102);
  txt(g, '插上电脑，换一种陪伴。', 70, 104, 44, C.ink, 700);
  txt(g, '拔掉电脑后，她仍可独立运行。', 72, 150, 24, C.muted, 400);
  const lt = m.lt ?? 0;
  if (m.panel === 'eyes') {
    const step = m.step ?? 1;
    const cards = [['01', '导入眼睛包', '仅导入，不自动设为默认。设备确认安装后，再选择试用。'], ['02', '先试用，再决定', '读取设备目录，选择想试用的素材。试用不会改变默认素材。'], ['03', '设为默认', '先读取并刷新设备保存的默认配置，再确认保存。保存和画面更新分别回读。']];
    cards.forEach(([n, title, body], i) => {
      const x = 70 + i * 600, y = 190, w = 570, h = 560;
      rr(g, x, y, w, h, 18, C.card, step - 1 === i ? C.green : C.line, step - 1 === i ? 4 : 2);
      txt(g, n, x + 30, y + 60, 30, C.green, 700); txt(g, title, x + 90, y + 60, 36, C.ink, 700);
      wrap(g, body, x + 30, y + 120, 24, C.muted, w - 60);
      if (i === 0) { rr(g, x + 30, y + 250, w - 60, 70, 12, C.soft); txt(g, '晨光眼睛.bkep', x + 54, y + 296, 28, C.ink, 600); if (step >= 1 && lt > 0.6) txt(g, '设备已安装 · pack_id chenguang · revision 1', x + 30, y + 370, 22, C.green, 600); }
      if (i === 1) { rr(g, x + 30, y + 250, w - 60, 70, 12, C.soft); txt(g, '试用时长（秒）30 · 表情 平静', x + 54, y + 296, 26, C.ink, 500); if (step >= 2) { rr(g, x + 30, y + 350, 220, 64, 32, C.green); txt(g, '开始试用', x + 140, y + 392, 26, '#fff', 600, SANS, 'center'); } if (step >= 2 && lt > 0.8) txt(g, '试用中 · 到期恢复，不更改默认', x + 30, y + 470, 22, C.green, 600); }
      if (i === 2) { rr(g, x + 30, y + 250, w - 60, 70, 12, C.soft); txt(g, '已安装素材：晨光眼睛', x + 54, y + 296, 26, C.ink, 500); if (step >= 3) { rr(g, x + 30, y + 350, 360, 64, 32, C.green); txt(g, '保存所选素材为默认', x + 210, y + 392, 26, '#fff', 600, SANS, 'center'); } if (step >= 3 && lt > 0.8) txt(g, '设备已保存默认 · 画面已回读', x + 30, y + 470, 22, C.green, 600); }
    });
  } else if (m.panel === 'camera') {
    rr(g, 70, 190, W - 140, 740, 18, C.card, C.line, 2);
    txt(g, '按需单帧取景', 110, 256, 40, C.ink, 700);
    wrap(g, '每次点击只采集一帧，显示在本机。需要 App 明确授予电脑“按需相机”权限；不会自动识别、上传或保存。', 110, 310, 24, C.muted, 900);
    const btn = (label, x, on) => { rr(g, x, 400, 260, 70, 35, on ? C.green : C.soft); txt(g, label, x + 130, 446, 28, on ? '#fff' : C.ink, 600, SANS, 'center'); };
    btn('采一帧', 110, !m.frame); btn('保存到本机', 400, !!m.frame && !m.saved); btn('释放设备缓存', 690, false);
    if (m.waiting) txt(g, '单帧传输中…（这不是 UVC 视频）', 110, 540, 26, C.muted, 500);
    if (m.frame && m.frameCanvas) { g.drawImage(m.frameCanvas, 1100, 300, 640, 480); g.strokeStyle = C.line; g.lineWidth = 2; g.strokeRect(1100, 300, 640, 480); txt(g, '本次明确请求的设备相机画面 · 640×480', 1100, 820, 22, C.muted, 500); }
    if (m.saved) { rr(g, 110, 600, 860, 80, 14, C.soft); txt(g, '已保存到本机：2027 / 第一张.jpg', 140, 652, 30, C.green, 700); }
  }
};
// File Explorer: where the photo folder is, and where it ends up.
APPS.explorer = (g, W, H, m, t) => {
  chrome(g, W, H, '文件资源管理器', false);
  rr(g, 24, 24, W - 48, H - 102, 10, '#ffffff', '#d6dade');
  // Drives.
  const drives = [['D:', '本地磁盘', m.dFree ?? '1.6 GB 可用', m.dLow !== false], ['E:', '移动硬盘 · 归档', m.eFree ?? '812 GB 可用', false]];
  drives.forEach(([d, name, free, low], i) => {
    const x = 70 + i * 560, y = 70;
    rr(g, x, y, 520, 120, 14, m.drive === d ? '#e8f1fb' : '#f5f7f9', m.drive === d ? '#5b9bd5' : '#d6dade', 2);
    txt(g, `${name}（${d}）`, x + 28, y + 50, 32, '#1d2228', 600);
    txt(g, free, x + 28, y + 96, 26, low ? '#c0392b' : '#4a8a5a', 600);
  });
  txt(g, m.path || 'D:\\照片', 70, 270, 34, '#5a636c', 500);
  const rows = m.rows || [];
  rows.forEach((r, i) => {
    const y = 300 + i * 96;
    rr(g, 60, y, W - 120, 84, 12, r.hl ? '#fff4e0' : (i % 2 ? '#fafbfc' : '#ffffff'), r.hl ? '#f0b44c' : null, 2);
    g.fillStyle = '#f2c94c'; g.beginPath(); g.roundRect(90, y + 22, 52, 40, 6); g.fill();
    txt(g, r.name, 170, y + 54, 34, '#1d2228', r.hl ? 700 : 500);
    txt(g, r.size || '', W - 120, y + 54, 30, '#5a636c', 500, SANS, 'right');
  });
  if (m.note) { rr(g, 60, H - 210, W - 120, 84, 14, m.noteOk ? '#e7f6ec' : '#fdecea'); txt(g, m.note, 90, H - 156, 32, m.noteOk ? '#1f7a44' : '#b03a2e', 600); }
};
APPS.off = (g, W, H) => { g.fillStyle = '#050506'; g.fillRect(0, 0, W, H); };

export function drawMonitorInto(g, W, H, mon, t) {
  g.save();
  (APPS[mon.app] || APPS.off)(g, W, H, mon, t);
  g.restore();
}

export function drawMonitor(canvas, mon, t, film) {
  const key = JSON.stringify(mon, (k, v) => (k === 'frameCanvas' ? undefined : v)) + (mon.anim ? Math.floor(t * 12) : '');
  if (canvas.__key === key) return key;
  canvas.__key = key;
  drawMonitorInto(canvas.getContext('2d'), canvas.width, canvas.height, mon, t);
  return key;
}
