// SPDX-License-Identifier: Apache-2.0
// Scenes 11–13: morning; giving "小鱼" back; his own eyes for her; the
// lighthouse finished; the photos kept but archived; "我在。来得及。"
import * as THREE from 'three';
import { SPOT, DESK } from './set.js';
import { clamp, easeInOut, easeOut, lerp, range, smooth, noise1 } from './util.js';
import { V, Q, EYE_POS, seated, resting, typing, sleeping, move, otsLeft, devPt, devNormal, devUp, handAt, faceQuat } from './rig.js';
import { RIGHT, MON, macro, monitor, phone, WIDE_BACKLEFT, SIDE , sideHand } from './templates.js';
import { phoneAt } from './screens.js';

const insert = (t0, t1, o = {}) => phone(t0, t1, (t) => phoneAt(t), { focus: o.focus ?? 330, zoom: o.zoom ?? 2.15, dim: o.dim ?? 0.55, look: V(0.3, DESK.y, -0.2) });
// She now sits on his right: over his right shoulder to her.
const otsRight = (t0, t1, o = {}) => ({ t0, t1,
  pose: o.pose || ((t) => resting({ lean: 0.24, twist: -0.3, headTurn: -0.28, headTilt: 0.12, face: o.face || 'neutral' })),
  cam: (t, k) => {
    const back = V(0.34, 1.36, 0.86).lerp(RIGHT, 0.04 * smooth(k));
    back.x += noise1(t * 0.5, 7) * 0.003;
    return { pos: back, target: RIGHT.clone().add(V(-0.10, 0.05, 0)), fov: o.fov ?? 22, focus: 1.12, aperture: 0.0018, maxblur: 0.006 };
  }, bloom: 0.3 });
const faceR = (t0, t1, mood, o = {}) => ({ t0, t1,
  pose: o.pose || ((t) => resting({ lean: o.lean ?? 0.24, twist: o.twist ?? -0.2, headTurn: o.headTurn ?? -0.15, headTilt: o.headTilt ?? 0.08, face: typeof mood === 'function' ? mood(t) : mood })),
  cam: move([[0, o.from || [0.36, 1.17, -0.04], o.at || [0.10, 1.19, 0.42], o.fov ?? 26], [1, o.to || [0.35, 1.17, -0.01], o.at || [0.10, 1.19, 0.42], (o.fov ?? 26) - 1.5]], { focus: () => 0.52, aperture: 0.002, maxblur: 0.006 }),
  exposure: o.exposure ?? 1.0, bloom: 0.25 });
const macroR = (t0, t1, o = {}) => macro(t0, t1, { pose: (t) => resting({ lean: 0.24, twist: -0.3, headTurn: -0.25, face: 'neutral' }), ...o });

const B = [];
// ------------------------------------------------------------ s11 天亮
B.push({ id: 's11a', t0: 404.0, t1: 406.6, pose: () => sleeping({ face: 'sleep' }),
  cam: move([[0, [-0.52, 1.02, -0.36], [-0.66, 0.97, -0.56], 34], [1, [-0.50, 1.02, -0.38], [-0.66, 0.97, -0.56], 32]], { focus: () => 0.24, aperture: 0.004, maxblur: 0.01 }),
  black: (t) => 1 - smooth(range(t, 404.0, 404.8)) });
B.push({ id: 's11b', t0: 406.6, t1: 409.2, pose: (t) => {
    const k = smooth(range(t, 407.0, 408.6));
    return resting({ lean: lerp(0.95, 0.24, k), headTilt: lerp(0.55, 0.1, k), headTurn: lerp(-0.45, -0.2, k), face: k < 0.5 ? 'sleep' : 'tired',
      handL: V(lerp(-0.06, -0.02, k), DESK.y + 0.03, lerp(-0.12, -0.02, k)), handR: V(lerp(0.22, 0.24, k), DESK.y + 0.03, lerp(-0.14, -0.02, k)) });
  },
  cam: move([[0, [0.86, 0.96, 0.32], [0.20, 0.92, 0.10], 36], [1, [0.82, 0.96, 0.30], [0.20, 0.94, 0.10], 35]]) });
B.push({ id: 's11c', ...monitor(409.2, 412.4, { pose: (t) => typing(t, { face: 'focus' }), region: [40, 40, 1300, 700] }) });
B.push({ id: 's11d', ...insert(412.4, 414.6, { focus: 430 }) });
B.push({ id: 's11e', ...insert(414.6, 417.6, { focus: 300 }) });
B.push({ id: 's11f', ...insert(417.6, 419.0, { focus: 330 }) });
B.push({ id: 's11g', ...otsRight(419.0, 423.0) });
B.push({ id: 's11h', ...faceR(423.0, 428.4, 'neutral') });
B.push({ id: 's11i', ...macroR(428.4, 433.2, { from: 0.23, to: 0.2 }) });
B.push({ id: 's11j', ...faceR(433.2, 434.0, 'hold') });
B.push({ id: 's11k', ...insert(434.0, 439.6, { focus: 300 }) });
B.push({ id: 's11l', ...insert(439.6, 441.2, { focus: 300 }) });
B.push({ id: 's11m', ...insert(441.2, 443.6, { focus: 420 }) });
// Black coffee in his own mug.
const sipOwn = (t) => {
  const up = smooth(range(t, 446.0, 446.6)) * (1 - smooth(range(t, 447.6, 448.1)));
  const rest = V(0.40, DESK.y, -0.33);
  const hand = rest.clone().add(V(0.0, 0.08, 0.03)).lerp(V(0.14, 1.10, 0.30), up);
  return { hand, mug: { pos: hand.clone().add(V(0.03, -0.06, -0.02)), quat: new THREE.Quaternion().setFromEuler(new THREE.Euler(up * 0.9, -0.4, 0)) } };
};
B.push({ id: 's11n', t0: 443.6, t1: 449.0,
  world: (t, k, c, w) => ({ ...w, myMugPose: t > 445.6 ? sipOwn(t).mug : null }),
  pose: (t) => resting({ lean: 0.22, twist: -0.1, headTurn: -0.05, face: t > 446.8 && t < 447.8 ? 'soft' : 'neutral', handR: t > 445.6 ? sipOwn(t).hand : V(0.30, DESK.y + 0.03, -0.02) }),
  cam: move([[0, [1.06, 1.14, 0.36], [0.08, 1.06, 0.20], 30], [1, [1.02, 1.14, 0.33], [0.08, 1.06, 0.20], 29]]) });
// Cuttings from the pothos root in her mug.
B.push({ id: 's11o', t0: 449.0, t1: 452.6, pose: () => resting({ lean: 0.34, twist: 0.2, headTurn: 0.15, headTilt: 0.35, face: 'soft', handL: V(-0.10, DESK.y + 0.14, -0.06) }),
  cam: move([[0, [-0.28, 0.96, 0.14], [-0.13, 0.82, -0.06], 30], [1, [-0.27, 0.95, 0.12], [-0.13, 0.83, -0.06], 28]], { focus: () => 0.27, aperture: 0.003, maxblur: 0.008 }) });
B.push({ id: 's11p', ...insert(452.6, 456.0, { focus: 520, zoom: 1.9 }) });
// He draws her a new pair of eyes.
B.push({ id: 's11q', t0: 456.0, t1: 459.6, pose: (t) => typing(t, { face: 'focus' }),
  cam: move([[0, [0.10, 1.30, 0.05], [0.10, DESK.y, -0.14], 40], [1, [0.10, 1.26, 0.04], [0.10, DESK.y, -0.14], 38]]),
  overlay: (t, k, c, w, o) => o.sketch(t, 456.0, 459.6) });
B.push({ id: 's11r', ...monitor(459.6, 462.4, { pose: (t) => typing(t, { face: 'focus' }), region: [40, 60, 1880, 1000] }) });
B.push({ id: 's11s', ...otsRight(462.4, 466.0) });
B.push({ id: 's11t', ...faceR(466.0, 468.2, 'neutral') });
B.push({ id: 's11u', ...macroR(468.2, 471.6, { from: 0.22, to: 0.19 }) });
B.push({ id: 's11v', ...SIDE(471.6, 476.4, { pose: (t) => typing(t, { face: 'soft', twist: -0.1 }) }) });

// ------------------------------------------------------------ s12 灯塔
const cardOn = (t, c, t0) => {
  const touch = smooth(range(t, t0, t0 + 0.35)) * (1 - smooth(range(t, t0 + 0.8, t0 + 1.2)));
  const n = devNormal(c);
  const target = devPt(c, 2, -52, 0).add(n.clone().multiplyScalar(0.004));
  const rest = devPt(c, -26, -46, 0).add(n.clone().multiplyScalar(0.08)).add(V(-0.05, 0.02, 0.0));
  return { pos: rest.clone().lerp(target, touch), quat: faceQuat(n, devUp(c)).multiply(Q(Math.PI / 2, 0, -0.15)) };
};
B.push({ id: 's12a', t0: 476.4, t1: 478.2,
  world: (t, k, c, w) => ({ ...w, doorCard: cardOn(t, c, 476.8) }),
  pose: (t, k, c, w) => {
    const n = devNormal(c);
    const corner = w.doorCard.pos.clone().add(V(-1, 0, 0).applyQuaternion(w.doorCard.quat).multiplyScalar(0.027)).add(V(0, 0, -1).applyQuaternion(w.doorCard.quat).multiplyScalar(0.012));
    return resting({ lean: 0.3, twist: -0.35, headTurn: -0.3, hideArms: true, face: 'focus',
      hands: [sideHand(c, 'hand_pinch', corner, -1)] });
  },
  cam: (t, k, c) => { const p = devPt(c, -4, -18, 0), n = devNormal(c); return { pos: p.clone().add(n.clone().multiplyScalar(0.27)).add(V(-0.10, 0.03, 0.02)), target: p, fov: 30, focus: 0.3, aperture: 0.003, maxblur: 0.008 }; },
  bloom: 0.26 });
B.push({ id: 's12b', ...WIDE_BACKLEFT(478.2, 480.4, { pose: (t) => typing(t, { face: 'focus' }) }) });
B.push({ id: 's12c', ...macroR(480.4, 482.8, { from: 0.21, to: 0.2 }) });
B.push({ id: 's12d', ...monitor(482.8, 486.6, { pose: (t) => typing(t, { face: 'focus' }), region: [30, 30, 1900, 560] }) });
B.push({ id: 's12e', ...macroR(486.6, 488.0, { from: 0.21, to: 0.2 }) });
B.push({ id: 's12f', ...monitor(488.0, 489.4, { pose: (t) => resting({ lean: 0.3, face: 'neutral' }), region: [30, 40, 1250, 470] }) });
B.push({ id: 's12g', ...macroR(489.4, 490.4, { from: 0.22, to: 0.21 }) });
B.push({ id: 's12h', ...insert(490.4, 494.0, { focus: 470, zoom: 1.95 }) });
B.push({ id: 's12i', ...monitor(494.0, 496.6, { pose: (t) => resting({ lean: 0.3, face: 'soft' }), region: [30, 40, 1900, 1010] }) });
B.push({ id: 's12j', ...monitor(496.6, 499.8, { pose: (t) => typing(t, { face: 'focus' }), region: [30, 40, 1250, 470] }) });
B.push({ id: 's12k', ...macroR(499.8, 500.6, { from: 0.22, to: 0.21 }) });
B.push({ id: 's12l', ...insert(500.6, 504.4, { focus: 470, zoom: 1.95 }) });
B.push({ id: 's12m', ...monitor(504.4, 508.6, { pose: (t) => resting({ lean: 0.26, face: 'soft' }), region: [30, 40, 1900, 1010] }) });
// One frame from her camera: him, holding up her mug with the cuttings, smiling.
const posing = (t) => {
  const up = smooth(range(t, 508.8, 509.6));
  const hand = V(-0.02, DESK.y + 0.05, -0.05).lerp(V(0.18, 1.06, 0.16), up);
  return { hand, mug: { pos: hand.clone().add(V(0.0, -0.05, -0.03)), quat: new THREE.Quaternion().setFromEuler(new THREE.Euler(0, 0.9, 0)) } };
};
const poseWithMug = (t) => resting({ lean: 0.22, twist: -0.3, headTurn: -0.3, headTilt: 0.04, face: 'smile', handL: posing(t).hand, handR: V(0.30, DESK.y + 0.03, 0.03) });
B.push({ id: 's12n', t0: 508.6, t1: 511.2,
  world: (t, k, c, w) => ({ ...w, fishMugPose: posing(t).mug }),
  pose: (t) => poseWithMug(t),
  cam: move([[0, [0.52, 0.96, -0.10], [0.12, 1.12, 0.38], 34], [1, [0.51, 0.96, -0.08], [0.12, 1.12, 0.38], 33]]) });
B.push({ id: 's12o', ...monitor(511.2, 515.6, { pose: (t) => poseWithMug(t), from: 0.56, to: 0.5 }),
  world: (t, k, c, w) => ({ ...w, fishMugPose: posing(511.2).mug }) });
B.push({ id: 's12p', ...WIDE_BACKLEFT(515.6, 518.0, { pose: (t) => typing(t, { face: 'soft' }) }),
  black: (t) => smooth(range(t, 517.6, 518.0)) * 0.6 });

// ------------------------------------------------------------ s13 在吗
B.push({ id: 's13a', t0: 518.0, t1: 524.2, pose: (t) => typing(t, { face: 'neutral' }),
  cam: move([[0, [-1.20, 1.46, 1.62], [-0.02, 0.98, -0.28], 38], [1, [-1.02, 1.40, 1.38], [-0.02, 0.98, -0.28], 36]]),
  black: (t) => 0.6 * (1 - smooth(range(t, 518.0, 518.6))) });
B.push({ id: 's13b', ...insert(524.2, 531.0, { focus: 420, zoom: 1.38 }) });
B.push({ id: 's13c', ...faceR(531.0, 532.6, (t) => (t < 531.7 ? 'neutral' : 'soft'), {
  pose: (t) => resting({ lean: 0.22, twist: lerp(0.0, -0.32, smooth(range(t, 531.8, 532.5))), headTurn: lerp(0.0, -0.32, smooth(range(t, 531.8, 532.5))), headTilt: 0.06, face: t < 531.7 ? 'neutral' : 'soft' }) }) });
B.push({ id: 's13d', ...otsRight(532.6, 535.4, { pose: (t) => resting({ lean: 0.24, twist: -0.32, headTurn: -0.32, headTilt: 0.08, face: t > 533.8 ? 'smile' : 'soft' }), fov: 24 }) });
// The finished lighthouse, the new leaf, the two of them at work.
B.push({ id: 's13e', t0: 535.4, t1: 541.4, pose: (t) => typing(t, { face: 'soft', twist: -0.1 }),
  cam: move([[0, [0.62, 1.10, 0.20], [0.36, 1.02, -0.46], 34], [0.45, [0.62, 1.46, 0.78], [0.22, 1.02, -0.46], 36], [1, [-0.30, 1.72, 1.60], [0.16, 1.06, -0.50], 38]], { focus: (t, k) => lerp(0.7, 2.2, k), aperture: 0.0012, maxblur: 0.004 }) });
B.push({ id: 's13f', t0: 541.4, t1: 557.0, pose: (t) => typing(t, { face: 'soft', twist: -0.1 }),
  cam: move([[0, [-0.30, 1.72, 1.60], [0.16, 1.06, -0.50], 38], [1, [-0.40, 1.78, 1.76], [0.15, 1.08, -0.52], 38]]),
  black: (t) => smooth(range(t, 541.6, 543.4)) });

export const SCENES_B = B;
