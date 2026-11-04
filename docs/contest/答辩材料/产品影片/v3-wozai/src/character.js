// SPDX-License-Identifier: Apache-2.0
// 周屿: the Blender-built skinned figure (blender/build_character.py), posed by
// aiming bones at world targets. Every bone's length runs along its local +Y,
// so a pose is a set of world directions, solved fresh for each frame.
import * as THREE from 'three';
import { Face } from './face.js';

const UP = new THREE.Vector3(0, 1, 0);
const _q = new THREE.Quaternion(), _q2 = new THREE.Quaternion(), _v = new THREE.Vector3(), _v2 = new THREE.Vector3();

export class Character {
  constructor(gltf, handsGltf) {
    this.root = gltf.scene;
    this.root.traverse((o) => {
      if (o.isMesh) { o.castShadow = true; o.receiveShadow = true; o.frustumCulled = false; }
      if (o.isSkinnedMesh) this.mesh = o;
    });
    // Lenses: thin, slightly tinted glass that catches her light.
    this.root.traverse((o) => {
      if (!o.isMesh) return;
      const mats = Array.isArray(o.material) ? o.material : [o.material];
      mats.forEach((m, i) => {
        if (m.name === 'lens') {
          const g = new THREE.MeshPhysicalMaterial({ color: 0xbfd4e6, roughness: 0.04, metalness: 0, transparent: true, opacity: 0.32,
            clearcoat: 1, envMapIntensity: 2.2, depthWrite: false });
          g.name = 'lens';
          if (Array.isArray(o.material)) o.material[i] = g; else o.material = g;
        }
        if (m.name === 'sweater' || m.name === 'trousers') { m.roughness = 0.95; m.sheen = 0; }
      });
    });
    this.bones = {};
    this.mesh.skeleton.bones.forEach((b) => { this.bones[b.name] = b; b.userData.rest = b.quaternion.clone(); });
    this.root.updateMatrixWorld(true);
    this.face = new Face(this.bones.head);
    this.hipOffset = this.bones.root.getWorldPosition(new THREE.Vector3()).sub(this.root.position);
    this.len = {};
    for (const [a, b] of [['upperarmL', 'forearmL'], ['forearmL', 'handL'], ['upperarmR', 'forearmR'], ['forearmR', 'handR'],
      ['thighL', 'shinL'], ['shinL', 'footL'], ['thighR', 'shinR'], ['shinR', 'footR'], ['handL', null], ['handR', null]]) {
      this.len[a] = b ? this.bones[b].position.length() : 0.085;
    }
    // Close-up hands for POV shots (hands.glb: relaxed, grip, point, pinch).
    this.hands = { L: {}, R: {} };
    if (handsGltf) {
      for (const node of handsGltf.scene.children) {
        if (!/^hand_/.test(node.name)) continue;
        for (const side of ['L', 'R']) {
          const m = node.clone(true); m.position.set(0, 0, 0);
          m.traverse((o) => { if (o.isMesh) { o.castShadow = true; o.receiveShadow = true; } });
          const holder = new THREE.Group(); holder.add(m); holder.visible = false;
          if (side === 'L') holder.scale.x = -1;
          this.hands[side][node.name] = holder;
        }
      }
    }
  }

  // Two small reflections of her round screens in each lens (stylised).
  attachLensReflections(screenCanvasL, screenCanvasR) {
    const c = document.createElement('canvas'); c.width = 256; c.height = 128;
    this.lensCanvas = c; this.lensSrc = [screenCanvasL, screenCanvasR];
    const tex = new THREE.CanvasTexture(c); tex.colorSpace = THREE.SRGBColorSpace; this.lensTex = tex;
    const mat = new THREE.MeshBasicMaterial({ map: tex, transparent: true, opacity: 0, blending: THREE.AdditiveBlending, depthWrite: false, toneMapped: false });
    this.lensMat = mat;
    const head = this.bones.head;
    this.root.updateMatrixWorld(true);
    const inv = new THREE.Matrix4().copy(head.matrixWorld).invert();
    const rootInv = new THREE.Matrix4().copy(this.root.matrixWorld);
    for (const x of [0.040, -0.040]) {
      const m = new THREE.Mesh(new THREE.PlaneGeometry(0.019, 0.0095), mat);
      const world = new THREE.Vector3(x + Math.sign(x) * 0.006, 1.650, 0.1385).applyMatrix4(rootInv);
      m.position.copy(world.applyMatrix4(inv));
      head.add(m);
    }
  }

  updateLens(opacity) {
    if (!this.lensMat) return;
    this.lensMat.opacity = opacity * 0.75;
    if (opacity <= 0) return;
    const g = this.lensCanvas.getContext('2d');
    g.clearRect(0, 0, 256, 128);
    this.lensSrc.forEach((src, i) => { g.save(); g.beginPath(); g.arc(70 + i * 116, 64, 46, 0, 7); g.clip(); g.drawImage(src, 24 + i * 116, 18, 92, 92); g.restore(); });
    this.lensTex.needsUpdate = true;
  }

  addTo(scene) {
    scene.add(this.root);
    for (const side of ['L', 'R']) for (const h of Object.values(this.hands[side])) scene.add(h);
  }

  reset() {
    for (const b of Object.values(this.bones)) { b.quaternion.copy(b.userData.rest); b.scale.set(1, 1, 1); }
  }

  // Point a bone's +Y at a world direction.
  aim(name, worldDir, roll = 0) {
    const b = this.bones[name];
    b.parent.getWorldQuaternion(_q).invert();
    _v.copy(worldDir).normalize().applyQuaternion(_q);
    b.quaternion.setFromUnitVectors(UP, _v);
    if (roll) { _q2.setFromAxisAngle(UP, roll); b.quaternion.multiply(_q2); }
    b.updateMatrixWorld(true);
  }

  // Two-bone IK: upper bone, lower bone, world target, world pole hint.
  ik(upper, lower, target, pole) {
    const bu = this.bones[upper];
    bu.updateMatrixWorld(true);
    const a = bu.getWorldPosition(new THREE.Vector3());
    const l1 = this.len[upper], l2 = this.len[lower];
    const toT = new THREE.Vector3().subVectors(target, a);
    const d = Math.min(Math.max(toT.length(), 1e-4), (l1 + l2) * 0.999);
    const dir = toT.clone().normalize();
    const cosA = (l1 * l1 + d * d - l2 * l2) / (2 * l1 * d);
    const ang = Math.acos(Math.min(1, Math.max(-1, cosA)));
    const poleDir = new THREE.Vector3().copy(pole).sub(a);
    const side = poleDir.sub(dir.clone().multiplyScalar(poleDir.dot(dir))).normalize();
    const elbowDir = dir.clone().multiplyScalar(Math.cos(ang)).add(side.multiplyScalar(Math.sin(ang)));
    this.aim(upper, elbowDir);
    const elbow = a.clone().add(elbowDir.multiplyScalar(l1));
    this.aim(lower, new THREE.Vector3().subVectors(a.clone().add(dir.multiplyScalar(d)), elbow));
  }

  // pose = {pos, yaw, sit, lean, headTurn, headTilt, armL/armR: {target, pole, handDir},
  //         legs: 'sit'|'stand', povHide, handsVisible}
  pose(p) {
    this.reset();
    const r = this.root;
    // p.pos is the hips (root bone) position in the world.
    r.position.copy(p.pos).sub(this.hipOffset);
    r.rotation.set(0, p.yaw || 0, 0);
    r.updateMatrixWorld(true);
    const B = this.bones;
    const fwd = new THREE.Vector3(0, 0, 1).applyAxisAngle(UP, p.yaw || 0); // character faces +Z after export
    const right = new THREE.Vector3(-1, 0, 0).applyAxisAngle(UP, p.yaw || 0);
    // Spine chain: lean forward (radians) and twist.
    const lean = p.lean ?? 0.12, twist = p.twist ?? 0;
    const spineDir = new THREE.Vector3(0, 1, 0).applyAxisAngle(right, -lean * 0.5);
    this.aim('spine', spineDir.clone().applyAxisAngle(UP, twist * 0.4));
    this.aim('chest', new THREE.Vector3(0, 1, 0).applyAxisAngle(right, -lean).applyAxisAngle(UP, twist));
    const headDir = new THREE.Vector3(0, 1, 0).applyAxisAngle(right, -(lean * 0.6 + (p.headTilt ?? 0))).applyAxisAngle(UP, (p.headTurn ?? 0) + twist);
    this.aim('neck', headDir);
    this.aim('head', headDir);
    // Legs.
    const hipL = B.thighL.getWorldPosition(new THREE.Vector3());
    const hipR = B.thighR.getWorldPosition(new THREE.Vector3());
    if (p.legs === 'stand') {
      this.ik('thighL', 'shinL', hipL.clone().add(new THREE.Vector3(0, -0.84, 0.02)), hipL.clone().add(fwd));
      this.ik('thighR', 'shinR', hipR.clone().add(new THREE.Vector3(0, -0.84, 0.02)), hipR.clone().add(fwd));
    } else {
      const knee = 0.44, spread = p.spread ?? 0.04;
      for (const [s, hip, sx] of [['L', hipL, -1], ['R', hipR, 1]]) {
        const foot = hip.clone().add(fwd.clone().multiplyScalar(knee * 0.98)).add(right.clone().multiplyScalar(sx * spread));
        foot.y = 0.09;
        this.ik('thigh' + s, 'shin' + s, foot, hip.clone().add(fwd.clone().multiplyScalar(1)).add(new THREE.Vector3(0, 0.6, 0)));
      }
      this.aim('footL', fwd.clone().add(new THREE.Vector3(0, -0.25, 0)));
      this.aim('footR', fwd.clone().add(new THREE.Vector3(0, -0.25, 0)));
    }
    // Arms.
    for (const s of ['L', 'R']) {
      const a = p['arm' + s];
      if (!a) continue;
      const sh = B['upperarm' + s].getWorldPosition(new THREE.Vector3());
      const pole = a.pole || sh.clone().add(new THREE.Vector3(0, -0.6, -0.2)).add(right.clone().multiplyScalar(s === 'L' ? 0.4 : -0.4));
      this.ik('upperarm' + s, 'forearm' + s, a.target, pole);
      this.aim('hand' + s, a.handDir || new THREE.Vector3().subVectors(a.target, B['forearm' + s].getWorldPosition(new THREE.Vector3())));
    }
    this.updateLens(p.lens || 0);
    this.face.update({ mood: p.face || 'neutral', talk: p.talk || 0, seed: 3 }, p.t ?? 0);
    // POV: collapse the head (camera sits there) and optionally the arms.
    if (p.povHide) { B.head.scale.setScalar(0.001); B.neck.scale.setScalar(0.6); }
    if (p.hideArms) { for (const s of ['L', 'R']) B['upperarm' + s].scale.setScalar(0.001); }
    this.root.visible = p.visible !== false;
    this.root.updateMatrixWorld(true);
    // Close-up hands placed in world space for POV shots.
    for (const s of ['L', 'R']) for (const h of Object.values(this.hands[s])) h.visible = false;
    for (const h of p.hands || []) {
      const holder = this.hands[h.side || 'R'][h.kind || 'hand_grip'];
      if (!holder) continue;
      holder.visible = true;
      holder.position.copy(h.pos);
      holder.quaternion.copy(h.quat);
    }
  }

  eyeWorld() {
    const hb = this.bones.head;
    return hb.localToWorld(new THREE.Vector3(0, 0.14, 0.09));
  }
}
