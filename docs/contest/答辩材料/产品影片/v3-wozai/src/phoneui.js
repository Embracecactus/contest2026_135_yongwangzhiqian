// SPDX-License-Identifier: Apache-2.0
// The companion App, redrawn for the film from its real strings and design
// tokens (android/shaniu-companion: DESIGN.md, res/values[-night]/colors.xml,
// DevicePage/CustomizePage/SettingsPage/UpdatePage, MainActivity). The layout
// is simplified for legibility on screen; wording is the App's own.
import { clamp, easeOut, smooth } from './util.js';

const SANS = '"Noto Sans SC", sans-serif';
export const THEMES = {
  light: { bg: '#F4F1EC', surface: '#FFFDFA', ink: '#17161B', muted: '#6A6670', onAccent: '#FFFFFF', selected: '#EBE6DF', divider: '#E2DDD5',
    outline: '#C9C3BA', warning: '#8F5200', warnSurface: '#FAEFD9', danger: '#B3261E', dangerSurface: '#FBE9E7', eye: '#4FD8A0', eyeInk: '#127A56', bezel: '#1D1D22', stage: '#0C0C0F' },
  dark: { bg: '#0E0E11', surface: '#18181C', ink: '#F2EFEA', muted: '#A29EA7', onAccent: '#121215', selected: '#26262C', divider: '#2B2B31',
    outline: '#3A3A42', warning: '#F2C27B', warnSurface: '#2E2416', danger: '#FFB4AB', dangerSurface: '#3A1F1D', eye: '#7CF0BF', eyeInk: '#7CF0BF', bezel: '#2A2A30', stage: '#0C0C0F' },
};

// ---------------------------------------------------------------- kit
class UI {
  constructor(g, W, H, theme) {
    this.g = g; this.W = W; this.H = H; this.s = W / 393; this.c = THEMES[theme] || THEMES.light; this.y = 0;
  }
  d(v) { return v * this.s; }
  font(size, weight = 400) { this.g.font = `${weight} ${this.d(size)}px ${SANS}`; }
  text(str, x, y, size, color, weight = 400, align = 'left', maxW = null) {
    const g = this.g; this.font(size, weight); g.fillStyle = color; g.textAlign = align; g.textBaseline = 'alphabetic';
    if (maxW) {
      // Wrap Chinese text by measured width.
      let line = '', yy = y; const lh = this.d(size * 1.45), lines = [];
      for (const ch of str) {
        if (ch === '\n') { lines.push(line); line = ''; continue; }
        if (g.measureText(line + ch).width > this.d(maxW) && line) { lines.push(line); line = ch; } else line += ch;
      }
      if (line) lines.push(line);
      lines.forEach((l, i) => g.fillText(l, this.d(x), this.d(y) + i * lh));
      return lines.length * size * 1.45;
    }
    g.fillText(str, this.d(x), this.d(y));
    return size * 1.45;
  }
  rect(x, y, w, h, r, color, stroke = null, lw = 1) {
    const g = this.g; g.beginPath(); g.roundRect(this.d(x), this.d(y), this.d(w), this.d(h), this.d(r));
    if (color) { g.fillStyle = color; g.fill(); }
    if (stroke) { g.strokeStyle = stroke; g.lineWidth = this.d(lw); g.stroke(); }
  }
  circle(x, y, r, color, stroke = null, lw = 1.5) {
    const g = this.g; g.beginPath(); g.arc(this.d(x), this.d(y), this.d(r), 0, Math.PI * 2);
    if (color) { g.fillStyle = color; g.fill(); }
    if (stroke) { g.strokeStyle = stroke; g.lineWidth = this.d(lw); g.stroke(); }
  }
  bg() { this.g.fillStyle = this.c.bg; this.g.fillRect(0, 0, this.W, this.H); }
  status(time = '23:41', opts = {}) {
    const c = this.c;
    this.text(time, 24, 30, 14, c.ink, 600);
    const x = 393 - 24;
    this.g.fillStyle = c.ink;
    if (opts.wifi !== false) { for (let i = 0; i < 3; i++) { this.g.beginPath(); this.g.arc(this.d(x - 56), this.d(30), this.d(4 + i * 4), Math.PI * 1.25, Math.PI * 1.75); this.g.lineWidth = this.d(1.6); this.g.strokeStyle = c.ink; this.g.stroke(); } }
    else this.text('无网络', x - 56, 30, 11, c.muted, 500, 'center');
    this.rect(x - 30, 21, 24, 11, 3, null, c.ink, 1.3);
    this.rect(x - 28, 23, 20 * (opts.battery ?? 0.62), 7, 2, c.ink);
    this.y = 48;
  }
  pageTitle(title, sub = '', back = false) {
    const c = this.c; let y = this.y + 16;
    if (back) { this.g.strokeStyle = c.ink; this.g.lineWidth = this.d(2); this.g.beginPath(); this.g.moveTo(this.d(36), this.d(y + 4)); this.g.lineTo(this.d(28), this.d(y + 12)); this.g.lineTo(this.d(36), this.d(y + 20)); this.g.stroke(); y += 30; }
    this.text(title, 24, y + 30, 30, c.ink, 700);
    let h = 44;
    if (sub) h += this.text(sub, 24, y + 62, 15, c.muted, 400, 'left', 345);
    this.y = y + h + 18;
  }
  section(label) { this.text(label, 24, this.y + 18, 12, this.c.muted, 600); this.y += 30; }
  group(rows, opts = {}) {
    const c = this.c, x = 16, w = 361, rh = opts.rowH || 64;
    const h = rows.length * rh;
    this.rect(x, this.y, w, h, 22, c.surface);
    rows.forEach((r, i) => {
      const y = this.y + i * rh;
      if (r.selected) this.rect(x, y, w, rh, i === 0 ? 22 : (i === rows.length - 1 ? 22 : 0), c.selected);
      if (i) { this.g.fillStyle = c.divider; this.g.fillRect(this.d(x + 64), this.d(y), this.d(w - 64), Math.max(1, this.d(0.6))); }
      if (r.icon !== false) { this.circle(x + 32, y + rh / 2, 18, c.selected); this.glyph(r.icon || 'dot', x + 32, y + rh / 2, r.iconColor || c.ink); }
      const tx = r.icon === false ? x + 20 : x + 62;
      this.text(r.title, tx, y + (r.sub ? rh / 2 - 3 : rh / 2 + 6), 16, r.dim ? c.muted : c.ink, 500);
      if (r.sub) this.text(r.sub, tx, y + rh / 2 + 18, 13, r.subColor || c.muted, 400);
      const ex = x + w - 22;
      if (r.trail === 'chevron') { this.g.strokeStyle = c.muted; this.g.lineWidth = this.d(1.8); this.g.beginPath(); this.g.moveTo(this.d(ex - 4), this.d(y + rh / 2 - 6)); this.g.lineTo(this.d(ex + 2), this.d(y + rh / 2)); this.g.lineTo(this.d(ex - 4), this.d(y + rh / 2 + 6)); this.g.stroke(); }
      if (r.trail === 'check') this.check(ex - 4, y + rh / 2, c.eyeInk);
      if (r.trail === 'radio' || r.trail === 'radioOn') { this.circle(ex - 4, y + rh / 2, 9, null, r.trail === 'radioOn' ? c.ink : c.outline, 2); if (r.trail === 'radioOn') this.circle(ex - 4, y + rh / 2, 5, c.ink); }
      if (r.trail === 'box' || r.trail === 'boxOn') { this.rect(ex - 13, y + rh / 2 - 9, 18, 18, 4, r.trail === 'boxOn' ? c.ink : null, r.trail === 'boxOn' ? null : c.outline, 2); if (r.trail === 'boxOn') this.check(ex - 4, y + rh / 2, c.onAccent, 0.75); }
      if (r.trail === 'switch' || r.trail === 'switchOn') this.toggle(ex - 22, y + rh / 2, r.trail === 'switchOn');
      if (typeof r.trail === 'string' && !['chevron', 'check', 'radio', 'radioOn', 'box', 'boxOn', 'switch', 'switchOn'].includes(r.trail)) this.text(r.trail, ex, y + rh / 2 + 6, 14, c.muted, 500, 'right');
    });
    this.y += h + (opts.gap ?? 16);
    return this.y;
  }
  toggle(x, y, on) {
    const c = this.c; this.rect(x - 16, y - 12, 44, 24, 12, on ? c.ink : c.selected, on ? null : c.outline, 1.5);
    this.circle(on ? x + 16 : x - 4, y, 8, on ? c.onAccent : c.muted);
  }
  check(x, y, color, k = 1) {
    const g = this.g; g.strokeStyle = color; g.lineWidth = this.d(2.4 * k); g.lineCap = 'round'; g.lineJoin = 'round';
    g.beginPath(); g.moveTo(this.d(x - 7 * k), this.d(y)); g.lineTo(this.d(x - 2 * k), this.d(y + 5 * k)); g.lineTo(this.d(x + 7 * k), this.d(y - 6 * k)); g.stroke();
  }
  glyph(name, x, y, color) {
    const g = this.g, d = (v) => this.d(v); g.strokeStyle = color; g.fillStyle = color; g.lineWidth = d(1.7); g.lineCap = 'round';
    const arcs = () => { for (let i = 0; i < 3; i++) { g.beginPath(); g.arc(d(x), d(y + 6), d(3 + i * 4), Math.PI * 1.25, Math.PI * 1.75); g.stroke(); } };
    if (name === 'wifi') arcs();
    else if (name === 'eye') { g.beginPath(); g.ellipse(d(x), d(y), d(8), d(5), 0, 0, 7); g.stroke(); g.beginPath(); g.arc(d(x), d(y), d(2.2), 0, 7); g.fill(); }
    else if (name === 'mic') { g.beginPath(); g.roundRect(d(x - 3), d(y - 8), d(6), d(11), d(3)); g.stroke(); g.beginPath(); g.arc(d(x), d(y), d(6), 0, Math.PI); g.stroke(); }
    else if (name === 'cloud') { g.beginPath(); g.arc(d(x - 3), d(y + 1), d(5), Math.PI * 0.5, Math.PI * 1.6); g.arc(d(x + 2), d(y - 1), d(5), Math.PI, Math.PI * 2); g.arc(d(x + 5), d(y + 2), d(4), Math.PI * 1.5, Math.PI * 0.5); g.closePath(); g.stroke(); }
    else if (name === 'pc') { g.strokeRect(d(x - 8), d(y - 6), d(16), d(10)); g.beginPath(); g.moveTo(d(x - 4), d(y + 8)); g.lineTo(d(x + 4), d(y + 8)); g.stroke(); }
    else if (name === 'shield') { g.beginPath(); g.moveTo(d(x), d(y - 8)); g.lineTo(d(x + 7), d(y - 5)); g.lineTo(d(x + 6), d(y + 3)); g.lineTo(d(x), d(y + 8)); g.lineTo(d(x - 6), d(y + 3)); g.lineTo(d(x - 7), d(y - 5)); g.closePath(); g.stroke(); }
    else if (name === 'card') { g.strokeRect(d(x - 8), d(y - 5), d(16), d(11)); g.fillRect(d(x - 8), d(y + 1), d(16), d(2)); }
    else if (name === 'spark') { g.beginPath(); g.moveTo(d(x), d(y - 8)); g.lineTo(d(x + 2), d(y - 2)); g.lineTo(d(x + 8), d(y)); g.lineTo(d(x + 2), d(y + 2)); g.lineTo(d(x), d(y + 8)); g.lineTo(d(x - 2), d(y + 2)); g.lineTo(d(x - 8), d(y)); g.lineTo(d(x - 2), d(y - 2)); g.closePath(); g.stroke(); }
    else if (name === 'down') { g.beginPath(); g.moveTo(d(x), d(y - 7)); g.lineTo(d(x), d(y + 4)); g.moveTo(d(x - 5), d(y)); g.lineTo(d(x), d(y + 5)); g.lineTo(d(x + 5), d(y)); g.moveTo(d(x - 7), d(y + 8)); g.lineTo(d(x + 7), d(y + 8)); g.stroke(); }
    else if (name === 'speaker') { g.beginPath(); g.moveTo(d(x - 7), d(y - 3)); g.lineTo(d(x - 3), d(y - 3)); g.lineTo(d(x + 2), d(y - 8)); g.lineTo(d(x + 2), d(y + 8)); g.lineTo(d(x - 3), d(y + 3)); g.lineTo(d(x - 7), d(y + 3)); g.closePath(); g.stroke(); }
    else { g.beginPath(); g.arc(d(x), d(y), d(3), 0, 7); g.fill(); }
  }
  button(label, y, primary = true, enabled = true, w = 361, x = 16) {
    const c = this.c;
    this.rect(x, y, w, 56, 28, enabled ? (primary ? c.ink : null) : c.selected, primary || !enabled ? null : c.divider, 1);
    this.text(label, x + w / 2, y + 35, 16, enabled ? (primary ? c.onAccent : c.ink) : c.muted, 600, 'center');
  }
  notice(msg, warn = false) {
    const c = this.c;
    this.g.font = `400 ${this.d(14)}px ${SANS}`;
    const lines = Math.ceil(this.g.measureText(msg).width / this.d(320)) || 1;
    const h = 30 + lines * 20;
    this.rect(16, this.y, 361, h, 16, warn ? c.warnSurface : c.selected);
    this.text(msg, 32, this.y + 30, 14, warn ? c.warning : c.ink, 400, 'left', 328);
    this.y += h + 14;
  }
  field(label, value, opts = {}) {
    const c = this.c;
    this.rect(16, this.y, 361, 60, 14, null, opts.focus ? c.ink : c.outline, opts.focus ? 2 : 1.2);
    this.text(label, 30, this.y + 20, 12, c.muted, 500);
    this.text(value + (opts.cursor ? '|' : ''), 30, this.y + 46, 17, c.ink, 500);
    this.y += 74;
  }
  presence(cy, mood = 'awake', size = 1, t = 0) {
    // CompanionPresenceView: two round displays, mint pill eyes.
    const c = this.c, r = 34 * size, gap = 10 * size, cx1 = 196.5 - r - gap / 2, cx2 = 196.5 + r + gap / 2;
    for (const cx of [cx1, cx2]) {
      this.circle(cx, cy, r, c.bezel); this.circle(cx, cy, r * 0.88, '#09090B');
      if (mood === 'asleep' || mood === 'offline') { this.rect(cx - r * 0.22, cy + r * 0.04, r * 0.44, r * 0.11, r * 0.055, '#3A3A42'); continue; }
      const ew = r * 0.25, eh = r * 0.58 * (mood === 'happy' ? 0.5 : 1);
      this.g.fillStyle = c.eye; this.g.beginPath(); this.g.roundRect(this.d(cx - ew / 2), this.d(cy - eh / 2), this.d(ew), this.d(eh), this.d(ew / 2)); this.g.fill();
    }
  }
  chain(nodes, y) {
    // connectionChain: phone → 傻妞 → 语音服务; live = mint, broken = amber gap.
    const c = this.c; const xs = [70, 196.5, 323];
    this.rect(16, y, 361, 120, 22, c.surface);
    for (let i = 0; i < 2; i++) {
      const st = nodes[i + 1].link;
      const a = xs[i] + 26, b = xs[i + 1] - 26;
      this.g.lineWidth = this.d(3); this.g.lineCap = 'round';
      if (st === 'live') { this.g.strokeStyle = c.eye; this.g.setLineDash([]); }
      else if (st === 'broken') { this.g.strokeStyle = c.warning; this.g.setLineDash([this.d(6), this.d(10)]); }
      else { this.g.strokeStyle = c.outline; this.g.setLineDash([this.d(3), this.d(6)]); }
      this.g.beginPath(); this.g.moveTo(this.d(a), this.d(y + 46)); this.g.lineTo(this.d(b), this.d(y + 46)); this.g.stroke(); this.g.setLineDash([]);
    }
    nodes.forEach((n, i) => {
      this.circle(xs[i], y + 46, 24, c.selected, n.ok ? c.eye : c.outline, 2);
      this.glyph(n.icon, xs[i], y + 46, c.ink);
      this.text(n.label, xs[i], y + 98, 13, c.ink, 500, 'center');
    });
    return y + 136;
  }
  sheet(y0) {
    const c = this.c; this.g.fillStyle = 'rgba(0,0,0,0.45)'; this.g.fillRect(0, 0, this.W, this.H);
    this.rect(0, y0, 393, 900, 28, c.surface);
    this.rect(176, y0 + 10, 40, 4, 2, c.outline);
    this.y = y0 + 24;
  }
  tap(x, y, k) {
    if (k <= 0 || k >= 1) return;
    this.g.fillStyle = `rgba(127,127,127,${0.28 * (1 - k)})`;
    this.g.beginPath(); this.g.arc(this.d(x), this.d(y), this.d(12 + 26 * easeOut(k)), 0, 7); this.g.fill();
  }
  spinner(x, y, t) {
    const g = this.g; g.strokeStyle = this.c.ink; g.lineWidth = this.d(2.4); g.lineCap = 'round';
    g.beginPath(); g.arc(this.d(x), this.d(y), this.d(9), t * 6, t * 6 + 4.2); g.stroke();
  }
}

// ---------------------------------------------------------------- screens
const QR = (() => { // a QR-like grid matching the device's claim code pattern
  let a = 1203 >>> 0; const r = () => { a |= 0; a = (a + 0x6D2B79F5) | 0; let t = Math.imul(a ^ (a >>> 15), 1 | a); t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t; return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
  const n = 25, g = []; for (let y = 0; y < n; y++) { g.push([]); for (let x = 0; x < n; x++) g[y].push(r() < 0.47 ? 1 : 0); } return g;
})();

const S = {};
S.chat = (u, p, lt) => {
  const c = u.c; u.bg(); u.status(p.time || '23:41');
  u.rect(0, 48, 393, 56, 0, c.surface); u.text('‹', 18, 86, 26, c.ink, 400); u.text(p.contact, 196.5, 84, 18, c.ink, 600, 'center');
  let y = 140;
  for (const m of p.messages || []) {
    if (m.date) { u.text(m.date, 196.5, y, 12, c.muted, 400, 'center'); y += 26; continue; }
    u.g.font = `400 ${u.d(16)}px ${SANS}`;
    // Wrap into lines of at most 230 dp.
    const lines = []; let line = '';
    for (const ch of m.text) { if (u.g.measureText(line + ch).width / u.s > 230 && line) { lines.push(line); line = ch; } else line += ch; }
    if (line) lines.push(line);
    const w = Math.min(260, Math.max(...lines.map((l) => u.g.measureText(l).width / u.s)) + 30), h = 18 + lines.length * 24;
    const x = m.me ? 393 - 16 - w - 44 : 60;
    u.circle(m.me ? 393 - 34 : 34, y + 20, 18, m.me ? '#9fb6c8' : '#d9c4b0');
    u.text(m.me ? '屿' : (p.avatar || '晴'), m.me ? 393 - 34 : 34, y + 26, 14, '#ffffff', 600, 'center');
    u.rect(x, y, w, h, 12, m.me ? '#95EC69' : (p.theme === 'dark' ? '#2C2C2E' : '#FFFFFF'));
    lines.forEach((l, i) => u.text(l, x + 15, y + 27 + i * 24, 16, m.me ? '#111111' : c.ink, 400));
    y += h + 16;
  }
  // Input bar and keyboard.
  const by = 852 - (p.keyboard ? 290 : 0) - 64;
  u.rect(0, by, 393, 64, 0, c.surface);
  u.rect(16, by + 12, 290, 40, 10, c.bg);
  const draft = p.draft || '';
  u.text(draft + (Math.floor(lt * 2.4) % 2 === 0 ? '|' : ''), 28, by + 39, 16, c.ink, 400);
  u.rect(316, by + 12, 62, 40, 10, draft ? '#07C160' : c.selected); u.text('发送', 347, by + 38, 15, draft ? '#ffffff' : c.muted, 500, 'center');
  if (p.keyboard) {
    u.rect(0, 852 - 290, 393, 290, 0, c.selected);
    const rows = ['qwertyuiop', 'asdfghjkl', 'zxcvbnm'];
    rows.forEach((r, i) => [...r].forEach((k, j) => { const kw = 34, x = 8 + i * 17 + j * 37.8; u.rect(x, 852 - 270 + i * 58, kw, 46, 6, c.surface); u.text(k, x + kw / 2, 852 - 240 + i * 58, 17, c.ink, 400, 'center'); }));
    u.rect(90, 852 - 270 + 3 * 58, 213, 46, 6, c.surface); u.text('空格', 196.5, 852 - 240 + 3 * 58, 15, c.muted, 400, 'center');
  }
};
S.lock = (u, p) => {
  const c = u.c; u.bg(); u.status(p.time, { wifi: false, battery: 0.21 });
  u.text(p.time, 196.5, 230, 64, c.ink, 300, 'center');
  u.text(p.date || '11月17日 星期二', 196.5, 270, 15, c.muted, 400, 'center');
  u.y = 330; u.notice('Wi-Fi 已断开 · 无网络', true);
};
S.home = (u, p, lt) => {
  const c = u.c; u.bg(); u.status(p.time);
  u.pageTitle('我的傻妞', '');
  if (!p.claimed) {
    u.rect(16, u.y, 361, 200, 22, c.stage); u.presence(u.y + 100, 'asleep', 1.2); u.y += 220;
    u.text('让傻妞，回到你身边。', 24, u.y + 10, 22, c.ink, 700); u.y += 30;
    u.text('无需云账号 · 认领后再设置网络和语音服务', 24, u.y + 10, 14, c.muted, 400); u.y += 40;
    u.button('添加傻妞 · 扫码连接', u.y, true); u.tap(196, u.y + 28, p.tapK ?? 0);
    return;
  }
  u.rect(16, u.y, 361, 180, 22, c.stage); u.presence(u.y + 82, p.mood || 'awake', 1.15);
  u.text(p.wifi === false ? '设备未联网' : '我在，随时听你说。', 196.5, u.y + 160, 14, '#F2EFEA', 500, 'center'); u.y += 196;
  u.y = u.chain([{ label: '手机', icon: 'shield', ok: true }, { label: '傻妞', icon: 'eye', ok: true, link: 'live' }, { label: '语音服务', icon: 'cloud', ok: p.wifi !== false, link: p.wifi === false ? 'broken' : 'live' }], u.y);
  if (p.wifi === false) { u.notice('连接还在，不必重新认领', true); u.group([{ title: '更换 Wi-Fi', sub: '手机仍可通过蓝牙为傻妞更换网络。', icon: 'wifi', trail: 'chevron', selected: p.tapWifi }]); }
  else u.text('设备独立对话，关闭 App 也不打断', 24, u.y + 4, 14, c.muted, 400);
  if (p.version) { u.y += 20; u.group([{ title: '固件更新', sub: p.version, icon: 'down', trail: 'chevron' }]); }
};
S.scan = (u, p, lt) => {
  u.g.fillStyle = '#050506'; u.g.fillRect(0, 0, u.W, u.H); u.c = THEMES.dark;
  u.status(p.time); u.text('扫描设备屏幕，离线安全认领', 196.5, 110, 16, '#F2EFEA', 500, 'center');
  // Viewfinder: the device's round screen showing its claim code.
  const cy = 420; u.circle(196.5, cy, 150, '#101014'); u.circle(196.5, cy, 132, '#000');
  const n = QR.length, box = 170, cell = box / n, ox = 196.5 - box / 2, oy = cy - box / 2;
  u.rect(ox - 10, oy - 10, box + 20, box + 20, 2, '#f4f4f0');
  for (let y = 0; y < n; y++) for (let x = 0; x < n; x++) if (QR[y][x]) u.rect(ox + x * cell, oy + y * cell, cell + 0.4, cell + 0.4, 0, '#0b0b0b');
  const k = smooth(clamp(lt / 0.8)); const fr = 120 - 18 * k;
  u.g.strokeStyle = '#7CF0BF'; u.g.lineWidth = u.d(4);
  for (const [sx, sy] of [[-1, -1], [1, -1], [-1, 1], [1, 1]]) {
    u.g.beginPath(); u.g.moveTo(u.d(196.5 + sx * fr), u.d(cy + sy * fr - sy * 28)); u.g.lineTo(u.d(196.5 + sx * fr), u.d(cy + sy * fr)); u.g.lineTo(u.d(196.5 + sx * fr - sx * 28), u.d(cy + sy * fr)); u.g.stroke();
  }
  u.text('确认是你的设备，再开始连接。', 196.5, 700, 15, '#A29EA7', 400, 'center');
};
S.verify = (u, p, lt) => {
  const c = u.c; u.bg(); u.status(p.time); u.pageTitle('连接傻妞', '确认是你的设备，再开始连接。');
  u.rect(16, u.y, 361, 150, 22, c.stage); u.presence(u.y + 75, p.state === 'ok' ? 'awake' : 'asleep', 1.0); u.y += 170;
  if (p.state === 'ok') { u.rect(16, u.y, 361, 64, 22, c.surface); u.check(46, u.y + 32, c.eyeInk, 1.3); u.text('已验证连接傻妞', 72, u.y + 39, 17, c.ink, 600); u.y += 80; u.text('蓝牙已连接 · 身份已验证', 24, u.y + 4, 14, c.muted, 400); }
  else { u.rect(16, u.y, 361, 64, 22, c.surface); u.spinner(46, u.y + 32, lt); u.text('正在连接并验证设备', 72, u.y + 39, 17, c.ink, 500); u.y += 80; u.text('无需云账号 · 认领后再设置网络和语音服务', 24, u.y + 4, 14, c.muted, 400); }
};
S.wifi = (u, p, lt) => {
  const c = u.c; u.bg(); u.status(p.time); u.pageTitle('设置网络', '设备扫描到的附近 Wi-Fi', true);
  const nets = p.nets || ['屿和晴的家', 'ChinaNet-5G2', 'TP-LINK_3F12', 'MiWiFi-1203'];
  u.group(nets.map((n, i) => ({ title: n, icon: 'wifi', trail: i === 0 && p.picked ? 'check' : (i === 0 ? '信号强' : ''), selected: i === 0 && p.picked })), { rowH: 58 });
  if (p.picked) { u.field('密码', '•'.repeat(p.dots || 0), { focus: true, cursor: p.dots < 8 }); u.button('保存', u.y, true, (p.dots || 0) >= 8); u.tap(196, u.y + 28, p.tapK ?? 0); }
};
S.cloud = (u, p, lt) => {
  const c = u.c; u.bg(); u.status(p.time); u.pageTitle('云服务与模型', '听、想、说，分别选择适合的服务。', true);
  u.group([{ title: '听 · 语音识别', sub: 'mimo-v2.5-asr', icon: 'mic' }, { title: '想 · 对话模型', sub: 'mimo-v2.6-flash', icon: 'spark' }, { title: '说 · 语音合成', sub: 'mimo-v2.5-tts', icon: 'speaker' }]);
  u.field('服务地址', 'https://api.xiaomimimo.com/v1'); u.field('API Key', '••••••••••••••••');
  if (p.state === 'saving') { u.rect(16, u.y, 361, 56, 28, c.selected); u.spinner(150, u.y + 28, lt); u.text('等待设备回读', 205, u.y + 34, 16, c.ink, 500, 'center'); }
  else if (p.state === 'ok') { u.rect(16, u.y, 361, 56, 28, c.selected); u.check(110, u.y + 28, c.eyeInk, 1.1); u.text('设备已联网 · 语音服务就绪', 205, u.y + 34, 16, c.ink, 600, 'center'); }
  else { u.button('保存', u.y); u.tap(196, u.y + 28, p.tapK ?? 0); }
};
S.persona = (u, p) => {
  const c = u.c; u.bg(); u.status(p.time); u.pageTitle('她的设定', '选择陪伴的语气。', true);
  const names = ['温柔陪伴', '活泼俏皮', '安静倾听', '认真交流', '轻轻嘴硬'];
  const desc = ['慢慢聊，温柔回应', '轻松一点，多一点趣味', '留些空间，听你说完', '一起理清思路', '带一点俏皮的小别扭'];
  u.group(names.map((n, i) => ({ title: n, sub: desc[i], icon: false, trail: p.sel === i ? 'radioOn' : 'radio', selected: p.sel === i })), { rowH: 66 });
  u.text('对话进行中，结束后可切换风格。', 24, u.y + 6, 13, c.muted, 400);
};
S.memory = (u, p) => {
  const c = u.c; u.bg(); u.status(p.time); u.pageTitle('对话记忆', '称呼、偏好与近期话题', true);
  u.group([{ title: '跨重启记忆', sub: p.on ? '已开启 · 加密保存近期对话和事实' : '已关闭 · 不读取或新增保存', icon: 'shield', trail: p.on ? 'switchOn' : 'switch' }]);
  u.notice('允许设备加密保存最近三轮对话及明确表达的称呼、偏好和近期话题，并在下次启动后继续使用。你可以查看、纠正或删除事实。');
  u.group([{ title: '查看与编辑事实', icon: 'eye', trail: 'chevron' }, { title: '清空近期对话', sub: '让下一次聊天从新的话题开始', icon: 'dot', trail: 'chevron' }, { title: '删除设备记忆', icon: 'dot', trail: 'chevron' }]);
};
S.wakeReply = (u, p) => {
  const c = u.c; u.bg(); u.status(p.time); u.pageTitle('唤醒应答', '本地应答与云端语音分开管理。', true);
  u.group([{ title: '当前唤醒词', sub: '你好，openvela', icon: 'mic' }, { title: '唤醒应答', sub: '“我在”', icon: 'speaker', selected: true }]);
  u.notice('当前固件未提供独立的唤醒应答设置，不能在 App 中修改应答语。', true);
};
S.scenes = (u, p, lt) => {
  const c = u.c; u.bg(); u.status(p.time); u.pageTitle('定制', '一点点，变成你熟悉的她。');
  u.sheet(150);
  u.text('场景卡片', 24, u.y + 22, 22, c.ink, 700); u.text('为兼容卡片选择一个明确操作。', 24, u.y + 50, 14, c.muted, 400); u.y += 70;
  const acts = ['开始专注', '暂停专注', '继续专注', '取消专注', '播放四拍节律（4 秒）', '取消本地内容', '启动原生功能'];
  u.group(acts.map((a, i) => ({ title: a, icon: false, trail: p.sel === i ? 'radioOn' : 'radio', selected: p.sel === i, dim: i === 6 })), { rowH: 52, gap: 12 });
  if (p.state === 'wait') { u.notice('请把卡片放到傻妞上'); }
  else if (p.state === 'ok') { u.rect(16, u.y, 361, 56, 28, c.selected); u.check(120, u.y + 28, c.eyeInk, 1.1); u.text(`已登记 · ${p.label || '门禁卡 1203'}`, 205, u.y + 34, 16, c.ink, 600, 'center'); }
};
S.sensitivity = (u, p) => {
  const c = u.c; u.bg(); u.status(p.time); u.pageTitle('回答与唤醒', '她怎样回应你，多容易被唤醒。', true);
  u.group([{ title: '当前唤醒词', sub: '你好，openvela', icon: 'mic' }]);
  u.section('唤醒灵敏度');
  const v = p.value ?? 0.85;
  u.rect(16, u.y, 361, 110, 22, c.surface);
  u.text(`设备门限 ${v.toFixed(2)}（越低越易唤醒，也可能误唤醒）`, 32, u.y + 34, 14, c.ink, 500);
  const x0 = 40, x1 = 353, kx = x0 + (x1 - x0) * (1 - (v - 0.5) / 0.5);
  u.rect(x0, u.y + 66, x1 - x0, 6, 3, c.selected); u.rect(kx, u.y + 66, x1 - kx, 6, 3, c.ink);
  u.circle(kx, u.y + 69, 12, c.ink); u.y += 126;
};
S.answer = (u, p) => {
  const c = u.c; u.bg(); u.status(p.time); u.pageTitle('声音与回答', '让连接、声音和隐私都由你掌握。', true);
  u.section('回答长度');
  const segs = ['简洁', '标准', '详细'];
  u.rect(16, u.y, 361, 52, 26, c.surface, c.divider, 1);
  segs.forEach((s, i) => { if (p.len === i) u.rect(16 + i * 120.3 + 4, u.y + 4, 112, 44, 22, c.ink); u.text(s, 16 + i * 120.3 + 60, u.y + 33, 16, p.len === i ? c.onAccent : c.ink, 600, 'center'); });
  u.y += 64; u.text(`${segs[p.len ?? 1]}；明确的本轮用户要求优先`, 24, u.y + 4, 13, c.muted, 400); u.y += 24;
  u.section('回答模式');
  u.group([{ title: '快速对话 · 关闭深度思考', sub: '识别和语音合成仍需网络等待', icon: false, trail: p.fast ? 'radioOn' : 'radio', selected: p.fast }, { title: '深度思考 · 回答可能更慢', icon: false, trail: p.fast ? 'radio' : 'radioOn', selected: !p.fast }]);
};
S.pcAuth = (u, p, lt) => {
  const c = u.c; u.bg(); u.status(p.time); u.pageTitle('电脑授权', '配对、查看或撤销电脑的访问权限', true);
  u.group([{ title: '导入电脑配对请求', sub: 'pc-request.json', icon: 'pc', trail: 'check' }]);
  u.notice('电脑标识：屿的电脑 · 摘要 3F2A 91C4 7D0E');
  const perms = [['资源管理（眼睛包等素材）', true, 2], ['场景', false, 9], ['任务提醒', true, 0], ['有限诊断', false, 9], ['按需相机（可传回一帧画面）', true, 1]];
  u.group(perms.map(([n, on, order]) => ({ title: n, icon: false, trail: (on && (p.checked ?? 9) > order) ? 'boxOn' : 'box' })), { rowH: 52 });
  u.button(p.exported ? '已导出加密响应' : '导出加密响应', u.y, !p.exported); u.tap(196, u.y + 28, p.tapK ?? 0);
};
S.update = (u, p, lt) => {
  const c = u.c; u.bg(); u.status(p.time); u.pageTitle('更新', '每次进步，都清楚可见。');
  u.group([{ title: '固件更新', sub: `当前 ${p.current || '0.7.128 · 774'}`, icon: 'down' }]);
  u.notice('更新期间请保持 App 在前台。蓝牙发送更新指令，设备通过局域网 HTTPS 拉取镜像；重连并核对版本后才确认完成。');
  if (!p.phase) { u.group([{ title: '所选固件包', sub: '版本 0.7.129 · 设备 aidk_ai_toy', icon: 'card' }]); u.button('开始固件更新', u.y); u.tap(196, u.y + 28, p.tapK ?? 0); return; }
  const steps = ['正在下载', '正在校验', '正在重启', '已核对新版本'];
  u.rect(16, u.y, 361, 4 * 52 + 16, 22, c.surface);
  steps.forEach((s, i) => {
    const y = u.y + 34 + i * 52;
    const done = i < p.phase, cur = i === p.phase;
    u.circle(44, y - 5, 10, done || (cur && i === 3) ? c.eye : (cur ? c.selected : null), done || cur ? null : c.outline, 2);
    if (done || (cur && i === 3)) u.check(44, y - 5, c.onAccent === '#FFFFFF' ? '#0b3d2c' : '#0b3d2c', 0.8);
    else if (cur) u.spinner(44, y - 5, lt);
    u.text(s + (i === 3 && (cur || done) ? ' · 0.7.129 · 775' : ''), 68, y, 16, done || cur ? c.ink : c.muted, cur ? 600 : 400);
  });
  u.y += 4 * 52 + 32;
};
S.approval = (u, p, lt) => {
  const c = u.c; u.bg(); u.status(p.time); u.pageTitle('电脑审批', '核对并批准或拒绝电脑上 AI 助手请求的操作', true);
  u.group([{ title: '请求方', sub: 'AI 助手 · 屿的电脑', icon: 'pc' }, { title: '目标', sub: p.target, icon: 'card' }]);
  u.section('命令原文（逐字核对）');
  u.rect(16, u.y, 361, 112, 16, c.stage);
  u.text(p.command, 30, u.y + 38, 17, '#F2EFEA', 600, 'left', 330);
  u.y += 128;
  if (p.state === 'pending') {
    u.text(`剩余 ${Math.max(0, Math.ceil(p.left))} 秒 · 超时、改动或断开都按拒绝处理`, 24, u.y + 6, 13, c.warning, 500); u.y += 26;
    u.button('拒绝', u.y, false, true, 174, 16); u.button('批准', u.y, true, true, 174, 203);
    if (p.tap === 'reject') u.tap(103, u.y + 28, p.tapK ?? 0);
    if (p.tap === 'approve') u.tap(290, u.y + 28, p.tapK ?? 0);
  } else {
    const labels = { expired: '已过期 · 未执行', rejected: '已拒绝 · 未执行', approved: '已批准 · 签名回执已发给电脑' };
    u.rect(16, u.y, 361, 56, 28, p.state === 'approved' ? c.selected : c.warnSurface);
    u.text(labels[p.state], 196.5, u.y + 35, 16, p.state === 'approved' ? c.ink : c.warning, 600, 'center');
  }
};
S.reset = (u, p, lt) => {
  const c = u.c; u.bg(); u.status(p.time); u.pageTitle('设置', '让连接、声音和隐私都由你掌握。');
  u.group([{ title: '隐私与记忆', icon: 'shield', trail: 'chevron' }, { title: '电脑授权', icon: 'pc', trail: 'chevron' }, { title: '转交或恢复出厂', sub: '清除用户配置，撤销旧控制凭据', icon: 'dot', trail: 'chevron', selected: true }]);
  u.sheet(380);
  u.text('转交或恢复出厂', 24, u.y + 24, 22, c.ink, 700); u.y += 40;
  u.notice('清除用户配置，撤销旧控制凭据。此操作无法撤销。', true);
  u.button('取消', u.y, false, true, 174, 16); u.button('确认恢复出厂', u.y, true, true, 174, 203);
  if (p.tap === 'cancel') u.tap(103, u.y + 28, p.tapK ?? 0);
};
S.facts = (u, p, lt) => {
  const c = u.c; u.bg(); u.status(p.time); u.pageTitle('查看与编辑事实', '只保存你明确表达的信息，可随时纠正。', true);
  const f = p.facts || [['称呼', '小鱼'], ['偏好', '加两勺糖的咖啡'], ['近期话题', '灯塔']];
  u.group(f.map(([k, v], i) => ({ title: v, sub: k, icon: false, trail: p.editing === i ? '编辑中' : 'chevron', selected: p.editing === i })), { rowH: 66 });
  if (p.editing != null && p.editing >= 0) u.field(f[p.editing][0], p.draft ?? f[p.editing][1], { focus: true, cursor: true });
  if (p.state === 'saving') { u.rect(16, u.y, 361, 56, 28, c.selected); u.spinner(150, u.y + 28, lt); u.text('等待设备回读', 205, u.y + 34, 16, c.ink, 500, 'center'); }
  if (p.state === 'ok') { u.rect(16, u.y, 361, 56, 28, c.selected); u.check(140, u.y + 28, c.eyeInk, 1.1); u.text('设备已保存', 210, u.y + 34, 16, c.ink, 600, 'center'); }
};
S.clearRecent = (u, p) => {
  const c = u.c; u.bg(); u.status(p.time); u.pageTitle('对话记忆', '称呼、偏好与近期话题', true);
  u.group([{ title: '清空近期对话', sub: '让下一次聊天从新的话题开始', icon: 'dot', trail: p.done ? 'check' : 'chevron', selected: true }]);
  u.notice('清除设备用于继续聊天的近期上下文。云端服务可能保留的记录不在此清除范围内，人物与配网设置会保留。');
  if (p.done) { u.rect(16, u.y, 361, 56, 28, c.selected); u.check(130, u.y + 28, c.eyeInk, 1.1); u.text('近期对话已清空', 210, u.y + 34, 16, c.ink, 600, 'center'); }
};

// ---------------------------------------------------------------- entry
export function drawPhoneInto(g, W, H, ph, t) {
  const u = new UI(g, W, H, ph.theme || 'light');
  g.save();
  const fn = S[ph.screen] || S.home;
  fn(u, ph, ph.lt ?? 0);
  g.restore();
}

export function drawPhone(canvas, ph, t) {
  if (!ph) return 'none';
  const animated = ph.anim ? Math.floor(t * 15) : 0;
  const key = JSON.stringify(ph) + animated;
  if (canvas.__key === key) return key;
  canvas.__key = key;
  const g = canvas.getContext('2d');
  if (ph.off) { g.fillStyle = '#000'; g.fillRect(0, 0, canvas.width, canvas.height); return key; }
  drawPhoneInto(g, canvas.width, canvas.height, ph, t);
  return key;
}
