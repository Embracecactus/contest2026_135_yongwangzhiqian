// SPDX-License-Identifier: Apache-2.0
// Shots by scene. Scene 10 (停电) lives here; scenes 1–9 in scenes_a.js and
// 11–13 in scenes_b.js.
//
// Scene 10 turns on what can be seen, not on exposition: he calls and gets
// silence (offline, the wake is consumed with no "我在"); the card's four beats
// play (local) and nobody comes in; he lifts her and her eyes glance (the real
// 800 ms motion frame); he reads the card in her light; he makes room for her
// beside his own work; he waters the plant himself. Narration only at the end.
import * as THREE from 'three';
import { SPOT, DESK } from './set.js';
import { clamp, easeInOut, easeOut, lerp, range, smooth, track, noise1 } from './util.js';
import { V, Q, HIPS, EYE_POS, seated, resting, sleeping, cam, move, shaniuEyes, devPt, devNormal, devUp, handAt, faceQuat, otsLeft } from './rig.js';
import { LEFT, RIGHT, phone, boardGrip, reachBoard, facingCard, sideHand } from './templates.js';
import { phoneAt } from './screens.js';
import { eyeAt } from './world.js';
import { SCENES_A } from './scenes_a.js';
import { SCENES_B } from './scenes_b.js';

const HELD = V(0.10, 1.12, 0.21);                       // POV hold, ~25 cm from his eyes
const toEye = (p) => faceQuat(EYE_POS.clone().sub(p).normalize(), V(0, 1, 0));

const S10 = [];
// Weeks pass over her dark, dusty screens; the plant droops again.
S10.push({ id: 's10a', t0: 334.0, t1: 342.0,
  pose: () => ({ ...resting(), visible: false }),
  cam: move([[0, [-0.20, 0.905, 0.03], [-0.40, 0.80, -0.214], 30], [1, [-0.26, 0.875, -0.05], [-0.40, 0.80, -0.214], 28]], { focus: (t, k) => lerp(0.31, 0.22, k), aperture: 0.004, maxblur: 0.01 }),
  black: (t) => 1 - smooth(range(t, 334.0, 334.8)) });
// Storm, flicker, blackout: the opening wide, now a stranger's room.
S10.push({ id: 's10b', t0: 342.0, t1: 345.6,
  pose: (t) => resting({ lean: 0.26, headTilt: 0.28, face: 'tired', headTurn: t > 344.7 ? 0.25 * smooth(range(t, 344.7, 345.4)) : 0 }),
  cam: move([[0, [-1.15, 1.42, 1.55], [-0.05, 0.95, -0.25], 38], [1, [-1.06, 1.40, 1.42], [-0.05, 0.94, -0.25], 37]]) });
// His phone: no network. He switches the torch on.
S10.push({ id: 's10b2', ...phone(345.6, 347.6, (t) => phoneAt(t), { focus: 300, zoom: 2.1, dim: 0.72 }),
  world: (t, k, c, w) => ({ ...w, torch: t > 346.9 ? 2.2 : 0 }) });
// Power bank into her USB-C, by torchlight — his fingers on the plug.
S10.push({ id: 's10c1', t0: 347.6, t1: 349.8,
  world: (t, k, c, w) => ({ ...w, torch: 1.8, usbPlug: { k: smooth(range(t, 347.9, 348.5)) } }),
  pose: (t, k, c, w) => {
    const plugTip = V(0, 0.0, -0.022).applyQuaternion(c.S.usbPlug.quaternion).add(c.S.usbPlug.position);
    const out = devPt(c, -5.5, -40, 2).sub(devPt(c, -5.5, -25, 2)).normalize();
    return seated({ pov: true, hideArms: true, hands: [sideHand(c, 'hand_pinch', plugTip.add(out.clone().multiplyScalar(0.006)), 1)] });
  },
  phone: () => ({ pos: V(-0.18, 1.02, 0.16), quat: faceQuat(V(0.0, 0.9, 0.6).normalize(), V(0, 0, -1)) }),
  cam: (t, k, c) => {
    const p = devPt(c, -5.5, -25, 2), n = devNormal(c);
    return { pos: p.clone().add(n.clone().multiplyScalar(0.10)).add(V(0.07, 0.0, 0.04)), target: p.clone().add(V(0, 0.006, 0)), fov: 34, focus: 0.12, aperture: 0.008, maxblur: 0.012 };
  } });
// Press K2.
S10.push({ id: 's10c2', t0: 349.8, t1: 351.4,
  world: (t, k, c, w) => ({ ...w, torch: 1.8 }),
  eye: (t, k, c, w) => null,
  pose: (t, k, c) => {
    const tip = devPt(c, 13.3, -23.6, 1.6);
    const n = devNormal(c);
    const press = smooth(range(t, 350.15, 350.4)) * (1 - smooth(range(t, 350.6, 350.9)));
    const tipPos = tip.clone().add(n.clone().multiplyScalar(0.012 * (1 - press) + 0.0005));
    return seated({ pov: true, hideArms: true, hands: [handAt('hand_point', 'R', tipPos, n.clone().negate(), V(0, 1, 0).add(n.clone().multiplyScalar(0.3)))] });
  },
  phone: () => ({ pos: V(-0.10, 1.04, 0.18), quat: faceQuat(V(-0.35, 0.8, 0.6).normalize(), V(0, 0, -1)) }),
  cam: (t, k, c) => {
    const p = devPt(c, 8, -16, 2), n = devNormal(c);
    return { pos: p.clone().add(n.clone().multiplyScalar(0.11)).add(V(0.07, 0.03, 0.03)), target: p, fov: 34, focus: 0.13, aperture: 0.008, maxblur: 0.012 };
  } });
// Her eyes come on. The only light in the room is hers, on his face.
S10.push({ id: 's10c3', t0: 351.4, t1: 353.4,
  world: (t, k, c, w) => ({ ...w, torch: 1.2 * (1 - smooth(range(t, 351.6, 352.3))) }),
  pose: (t) => resting({ lean: 0.30, twist: 0.30, headTurn: 0.20, headTilt: 0.10, face: 'hold', lens: smooth(range(t, 351.4, 352.2)) * 0.95,
    handL: V(-0.16, DESK.y + 0.02, -0.06), handR: V(0.12, DESK.y + 0.02, -0.03) }),
  phone: () => ({ pos: V(-0.02, DESK.y + 0.01, 0.0), quat: Q(-Math.PI / 2, 0, 0.6) }),
  cam: move([[0, [-0.31, 0.98, -0.15], [0.08, 1.16, 0.42], 33], [1, [-0.30, 0.985, -0.12], [0.08, 1.16, 0.42], 31]], { focus: () => 0.7, aperture: 0.002, maxblur: 0.006 }),
  exposure: 1.1, bloom: 0.28 });
// "你好，openvela。" — silence.
const calling = (o = {}) => (t) => resting({ lean: 0.36, twist: 0.42, headTurn: 0.32, headTilt: 0.20, handL: V(-0.18, DESK.y + 0.02, -0.04), handR: V(0.10, DESK.y + 0.02, -0.02), ...o });
S10.push({ id: 's10d1', t0: 353.4, t1: 356.8, pose: calling({ face: 'neutral' }),
  cam: (t, k) => {
    const a = otsLeft(LEFT, { back: 0.40, up: 0.13, side: -0.17 }), b = otsLeft(LEFT, { back: 0.34, up: 0.12, side: -0.16 });
    const pos = a.clone().lerp(b, smooth(k)); pos.x += noise1(t * 0.6, 4) * 0.003;
    return { pos, target: LEFT.clone().add(V(0.12, 0.05, 0)), fov: 20, focus: 1.1, aperture: 0.002, maxblur: 0.007 };
  }, bloom: 0.3 });
S10.push({ id: 's10d2', t0: 356.8, t1: 358.2, pose: calling({ face: 'hold', lens: 0.9 }),
  cam: move([[0, [-0.22, 1.13, -0.08], [0.08, 1.14, 0.40], 34], [1, [-0.21, 1.13, -0.06], [0.08, 1.14, 0.40], 33]], { focus: () => 0.56, aperture: 0.002, maxblur: 0.006 }),
  exposure: 1.15, bloom: 0.26 });
S10.push({ id: 's10d3', t0: 358.2, t1: 360.8, pose: calling({ face: 'hold' }),
  cam: (t, k, c) => {
    const p = devPt(c, 0, 4, 0), n = devNormal(c);
    const d = lerp(0.26, 0.22, smooth(k));
    return { pos: p.clone().add(n.clone().multiplyScalar(d)).add(V(0.03, 0.02, 0)), target: p, fov: 26, focus: d, aperture: 0.004, maxblur: 0.008 };
  }, bloom: 0.3 });
// He gives up, lowers his eyes — to the door card lying by her.
S10.push({ id: 's10d4', t0: 360.8, t1: 362.6,
  pose: (t) => calling({ face: 'sad', lens: 0.7, headTilt: lerp(0.2, 0.45, smooth(range(t, 361.0, 361.8))), headTurn: lerp(0.32, 0.45, smooth(range(t, 361.0, 361.8))) })(t),
  cam: move([[0, [-0.22, 1.12, -0.06], [0.08, 1.12, 0.40], 34], [1, [-0.215, 1.115, -0.04], [0.08, 1.11, 0.40], 33]], { focus: () => 0.55, aperture: 0.002, maxblur: 0.006 }),
  exposure: 1.15, bloom: 0.26 });
// The card on her: four beats, from her speaker. Printed side toward him.
const cardTap = (t, c) => {
  const touch = smooth(range(t, 362.8, 363.25)) * (1 - smooth(range(t, 363.9, 364.4)));
  const n = devNormal(c);
  const target = devPt(c, 2, -52, 0).add(n.clone().multiplyScalar(0.004));
  const rest = devPt(c, 26, -46, 0).add(n.clone().multiplyScalar(0.08)).add(V(0.05, 0.02, 0.0));
  const pos = rest.clone().lerp(target, touch);
  const quat = faceQuat(n, devUp(c)).multiply(Q(Math.PI / 2, 0, 0.15));
  return { pos, quat };
};
S10.push({ id: 's10e1', t0: 362.6, t1: 364.6,
  world: (t, k, c, w) => ({ ...w, doorCard: cardTap(t, c) }),
  pose: (t, k, c, w) => {
    const edge = w.doorCard.pos.clone().add(V(1, 0, 0).applyQuaternion(w.doorCard.quat).multiplyScalar(0.027)).add(V(0, 0, -1).applyQuaternion(w.doorCard.quat).multiplyScalar(0.012));
    return resting({ lean: 0.34, twist: 0.4, headTurn: 0.3, headTilt: 0.25, hideArms: true, face: 'hold', hands: [sideHand(c, 'hand_pinch', edge, 1)] });
  },
  cam: (t, k, c) => {
    const p = devPt(c, 4, -18, 0), n = devNormal(c);
    return { pos: p.clone().add(n.clone().multiplyScalar(0.26)).add(V(-0.06, 0.03, 0.02)), target: p, fov: 30, focus: 0.27, aperture: 0.003, maxblur: 0.008 };
  }, bloom: 0.28 });
// He turns toward the door. Four knocks. Nobody.
S10.push({ id: 's10e2', t0: 364.6, t1: 368.8,
  pose: (t) => resting({ lean: 0.18, twist: lerp(0.0, -0.55, smooth(range(t, 364.7, 365.6))), headTurn: lerp(0.1, -0.85, smooth(range(t, 364.8, 365.8))), headTilt: -0.04, face: 'hold',
    handL: V(-0.10, DESK.y + 0.02, -0.02), handR: V(0.24, DESK.y + 0.02, 0.0) }),
  world: (t, k, c, w) => ({ ...w, exposureMul: 2.2 }),
  cam: move([[0, [-0.10, 1.40, 0.05], [1.86, 1.0, 1.45], 38], [1, [-0.09, 1.40, 0.08], [1.86, 1.0, 1.45], 35]], { focus: () => 2.3, aperture: 0.0014, maxblur: 0.005 }),
  bloom: 0.2 });
// He looks back at her.
S10.push({ id: 's10e3', t0: 368.8, t1: 370.6,
  pose: (t) => resting({ lean: 0.32, twist: lerp(-0.3, 0.38, smooth(range(t, 368.8, 369.6))), headTurn: lerp(-0.5, 0.3, smooth(range(t, 368.8, 369.6))), headTilt: 0.16, face: 'hold', lens: smooth(range(t, 369.2, 369.8)) * 0.9 }),
  cam: move([[0, [-0.30, 0.99, -0.13], [0.08, 1.16, 0.42], 32], [1, [-0.295, 0.995, -0.11], [0.08, 1.16, 0.42], 31]], { focus: () => 0.68, aperture: 0.002, maxblur: 0.006 }),
  exposure: 1.1, bloom: 0.28 });
// He lifts her: her eyes glance (800 ms). He wipes the dust off with a finger.
const liftPath = (t, c) => {
  const up = smooth(range(t, 370.7, 371.9));
  const from = SPOT.shaniuLeft.clone().add(V(0, 0.045, 0.004));
  const pos = from.clone().lerp(HELD, up);
  const q0 = new THREE.Quaternion().setFromEuler(new THREE.Euler(-THREE.MathUtils.degToRad(24), 0.42, 0, 'YXZ'));
  const quat = q0.clone().slerp(toEye(HELD), up);
  return { pos, quat };
};
S10.push({ id: 's10f1', t0: 370.6, t1: 373.8,
  world: (t, k, c, w) => ({ ...w, shaniuHeld: liftPath(t, c) }),
  pose: (t, k, c) => {
    const hands = boardGrip(c, { y: 4 }).filter((h) => h.side === 'L');
    const wipe = range(t, 372.3, 373.6);
    if (wipe > 0 && wipe < 1) {
      const n = devNormal(c);
      const x = lerp(24, -22, easeInOut(wipe)), tip = devPt(c, x, 22 + 2 * Math.sin(wipe * Math.PI), 3.0);
      const left = devPt(c, -1, 0, 0).sub(devPt(c, 0, 0, 0)).normalize();
      hands.push(handAt('hand_point', 'R', tip, left.clone().add(n.clone().multiplyScalar(-0.15)).normalize(), n.clone()));
    } else hands.push(...boardGrip(c, { y: 4 }).filter((h) => h.side === 'R'));
    return seated({ pov: true, hideArms: true, lean: 0.2, hands });
  },
  cam: (t) => ({ pos: EYE_POS.clone().add(V(0, -0.01, 0.02 - 0.02 * smooth(range(t, 371.9, 373.8)))), target: HELD.clone().lerp(V(-0.30, 0.86, -0.15), 1 - smooth(range(t, 370.6, 371.9))), fov: 30, focus: 0.21, aperture: 0.003, maxblur: 0.008 }),
  bloom: 0.28 });
S10.push({ id: 's10f2', t0: 373.8, t1: 377.2,
  eye: (t, k, c, w) => ({ ...eyeAt(t, w), glow: 0.045 }),
  world: (t, k, c, w) => ({ ...w, shaniuHeld: { pos: HELD.clone().add(V(0, 0.002 * Math.sin(t * 2.1), 0)), quat: toEye(HELD) } }),
  pose: (t, k, c) => seated({ lean: 0.2, headTilt: 0.16, lens: 0.95, face: t < 375.0 ? 'hold' : 'soft', ...reachBoard(c) }),
  cam: move([[0, [-0.30, 1.16, 0.07], [0.10, 1.215, 0.41], 29], [1, [-0.28, 1.17, 0.09], [0.10, 1.215, 0.41], 26]], { focus: () => 0.47, aperture: 0.002, maxblur: 0.006 }),
  exposure: 1.0, bloom: 0.24 });
// He lowers her over the desk corner; her light finds the card.
const toCard = (t) => {
  const k = smooth(range(t, 377.2, 379.0));
  const a = HELD, b = V(-0.47, 0.90, 0.10);
  const pos = a.clone().lerp(b, k);
  const quat = toEye(a).slerp(faceQuat(V(-0.2, -0.9, -0.3).normalize(), V(0, 0, -1)), k);
  return { pos, quat };
};
S10.push({ id: 's10g1', t0: 377.2, t1: 379.6,
  world: (t, k, c, w) => ({ ...w, shaniuHeld: toCard(t) }),
  pose: (t, k, c) => seated({ lean: 0.42, twist: 0.45, headTurn: 0.3, headTilt: 0.45, face: 'hold', ...reachBoard(c) }),
  cam: move([[0, [-0.30, 1.22, 0.42], [-0.52, 0.80, -0.05], 34], [1, [-0.36, 1.12, 0.30], [-0.55, 0.76, -0.04], 30]], { focus: () => 0.5, aperture: 0.002, maxblur: 0.006 }),
  exposure: 1.2, bloom: 0.26 });
S10.push({ id: 's10g2', t0: 379.6, t1: 383.4,
  world: (t, k, c, w) => ({ ...w, shaniuHeld: toCard(379.0) }),
  pose: (t, k, c) => seated({ lean: 0.42, twist: 0.45, headTurn: 0.3, headTilt: 0.45, face: 'hold', ...reachBoard(c) }),
  cam: move([[0, [-0.52, 1.0, 0.12], [-0.56, DESK.y, -0.025], 28], [1, [-0.545, 0.95, 0.07], [-0.56, DESK.y, -0.025], 25]], { focus: () => 0.22, aperture: 0.003, maxblur: 0.008 }),
  exposure: 1.25, bloom: 0.26 });
// He makes room beside his keyboard and sets her down there, facing him.
const toRight = (t) => {
  const k = easeInOut(range(t, 383.6, 387.0));
  const a = toCard(379.0), end = SPOT.shaniuRight.clone().add(V(0, 0.045, 0.004));
  const mid = V(0.20, 0.93, -0.02);
  const pos = k < 0.55 ? a.pos.clone().lerp(mid, k / 0.55) : mid.clone().lerp(end, (k - 0.55) / 0.45);
  const qEnd = new THREE.Quaternion().setFromEuler(new THREE.Euler(-THREE.MathUtils.degToRad(24), -0.38, 0, 'YXZ'));
  return { pos, quat: a.quat.clone().slerp(toEye(mid), Math.min(1, k / 0.55)).slerp(qEnd, Math.max(0, (k - 0.55) / 0.45)) };
};
S10.push({ id: 's10h1', t0: 383.4, t1: 387.8,
  world: (t, k, c, w) => (t >= 387.0 ? w : { ...w, shaniuHeld: toRight(t), powerBank: t > 385.6 ? V(0.58, DESK.y + 0.008, -0.33) : w.powerBank }),
  pose: (t, k, c, w) => {
    const base = { lean: 0.40, twist: lerp(0.3, -0.2, smooth(range(t, 383.6, 386.4))), headTurn: lerp(0.2, -0.25, smooth(range(t, 383.6, 386.4))), headTilt: 0.42, face: 'focus' };
    if (t >= 387.0) return seated({ ...base, handL: V(0.10, DESK.y + 0.02, -0.04), handR: V(0.34, DESK.y + 0.03, 0.0) });
    const hold = reachBoard(c);
    if (t > 384.5 && t < 385.4) {   // left hand keeps her; right hand slides the mouse aside
      const m = V(0.40, DESK.y + 0.03, -0.11).lerp(V(0.30, DESK.y + 0.03, 0.03), smooth(range(t, 384.6, 385.3)));
      return seated({ ...base, handL: hold.handL, handR: m });
    }
    return seated({ ...base, ...hold });
  },
  cam: (t, k) => (t < 387.05
    ? move([[0, [0.14, 1.78, -0.06], [0.14, DESK.y, -0.12], 50], [1, [0.18, 1.70, -0.06], [0.20, DESK.y, -0.12], 46]])(t, range(t, 383.4, 387.05))
    : move([[0, [0.74, 0.87, 0.12], [0.47, 0.80, -0.14], 34], [1, [0.73, 0.865, 0.10], [0.47, 0.80, -0.14], 32]], { focus: () => 0.37, aperture: 0.002, maxblur: 0.006 })(t, range(t, 387.05, 387.8))),
  exposure: 1.5, bloom: 0.24 });
// The pothos on the sill, drooping again — a lightning flash shows it.
S10.push({ id: 's10i1', t0: 387.8, t1: 390.4,
  pose: (t) => resting({ lean: 0.28, twist: -0.25, headTurn: lerp(-0.3, -0.42, smooth(range(t, 387.9, 388.6))), headTilt: -0.12, face: 'neutral', handL: V(0.10, DESK.y + 0.02, -0.04), handR: V(0.34, DESK.y + 0.03, 0.0) }),
  cam: move([[0, [0.36, 1.10, -0.18], [0.54, 1.02, -0.60], 34], [1, [0.37, 1.10, -0.21], [0.54, 1.01, -0.60], 32]], { focus: () => 0.42, aperture: 0.002, maxblur: 0.006 }),
  exposure: 1.6, bloom: 0.2 });
// Into her light; he waters it.
const pourBottle = (t) => {
  const lift = smooth(range(t, 390.5, 391.0));
  const tilt = smooth(range(t, 391.1, 391.6)) * (1 - smooth(range(t, 393.0, 393.5)));
  const rest = V(0.27, DESK.y, -0.44), held = V(0.46, 0.96, 0.03);
  const pos = rest.clone().lerp(held, lift);
  const quat = new THREE.Quaternion().setFromEuler(new THREE.Euler(0, 0, -1.85 * tilt));
  return { pos, quat };
};
S10.push({ id: 's10i2', t0: 390.4, t1: 393.8,
  world: (t, k, c, w) => {
    const b = pourBottle(t), tilt = smooth(range(t, 391.3, 391.6)) * (1 - smooth(range(t, 392.9, 393.1)));
    const cap = V(0, 0.215, 0).applyQuaternion(b.quat).add(b.pos);
    return { ...w, bottle: b, stream: tilt > 0.5 ? { from: cap, to: w.plantPos.clone().add(V(0.0, 0.11, 0)) } : null };
  },
  pose: (t, k, c, w) => {
    const grip = w.bottle.pos.clone().add(V(0, 0.09, 0).applyQuaternion(w.bottle.quat));
    return resting({ lean: 0.36, twist: -0.3, headTurn: -0.32, headTilt: 0.36, face: 'soft', handR: grip, handL: V(0.08, DESK.y + 0.02, -0.02) });
  },
  cam: move([[0, [0.92, 1.0, 0.36], [0.60, 0.86, 0.0], 32], [1, [0.90, 0.99, 0.33], [0.60, 0.86, 0.0], 30]], { focus: () => 0.45, aperture: 0.002, maxblur: 0.006 }),
  exposure: 1.4, bloom: 0.22 });
// He rests beside her. Only now the narration.
const resting10 = (t) => {
  const k = smooth(range(t, 395.2, 397.4));
  return resting({ lean: lerp(0.3, 0.95, k), headTilt: lerp(0.12, 0.55, k), headTurn: lerp(-0.3, -0.5, k), twist: lerp(-0.15, -0.05, k), face: t > 397.6 ? 'sleep' : (t > 394.6 ? 'soft' : 'hold'),
    handL: V(lerp(0.0, -0.05, k), DESK.y + 0.03, lerp(-0.02, -0.10, k)), handR: V(lerp(0.30, 0.20, k), DESK.y + 0.03, lerp(0.0, -0.12, k)) });
};
S10.push({ id: 's10j1', t0: 393.8, t1: 397.8, pose: resting10,
  cam: (t, k) => {
    const d = smooth(range(t, 395.2, 397.4));
    // Hold the frame; he lays his head down out of it.
    return { pos: V(0.32, 1.22, -0.16).lerp(V(0.325, 1.215, -0.17), d), target: V(0.13, 1.20, 0.26), fov: 32, focus: 0.46, aperture: 0.0018, maxblur: 0.006 };
  },
  exposure: 1.25, bloom: 0.24 });
S10.push({ id: 's10j2', t0: 397.8, t1: 400.6, pose: resting10,
  cam: (t, k, c) => { const p = devPt(c, 0, 4, 0), n = devNormal(c); const d = lerp(0.24, 0.21, smooth(k)); return { pos: p.clone().add(n.clone().multiplyScalar(d)).add(V(-0.02, 0.015, 0)), target: p, fov: 28, focus: d, aperture: 0.004, maxblur: 0.008 }; },
  bloom: 0.28 });
S10.push({ id: 's10j3', t0: 400.6, t1: 404.0, pose: resting10,
  cam: move([[0, [0.84, 1.14, 0.16], [0.32, 0.86, -0.07], 38], [1, [0.82, 1.13, 0.16], [0.32, 0.86, -0.07], 36]], { focus: () => 0.6, aperture: 0.0014, maxblur: 0.005 }),
  exposure: 1.3, bloom: 0.24,
  black: (t) => smooth(range(t, 402.6, 404.0)) });

export const SCENE_SHOTS = [...SCENES_A, ...S10, ...SCENES_B];
