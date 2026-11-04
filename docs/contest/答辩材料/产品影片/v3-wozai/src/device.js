// SPDX-License-Identifier: Apache-2.0
// 傻妞: AIDK AI Toy board in its clear acrylic case, modelled from the public
// Beken layout drawing (60 x 50 mm) and the owner's rectified top photo
// (assets/textures/aidk_top.jpg, see SOURCES.md). The two round displays are
// live canvases drawing the project's real eye pack (shaniu-cyan-v2) and the
// firmware's own focus / approval / result glyphs (app/bk7258/bk7258_focus_pixels.h).
import * as THREE from 'three';
import { RoundedBoxGeometry } from 'three/addons/geometries/RoundedBoxGeometry.js';
import { clamp, hash1, mulberry32, smooth } from './util.js';

const MM = 0.001;
// Board millimetres (drawing origin lower-left) → device local metres, centred.
const bx = (x) => (x - 30) * MM;
const by = (y) => (y - 25) * MM;

export const EYE_CROPS = {
  neutral: [10, 25], blink_half: [310, 25], blink_closed: [612, 25], sleepy: [916, 25],
  look_left: [10, 308], look_right: [310, 308], look_up: [612, 308], look_down: [916, 308],
  happy: [10, 595], shy: [310, 595], sad: [612, 595], surprised: [916, 595],
  thinking: [10, 878], listening: [310, 878], speaking: [612, 878], error: [916, 878], offline: [916, 25],
};

const SCREEN_PX = 512;
const CENTERS = { L: [13.4, 24.0], R: [45.3, 22.0] };

function qrPattern(seed) {
  const n = 25, rnd = mulberry32(seed), g = [];
  for (let y = 0; y < n; y++) { g.push([]); for (let x = 0; x < n; x++) g[y].push(rnd() < 0.47 ? 1 : 0); }
  const finder = (ox, oy) => {
    for (let y = -1; y < 8; y++) for (let x = -1; x < 8; x++) {
      const gx = ox + x, gy = oy + y; if (gx < 0 || gy < 0 || gx >= n || gy >= n) continue;
      const ring = Math.max(Math.abs(x - 3), Math.abs(y - 3));
      g[gy][gx] = (x < 0 || y < 0 || x > 6 || y > 6) ? 0 : (ring === 3 || ring <= 1 ? 1 : 0);
    }
  };
  finder(0, 0); finder(n - 7, 0); finder(0, n - 7);
  for (let i = 8; i < n - 8; i++) { g[6][i] = i % 2 === 0 ? 1 : 0; g[i][6] = i % 2 === 0 ? 1 : 0; }
  return g;
}

export class Device {
  constructor(assets) {
    this.assets = assets;
    this.group = new THREE.Group();
    this.board = new THREE.Group();
    this.group.add(this.board);
    this.canvases = {};
    this.textures = {};
    this.lastKey = { L: '', R: '' };
    this.qr = qrPattern(1203);
    this._sprites(assets.eyeSheet);
    this._build(assets);
  }

  _sprites(sheet) {
    // Crop each expression once; also build the warm "晨光" pack the
    // protagonist draws in the film (a hue rotation of the same sprites).
    this.sprites = { cyan: {}, warm: {} };
    for (const [name, [sx, sy]] of Object.entries(EYE_CROPS)) {
      for (const pack of ['cyan', 'warm']) {
        const c = document.createElement('canvas');
        c.width = c.height = 300;
        const g = c.getContext('2d');
        if (pack === 'warm') g.filter = 'hue-rotate(-152deg) saturate(1.15) brightness(1.04)';
        g.drawImage(sheet, sx, sy, 300, 300, 0, 0, 300, 300);
        this.sprites[pack][name] = c;
      }
    }
  }

  _build(assets) {
    const dark = new THREE.MeshStandardMaterial({ color: 0x121512, roughness: 0.55, metalness: 0.1 });
    const boardTex = assets.boardTex;
    boardTex.colorSpace = THREE.SRGBColorSpace;
    boardTex.anisotropy = 4;
    const top = new THREE.MeshStandardMaterial({ map: boardTex, roughness: 0.42, metalness: 0.12 });
    // PCB: 60 x 50 x 1.6 mm with the photo on the top (+Z) face.
    const pcb = new THREE.Mesh(new RoundedBoxGeometry(60 * MM, 50 * MM, 1.6 * MM, 2, 1.0 * MM), [dark, dark, dark, dark, top, dark]);
    pcb.position.z = -0.8 * MM;
    pcb.castShadow = pcb.receiveShadow = true;
    this.board.add(pcb);

    const black = new THREE.MeshStandardMaterial({ color: 0x0b0b0d, roughness: 0.38, metalness: 0.2 });
    const plastic = new THREE.MeshStandardMaterial({ color: 0x16161a, roughness: 0.6 });
    // Round displays: bezel, live screen, glass.
    this.screenMesh = {};
    for (const side of ['L', 'R']) {
      const [cx, cy] = CENTERS[side];
      const bez = new THREE.Mesh(new THREE.CylinderGeometry(10.9 * MM, 10.9 * MM, 2.4 * MM, 48), black);
      bez.rotation.x = Math.PI / 2;
      bez.position.set(bx(cx), by(cy), 1.2 * MM);
      bez.castShadow = true;
      this.board.add(bez);
      const c = document.createElement('canvas');
      c.width = c.height = SCREEN_PX;
      this.canvases[side] = c;
      const tex = new THREE.CanvasTexture(c);
      tex.colorSpace = THREE.SRGBColorSpace;
      tex.anisotropy = 4;
      this.textures[side] = tex;
      const screen = new THREE.Mesh(new THREE.CircleGeometry(9.7 * MM, 64),
        new THREE.MeshBasicMaterial({ map: tex, toneMapped: false }));
      screen.position.set(bx(cx), by(cy), 2.42 * MM);
      this.board.add(screen);
      this.screenMesh[side] = screen;
      const glass = new THREE.Mesh(new THREE.CircleGeometry(10.2 * MM, 64),
        new THREE.MeshPhysicalMaterial({ color: 0x000000, roughness: 0.06, metalness: 0.0, transparent: true,
          opacity: 0.22, clearcoat: 1, clearcoatRoughness: 0.05, envMapIntensity: 1.6, depthWrite: false }));
      glass.position.set(bx(cx), by(cy), 2.5 * MM);
      glass.renderOrder = 2;
      this.board.add(glass);
    }
    // Dust that settles while she is switched off (film act three).
    const dc = document.createElement('canvas'); dc.width = dc.height = 256;
    const dg = dc.getContext('2d'); const rnd = mulberry32(77);
    for (let i = 0; i < 380; i++) { dg.fillStyle = `rgba(200,196,188,${0.10 + rnd() * 0.35})`; const r = rnd() * 1.1 + 0.25; dg.beginPath(); dg.arc(rnd() * 256, rnd() * 256, r, 0, 7); dg.fill(); }
    this.dustMat = new THREE.MeshBasicMaterial({ map: new THREE.CanvasTexture(dc), transparent: true, opacity: 0, depthWrite: false });
    for (const side of ['L', 'R']) {
      const [cx, cy] = CENTERS[side];
      const d = new THREE.Mesh(new THREE.CircleGeometry(10.2 * MM, 48), this.dustMat);
      d.position.set(bx(cx), by(cy), 2.56 * MM); d.renderOrder = 3; this.board.add(d);
    }
    // Camera module and lens.
    const cam = new THREE.Mesh(new RoundedBoxGeometry(8.5 * MM, 8.3 * MM, 4.2 * MM, 2, 0.6 * MM), black);
    cam.position.set(bx(17.8), by(46.3), 2.1 * MM); cam.castShadow = true; this.board.add(cam);
    const lens = new THREE.Mesh(new THREE.CylinderGeometry(2.4 * MM, 2.6 * MM, 1.6 * MM, 32),
      new THREE.MeshPhysicalMaterial({ color: 0x050608, roughness: 0.05, clearcoat: 1, metalness: 0.3 }));
    lens.rotation.x = Math.PI / 2; lens.position.set(bx(17.8), by(46.3), 4.9 * MM); this.board.add(lens);
    this.lensPos = new THREE.Vector3(bx(17.8), by(46.3), 6 * MM);
    // Microphone.
    const mic = new THREE.Mesh(new THREE.CylinderGeometry(3.1 * MM, 3.1 * MM, 3.5 * MM, 32), plastic);
    mic.rotation.x = Math.PI / 2; mic.position.set(bx(28.8), by(6.3), 1.75 * MM); this.board.add(mic);
    // Keys S1 (K1, volume -), S2 (K2, power), S3 (K3, volume +).
    this.keys = {};
    const keyMat = new THREE.MeshStandardMaterial({ color: 0x2a2a2e, roughness: 0.5, metalness: 0.4 });
    [['K1', 37.0], ['K2', 43.3], ['K3', 49.5]].forEach(([n, x]) => {
      const k = new THREE.Group();
      const base = new THREE.Mesh(new THREE.BoxGeometry(4.2 * MM, 3.2 * MM, 1.4 * MM), keyMat);
      base.position.z = 0.7 * MM; k.add(base);
      const cap = new THREE.Mesh(new THREE.BoxGeometry(1.6 * MM, 1.2 * MM, 0.9 * MM), new THREE.MeshStandardMaterial({ color: 0x0c0c0c, roughness: 0.6 }));
      cap.position.set(0, -1.4 * MM, 0.9 * MM); k.add(cap);
      k.position.set(bx(x), by(2.6), 0); this.board.add(k); this.keys[n] = { group: k, cap };
    });
    // USB-C receptacles.
    const steel = new THREE.MeshStandardMaterial({ color: 0xb8bcc2, roughness: 0.3, metalness: 0.9 });
    const usb1 = new THREE.Mesh(new RoundedBoxGeometry(7.4 * MM, 9.0 * MM, 3.2 * MM, 2, 1.2 * MM), steel);
    usb1.position.set(bx(1.0), by(14.5), 1.6 * MM); this.board.add(usb1);
    const usb2 = new THREE.Mesh(new RoundedBoxGeometry(9.0 * MM, 7.4 * MM, 3.2 * MM, 2, 1.2 * MM), steel);
    usb2.position.set(bx(24.5), by(1.0), 1.6 * MM); this.board.add(usb2);
    this.usbPort = new THREE.Vector3(bx(24.5), by(-2.0), 1.6 * MM);
    // Nylon standoffs with screw caps, clear acrylic base plate.
    const nylon = new THREE.MeshStandardMaterial({ color: 0xe6e3dc, roughness: 0.55 });
    this.kitParts = [];
    for (const [x, y] of [[3, 3], [57, 3], [3, 47], [57, 47]]) {
      const post = new THREE.Mesh(new THREE.CylinderGeometry(2.6 * MM, 2.6 * MM, 9.6 * MM, 6), nylon);
      post.rotation.x = Math.PI / 2; post.position.set(bx(x), by(y), -6.4 * MM); this.board.add(post);
      const capm = new THREE.Mesh(new THREE.SphereGeometry(3.0 * MM, 20, 12, 0, Math.PI * 2, 0, Math.PI / 2), nylon);
      capm.rotation.x = Math.PI / 2; capm.position.set(bx(x), by(y), 0.2 * MM); capm.scale.set(1, 0.75, 1); this.board.add(capm);
      this.kitParts.push(post, capm);
    }
    const acrylic = new THREE.MeshPhysicalMaterial({ color: 0xdfeff2, roughness: 0.08, transparent: true, opacity: 0.28,
      clearcoat: 1, envMapIntensity: 1.3, depthWrite: false });
    const plate = new THREE.Mesh(new RoundedBoxGeometry(64 * MM, 54 * MM, 3 * MM, 2, 2 * MM), acrylic);
    plate.position.z = -12.6 * MM; plate.renderOrder = 1; this.board.add(plate);
    this.plate = plate; this.kitParts.push(plate);
    // Walnut wedge stand (a prop in the film, not part of the kit).
    this.stand = new THREE.Group();
    const walnut = new THREE.MeshStandardMaterial({ color: 0x5b3a24, roughness: 0.55 });
    const wedgeShape = new THREE.Shape([new THREE.Vector2(-0.02, 0), new THREE.Vector2(0.034, 0), new THREE.Vector2(0.034, 0.018), new THREE.Vector2(-0.006, 0.012)]);
    const wedge = new THREE.Mesh(new THREE.ExtrudeGeometry(wedgeShape, { depth: 0.078, bevelEnabled: true, bevelSize: 0.0015, bevelThickness: 0.0015, bevelSegments: 2 }), walnut);
    wedge.rotation.y = Math.PI / 2; wedge.position.set(-0.039, 0, 0.0);
    wedge.castShadow = wedge.receiveShadow = true;
    this.stand.add(wedge);
    this.group.add(this.stand);
    // Board leans back 24 degrees from vertical, resting in the stand.
    this.board.rotation.x = -THREE.MathUtils.degToRad(24);
    this.board.position.set(0, 0.0345, 0.006);
    // Eye light: two small area lights on the screen faces. They emit forward
    // only, so the desk, the card and his face catch her light while the board
    // itself stays dark (the screens are the light source, not the PCB).
    this.glowLights = [];
    for (const side of ['L', 'R']) {
      const [cx, cy] = CENTERS[side];
      const l = new THREE.RectAreaLight(0x3fd2ff, 0, 19 * MM, 19 * MM);
      l.position.set(bx(cx), by(cy), 3.2 * MM);
      l.rotation.y = Math.PI;            // a RectAreaLight emits along its -Z; face it out of the screen
      this.board.add(l); this.glowLights.push(l);
    }
    const lights = this.glowLights;
    this.glow = { set intensity(v) { for (const l of lights) l.intensity = v; }, color: { set: (c) => { for (const l of lights) l.color.set(c); } } };
  }

  // Draw one round screen. d = {mode, expr, pack, brightness, ring, digits, look}
  drawScreen(side, d, t) {
    const key = JSON.stringify(d) + (d.mode === 'eye' && d.anim ? Math.floor(t * 30) : '');
    if (key === this.lastKey[side]) return;
    this.lastKey[side] = key;
    const c = this.canvases[side], g = c.getContext('2d'), S = SCREEN_PX, R = S / 2;
    g.save();
    g.fillStyle = '#000'; g.fillRect(0, 0, S, S);
    g.beginPath(); g.arc(R, R, R, 0, Math.PI * 2); g.clip();
    const b = clamp(d.brightness ?? 1);
    if (b > 0) {
      g.globalAlpha = b;
      if (d.mode === 'eye') {
        const sp = this.sprites[d.pack || 'cyan'][d.expr] || this.sprites.cyan.neutral;
        const s = S * 1.02; g.drawImage(sp, (S - s) / 2, (S - s) / 2, s, s);
      } else if (d.mode === 'qr') {
        const n = this.qr.length, box = S * 0.6, cell = box / n, o = (S - box) / 2;
        g.fillStyle = '#f4f4f0'; g.fillRect(o - cell * 2, o - cell * 2, box + cell * 4, box + cell * 4);
        g.fillStyle = '#0b0b0b';
        for (let y = 0; y < n; y++) for (let x = 0; x < n; x++) if (this.qr[y][x]) g.fillRect(o + x * cell, o + y * cell, cell + 0.5, cell + 0.5);
      } else {
        this._glyph(g, d, S);
      }
    }
    g.restore();
    this.textures[side].needsUpdate = true;
  }

  // Firmware glyphs on a 160-pixel logical display, scaled to the canvas.
  _glyph(g, d, S) {
    const k = S / 160, C = S / 2;
    const px = (x, y, w, h, col) => { g.fillStyle = col; g.fillRect(C + x * k, C + y * k, w * k, h * k); };
    const ringN = d.segments ?? 0, amber = d.mode === 'approval';
    if (d.mode === 'focus' || d.mode === 'approval' || d.mode === 'check' || d.ring) {
      for (let i = 0; i < 32; i++) {
        const a = -Math.PI / 2 + i * Math.PI * 2 / 32;
        const lit = i < ringN;
        g.fillStyle = lit ? (amber ? '#ffa400' : '#00fcff') : '#22242a';
        g.beginPath(); g.arc(C + Math.cos(a) * 56 * k, C + Math.sin(a) * 56 * k, 4 * k, 0, 7); g.fill();
      }
    }
    if (d.mode === 'approval') {
      px(-4, -26, 9, 33, '#ffa400'); px(-4, 14, 9, 9, '#ffa400');
    } else if (d.mode === 'check') {
      g.strokeStyle = '#18e04a'; g.lineWidth = 7 * k; g.lineCap = 'round'; g.lineJoin = 'round';
      g.beginPath(); g.moveTo(C - 16 * k, C + 4 * k); g.lineTo(C - 2 * k, C + 17 * k); g.lineTo(C + 22 * k, C - 12 * k); g.stroke();
    } else if (d.mode === 'cross') {
      g.strokeStyle = '#ff2a1e'; g.lineWidth = 7 * k; g.lineCap = 'round';
      g.beginPath(); g.moveTo(C - 19 * k, C - 19 * k); g.lineTo(C + 19 * k, C + 19 * k); g.moveTo(C + 19 * k, C - 19 * k); g.lineTo(C - 19 * k, C + 19 * k); g.stroke();
    } else if (d.mode === 'focus') {
      const s = Math.max(0, Math.round(d.seconds ?? 0));
      const txt = `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`;
      g.fillStyle = d.paused ? '#ffe600' : '#ffe600';
      g.font = `600 ${26 * k}px "Noto Sans SC", sans-serif`; g.textAlign = 'center'; g.textBaseline = 'middle';
      g.fillText(txt, C, C - 1 * k);
      if (d.paused) { px(-12, 20, 24, 4, '#ffe600'); }
      // small inner ring of the running timer
      g.strokeStyle = '#00fcff'; g.lineWidth = 2.2 * k; g.beginPath(); g.arc(C, C, 22 * k + 18 * k, 0, 0.001); g.stroke();
    }
  }

  // Blink/glance schedule for idle eyes, deterministic in t.
  static idleExpr(t, seed = 0) {
    const period = 4.6 + 1.7 * hash1(Math.floor(t / 5.3) + seed);
    const ph = t % period;
    if (ph < 0.06 || ph > period - 0.04) return 'blink_half';
    if (ph < 0.14) return 'blink_closed';
    if (ph < 0.2) return 'blink_half';
    const g = Math.floor(t / 7.7 + seed * 0.3);
    const glance = hash1(g * 3.1 + seed);
    const gp = (t / 7.7 + seed * 0.3) - g;
    if (glance > 0.62 && gp > 0.45 && gp < 0.62) return glance > 0.81 ? 'look_left' : 'look_right';
    return 'neutral';
  }

  // Bare development board (gift) vs. assembled with standoffs and acrylic.
  setAssembled(v) { for (const m of this.kitParts) m.visible = v; }

  // Resolve one frame: desc = {mode, expr?, pack?, brightness, glow, dust, ...}
  update(desc, t) {
    const both = desc.mode === 'eye' && desc.expr === 'idle'
      ? { ...desc, expr: Device.idleExpr(t, desc.seed || 0), anim: false } : desc;
    this.drawScreen('L', both, t);
    this.drawScreen('R', both, t);
    const b = clamp(desc.brightness ?? 1);
    this.glow.intensity = (desc.glow ?? 0.06) * b * 1500;     // ~glow candela spread over two 19 mm screens
    this.glow.color.set(desc.mode === 'approval' ? 0xffa64a : (desc.pack === 'warm' ? 0xffc27a : 0x3fd2ff));
    this.dustMat.opacity = clamp(desc.dust ?? 0) * 0.45;
    // K2 travel for the press shots.
    const press = clamp(desc.k2 ?? 0);
    this.keys.K2.cap.position.z = (0.9 - 0.4 * press) * MM;
  }
}
