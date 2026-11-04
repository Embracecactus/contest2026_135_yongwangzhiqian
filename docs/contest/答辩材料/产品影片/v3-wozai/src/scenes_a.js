// SPDX-License-Identifier: Apache-2.0
// Scenes 1–9: the gift, his decision to make her, making her into 晴, the
// photo request he cannot answer, 3 a.m.
import * as THREE from 'three';
import { SPOT, DESK } from './set.js';
import { clamp, easeInOut, easeOut, lerp, range, smooth, noise1 } from './util.js';
import { V, Q, EYE_POS, seated, resting, typing, sleeping, move, otsLeft, devPt, devNormal, devUp, handAt, faceQuat } from './rig.js';
import { LEFT, MON, ots, macro, monitor, phone, WIDE_BACKLEFT, SIDE, FRONT, boardGrip, reachBoard, facingCard , sideHand } from './templates.js';
import { phoneAt } from './screens.js';

const toEye = (p) => faceQuat(EYE_POS.clone().sub(p).normalize(), V(0, 1, 0));
const PH = (focus = 330, zoom = 2.15) => ({ focus, zoom });
const insert = (t0, t1, o = {}) => phone(t0, t1, (t) => phoneAt(t), { focus: o.focus ?? 330, zoom: o.zoom ?? 2.15, dim: o.dim ?? 0.62, enter: o.enter, exit: o.exit, shot: o.shot });
// A face shot from across the desk (camera floats over the keyboard).
const face = (t0, t1, mood, o = {}) => ({ t0, t1,
  pose: o.pose || ((t) => resting({ lean: o.lean ?? 0.24, twist: o.twist ?? 0.1, headTurn: o.headTurn ?? 0.05, headTilt: o.headTilt ?? 0.1, face: typeof mood === 'function' ? mood(t) : mood, lens: o.lens || 0 })),
  cam: move([[0, o.from || [-0.10, 1.18, -0.04], o.at || [0.10, 1.19, 0.42], o.fov ?? 26], [1, o.to || [-0.09, 1.18, -0.01], o.at || [0.10, 1.19, 0.42], (o.fov ?? 26) - 1.5]], { focus: () => o.focus ?? 0.48, aperture: 0.002, maxblur: 0.006 }),
  exposure: o.exposure ?? 1.0, bloom: o.bloom ?? 0.3 });

const A = [];
// ------------------------------------------------------------ s01 雨夜
A.push({ id: 's01a', t0: 0.0, t1: 6.0, pose: () => typing(0, { face: 'tired' }),
  cam: move([[0, [0.05, 1.42, -0.52], [0.02, 1.38, -0.72], 42], [1, [0.06, 1.33, -0.12], [0.0, 1.30, -0.72], 44]], { focus: (t, k) => lerp(0.2, 0.6, k), aperture: 0.004, maxblur: 0.01 }),
  black: (t) => 1 - smooth(range(t, 0, 1.2)) });
A.push({ id: 's01b', t0: 6.0, t1: 9.6, pose: (t) => resting({ lean: 0.22, face: 'tired' }),
  cam: move([[0, [-0.36, 0.86, 0.05], [-0.18, 0.80, -0.10], 36], [0.5, [-0.20, 0.87, 0.02], [-0.05, 0.86, -0.30], 34], [1, [-0.05, 0.92, -0.02], [0.08, 1.02, -0.44], 32]], { focus: (t, k) => lerp(0.18, 0.45, k), aperture: 0.003, maxblur: 0.008 }) });
A.push({ id: 's01c', ...insert(9.6, 17.6, { focus: 480, zoom: 1.38, dim: 0.66 }) });
A.push({ id: 's01d', t0: 17.6, t1: 19.0,
  pose: (t) => resting({ lean: 0.3, headTilt: 0.35, face: 'tired', handR: V(0.34, DESK.y + 0.02, 0.04) }),
  phone: (t) => {
    const f = smooth(range(t, 17.7, 18.1));
    const q = new THREE.Quaternion().setFromEuler(new THREE.Euler(-Math.PI / 2 + Math.PI * f, 0, 0.3));
    return { pos: V(0.27, DESK.y + 0.0045 + 0.03 * Math.sin(Math.PI * f), -0.02), quat: q };
  },
  cam: move([[0, [0.24, 1.02, 0.06], [0.27, DESK.y, -0.03], 30], [1, [0.24, 1.0, 0.05], [0.27, DESK.y, -0.03], 29]]), exposure: 0.5, bloom: 0.1 });
// ------------------------------------------------------------ s02 礼物
A.push({ id: 's02a', t0: 19.0, t1: 21.4, pose: () => resting({ lean: 0.2, twist: 0.3, headTurn: 0.4, headTilt: 0.4, face: 'neutral' }),
  cam: move([[0, [-0.62, 0.42, 0.52], [-0.60, 0.12, 0.38], 34], [1, [-0.61, 0.40, 0.50], [-0.60, 0.12, 0.38], 32]], { focus: () => 0.3, aperture: 0.004, maxblur: 0.008 }) });
A.push({ id: 's02b', t0: 21.4, t1: 23.8, pose: (t) => resting({ lean: 0.3, face: 'neutral', handL: V(-0.43, DESK.y + 0.06, -0.24), handR: V(-0.17, DESK.y + 0.06, -0.24) }),
  cam: move([[0, [-0.95, 1.30, 0.75], [-0.25, 0.86, -0.25], 36], [1, [-0.90, 1.28, 0.70], [-0.25, 0.86, -0.25], 35]]) });
A.push({ id: 's02c', t0: 23.8, t1: 29.6, pose: () => resting({ lean: 0.3, handL: V(-0.47, DESK.y + 0.12, -0.24), handR: V(-0.13, DESK.y + 0.12, -0.24), face: 'neutral' }),
  cam: move([[0, [-0.28, 1.36, -0.12], [-0.30, DESK.y + 0.12, -0.30], 40], [1, [-0.29, 1.24, -0.16], [-0.30, DESK.y + 0.12, -0.30], 36]], { focus: (t, k) => lerp(0.55, 0.42, k), aperture: 0.002, maxblur: 0.006 }) });
// The card in his hand: read.
const cardHeld = (t) => {
  const pos = EYE_POS.clone().add(V(-0.02, -0.09, -0.29)).add(V(0, 0.002 * Math.sin(t * 1.7), 0));
  return { pos, quat: facingCard(pos, EYE_POS, V(0, 1, 0.3).normalize()) };
};
A.push({ id: 's02d', t0: 29.6, t1: 34.4,
  world: (t, k, c, w) => { const h = cardHeld(t); return { ...w, cardVisible: true, cardPos: h.pos, cardQuat: h.quat }; },
  pose: (t, k, c, w) => {
    const right = V(1, 0, 0).applyQuaternion(w.cardQuat), down = V(0, 0, 1).applyQuaternion(w.cardQuat);
    const tipR = w.cardPos.clone().add(right.clone().multiplyScalar(0.016)).add(down.clone().multiplyScalar(0.018));
    const tipL = w.cardPos.clone().add(right.clone().multiplyScalar(-0.016)).add(down.clone().multiplyScalar(0.024));
    const toward = EYE_POS.clone().sub(w.cardPos).normalize();
    return seated({ pov: true, hideArms: true, hands: [
      handAt('hand_pinch', 'R', tipR, right.clone().negate().add(toward.clone().multiplyScalar(-0.2)).normalize(), toward.clone().negate().add(V(0, 0.5, 0)).normalize()),
      handAt('hand_pinch', 'L', tipL, right.clone().add(toward.clone().multiplyScalar(-0.2)).normalize(), toward.clone().negate().add(V(0, 0.5, 0)).normalize())] });
  },
  cam: (t, k) => ({ pos: EYE_POS.clone().add(V(0, -0.005, 0.01)), target: cardHeld(t).pos.clone(), fov: 34, focus: 0.3, aperture: 0.003, maxblur: 0.008 }),
  bloom: 0.25 });
// His decision: the bare board held over the clean rectangle where her laptop stood.
const decision = (t) => {
  const up = smooth(range(t, 34.5, 35.6));
  const from = V(-0.30, DESK.y + 0.125, -0.31), to = V(-0.18, 1.05, -0.02);
  const pos = from.clone().lerp(to, up);
  const qFlat = new THREE.Quaternion().setFromEuler(new THREE.Euler(-Math.PI / 2, 0, 0.12));
  return { pos, quat: qFlat.slerp(toEye(to), up) };
};
A.push({ id: 's02e', t0: 34.4, t1: 37.8,
  world: (t, k, c, w) => ({ ...w, shaniuHeld: decision(t) }),
  pose: (t, k, c) => seated({ lean: 0.34, twist: 0.36, headTurn: 0.28, headTilt: 0.16, face: 'hold', ...reachBoard(c) }),
  cam: (t, k) => {
    const target = V(-0.18, 1.05, -0.02);
    const a = V(-0.02, 1.30, 0.66), b = V(-0.04, 1.29, 0.60);   // over his left shoulder, the board up like a face
    return { pos: a.lerp(b, smooth(k)), target: target.clone().add(V(0.0, -0.02, 0)), fov: 26, focus: 0.66, aperture: 0.0022, maxblur: 0.006 };
  }, bloom: 0.25 });
A.push({ id: 's02f', ...face(37.8, 40.4, (t) => (t < 39.2 ? 'hold' : 'focus'), {
  pose: (t) => resting({ lean: 0.28, twist: 0.3, headTurn: 0.22, headTilt: 0.12 + 0.07 * Math.sin(Math.PI * range(t, 39.1, 39.7)), face: t < 39.2 ? 'hold' : 'focus', lens: 0 }),
  from: [-0.28, 1.16, -0.02], to: [-0.27, 1.16, 0.0], at: [0.08, 1.18, 0.42] }),
  world: (t, k, c, w) => ({ ...w, shaniuHeld: decision(37.8) }), bloom: 0.2 });

// ------------------------------------------------------------ s03 做她
A.push({ id: 's03a', t0: 40.4, t1: 43.6, pose: (t) => resting({ lean: 0.4, twist: 0.25, headTurn: 0.3, headTilt: 0.5, face: t < 41.6 ? 'neutral' : 'hold', handL: V(-0.38, DESK.y - 0.04, 0.12) }),
  cam: move([[0, [-0.22, 1.08, 0.40], [-0.38, 0.70, 0.17], 36], [1, [-0.24, 1.04, 0.36], [-0.38, 0.70, 0.17], 34]], { focus: () => 0.45, aperture: 0.002, maxblur: 0.006 }) });
// Standoffs, then the acrylic plate: his fingers at the corners.
A.push({ id: 's03b', t0: 43.6, t1: 46.8,
  world: (t, k, c, w) => ({ ...w, assembled: t >= 45.6 }),
  pose: (t, k, c) => {
    const corner = Math.min(3, Math.floor((t - 43.8) / 0.45));
    const [x, y] = [[-27, -22], [27, -22], [27, 22], [-27, 22]][Math.max(0, corner)];
    const tip = devPt(c, x, y, 2), n = devNormal(c);
    const twist = Math.sin(t * 16) * 0.3;
    return seated({ pov: true, hideArms: true, hands: [handAt('hand_pinch', 'R', tip, n.clone().negate().add(V(-0.4, 0, 0.2)).normalize(), V(Math.sin(twist), 0, -Math.cos(twist)).normalize())] });
  },
  cam: (t, k, c) => { const p = devPt(c, 0, 0, 0); return { pos: p.clone().add(V(0.03, 0.30, 0.10)), target: p, fov: 36, focus: 0.31, aperture: 0.003, maxblur: 0.008 }; }, exposure: 0.5, bloom: 0.12 });
A.push({ id: 's03c', ...monitor(46.8, 48.6, { pose: (t) => typing(t, { face: 'focus' }), region: [30, 30, 1300, 330] }) });
A.push({ id: 's03d', t0: 48.6, t1: 49.6,
  world: (t, k, c, w) => ({ ...w, usbPlug: { k: smooth(range(t, 48.62, 48.75)) } }),
  pose: (t, k, c) => seated({ pov: true, hideArms: true }),
  cam: (t, k, c) => { const p = devPt(c, -5.5, -28, 0); return { pos: p.clone().add(V(0.10, 0.12, 0.10)), target: p, fov: 32, focus: 0.18, aperture: 0.004, maxblur: 0.008 }; }, exposure: 0.5, bloom: 0.12 });
A.push({ id: 's03e', t0: 49.6, t1: 52.0,
  world: (t, k, c, w) => ({ ...w, usbPlug: { k: 1 } }),
  pose: () => seated({ pov: true, hideArms: true }),
  cam: (t, k, c) => { const p = devPt(c, 0, 4, 0); return { pos: p.clone().add(V(0.0, lerp(0.21, 0.17, smooth(k)), 0.05)), target: p, fov: 32, focus: 0.19, aperture: 0.004, maxblur: 0.008 }; },
  bloom: 0.12, exposure: 0.5 });

// ------------------------------------------------------------ s04 认领与配网
A.push({ id: 's04a', ...insert(52.0, 54.4, { focus: 470 }), world: (t, k, c, w) => ({ ...w, usbPlug: { k: 1 } }) });
A.push({ id: 's04b', t0: 54.4, t1: 57.0,
  world: (t, k, c, w) => ({ ...w, usbPlug: { k: 1 } }),
  pose: () => seated({ pov: true, hideArms: true }),
  phone: (t, k, c) => { const p = devPt(c, 0, 4, 0).add(V(0.0, 0.16 - 0.03 * smooth(k), 0.06)); return { pos: p, quat: faceQuat(V(0, 0.75, 0.66).normalize(), V(0, 0, -1)) }; },
  cam: (t, k, c) => { const p = devPt(c, 0, 4, 0); return { pos: p.clone().add(V(0.0, 0.40, 0.16)), target: p.clone().add(V(0, 0.12, 0.03)), fov: 40, focus: 0.3, aperture: 0.002, maxblur: 0.006 }; }, exposure: 0.5, bloom: 0.12 });
A.push({ id: 's04c', ...insert(57.0, 61.6, { focus: 300 }), world: (t, k, c, w) => ({ ...w, usbPlug: { k: 1 } }) });
A.push({ id: 's04d', ...insert(61.6, 67.0, { focus: 300 }), world: (t, k, c, w) => ({ ...w, usbPlug: { k: 1 } }) });
A.push({ id: 's04e', ...insert(67.0, 71.0, { focus: 330 }), world: (t, k, c, w) => ({ ...w, usbPlug: { k: 1 } }) });
A.push({ id: 's04f', ...insert(71.0, 72.2, { focus: 330 }), world: (t, k, c, w) => ({ ...w, usbPlug: { k: 1 } }) });
A.push({ id: 's04g', t0: 72.2, t1: 74.0,
  world: (t, k, c, w) => ({ ...w, usbPlug: { k: 1 } }),
  pose: () => seated({ pov: true, hideArms: true }),
  cam: (t, k, c) => { const p = devPt(c, 0, 4, 0); return { pos: p.clone().add(V(0.0, lerp(0.17, 0.155, k), 0.05)), target: p, fov: 32, focus: 0.17, aperture: 0.004, maxblur: 0.008 }; },
  bloom: 0.12, exposure: 0.5 });

// ------------------------------------------------------------ s05 我在
// He stands her up on the clean rectangle, her side of the desk.
const standUp = (t) => {
  const k = easeInOut(range(t, 74.6, 76.4));
  const from = V(0.02, DESK.y + 0.012, -0.02), to = SPOT.shaniuLeft.clone().add(V(0, 0.045, 0.004));
  const mid = V(-0.2, 0.92, -0.06);
  const pos = k < 0.5 ? from.clone().lerp(mid, k * 2) : mid.clone().lerp(to, (k - 0.5) * 2);
  const qFlat = new THREE.Quaternion().setFromEuler(new THREE.Euler(-Math.PI / 2, 0, 0));
  const qStand = new THREE.Quaternion().setFromEuler(new THREE.Euler(-THREE.MathUtils.degToRad(24), 0.42, 0, 'YXZ'));
  return { pos, quat: qFlat.slerp(qStand, k) };
};
A.push({ id: 's05a', t0: 74.0, t1: 77.4,
  world: (t, k, c, w) => (t < 76.4 ? { ...w, shaniuHeld: standUp(t), usbPlug: null } : w),
  pose: (t, k, c) => (t < 76.4 ? seated({ lean: 0.34, twist: 0.3, headTurn: 0.3, headTilt: 0.3, face: 'focus', ...reachBoard(c) }) : resting({ lean: 0.3, twist: 0.3, headTurn: 0.3, headTilt: 0.2, face: 'neutral' })),
  cam: move([[0, [-0.95, 1.06, 0.20], [-0.22, 0.86, -0.12], 34], [1, [-0.90, 1.05, 0.16], [-0.26, 0.86, -0.14], 32]]), exposure: 0.8 });
A.push({ id: 's05b', ...ots(77.4, 81.8, { push: 0.06, poseOpts: { face: 'neutral' } }) });
A.push({ id: 's05c', ...face(81.8, 83.4, (t) => (t < 82.5 ? 'surprised' : 'hold'), { lean: 0.26, twist: 0.32, headTurn: 0.22, lens: 0.5, exposure: 1.05 }) });
A.push({ id: 's05d', ...ots(83.4, 85.8, { push: 0.03, poseOpts: { face: 'neutral' } }) });
A.push({ id: 's05e', ...macro(85.8, 89.0, { from: 0.22, to: 0.19 }) });
A.push({ id: 's05f', ...face(89.0, 90.4, 'neutral', { lean: 0.26, twist: 0.32, headTurn: 0.22 }) });
// The dive: through her eyes into the chip.
A.push({ id: 's05g', t0: 90.4, t1: 91.2, pose: () => resting({ lean: 0.26, twist: 0.3, headTurn: 0.2, face: 'neutral' }),
  cam: (t, k) => {
    const dive = easeInOut(k);
    const a = otsLeft(LEFT, { back: 0.34, up: 0.12, side: -0.16 }), b = LEFT.clone().add(V(0.05, 0.02, 0.07));
    return { pos: a.clone().lerp(b, dive * dive), target: LEFT.clone().add(V(0.06, 0.03, 0)), fov: lerp(22, 16, dive), focus: null };
  }, white: (t) => smooth(range(t, 90.9, 91.2)) * 0.9, bloom: 0.35 });
A.push({ id: 's05h', t0: 91.2, t1: 103.4, cutaway: 'voice' });
A.push({ id: 's05i', t0: 103.4, t1: 107.0, pose: (t) => resting({ lean: 0.28, twist: 0.32, headTurn: 0.26, headTilt: 0.1, face: t < 104.6 ? 'neutral' : 'soft' }),
  cam: move([[0, [1.15, 1.10, 0.42], [-0.10, 0.98, 0.0], 34], [1, [1.10, 1.10, 0.38], [-0.10, 0.98, 0.0], 33]]),
  black: (t) => 0.0 });

// ------------------------------------------------------------ s06 把她调成晴
A.push({ id: 's06a', ...insert(107.0, 112.0, { focus: 420 }) });
A.push({ id: 's06b', ...insert(112.0, 115.4, { focus: 300 }) });
A.push({ id: 's06c', ...ots(115.4, 119.4, { push: 0.04, poseOpts: { face: 'neutral' } }) });
A.push({ id: 's06d', ...face(119.4, 122.6, 'neutral', { lean: 0.26, twist: 0.32, headTurn: 0.22 }) });
A.push({ id: 's06e', ...macro(122.6, 124.6, { from: 0.21, to: 0.19 }) });
// The fish mug, two spoons of sugar into his coffee — her mug.
const mugAt = V(-0.13, DESK.y, -0.06);
A.push({ id: 's06f', t0: 124.6, t1: 129.2, pose: (t) => resting({ lean: 0.3, twist: 0.2, headTurn: 0.15, headTilt: 0.35, face: 'neutral', handR: V(-0.08, DESK.y + 0.12 + 0.03 * Math.abs(Math.sin((t - 127.6) * 4)) * (t > 127.6 && t < 129 ? 1 : 0), -0.06), handL: V(-0.2, DESK.y + 0.02, 0.0) }),
  cam: move([[0, [-0.30, 0.98, 0.16], [-0.13, 0.80, -0.06], 32], [1, [-0.28, 0.96, 0.13], [-0.13, 0.80, -0.06], 30]], { focus: () => 0.3, aperture: 0.003, maxblur: 0.008 }) });
A.push({ id: 's06g', ...face(129.2, 133.2, 'neutral', { lean: 0.26, twist: 0.3, headTurn: 0.2 }) });
A.push({ id: 's06h', ...ots(133.2, 135.0, { poseOpts: { face: 'neutral' } }) });
// He drinks. The sweetness. Drinks again.
const sip = (t) => {
  const up = smooth(range(t, 135.1, 135.6)) * (1 - smooth(range(t, 135.9, 136.2))) + smooth(range(t, 136.8, 137.2)) * (1 - smooth(range(t, 137.6, 138.0)));
  const hand = V(-0.06, DESK.y + 0.08, -0.04).lerp(V(0.07, 1.10, 0.30), up);
  const tilt = up * 0.9;
  return { hand, mug: { pos: hand.clone().add(V(-0.04, -0.06, -0.02)), quat: new THREE.Quaternion().setFromEuler(new THREE.Euler(tilt, 0.6, 0)) } };
};
A.push({ id: 's06i', t0: 135.0, t1: 138.2,
  world: (t, k, c, w) => ({ ...w, fishMugPose: sip(t).mug }),
  pose: (t) => resting({ lean: 0.2, twist: 0.1, headTurn: 0.05, headTilt: 0.05, face: t > 135.7 && t < 136.6 ? 'wince' : 'neutral', handR: sip(t).hand, handL: V(-0.05, DESK.y + 0.02, 0.0) }),
  cam: move([[0, [1.08, 1.14, 0.36], [0.05, 1.10, 0.30], 30], [1, [1.04, 1.14, 0.34], [0.05, 1.10, 0.30], 29]]) });
A.push({ id: 's06j', ...insert(138.2, 143.6, { focus: 380 }) });
A.push({ id: 's06k', ...insert(143.6, 146.2, { focus: 520, zoom: 1.9 }) });
// The door card on her; registered; tapped again: four beats.
const doorTap = (t, c, t0) => {
  const touch = smooth(range(t, t0, t0 + 0.35)) * (1 - smooth(range(t, t0 + 0.8, t0 + 1.2)));
  const n = devNormal(c);
  const target = devPt(c, 2, -52, 0).add(n.clone().multiplyScalar(0.004));
  const rest = devPt(c, 26, -46, 0).add(n.clone().multiplyScalar(0.08)).add(V(0.05, 0.02, 0.0));
  return { pos: rest.clone().lerp(target, touch), quat: faceQuat(n, devUp(c)).multiply(Q(Math.PI / 2, 0, 0.15)) };
};
A.push({ id: 's06l', t0: 146.2, t1: 153.0,
  world: (t, k, c, w) => ({ ...w, doorCard: t < 151.8 ? doorTap(t, c, t < 147.6 ? 146.1 : 148.4) : w.doorCard }),
  pose: (t, k, c, w) => {
    if (t >= 151.8) return resting({ lean: 0.3, twist: 0.32, headTurn: 0.26, face: 'neutral' });
    const n = devNormal(c);
    const corner = w.doorCard.pos.clone().add(V(1, 0, 0).applyQuaternion(w.doorCard.quat).multiplyScalar(0.027)).add(V(0, 0, -1).applyQuaternion(w.doorCard.quat).multiplyScalar(0.012));
    return resting({ lean: 0.34, twist: 0.4, headTurn: 0.3, headTilt: 0.25, hideArms: true, face: 'neutral',
      hands: [sideHand(c, 'hand_pinch', corner, 1)] });
  },
  cam: (t, k, c) => { const p = devPt(c, 4, -18, 0), n = devNormal(c); return { pos: p.clone().add(n.clone().multiplyScalar(0.27 + 0.03 * k)).add(V(0.10, 0.03, 0.02)), target: p, fov: 30, focus: 0.3, aperture: 0.003, maxblur: 0.008 }; },
  bloom: 0.28 });
// "晴回家，总要敲四下门。明明有卡。" — he looks at the door.
A.push({ id: 's06m', t0: 153.0, t1: 157.8, pose: (t) => resting({ lean: 0.18, twist: lerp(0.0, -0.5, smooth(range(t, 153.2, 154.2))), headTurn: lerp(0.1, -0.8, smooth(range(t, 153.3, 154.4))), face: 'soft' }),
  cam: move([[0, [-0.10, 1.40, 0.05], [1.86, 1.0, 1.45], 38], [1, [-0.09, 1.40, 0.08], [1.86, 1.0, 1.45], 35]], { focus: () => 2.3, aperture: 0.0014, maxblur: 0.005 }),
  black: (t) => smooth(range(t, 157.2, 157.8)) });

// ------------------------------------------------------------ s07 日子
// (a) 02:13
A.push({ id: 's07a1', t0: 157.8, t1: 161.0, pose: () => resting({ lean: 0.4, twist: 0.38, headTurn: 0.3, headTilt: 0.3, face: 'tired' }),
  cam: move([[0, [0.95, 1.04, 0.48], [-0.20, 1.0, 0.10], 32], [1, [0.92, 1.04, 0.45], [-0.20, 1.0, 0.10], 31]]),
  black: (t) => 1 - smooth(range(t, 157.8, 158.4)) });
A.push({ id: 's07a2', ...insert(161.0, 164.6, { focus: 330 }) });
A.push({ id: 's07a3', ...ots(164.6, 168.8, { push: 0.05, poseOpts: { face: 'tired', lean: 0.38 } }) });
A.push({ id: 's07a4', ...face(168.8, 170.8, 'hold', { lean: 0.36, twist: 0.36, headTurn: 0.26, lens: 0.8, exposure: 1.2 }) });
A.push({ id: 's07a5', t0: 170.8, t1: 172.2,
  pose: (t, k, c) => {
    const press = (t > 170.95 && t < 171.15) || (t > 171.45 && t < 171.65) ? 1 : 0;
    const tip = devPt(c, 0.8, -23.6, 1.6), n = devNormal(c);
    return resting({ lean: 0.4, twist: 0.4, hideArms: true, hands: [handAt('hand_point', 'R', tip.clone().add(n.clone().multiplyScalar(0.012 * (1 - press) + 0.0005)), n.clone().negate(), V(0, 1, 0).add(n.clone().multiplyScalar(0.3)))] });
  },
  cam: (t, k, c) => { const p = devPt(c, 4, -16, 2), n = devNormal(c); return { pos: p.clone().add(n.clone().multiplyScalar(0.11)).add(V(0.06, 0.03, 0.03)), target: p, fov: 34, focus: 0.13, aperture: 0.008, maxblur: 0.012 }; },
  bloom: 0.28 });
A.push({ id: 's07a6', ...WIDE_BACKLEFT(172.2, 175.4, { pose: () => resting({ lean: 0.42, twist: 0.36, headTurn: 0.3, headTilt: 0.3, face: 'tired' }) }),
  black: (t) => smooth(range(t, 174.9, 175.4)) });
// (b) Day: the lighthouse lecture.
A.push({ id: 's07b1', ...SIDE(175.4, 179.6, { pose: () => resting({ lean: 0.26, twist: 0.3, headTurn: 0.26, face: 'neutral' }) }) });
A.push({ id: 's07b2', ...face(179.6, 182.6, 'neutral', { lean: 0.26, twist: 0.3, headTurn: 0.2 }) });
A.push({ id: 's07b3', ...macro(182.6, 185.2, { from: 0.22, to: 0.2 }) });
A.push({ id: 's07b4', ...face(185.2, 188.2, (t) => (t < 186.9 ? 'wince' : 'tired'), { lean: 0.26, twist: 0.3, headTurn: 0.2 }) });
// Chin on his hand: he waits for her to finish.
A.push({ id: 's07b5', t0: 188.2, t1: 194.2, pose: (t) => resting({ lean: 0.42, twist: 0.32, headTurn: 0.25, headTilt: 0.18, face: 'tired', handR: V(0.13, 1.08, 0.24), poleR: V(0.6, 0.6, 0.4) }),
  cam: move([[0, [1.20, 1.08, 0.50], [-0.12, 0.98, -0.02], 34], [1, [1.14, 1.08, 0.46], [-0.12, 0.98, -0.02], 33]]) });
A.push({ id: 's07b6', ...ots(194.2, 199.0, { poseOpts: { face: 'neutral' } }) });
A.push({ id: 's07b7', ...insert(199.0, 204.0, { focus: 330 }) });
// (c) Overcast morning: the plant.
A.push({ id: 's07c1', ...WIDE_BACKLEFT(204.0, 207.0, { pose: (t) => resting({ lean: 0.3, twist: 0.2, face: 'neutral', handL: t < 206.2 ? V(-0.22, DESK.y + 0.12, -0.08) : V(-0.05, DESK.y + 0.02, 0.0) }) }) });
A.push({ id: 's07c2', ...ots(207.0, 210.4, { poseOpts: { face: 'neutral' } }) });
A.push({ id: 's07c3', ...face(210.4, 213.0, 'neutral', { lean: 0.28, twist: 0.32, headTurn: 0.22 }) });
// What she sees: one 640×480 frame of the plant (F20).
A.push({ id: 's07c4', t0: 213.0, t1: 214.8, pose: () => resting({ lean: 0.3, twist: 0.3, headTurn: 0.22, face: 'neutral' }),
  cam: (t, k, c) => {
    const lens = c.device.board.localToWorld(c.device.lensPos.clone());
    const n = devNormal(c);
    return { pos: lens.add(n.clone().multiplyScalar(0.01)), target: SPOT.plantDesk.clone().add(V(0, 0.13, 0.02)), fov: 52 };
  },
  overlay: (t, k, c, w, o) => o.frame(t, 213.2, 214.6, '640 × 480 · 一帧'),
  white: (t) => 0.6 * Math.exp(-Math.max(0, t - 213.25) * 9) * (t > 213.25 ? 1 : 0) });
A.push({ id: 's07c5', ...macro(214.8, 219.4, { from: 0.24, to: 0.21, off: V(0.02, 0.02, 0) }) });
// He waters it (nobody else will).
const pour7 = (t) => {
  const tilt = smooth(range(t, 220.6, 221.1)) * (1 - smooth(range(t, 222.6, 223.0)));
  const lift = smooth(range(t, 219.8, 220.4));
  return { pos: V(-0.06, DESK.y + 0.13 * lift + 0.05 * tilt, -0.07), quat: new THREE.Quaternion().setFromEuler(new THREE.Euler(0, 0, 1.85 * tilt)) };
};
A.push({ id: 's07c6', t0: 219.4, t1: 225.6,
  world: (t, k, c, w) => {
    const b = t > 219.8 && t < 224 ? pour7(t) : null;
    const tilt = smooth(range(t, 220.8, 221.1)) * (1 - smooth(range(t, 222.4, 222.6)));
    const cap = b ? V(0, 0.215, 0).applyQuaternion(b.quat).add(b.pos) : null;
    return { ...w, bottle: b, stream: b && tilt > 0.5 ? { from: cap, to: w.plantPos.clone().add(V(0.0, 0.11, 0)) } : null };
  },
  pose: (t, k, c, w) => resting({ lean: 0.36, twist: 0.3, headTurn: 0.26, headTilt: 0.36, face: 'soft', handR: w.bottle ? w.bottle.pos.clone().add(V(0, 0.09, 0).applyQuaternion(w.bottle.quat)) : V(0.22, DESK.y + 0.02, -0.02) }),
  cam: move([[0, [-0.62, 1.00, 0.12], [-0.20, 0.88, -0.08], 34], [1, [-0.60, 0.99, 0.10], [-0.20, 0.88, -0.08], 32]], { focus: () => 0.46, aperture: 0.002, maxblur: 0.006 }) });
// Lifted to wipe the desk: her eyes glance. "……抱歉。"
const wipeLift = (t) => {
  const up = smooth(range(t, 226.2, 226.9)) * (1 - smooth(range(t, 229.0, 229.6)));
  const from = SPOT.shaniuLeft.clone().add(V(0, 0.045, 0.004)), to = V(-0.12, 1.04, 0.18);
  const q0 = new THREE.Quaternion().setFromEuler(new THREE.Euler(-THREE.MathUtils.degToRad(24), 0.42, 0, 'YXZ'));
  return { pos: from.clone().lerp(to, up), quat: q0.slerp(toEye(to), up) };
};
A.push({ id: 's07c7', t0: 225.6, t1: 231.0,
  world: (t, k, c, w) => (t >= 226.2 && t < 229.6 ? { ...w, shaniuHeld: wipeLift(t) } : w),
  pose: (t, k, c) => (t >= 226.2 && t < 229.6 ? seated({ lean: 0.3, twist: 0.3, headTurn: 0.25, headTilt: 0.15, face: t < 227.8 ? 'surprised' : (t < 228.9 ? 'wince' : 'soft'), ...reachBoard(c) }) : resting({ lean: 0.3, twist: 0.3, headTurn: 0.25, face: 'soft' })),
  cam: move([[0, [-0.62, 1.10, 0.62], [-0.15, 1.02, 0.10], 32], [1, [-0.60, 1.10, 0.58], [-0.15, 1.02, 0.10], 31]]) });
// (d) The PC learns to trust her once; the render runs through the night.
A.push({ id: 's07d1', ...monitor(231.0, 232.6, { pose: (t) => typing(t, { face: 'focus' }), region: [30, 30, 1500, 380] }) });
A.push({ id: 's07d2', ...insert(232.6, 236.4, { focus: 420, zoom: 1.95 }) });
A.push({ id: 's07d3', ...monitor(236.4, 240.6, { pose: (t) => typing(t, { face: 'focus' }), region: [30, 30, 1900, 620] }) });
A.push({ id: 's07d4', ...WIDE_BACKLEFT(240.6, 244.0, { pose: () => ({ ...resting(), visible: false }) }) });
A.push({ id: 's07d5', ...macro(244.0, 247.6, { from: 0.22, to: 0.2,
  pose: () => ({ ...resting(), visible: false }) }),
  cam: (t, k, c) => {
    const p = devPt(c, 0, 1, 0), n = devNormal(c), d = lerp(0.22, 0.2, smooth(k));
    const buzz = t > 246.4 && t < 246.46 ? 0.0006 * Math.sin(t * 900) : 0;
    return { pos: p.clone().add(n.clone().multiplyScalar(d)).add(V(buzz, 0.012, 0)), target: p.clone().add(V(buzz, 0.004, 0)), fov: 30, focus: d, aperture: 0.004, maxblur: 0.008 };
  } });
// (e) Evening: the update he is afraid of.
A.push({ id: 's07e1', ...insert(247.6, 252.3, { focus: 420 }) });
A.push({ id: 's07e2', t0: 252.3, t1: 261.3, cutaway: 'ota' });
A.push({ id: 's07e3', ...insert(261.3, 263.6, { focus: 430 }) });
A.push({ id: 's07e4', ...macro(263.6, 265.4, { from: 0.21, to: 0.2 }) });
A.push({ id: 's07e5', ...insert(265.4, 266.8, { focus: 430 }) });
A.push({ id: 's07e6', ...ots(266.8, 270.4, { poseOpts: { face: 'hold' } }) });
A.push({ id: 's07e7', ...face(270.4, 272.6, 'soft', { lean: 0.26, twist: 0.3, headTurn: 0.22 }), black: (t) => smooth(range(t, 272.1, 272.6)) });

// ------------------------------------------------------------ s08 照片
A.push({ id: 's08a', ...monitor(272.6, 276.6, { pose: (t) => resting({ lean: 0.3, face: 'tired' }), from: 0.56, to: 0.5 }),
  black: (t) => 1 - smooth(range(t, 272.6, 273.1)) });
A.push({ id: 's08b', ...monitor(276.6, 282.2, { pose: (t) => typing(t, { face: 'focus' }), region: [30, 40, 1300, 480] }) });
A.push({ id: 's08c', ...macro(282.2, 285.6, { from: 0.23, to: 0.2 }) });
A.push({ id: 's08d', ...insert(285.6, 290.0, { focus: 470, zoom: 1.95 }) });
A.push({ id: 's08e', ...face(290.0, 292.4, 'hold', { lean: 0.32, twist: 0.2, headTurn: 0.1, headTilt: 0.28 }) });
A.push({ id: 's08f', ...macro(292.4, 294.4, { from: 0.22, to: 0.21 }) });
A.push({ id: 's08g', ...insert(294.4, 299.6, { focus: 470, zoom: 1.95 }) });
A.push({ id: 's08h', ...monitor(299.6, 304.0, { pose: (t) => resting({ lean: 0.3, face: 'tired' }), region: [40, 40, 1880, 1000] }) });
A.push({ id: 's08i', ...WIDE_BACKLEFT(304.0, 306.4, { pose: () => resting({ lean: 0.3, headTilt: 0.3, face: 'tired' }) }),
  black: (t) => smooth(range(t, 305.8, 306.4)) });

// ------------------------------------------------------------ s09 凌晨三点
A.push({ id: 's09a', t0: 306.4, t1: 311.0, pose: () => resting({ lean: 0.4, twist: 0.36, headTurn: 0.3, headTilt: 0.3, face: 'tired' }),
  cam: move([[0, [-1.0, 1.25, 1.20], [-0.15, 0.95, -0.10], 34], [1, [-0.94, 1.22, 1.10], [-0.15, 0.95, -0.10], 33]]),
  black: (t) => 1 - smooth(range(t, 306.4, 307.0)) });
A.push({ id: 's09b', ...face(311.0, 314.2, 'sad', { lean: 0.36, twist: 0.36, headTurn: 0.26, headTilt: 0.2, lens: 0.85, exposure: 1.25, from: [-0.34, 0.95, -0.18], to: [-0.33, 0.955, -0.15], at: [0.06, 1.16, 0.40], fov: 24, focus: 0.72 }) });
A.push({ id: 's09c', ...macro(314.2, 318.6, { from: 0.23, to: 0.2 }) });
A.push({ id: 's09d', ...insert(318.6, 324.6, { focus: 470, zoom: 1.95 }) });
A.push({ id: 's09e', t0: 324.6, t1: 328.4,
  pose: (t, k, c) => {
    const held = t > 325.15 && t < 328.3 ? 1 : smooth(range(t, 324.9, 325.15)) * (t < 328.3 ? 1 : 0);
    const tip = devPt(c, 13.3, -23.6, 1.6), n = devNormal(c);
    return resting({ lean: 0.42, twist: 0.42, hideArms: true, hands: [handAt('hand_point', 'R', tip.clone().add(n.clone().multiplyScalar(0.012 * (1 - held) + 0.0005)), n.clone().negate(), V(0, 1, 0).add(n.clone().multiplyScalar(0.3)))] });
  },
  cam: (t, k, c) => { const p = devPt(c, 8, -10, 2), n = devNormal(c); return { pos: p.clone().add(n.clone().multiplyScalar(0.13)).add(V(0.07, 0.03, 0.03)), target: p, fov: 34, focus: 0.15, aperture: 0.006, maxblur: 0.01 }; },
  bloom: 0.28 });
A.push({ id: 's09f', ...macro(328.4, 331.0, { from: 0.24, to: 0.22 }) });
A.push({ id: 's09g', ...WIDE_BACKLEFT(331.0, 334.0, { pose: () => resting({ lean: 0.4, headTilt: 0.32, face: 'tired' }) }),
  black: (t) => smooth(range(t, 333.2, 334.0)) });

export const SCENES_A = A;
