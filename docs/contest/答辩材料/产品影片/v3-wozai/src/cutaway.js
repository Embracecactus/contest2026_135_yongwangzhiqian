// SPDX-License-Identifier: Apache-2.0
// Inside the BK7258: the camera dives through her eyes onto the die whenever
// the story asks how she works. Three uses:
//   voice   — first "我在": wake word on the AP, the rest via CP's Wi-Fi to the
//             cloud (听 / 想 / 说) and back as a stream.
//   ota     — a signed image lands in slot B; A is kept; data is untouched.
//   offline — CP searches for Wi-Fi; the AP half keeps her local abilities.
// Facts: README 系统架构, docs/platforms/bk7258/shaniu-feature-inventory.md,
// script/features.json (F07 F08 F10 F25 F45 F46 F49 F50).
// Text is not drawn here: render() returns labels anchored to 3D points and the
// 2D overlay draws them large and crisp (overlay.js labels()).
import * as THREE from 'three';
import { RoundedBoxGeometry } from 'three/addons/geometries/RoundedBoxGeometry.js';
import { clamp, easeInOut, easeOut, lerp, range, smooth, track, pulse, mulberry32 } from './util.js';

const V = (x, y, z) => new THREE.Vector3(x, y, z);
const SANS = '"Noto Sans SC", sans-serif';

// Die texture: dark silicon, a fine routing grid, gold buses, vias.
function dieTexture() {
  const N = 2048, c = document.createElement('canvas'); c.width = c.height = N;
  const g = c.getContext('2d'), r = mulberry32(7258);
  g.fillStyle = '#05080c'; g.fillRect(0, 0, N, N);
  // Standard-cell rows.
  for (let y = 0; y < N; y += 12) { g.fillStyle = (y / 12) % 2 ? '#070c11' : '#060a0e'; g.fillRect(0, y, N, 12); }
  // Routing: short orthogonal segments.
  for (let i = 0; i < 2600; i++) {
    const x = r() * N, y = r() * N, L = 20 + r() * 220, h = r() < 0.5;
    g.strokeStyle = r() < 0.7 ? 'rgba(90,150,180,0.16)' : 'rgba(200,160,90,0.18)'; g.lineWidth = 1 + (r() < 0.1 ? 2 : 0);
    g.beginPath(); g.moveTo(x, y); g.lineTo(h ? x + L : x, h ? y : y + L); g.stroke();
  }
  // Buses.
  g.strokeStyle = 'rgba(214,170,92,0.42)'; g.lineWidth = 5;
  for (let i = 0; i < 18; i++) { const y = 80 + i * 112; g.beginPath(); g.moveTo(0, y); g.lineTo(N, y); g.stroke(); }
  g.strokeStyle = 'rgba(120,190,220,0.25)'; g.lineWidth = 4;
  for (let i = 0; i < 18; i++) { const x = 60 + i * 114; g.beginPath(); g.moveTo(x, 0); g.lineTo(x, N); g.stroke(); }
  // Vias.
  for (let i = 0; i < 1600; i++) { g.fillStyle = 'rgba(230,200,140,0.35)'; g.fillRect(r() * N, r() * N, 3, 3); }
  const t = new THREE.CanvasTexture(c); t.colorSpace = THREE.SRGBColorSpace; t.anisotropy = 8;
  return t;
}

// A core's floorplan: a grid of cells with a few bright blocks.
function coreTexture(seed, tint) {
  const N = 512, c = document.createElement('canvas'); c.width = c.height = N;
  const g = c.getContext('2d'), r = mulberry32(seed);
  g.fillStyle = '#0a1418'; g.fillRect(0, 0, N, N);
  for (let y = 0; y < N; y += 16) for (let x = 0; x < N; x += 16) { g.fillStyle = `rgba(${tint},${0.05 + r() * 0.16})`; g.fillRect(x + 1, y + 1, 14, 14); }
  for (let i = 0; i < 7; i++) { g.fillStyle = `rgba(${tint},${0.25 + r() * 0.3})`; g.fillRect(32 + r() * 380, 32 + r() * 380, 40 + r() * 90, 30 + r() * 70); }
  g.strokeStyle = `rgba(${tint},0.9)`; g.lineWidth = 10; g.strokeRect(5, 5, N - 10, N - 10);
  const t = new THREE.CanvasTexture(c); t.colorSpace = THREE.SRGBColorSpace;
  return t;
}

// A floating card with big text (cloud stations, slots, the package).
function card(lines, w, h, opts = {}) {
  const S = 512, c = document.createElement('canvas'); c.width = S; c.height = Math.round(S * h / w);
  const g = c.getContext('2d');
  g.fillStyle = opts.bg || '#0f1a22'; g.beginPath(); g.roundRect(0, 0, c.width, c.height, 36); g.fill();
  g.strokeStyle = opts.edge || 'rgba(160,230,255,0.85)'; g.lineWidth = 8; g.beginPath(); g.roundRect(4, 4, c.width - 8, c.height - 8, 34); g.stroke();
  g.textAlign = 'center'; g.fillStyle = opts.ink || '#eaf6f8';
  lines.forEach(([txt, size, weight, col], i) => { g.font = `${weight || 600} ${size}px ${SANS}`; g.fillStyle = col || opts.ink || '#eaf6f8'; g.fillText(txt, S / 2, c.height * (lines.length === 1 ? 0.62 : 0.42 + i * 0.32)); });
  const tex = new THREE.CanvasTexture(c); tex.colorSpace = THREE.SRGBColorSpace;
  const m = new THREE.Mesh(new THREE.PlaneGeometry(w, h), new THREE.MeshBasicMaterial({ map: tex, transparent: true, toneMapped: false }));
  m.userData.mat = m.material;
  return m;
}

export class Cutaway {
  constructor(E) {
    this.E = E;
    this.scene = new THREE.Scene();
    this.scene.background = new THREE.Color(0x030508);
    this.scene.fog = new THREE.FogExp2(0x030508, 0.075);
    this.camera = new THREE.PerspectiveCamera(38, 16 / 9, 0.01, 80);
    this.labels = [];
    this.build();
  }

  async ready() {}

  build() {
    const S = this.scene;
    S.add(new THREE.HemisphereLight(0x5a7ea8, 0x080604, 0.5));
    const key = new THREE.DirectionalLight(0xcfe4ff, 1.0); key.position.set(-3, 6, 4); S.add(key);
    // Die.
    const dieTex = dieTexture();
    const die = new THREE.Mesh(new THREE.PlaneGeometry(9, 9), new THREE.MeshStandardMaterial({ map: dieTex, emissiveMap: dieTex, emissive: 0xffffff, emissiveIntensity: 0.55, roughness: 0.32, metalness: 0.7 }));
    die.rotation.x = -Math.PI / 2; S.add(die); this.die = die;
    // Package edge: the chip's outer ring, so the dive reads as entering a chip.
    const ring = new THREE.Mesh(new THREE.RingGeometry(6.2, 7.6, 4, 1), new THREE.MeshStandardMaterial({ color: 0x15171b, roughness: 0.6 }));
    ring.rotation.set(-Math.PI / 2, 0, Math.PI / 4); ring.position.y = -0.01; S.add(ring);
    // Cores: flat silicon tiles with glowing borders.
    this.cores = {};
    const tile = (name, x, z, w, d, tint, seed) => {
      const tex = coreTexture(seed, tint);
      const m = new THREE.Mesh(new RoundedBoxGeometry(w, 0.10, d, 2, 0.02), new THREE.MeshStandardMaterial({ map: tex, emissiveMap: tex, emissive: 0xffffff, emissiveIntensity: 0.25, roughness: 0.3, metalness: 0.4 }));
      m.position.set(x, 0.05, z); S.add(m); this.cores[name] = m;
      const light = new THREE.PointLight(new THREE.Color(`rgb(${tint})`), 0, 2.4, 2); light.position.set(x, 0.5, z); S.add(light); m.userData.light = light;
      return m;
    };
    tile('cpu0', -1.55, 0.0, 1.25, 1.25, '70,180,230', 11);
    tile('cpu1', 0.55, 0.0, 0.95, 1.25, '80,230,170', 12);
    tile('cpu2', 1.6, 0.0, 0.95, 1.25, '80,230,170', 13);
    // SMP bridge between CPU1 and CPU2.
    const bridge = new THREE.Mesh(new THREE.BoxGeometry(0.16, 0.06, 0.7), new THREE.MeshBasicMaterial({ color: 0x5fe6b0, toneMapped: false, transparent: true, opacity: 0.8 }));
    bridge.position.set(1.075, 0.06, 0); S.add(bridge);
    // Wake model block and the "我在" asset block on the AP side.
    this.kws = new THREE.Mesh(new RoundedBoxGeometry(0.42, 0.07, 0.3, 2, 0.015), new THREE.MeshBasicMaterial({ color: 0x9fffe0, toneMapped: false, transparent: true, opacity: 0.25 }));
    this.kws.position.set(0.55, 0.14, -0.28); S.add(this.kws);
    this.wav = new THREE.Group();
    for (let i = 0; i < 18; i++) { const b = new THREE.Mesh(new THREE.BoxGeometry(0.018, 0.02 + Math.abs(Math.sin(i * 1.7)) * 0.11, 0.018), new THREE.MeshBasicMaterial({ color: 0xffe6b0, toneMapped: false, transparent: true, opacity: 0.3 })); b.position.set(-0.17 + i * 0.02, 0.14, 0); this.wav.add(b); }
    this.wav.position.set(0.55, 0, 0.32); S.add(this.wav);
    // RPMsg: a channel CP ↔ AP with pulses.
    const curve = new THREE.CatmullRomCurve3([V(-0.92, 0.12, 0.25), V(-0.45, 0.42, 0.32), V(0.08, 0.12, 0.25)]);
    this.rpmsgCurve = curve;
    S.add(new THREE.Mesh(new THREE.TubeGeometry(curve, 48, 0.035, 10), new THREE.MeshBasicMaterial({ color: 0x7fd8ff, transparent: true, opacity: 0.35, toneMapped: false })));
    this.pulses = [];
    for (let i = 0; i < 5; i++) { const p = new THREE.Mesh(new THREE.SphereGeometry(0.04, 14, 10), new THREE.MeshBasicMaterial({ color: 0xd8fbff, toneMapped: false })); S.add(p); this.pulses.push(p); }
    // Sound in (from the microphone) and out (to the speaker): rings.
    this.waveIn = new THREE.Group(); this.waveOut = new THREE.Group();
    for (const grp of [this.waveIn, this.waveOut]) for (let i = 0; i < 4; i++) { const r = new THREE.Mesh(new THREE.TorusGeometry(0.25, 0.012, 8, 64), new THREE.MeshBasicMaterial({ color: 0xcdeeff, transparent: true, toneMapped: false })); grp.add(r); }
    this.waveIn.position.set(3.0, 0.35, 0.0); this.waveIn.rotation.y = Math.PI / 2; S.add(this.waveIn);
    this.waveOut.position.set(1.1, 0.35, 1.6); S.add(this.waveOut);
    // Cloud: three stations floating above, reached through CP's Wi-Fi.
    this.cloud = new THREE.Group();
    this.st = [
      card([['听', 120, 700], ['语音识别', 52, 500, '#b8d4e6']], 1.0, 0.72, { edge: 'rgba(130,180,230,0.9)' }),
      card([['想', 120, 700], ['对话模型', 52, 500, '#d0c4ee']], 1.0, 0.72, { edge: 'rgba(180,160,240,0.9)' }),
      card([['说', 120, 700], ['语音合成', 52, 500, '#b8e6d8']], 1.0, 0.72, { edge: 'rgba(120,220,190,0.9)' }),
    ];
    this.st.forEach((m, i) => { m.position.set(-1.3 + i * 1.3, 0, 0); this.cloud.add(m); });
    this.cloud.position.set(-0.3, 2.9, -1.2); S.add(this.cloud);
    // A sentence as light: a bright head with a fading trail.
    this.trail = [];
    for (let i = 0; i < 14; i++) { const p = new THREE.Mesh(new THREE.SphereGeometry(0.055 * (1 - i / 16), 12, 8), new THREE.MeshBasicMaterial({ color: 0xfff0c8, transparent: true, opacity: 1 - i / 14, toneMapped: false })); S.add(p); this.trail.push(p); }
    // Flash (separate chip on the board, drawn on the far edge of the die):
    this.flash = new THREE.Group();
    const slot = (y, col, label) => {
      const m = card([[label[0], 64, 700], [label[1], 40, 500, '#cfe0e6']], 1.9, 0.5, { edge: col, bg: '#0c141a' });
      m.position.set(0, y, 0); this.flash.add(m); return m;
    };
    this.slotA = slot(0.0, 'rgba(120,220,180,0.9)', ['A 槽', '当前固件 · 保留']);
    this.slotB = slot(0.62, 'rgba(120,220,180,0.9)', ['B 槽', '新固件 · 签名 CP + AP']);
    this.slotD = slot(1.24, 'rgba(240,200,110,0.95)', ['数据区', '记忆与设置 · 不动']);
    this.flash.position.set(0.1, 0.45, -2.6); S.add(this.flash);
    this.pkg = card([['0.7.129', 64, 700, '#1c1608'], ['已签名', 44, 600, '#5a4310']], 0.9, 0.5, { bg: '#f1dfa6', edge: 'rgba(120,90,20,0.9)', ink: '#1c1608' });
    S.add(this.pkg);
    this.check = card([['试运行通过 · 确认', 56, 700, '#0d2a1f']], 1.6, 0.36, { bg: '#7ff0c0', edge: 'rgba(20,90,60,0.9)', ink: '#0d2a1f' });
    S.add(this.check);
    // Searching ring (offline: CP looks for Wi-Fi).
    this.search = new THREE.Mesh(new THREE.RingGeometry(0.6, 0.64, 64), new THREE.MeshBasicMaterial({ color: 0xffc977, transparent: true, toneMapped: false, side: THREE.DoubleSide }));
    this.search.rotation.x = -Math.PI / 2; S.add(this.search);
  }

  // Anchor helper: a label attached to a world point (overlay draws it).
  L(text, sub, at, a, opts = {}) { if (a > 0.01) this.labels.push({ text, sub, at, a, ...opts }); }

  render(kind, t, k, shot) {
    const E = this.E;
    this.labels = [];
    const lt = t - shot.t0, dur = shot.t1 - shot.t0;
    for (const m of [this.pkg, this.check, this.search, this.cloud, this.flash, this.waveIn, this.waveOut]) m.visible = false;
    this.trail.forEach((p) => { p.visible = false; });
    this.pulses.forEach((p, i) => { const u = ((lt * 0.45 + i / this.pulses.length) % 1); p.position.copy(this.rpmsgCurve.getPointAt(u)); p.visible = true; });
    const glow = (name, v) => { const m = this.cores[name]; m.material.emissiveIntensity = 0.2 + v; m.userData.light.intensity = v * 2.2; };
    const rings = (grp, a, speed = 1) => { grp.visible = a > 0.01; grp.children.forEach((r, i) => { const u = (lt * 0.7 * speed + i / 4) % 1; r.scale.setScalar(0.6 + u * 2.2); r.material.opacity = a * (1 - u); }); };
    const head = (path) => { // path: [[t, V]...]; draw the head + trail along it
      for (let j = 0; j < this.trail.length; j++) {
        const tt = lt - j * 0.035;
        if (tt < path[0][0] || tt > path[path.length - 1][0]) { this.trail[j].visible = false; continue; }
        for (let i = 0; i < path.length - 1; i++) if (tt >= path[i][0] && tt < path[i + 1][0]) {
          this.trail[j].position.lerpVectors(path[i][1], path[i + 1][1], easeInOut((tt - path[i][0]) / (path[i + 1][0] - path[i][0])));
        }
        this.trail[j].visible = true;
      }
    };
    let cam;
    this.kws.material.opacity = 0.2; this.wav.children.forEach((b) => { b.material.opacity = 0.25; });
    this.st.forEach((m) => { m.material.opacity = 1; m.material.color.setScalar(1); });
    if (kind === 'voice') {
      // Paced to N04 (story time; the edit plays this cutaway 1.4x slower):
      // wake on the AP 1.6–5.0 · out through CP 5.2–6.6 · 听/想/说 6.6–8.0 ·
      // back as a stream 8.0–9.0 · offline: AP awake, no answer 9.0–12.2.
      const off = smooth(range(lt, 9.0, 9.6));
      const wake = pulse(lt, 1.4, 5.0, 0.3);
      glow('cpu1', (0.25 + 0.75 * wake + 0.5 * pulse(lt, 8.2, 9.2, 0.25)) * (1 - off) + off * (0.7 + 0.1 * Math.sin(lt * 1.6)));
      glow('cpu2', (0.25 + 0.75 * wake + 0.5 * pulse(lt, 8.2, 9.2, 0.25)) * (1 - off) + off * (0.7 + 0.1 * Math.sin(lt * 1.6 + 1)));
      glow('cpu0', (0.12 + 0.8 * pulse(lt, 5.4, 6.8, 0.3) + 0.8 * pulse(lt, 7.8, 8.6, 0.25)) * (1 - off) + off * (0.05 + 0.15 * (0.5 + 0.5 * Math.sin(lt * 4))));
      rings(this.waveIn, pulse(lt, 0.8, 3.2, 0.4));
      rings(this.waveOut, pulse(lt, 8.3, 9.2, 0.25), 1.3);
      this.kws.material.opacity = 0.2 + 0.7 * pulse(lt, 1.6, 5.0, 0.25) + 0.3 * off;
      this.wav.children.forEach((b, i) => { b.material.opacity = 0.25 + 0.75 * pulse(lt, 2.6 + i * 0.02, 4.9, 0.2); });
      this.cloud.visible = true;
      this.st.forEach((m, i) => { const on = pulse(lt, 6.6 + i * 0.45, 7.3 + i * 0.45, 0.15); m.material.color.setScalar((0.55 + 0.45 * Math.max(on, smooth(range(lt, 6.2, 6.6)) * 0.4)) * (1 - 0.75 * smooth(range(lt, 9.2, 9.8)))); m.scale.setScalar(1 + 0.08 * on); });
      this.search.visible = off > 0.01; this.search.position.set(-1.55, 0.13, 0);
      { const u = (lt * 0.5) % 1; this.search.scale.setScalar(0.6 + u * 2.4); this.search.material.opacity = 0.8 * (1 - u) * off; }
      head([[5.2, V(0.55, 0.2, 0)], [6.0, V(-1.55, 0.25, 0)], [6.6, V(-1.6, 2.6, -1.2)], [7.0, V(-1.6, 2.6, -1.0)], [7.45, V(-0.3, 2.6, -1.0)], [7.9, V(1.0, 2.6, -1.0)], [8.4, V(-1.55, 0.3, 0)], [8.8, V(0.55, 0.25, 0)], [9.2, V(1.1, 0.35, 1.6)]]);
      this.L('你好，openvela', 'TFLite Micro · 23 KB 唤醒模型 · 在本地认出', this.kws.position, pulse(lt, 1.6, 5.0, 0.3), { dx: -420, dy: -150, tone: 'mint' });
      this.L('“我在”', '预存在固件里的一段声音', this.wav.position, pulse(lt, 2.8, 5.0, 0.25), { dx: 250, dy: 120, tone: 'warm' });
      this.L('CPU1 + CPU2 · AP', 'openvela · 语音 · 双眼 · AI Agent', V(1.1, 0.1, -0.62), pulse(lt, 1.2, 5.1, 0.3), { dx: 160, dy: -300 });
      this.L('CPU0 · CP', 'Wi-Fi · TLS 加密出门', V(-1.55, 0.1, -0.62), pulse(lt, 5.2, 7.0, 0.25), { dx: -120, dy: -230 });
      this.L('RPMsg', '两半之间传话', V(-0.45, 0.42, 0.32), pulse(lt, 5.2, 6.6, 0.25), { dx: 40, dy: 160, small: true });
      this.L('云端', '听 · 想 · 说（由用户配置）', V(-0.3, 3.3, -1.2), pulse(lt, 6.4, 8.6, 0.3), { dx: 0, dy: -140, tone: 'violet' });
      this.L('一句句流回来', '缓冲 → 扬声器', V(1.1, 0.35, 1.6), pulse(lt, 8.2, 9.0, 0.2), { dx: 220, dy: -60, tone: 'warm' });
      this.L('断网时：她还醒着', '唤醒 · 双眼 · 动作 · NFC · 专注 · 都在本地', V(1.1, 0.1, -0.62), smooth(range(lt, 9.8, 10.3)), { dx: 140, dy: -260, tone: 'mint' });
      this.L('识别 · 回答 · 合成', '要靠网络——答不了话', V(-0.3, 3.3, -1.2), smooth(range(lt, 10.8, 11.3)), { dx: 0, dy: -150, tone: 'grey' });
      cam = {
        pos: track([[0, [2.6, 4.2, 4.2]], [1.4, [2.2, 1.35, 2.2]], [5.0, [1.6, 1.05, 1.95]], [6.2, [-0.4, 1.6, 3.2]], [7.4, [-0.3, 3.0, 3.6]], [8.6, [0.6, 1.8, 3.2]], [9.6, [1.2, 2.2, 3.8]], [dur, [0.3, 2.9, 4.4]]], lt),
        target: track([[0, [0.6, 0.0, 0.0]], [1.4, [0.9, 0.15, 0.0]], [5.0, [0.7, 0.15, 0.0]], [6.2, [-1.1, 0.4, -0.2]], [7.4, [-0.3, 2.6, -1.2]], [8.6, [0.6, 0.6, 0.4]], [9.6, [0.2, 0.6, 0.0]], [dur, [-0.2, 1.4, -0.5]]], lt),
      };
    } else if (kind === 'ota') {
      glow('cpu0', 0.55); glow('cpu1', 0.12); glow('cpu2', 0.12);
      this.flash.visible = true; this.pkg.visible = true;
      const into = easeInOut(range(lt, 0.8, 3.4));
      this.pkg.position.lerpVectors(V(-1.55, 1.9, 0.4), V(0.1, 0.45 + 0.62, -2.5), into);
      this.pkg.quaternion.copy(this.camera.quaternion);
      this.pkg.scale.setScalar(1 - 0.35 * into);
      this.pkg.material.opacity = 1 - smooth(range(lt, 3.3, 3.8));
      const bOn = smooth(range(lt, 3.4, 4.0));
      this.slotA.material.color.setScalar(0.85); this.slotB.material.color.setScalar(0.45 + 0.55 * bOn); this.slotD.material.color.setScalar(0.95);
      this.check.visible = lt > 6.6; this.check.position.set(0.1, 0.45 + 0.62, -2.35); this.check.material.opacity = smooth(range(lt, 6.6, 7.1)); this.check.quaternion.copy(this.camera.quaternion);
      this.L('签名镜像', '经局域网 HTTPS 拉取', this.pkg.position, pulse(lt, 0.6, 3.3, 0.25), { dx: -260, dy: -120, tone: 'warm' });
      this.L('B 槽：写入新固件', 'CP + AP 同槽签名', V(1.05, 1.07, -2.6), pulse(lt, 3.4, 6.4, 0.25), { dx: 320, dy: -40, tone: 'mint' });
      this.L('A 槽：原样保留', '试运行失败就回到 A', V(1.05, 0.45, -2.6), pulse(lt, 4.6, dur, 0.25), { dx: 330, dy: 70 });
      this.L('数据区不动', '记忆、设置、认领', V(1.05, 1.69, -2.6), pulse(lt, 5.2, dur, 0.25), { dx: 300, dy: -150, tone: 'warm' });
      this.L('MCUboot 验签', '确认后才算完成', V(0.1, 1.3, -2.35), pulse(lt, 6.4, dur, 0.25), { dx: -360, dy: -140, tone: 'mint' });
      cam = { pos: track([[0, [-2.2, 2.4, 2.2]], [3.4, [-0.6, 1.5, 0.7]], [dur, [0.2, 1.25, 0.3]]], lt),
        target: track([[0, [-1.2, 0.6, -0.4]], [3.4, [0.1, 1.0, -2.6]], [dur, [0.25, 1.0, -2.6]]], lt) };
    } else if (kind === 'offline') {
      glow('cpu0', 0.05 + 0.18 * (0.5 + 0.5 * Math.sin(lt * 4)));
      glow('cpu1', 0.7 + 0.1 * Math.sin(lt * 1.6)); glow('cpu2', 0.7 + 0.1 * Math.sin(lt * 1.6 + 1));
      this.cloud.visible = true;
      this.st.forEach((m) => { m.material.color.setScalar(0.22); m.scale.setScalar(1); });
      this.search.visible = true; this.search.position.set(-1.55, 0.13, 0);
      const u = (lt * 0.5) % 1; this.search.scale.setScalar(0.6 + u * 2.4); this.search.material.opacity = 0.8 * (1 - u);
      this.kws.material.opacity = 0.5 + 0.3 * Math.sin(lt * 2);
      this.L('CPU0 · CP', 'Wi-Fi：正在寻找……', V(-1.55, 0.1, -0.62), smooth(range(lt, 0.4, 1.2)), { dx: -140, dy: -230, tone: 'amber' });
      this.L('CPU1 + CPU2 · AP 还醒着', '唤醒 · 双眼 · 动作 · NFC · 专注', V(1.1, 0.1, -0.62), smooth(range(lt, 2.0, 2.8)), { dx: 140, dy: -260, tone: 'mint' });
      this.L('识别 · 回答 · 合成', '要靠网络；断网时不假装回答', V(-0.3, 3.3, -1.2), smooth(range(lt, 4.4, 5.2)), { dx: 0, dy: -150, tone: 'grey' });
      cam = { pos: track([[0, [2.6, 4.0, 4.0]], [1.4, [1.8, 1.6, 2.8]], [6.0, [0.2, 2.2, 3.8]], [dur, [-0.1, 2.9, 4.2]]], lt),
        target: track([[0, [0.4, 0.0, 0.0]], [1.4, [0.0, 0.3, 0.0]], [6.0, [-0.3, 1.0, -0.3]], [dur, [-0.3, 1.6, -0.6]]], lt) };
    }
    this.camera.position.set(...cam.pos);
    this.camera.lookAt(V(...cam.target));
    this.camera.fov = 38; this.camera.updateProjectionMatrix(); this.camera.updateMatrixWorld(true);
    E.renderPass.scene = this.scene; E.renderPass.camera = this.camera;
    E.bokeh.enabled = false;
    E.renderer.toneMappingExposure = 1.05;
    E.bloom.strength = 0.55;
    // Enter on a flash of her light, leave on a soft dip.
    E.grade.uniforms.white.value = 0.85 * (1 - smooth(range(lt, 0, 0.5)));
    E.grade.uniforms.black.value = smooth(range(lt, dur - 0.35, dur));
    E.grade.uniforms.vignette.value = 0.45;
    E.composer.render();
    return this.labels;
  }
}
