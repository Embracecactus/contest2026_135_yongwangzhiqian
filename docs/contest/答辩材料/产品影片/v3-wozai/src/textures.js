// SPDX-License-Identifier: Apache-2.0
// Procedural canvas textures for the room. Seeded, so every render matches.
import * as THREE from 'three';
import { mulberry32 } from './util.js';

export const HAND = '"LXGW WenKai", "Noto Sans SC", sans-serif';
export const SANS = '"Noto Sans SC", sans-serif';

function canvas(w, h) { const c = document.createElement('canvas'); c.width = w; c.height = h; return c; }
export function tex(c, srgb = true, repeat = null) {
  const t = new THREE.CanvasTexture(c);
  if (srgb) t.colorSpace = THREE.SRGBColorSpace;
  t.anisotropy = 8;
  if (repeat) { t.wrapS = t.wrapT = THREE.RepeatWrapping; t.repeat.set(repeat[0], repeat[1]); }
  return t;
}

export function wood(w = 1024, h = 512, base = [176, 122, 78], seed = 3, planks = 4) {
  const c = canvas(w, h), g = c.getContext('2d'), rnd = mulberry32(seed);
  g.fillStyle = `rgb(${base})`; g.fillRect(0, 0, w, h);
  const ph = h / planks;
  for (let p = 0; p < planks; p++) {
    const shade = (rnd() - 0.5) * 22;
    g.fillStyle = `rgba(${base[0] + shade},${base[1] + shade * 0.8},${base[2] + shade * 0.6},1)`;
    g.fillRect(0, p * ph, w, ph);
    for (let i = 0; i < 70; i++) {
      const y = p * ph + rnd() * ph, amp = 2 + rnd() * 7, freq = 0.002 + rnd() * 0.01, ph0 = rnd() * 9;
      g.strokeStyle = `rgba(${60 + rnd() * 30},${35 + rnd() * 20},${20},${0.06 + rnd() * 0.12})`;
      g.lineWidth = 0.6 + rnd() * 1.8;
      g.beginPath();
      for (let x = 0; x <= w; x += 8) g.lineTo(x, y + Math.sin(x * freq + ph0) * amp + Math.sin(x * freq * 3.1) * amp * 0.3);
      g.stroke();
    }
    g.fillStyle = 'rgba(40,24,12,0.35)'; g.fillRect(0, p * ph, w, 1.5);
  }
  return c;
}

export function plaster(w = 512, h = 512, base = [236, 231, 222], seed = 9) {
  const c = canvas(w, h), g = c.getContext('2d'), rnd = mulberry32(seed);
  g.fillStyle = `rgb(${base})`; g.fillRect(0, 0, w, h);
  for (let i = 0; i < 9000; i++) {
    const v = (rnd() - 0.5) * 14;
    g.fillStyle = `rgba(${base[0] + v},${base[1] + v},${base[2] + v},0.35)`;
    g.fillRect(rnd() * w, rnd() * h, 1 + rnd() * 3, 1 + rnd() * 3);
  }
  return c;
}

export function cityNight(w = 2048, h = 768, seed = 21, day = false) {
  const c = canvas(w, h), g = c.getContext('2d'), rnd = mulberry32(seed);
  g.clearRect(0, 0, w, h);
  const layers = day ? [[0.55, '#9aa6b4'], [0.7, '#7f8b99'], [0.86, '#6a7482']] : [[0.55, '#151b28'], [0.7, '#0f1420'], [0.86, '#0a0d16']];
  for (const [depth, col] of layers) {
    let x = -20;
    while (x < w) {
      const bw = 60 + rnd() * 160, bh = h * (0.25 + rnd() * 0.55) * depth;
      g.fillStyle = col; g.fillRect(x, h - bh, bw, bh);
      if (!day || depth > 0.8) {
        for (let wy = h - bh + 10; wy < h - 8; wy += 14) for (let wx = x + 8; wx < x + bw - 8; wx += 12) {
          const lit = rnd();
          if (day) { if (lit < 0.15) { g.fillStyle = 'rgba(200,215,230,0.35)'; g.fillRect(wx, wy, 6, 8); } }
          else if (lit < 0.28 * depth) {
            const warm = rnd() < 0.7;
            g.fillStyle = warm ? `rgba(255,${190 + rnd() * 40},${110 + rnd() * 50},${0.5 + rnd() * 0.5})` : `rgba(180,210,255,${0.4 + rnd() * 0.4})`;
            g.fillRect(wx, wy, 6, 8);
          }
        }
      }
      x += bw + rnd() * 18;
    }
  }
  return c;
}

export function giftCard(w = 1024, h = 720) {
  const c = canvas(w, h), g = c.getContext('2d');
  g.fillStyle = '#f6efe2'; g.fillRect(0, 0, w, h);
  g.strokeStyle = 'rgba(160,120,80,0.25)'; g.lineWidth = 6; g.strokeRect(24, 24, w - 48, h - 48);
  g.fillStyle = '#2b2622'; g.font = `84px ${HAND}`; g.textBaseline = 'alphabetic';
  g.fillText('生日快乐。', 96, 220);
  g.font = `66px ${HAND}`;
  g.fillText('以后有人提醒你', 96, 360);
  g.fillText('按时睡觉了。', 96, 460);
  g.font = `64px ${HAND}`; g.fillText('——晴', w - 300, 610);
  return c;
}

export function stickyNote(w = 512, h = 512) {
  const c = canvas(w, h), g = c.getContext('2d');
  g.fillStyle = '#f7d96a'; g.fillRect(0, 0, w, h);
  const grd = g.createLinearGradient(0, 0, 0, h); grd.addColorStop(0, 'rgba(255,255,255,0.08)'); grd.addColorStop(1, 'rgba(120,90,0,0.12)');
  g.fillStyle = grd; g.fillRect(0, 0, w, h);
  g.fillStyle = '#3a3022'; g.font = `120px ${HAND}`; g.textAlign = 'center';
  g.fillText('按时', w / 2, 230); g.fillText('吃饭', w / 2, 380);
  return c;
}

export function parcelLabel(w = 1024, h = 640) {
  const c = canvas(w, h), g = c.getContext('2d');
  g.fillStyle = '#fbfaf6'; g.fillRect(0, 0, w, h);
  g.fillStyle = '#1d1d1f'; g.font = `600 64px ${SANS}`; g.fillText('收件人：周屿', 56, 120);
  g.font = `48px ${SANS}`; g.fillStyle = '#333';
  g.fillText('AI 开发板套件 · 预售', 56, 210);
  g.fillText('7 月下单 · 11 月 14 日送达', 56, 285);
  g.fillStyle = '#b02a2a'; g.font = `600 44px ${SANS}`; g.fillText('已签收', 56, 400);
  for (let i = 0; i < 46; i++) { g.fillStyle = '#111'; g.fillRect(560 + i * 9, 430, (i * 7) % 5 + 2, 150); }
  return c;
}

export function doorCard(w = 512, h = 812) {
  const c = canvas(w, h), g = c.getContext('2d');
  g.fillStyle = '#f2f2ef'; g.fillRect(0, 0, w, h);
  g.fillStyle = '#4c78a8'; g.fillRect(0, h * 0.72, w, h * 0.07);
  g.fillStyle = '#ffffff'; g.fillRect(70, 120, 372, 200);
  g.strokeStyle = '#c8c8c4'; g.lineWidth = 4; g.strokeRect(70, 120, 372, 200);
  g.fillStyle = '#222'; g.font = `700 150px ${SANS}`; g.textAlign = 'center'; g.fillText('1203', w / 2, 278);
  return c;
}

export function fishMug(w = 1024, h = 512) {
  const c = canvas(w, h), g = c.getContext('2d');
  g.fillStyle = '#f4f2ec'; g.fillRect(0, 0, w, h);
  const fish = (x, y, s, col) => {
    g.fillStyle = col; g.beginPath(); g.ellipse(x, y, 60 * s, 30 * s, 0, 0, Math.PI * 2); g.fill();
    g.beginPath(); g.moveTo(x - 55 * s, y); g.lineTo(x - 95 * s, y - 28 * s); g.lineTo(x - 95 * s, y + 28 * s); g.closePath(); g.fill();
    g.fillStyle = '#f4f2ec'; g.beginPath(); g.arc(x + 32 * s, y - 6 * s, 6 * s, 0, 7); g.fill();
  };
  fish(560, 250, 1.6, '#3f7fb3');
  fish(320, 330, 0.6, '#7fb2d6');
  return c;
}

export function keyboardTex(w = 1024, h = 360) {
  const c = canvas(w, h), g = c.getContext('2d');
  g.fillStyle = '#26272b'; g.fillRect(0, 0, w, h);
  const rows = [14, 14, 13, 12, 9];
  rows.forEach((n, r) => {
    const kw = (w - 40) / 14.5;
    for (let i = 0; i < n; i++) {
      const x = 20 + i * kw + r * 6, y = 20 + r * 66;
      g.fillStyle = '#3a3b40'; g.fillRect(x, y, kw - 8, 56);
      g.fillStyle = 'rgba(255,255,255,0.06)'; g.fillRect(x, y, kw - 8, 6);
    }
  });
  return c;
}

export function linenTex(w = 512, h = 512, base = [64, 74, 92], seed = 5) {
  const c = canvas(w, h), g = c.getContext('2d'), rnd = mulberry32(seed);
  g.fillStyle = `rgb(${base})`; g.fillRect(0, 0, w, h);
  for (let i = 0; i < 2400; i++) {
    const v = (rnd() - 0.5) * 20;
    g.strokeStyle = `rgba(${base[0] + v},${base[1] + v},${base[2] + v},0.4)`; g.lineWidth = 1;
    const y = rnd() * h; g.beginPath(); g.moveTo(0, y); g.lineTo(w, y + (rnd() - 0.5) * 4); g.stroke();
  }
  return c;
}

export function sketchEyes(w = 1024, h = 720, k = 1) {
  // The protagonist's sketchbook page: warm "晨光" eyes being drawn; k = 0..1 progress.
  const c = canvas(w, h), g = c.getContext('2d');
  g.fillStyle = '#f7f3ea'; g.fillRect(0, 0, w, h);
  g.strokeStyle = 'rgba(120,110,95,0.25)'; g.lineWidth = 2;
  for (let y = 60; y < h; y += 48) { g.beginPath(); g.moveTo(40, y); g.lineTo(w - 40, y); g.stroke(); }
  const eye = (cx, cy, p) => {
    g.lineWidth = 5; g.strokeStyle = '#3a3128';
    g.beginPath(); g.arc(cx, cy, 120, 0, Math.PI * 2 * Math.min(1, p * 1.4)); g.stroke();
    if (p > 0.45) { g.fillStyle = `rgba(232,152,60,${Math.min(1, (p - 0.45) * 2.5)})`; g.beginPath(); g.arc(cx, cy + 6, 66, 0, 7); g.fill(); }
    if (p > 0.7) { g.fillStyle = '#2a1e14'; g.beginPath(); g.arc(cx, cy + 8, 34, 0, 7); g.fill(); g.fillStyle = '#fff'; g.beginPath(); g.arc(cx - 18, cy - 12, 12, 0, 7); g.fill(); }
    if (p > 0.85) { g.fillStyle = 'rgba(244,170,90,0.85)'; g.beginPath(); g.ellipse(cx, cy - 92, 118, 42, 0, Math.PI, 0); g.fill(); }
  };
  eye(300, 360, Math.min(1, k * 2));
  eye(720, 360, Math.max(0, k * 2 - 1));
  g.fillStyle = '#5a4a3a'; g.font = `48px ${HAND}`; g.fillText('晨光', 80, 120);
  return c;
}
