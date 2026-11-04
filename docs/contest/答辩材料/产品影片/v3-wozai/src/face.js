// SPDX-License-Identifier: Apache-2.0
// 周屿's face: a painted decal that follows the head and jaw surfaces of the
// Blender figure (blender/build_character.py) and rides on the head bone.
// Brows, eyes and mouth are drawn per frame from a small expression set, a
// deterministic blink schedule and the loudness of his own dialogue.
import * as THREE from 'three';
import { hash1, lerp } from './util.js';

// Rest-pose world frame (figure faces +Z). Ellipsoids from the build script.
const HEAD = { c: [0, 1.628, 0.012], r: [0.100, 0.120, 0.110] };
const JAW = { c: [0, 1.570, 0.040], r: [0.078, 0.070, 0.078] };
const BOX = { x0: -0.075, x1: 0.075, y0: 1.535, y1: 1.705 };
const PX = 512;
const INK = '#2a1d19', LIP = '#8d5446', HI = 'rgba(255,255,255,0.92)';

function surfZ(e, x, y) {
  const a = 1 - ((x - e.c[0]) / e.r[0]) ** 2 - ((y - e.c[1]) / e.r[1]) ** 2;
  return a > 0 ? e.c[2] + e.r[2] * Math.sqrt(a) : -1;
}

export function buildFaceGeometry() {
  const N = 48, pos = [], uv = [], idx = [];
  for (let j = 0; j <= N; j++) for (let i = 0; i <= N; i++) {
    const u = i / N, v = j / N;
    const x = lerp(BOX.x0, BOX.x1, u), y = lerp(BOX.y1, BOX.y0, v);
    let z = Math.max(surfZ(HEAD, x, y), surfZ(JAW, x, y));
    if (z < 0) z = 0.0;            // off the surface: hidden inside the head, transparent anyway
    pos.push(x, y, z + 0.0011); uv.push(u, 1 - v);
  }
  for (let j = 0; j < N; j++) for (let i = 0; i < N; i++) {
    const a = j * (N + 1) + i, b = a + 1, c = a + N + 1, d = c + 1;
    idx.push(a, c, b, b, c, d);
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  g.setAttribute('uv', new THREE.Float32BufferAttribute(uv, 2));
  g.setIndex(idx); g.computeVertexNormals();
  return g;
}

const cx = (x) => (x - BOX.x0) / (BOX.x1 - BOX.x0) * PX;
const cy = (y) => (BOX.y1 - y) / (BOX.y1 - BOX.y0) * PX;

// Expression presets: brow inner/outer lift (px, + = up), eye openness 0..1,
// mouth curve (+ = smile), mouth width scale.
const MOODS = {
  neutral: { browIn: 0, browOut: 0, open: 1, curve: 0.5, w: 1 },
  soft: { browIn: 2, browOut: 0, open: 0.9, curve: 4, w: 1.05 },
  smile: { browIn: 3, browOut: 1, open: 0.72, curve: 9, w: 1.12 },
  sad: { browIn: 9, browOut: -6, open: 0.78, curve: -4, w: 0.92 },
  tired: { browIn: 1, browOut: -3, open: 0.55, curve: -1, w: 0.95 },
  surprised: { browIn: 9, browOut: 8, open: 1.15, curve: 0, w: 0.85 },
  focus: { browIn: -5, browOut: 1, open: 0.85, curve: -0.5, w: 0.95 },
  wince: { browIn: 6, browOut: -4, open: 0.6, curve: -6, w: 1.0 },
  sleep: { browIn: 0, browOut: -2, open: 0, curve: 0, w: 0.9 },
  hold: { browIn: 7, browOut: -3, open: 0.8, curve: 2, w: 1.0 },  // moved, holding it in
};

export function faceKey(f, t) {
  const m = { ...MOODS[f.mood] || MOODS.neutral };
  const blink = f.mood === 'sleep' ? 0 : blinkAt(t, f.seed ?? 3);
  const open = m.open * (1 - blink);
  const talk = Math.round((f.talk || 0) * 6);
  return { m, open: Math.round(open * 12) / 12, talk, key: `${f.mood}|${Math.round(open * 12)}|${talk}` };
}

// Blink every 2.5–5.5 s, 0.16 s long; deterministic in t.
export function blinkAt(t, seed = 3) {
  const slot = Math.floor(t / 4), start = slot * 4 + hash1(slot * 7.3 + seed) * 3.2;
  const d = t - start;
  if (d < 0 || d > 0.16) return 0;
  return Math.sin(Math.PI * d / 0.16);
}

export function drawFace(g, f, t) {
  const { m, open, talk } = faceKey(f, t);
  g.clearRect(0, 0, PX, PX);
  g.lineCap = 'round'; g.lineJoin = 'round';
  for (const s of [-1, 1]) {
    const ex = cx(0.038 * s), ey = cy(1.640);
    // Eye: a dark bean with a highlight; lids cut it from the top as it closes.
    if (open > 0.08) {
      g.save();
      const rx = 19, ry = 23 * Math.min(1.15, Math.max(0.2, open));
      g.beginPath(); g.ellipse(ex, ey + (1 - Math.min(1, open)) * 9, rx, ry, 0, 0, 7); g.fillStyle = INK; g.fill();
      g.fillStyle = HI; g.beginPath(); g.arc(ex + 6 * s * -1 + 4, ey - ry * 0.35, 5.5, 0, 7); g.fill();
      g.restore();
      if (open < 0.9) { // heavy lid line
        g.strokeStyle = INK; g.lineWidth = 6;
        const ly = ey - 23 * open + 10;
        g.beginPath(); g.moveTo(ex - 24, ly + 2); g.quadraticCurveTo(ex, ly - 6, ex + 24, ly + 2); g.stroke();
      }
    } else { // closed: a soft downward arc
      g.strokeStyle = INK; g.lineWidth = 6;
      g.beginPath(); g.moveTo(ex - 22, ey + 4); g.quadraticCurveTo(ex, ey + 14, ex + 22, ey + 4); g.stroke();
    }
    // Brow: inner end near the nose (x→0).
    const bi = cx(0.018 * s), bo = cx(0.060 * s), by = cy(1.674);
    g.strokeStyle = '#231a18'; g.lineWidth = 13;
    g.beginPath(); g.moveTo(bi, by - m.browIn); g.quadraticCurveTo((bi + bo) / 2, by - 8 - (m.browIn + m.browOut) / 2, bo, by - m.browOut + 4); g.stroke();
  }
  // Mouth.
  const mx = cx(0), my = cy(1.572), hw = 30 * m.w;
  g.strokeStyle = LIP; g.fillStyle = '#5a2c25'; g.lineWidth = 7;
  if (talk > 0) {
    const h = 5 + talk * 3.4;
    g.beginPath(); g.ellipse(mx, my + 2, hw * (0.62 + talk * 0.03), h, 0, 0, 7); g.fill();
  } else {
    g.beginPath(); g.moveTo(mx - hw, my - m.curve * 0.4); g.quadraticCurveTo(mx, my + m.curve, mx + hw, my - m.curve * 0.4); g.stroke();
  }
}

export class Face {
  constructor(headBone) {
    const c = document.createElement('canvas'); c.width = c.height = PX;
    this.canvas = c; this.g = c.getContext('2d');
    this.tex = new THREE.CanvasTexture(c); this.tex.colorSpace = THREE.SRGBColorSpace; this.tex.anisotropy = 4;
    const mat = new THREE.MeshStandardMaterial({ map: this.tex, transparent: true, roughness: 0.62, metalness: 0,
      depthWrite: false, polygonOffset: true, polygonOffsetFactor: -2, polygonOffsetUnits: -2 });
    const geo = buildFaceGeometry();
    headBone.updateMatrixWorld(true);
    geo.applyMatrix4(new THREE.Matrix4().copy(headBone.matrixWorld).invert());
    this.mesh = new THREE.Mesh(geo, mat); this.mesh.renderOrder = 2; this.mesh.frustumCulled = false;
    headBone.add(this.mesh);
    this.key = '';
  }

  update(f, t) {
    const k = faceKey(f, t).key;
    if (k === this.key) return;
    this.key = k; drawFace(this.g, f, t); this.tex.needsUpdate = true;
  }
}
