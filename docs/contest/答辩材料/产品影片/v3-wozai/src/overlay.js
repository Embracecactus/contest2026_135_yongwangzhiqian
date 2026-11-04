// SPDX-License-Identifier: Apache-2.0
// The flat layer over the 3D frame: readable phone inserts, the fiction
// disclaimer, the title and the end card. Drawn from time like everything else.
import { clamp, easeInOut, easeOut, range, smooth } from './util.js';
import { drawPhoneInto } from './phoneui.js';
import { sketchEyes } from './textures.js';

const SANS = '"Noto Sans SC", sans-serif';
const HAND = '"LXGW WenKai", "Noto Sans SC", sans-serif';

export class Overlay {
  constructor(canvas) { this.c = canvas; this.g = canvas.getContext('2d'); }

  clear() { this.g.clearRect(0, 0, 1920, 1080); }

  // A close-up of the phone screen. ph = screen descriptor; o = {t0, t1, focus (dp y), zoom (px per dp), x, dim}
  phone(ph, o, t) {
    const g = this.g;
    const k = easeOut(range(t, o.t0, o.t0 + 0.35)) * (o.t1 ? 1 - easeInOut(range(t, o.t1 - 0.3, o.t1)) : 1);
    if (k <= 0) return;
    const z = o.zoom ?? 2.3, w = 393 * z, h = 852 * z;
    const cx = o.x ?? 960, fy = o.focus ?? 300;
    const x = cx - w / 2, y = 540 - fy * z + (1 - k) * 1100;
    g.save();
    g.fillStyle = `rgba(4,6,10,${(o.dim ?? 0.55) * k})`; g.fillRect(0, 0, 1920, 1080);
    // Phone body.
    g.fillStyle = '#121316'; g.beginPath(); g.roundRect(x - 18, y - 18, w + 36, h + 36, 70); g.fill();
    g.save(); g.beginPath(); g.roundRect(x, y, w, h, 56); g.clip();
    g.translate(x, y); g.scale(z * 393 / 393, z * 393 / 393);
    // drawPhoneInto expects canvas pixels; pass a 393-dp-wide surface at scale z.
    g.setTransform(z, 0, 0, z, x, y);
    drawPhoneInto(g, 393, 852, ph, t);
    g.restore();
    g.restore();
  }

  // Labels for the chip cutaway: anchored to 3D points, drawn large with a leader line.
  labels(list, camera) {
    const g = this.g;
    const TONES = { mint: '#9ff3d2', warm: '#ffd9a0', amber: '#ffc977', violet: '#cdbcff', grey: '#aab4bd' };
    for (const L of list) {
      const p = L.at.clone().project(camera);
      if (p.z > 1) continue;
      const ax = (p.x + 1) / 2 * 1920, ay = (1 - p.y) / 2 * 1080;
      const bx = Math.min(1920 - 120, Math.max(120, ax + (L.dx ?? 0))), by = Math.min(1080 - 90, Math.max(110, ay + (L.dy ?? 0)));
      const col = TONES[L.tone] || '#e8f3f6';
      g.save(); g.globalAlpha = clamp(L.a);
      g.strokeStyle = col; g.lineWidth = 2.5; g.beginPath(); g.moveTo(ax, ay); g.lineTo(bx, by); g.stroke();
      g.fillStyle = col; g.beginPath(); g.arc(ax, ay, 7, 0, 7); g.fill();
      g.strokeStyle = col; g.lineWidth = 2; g.beginPath(); g.arc(ax, ay, 14, 0, 7); g.stroke();
      const T = L.small ? 34 : 44, S2 = L.small ? 26 : 30;
      g.font = `700 ${T}px ${SANS}`; const w1 = g.measureText(L.text).width;
      g.font = `400 ${S2}px ${SANS}`; const w2 = L.sub ? g.measureText(L.sub).width : 0;
      const w = Math.max(w1, w2) + 48, h = L.sub ? T + S2 + 46 : T + 34;
      const left = Math.min(1920 - 40 - w, Math.max(40, bx + (bx > ax ? 0 : -w))), top = Math.min(1080 - 40 - h, Math.max(40, by - h / 2));
      g.fillStyle = 'rgba(6,10,14,0.78)'; g.beginPath(); g.roundRect(left, top, w, h, 14); g.fill();
      g.strokeStyle = col; g.globalAlpha *= 0.6; g.lineWidth = 2; g.stroke(); g.globalAlpha = clamp(L.a);
      g.textAlign = 'left'; g.fillStyle = col; g.font = `700 ${T}px ${SANS}`; g.fillText(L.text, left + 24, top + 18 + T * 0.86);
      if (L.sub) { g.fillStyle = '#d7e1e6'; g.font = `400 ${S2}px ${SANS}`; g.fillText(L.sub, left + 24, top + 30 + T + S2 * 0.86); }
      g.restore();
    }
  }

  // What her camera takes: viewfinder corners and a small caption (F20).
  frame(t, t0, t1, label) {
    const a = smooth(range(t, t0, t0 + 0.2)) * (1 - smooth(range(t, t1 - 0.2, t1)));
    if (a <= 0) return;
    const g = this.g; g.save(); g.globalAlpha = a; g.strokeStyle = 'rgba(240,248,250,0.9)'; g.lineWidth = 5;
    const x0 = 360, y0 = 120, x1 = 1560, y1 = 960, L = 70;
    for (const [x, y, sx, sy] of [[x0, y0, 1, 1], [x1, y0, -1, 1], [x0, y1, 1, -1], [x1, y1, -1, -1]]) { g.beginPath(); g.moveTo(x, y + sy * L); g.lineTo(x, y); g.lineTo(x + sx * L, y); g.stroke(); }
    g.font = `500 30px ${SANS}`; g.fillStyle = 'rgba(240,248,250,0.92)'; g.textAlign = 'left'; g.fillText(label, x0, y1 + 50);
    g.restore();
  }

  // His sketchbook: a new pair of eyes, drawn — "不像谁，就像她自己".
  sketch(t, t0, t1) {
    const a = smooth(range(t, t0, t0 + 0.4)) * (1 - smooth(range(t, t1 - 0.3, t1)));
    if (a <= 0) return;
    if (!this._sk) this._sk = {};
    const k = Math.round(clamp((t - t0 - 0.2) / (t1 - t0 - 0.9)) * 24) / 24;
    if (!this._sk[k]) this._sk[k] = sketchEyes(1024, 720, k);
    const g = this.g; g.save(); g.globalAlpha = a;
    g.fillStyle = 'rgba(6,8,12,0.35)'; g.fillRect(0, 0, 1920, 1080);
    g.translate(960, 560); g.rotate(-0.025);
    g.shadowColor = 'rgba(0,0,0,0.35)'; g.shadowBlur = 30;
    g.drawImage(this._sk[k], -512 * 1.15, -360 * 1.15, 1024 * 1.15, 720 * 1.15);
    g.restore();
  }

  disclaimer(t, t0, t1) {
    const a = smooth(range(t, t0, t0 + 0.6)) * (1 - smooth(range(t, t1 - 0.6, t1)));
    if (a <= 0) return;
    const g = this.g; g.save(); g.globalAlpha = a * 0.82;
    g.font = `400 26px ${SANS}`; g.fillStyle = '#e8ecef'; g.textAlign = 'left';
    g.fillText('人物与情节纯属虚构｜产品交互为动画演绎', 96, 1010);
    g.restore();
  }

  title(t, t0, t1) {
    const a = smooth(range(t, t0, t0 + 1.2)) * (1 - smooth(range(t, t1 - 0.8, t1)));
    if (a <= 0) return;
    const g = this.g; g.save();
    g.fillStyle = `rgba(6,8,12,${a})`; g.fillRect(0, 0, 1920, 1080);
    g.globalAlpha = a; g.fillStyle = '#f4efe6'; g.textAlign = 'center';
    g.font = `300 168px ${SANS}`; g.fillText('我在', 960, 590);
    g.font = `400 34px ${SANS}`; g.fillStyle = '#a8b0b8';
    g.fillText('一个关于傻妞的故事', 960, 680);
    g.restore();
  }

  endCard(t, t0, t1, eyes) {
    const a = smooth(range(t, t0, t0 + 0.8));
    if (a <= 0) return;
    const g = this.g; g.save(); g.globalAlpha = a;
    g.fillStyle = '#0b0d11'; g.fillRect(0, 0, 1920, 1080);
    if (eyes) { // her real eyes, carried in from the film
      const s = 168; g.save(); for (const [i, cx] of [[0, 760], [1, 1160]]) { g.beginPath(); g.arc(cx, 330, s / 2, 0, 7); g.clip(); g.drawImage(eyes[i], cx - s / 2, 330 - s / 2, s, s); g.restore(); g.save(); } g.restore();
    }
    g.textAlign = 'center'; g.fillStyle = '#f4efe6';
    g.font = `700 64px ${SANS}`; g.fillText('傻妞', 960, 520);
    g.font = `400 34px ${SANS}`; g.fillStyle = '#c9d0d6'; g.fillText('放在桌上的 openvela AI 伴侣', 960, 578);
    g.font = `400 26px ${SANS}`; g.fillStyle = '#8f98a1';
    const lines = [
      '勇往直前 · BK7258 三核适配（CPU0 CP + CPU1/CPU2 AP SMP）· 自研 BL1 · 产品服务 · Android App · 电脑工作台',
      '基于 openvela：NuttX · AI Agent · Media · TFLite Micro；MCUboot · mbedTLS；云端语音由用户自行配置（片中为 MiMo）',
      '人物与情节纯属虚构｜产品交互为动画演绎｜片中等待经过剪辑，不代表实测时延',
      '“我在”为项目公开内置应答音频（Kokoro 合成，非克隆），不是实板扬声器录音',
      '傻妞其他台词为相近音色的合成演绎；周屿配音：剪映「温柔男主」',
      '功能、实现与实测证据的区分见 FEATURE_MAP.md',
    ];
    // Six lines kept inside the title-safe area (x 192–1728, y ≤ 972).
    lines.forEach((l, i) => g.fillText(l, 960, 694 + i * 46));
    g.restore();
  }

  hand(text, t, t0, t1, x, y, size = 40) {
    const a = smooth(range(t, t0, t0 + 0.5)) * (1 - smooth(range(t, t1 - 0.5, t1)));
    if (a <= 0) return;
    const g = this.g; g.save(); g.globalAlpha = a; g.font = `${size}px ${HAND}`; g.fillStyle = '#f4efe6'; g.fillText(text, x, y); g.restore();
  }
}
