// SPDX-License-Identifier: Apache-2.0
// Reusable shot templates so each scene reads as staging, not plumbing.
import * as THREE from 'three';
import { SPOT, DESK } from './set.js';
import { clamp, easeInOut, lerp, noise1, range, smooth, track } from './util.js';
import { V, Q, EYE_POS, seated, resting, typing, move, shaniuEyes, otsLeft, devPt, devNormal, faceQuat, handAt } from './rig.js';

export const LEFT = shaniuEyes(SPOT.shaniuLeft);
export const RIGHT = shaniuEyes(SPOT.shaniuRight);
export const MON = V(0.10, DESK.y + 0.283, -0.428);

// Over his left shoulder to Shaniu on her side of the desk.
export function ots(t0, t1, extra = {}) {
  const target = extra.target || LEFT;
  return { t0, t1, pose: extra.pose || ((t) => resting({ lean: 0.24, twist: 0.32, headTurn: 0.26, headTilt: 0.12, ...(extra.poseOpts || {}) })),
    cam: (t, k) => {
      const a = otsLeft(target, { back: extra.back ?? 0.40, up: extra.up ?? 0.13, side: extra.side ?? -0.17 });
      const push = (extra.push ?? 0.04) * smooth(k);
      const pos = a.clone().lerp(target, push);
      pos.x += noise1(t * 0.5, 7) * 0.003; pos.y += noise1(t * 0.4, 8) * 0.002;
      return { pos, target: target.clone().add(V(0.12, 0.05, 0)), fov: extra.fov ?? 22, focus: 1.18 - push * 1.0, aperture: 0.0018, maxblur: 0.006 };
    }, bloom: extra.bloom ?? 0.32, ...extra.shot };
}

// Close on Shaniu's face. side = 'L' (her side) or 'R' (his side).
export function macro(t0, t1, extra = {}) {
  return { t0, t1, pose: extra.pose || ((t) => resting({ lean: 0.24, twist: 0.3, headTurn: 0.2 })),
    cam: (t, k, c) => {
      const p = devPt(c, 0, 1, 0), n = devNormal(c);
      const dist = lerp(extra.from ?? 0.20, extra.to ?? 0.17, smooth(k));
      const off = extra.off || V(0.0, 0.012, 0);
      return { pos: p.clone().add(n.clone().multiplyScalar(dist)).add(off), target: p.clone().add(V(0, 0.004, 0)), fov: extra.fov ?? 30, focus: dist, aperture: 0.004, maxblur: 0.008 };
    }, bloom: extra.bloom ?? 0.3, ...extra.shot };
}

// Monitor close-up, straight on. extra.region = [x0, y0, x1, y1] in screen
// pixels (1920×1080 canvas on a 0.54 × 0.304 m panel) frames that part.
export function monitor(t0, t1, extra = {}) {
  let dx = extra.dx ?? 0, dy = extra.dy ?? 0, from = extra.from ?? 0.50, to = extra.to ?? 0.46;
  const fov = extra.fov ?? 34;
  if (extra.region) {
    const [x0, y0, x1, y1] = extra.region;
    dx = ((x0 + x1) / 2 / 1920 - 0.5) * 0.54; dy = (0.5 - (y0 + y1) / 2 / 1080) * 0.304;
    const w = (x1 - x0) / 1920 * 0.54, h = (y1 - y0) / 1080 * 0.304, tv = Math.tan(fov * Math.PI / 360);
    const d = Math.max(h / 2 / tv, w / 2 / (tv * 16 / 9)) * 1.06;
    from = d * 1.08; to = d;
  }
  return { t0, t1, pose: extra.pose || ((t) => typing(t)),
    cam: (t, k) => {
      const d = lerp(from, to, smooth(k));
      return { pos: MON.clone().add(V(dx, dy, d)), target: MON.clone().add(V(dx, dy, 0)), fov };
    }, bloom: 0.08, exposure: 0.95, ...extra.shot };
}

// A readable phone close-up over a dimmed POV of the desk.
export function phone(t0, t1, ph, extra = {}) {
  return { t0, t1, pose: (t) => seated({ pov: true, hideArms: true, lean: 0.25 }),
    cam: (t) => ({ pos: EYE_POS.clone().add(V(0, -0.01, 0.02)), target: (extra.look || V(-0.2, DESK.y, -0.2)), fov: 40, focus: 0.3, aperture: 0.006, maxblur: 0.014 }),
    insert: ph,
    overlay: (t, k, c, w, o) => o.phone(typeof ph === 'function' ? ph(t, t - t0) : ph, { t0: extra.enter === false ? -1 : t0, t1: extra.exit === false ? null : t1, focus: extra.focus ?? 330, zoom: extra.zoom ?? 2.15, x: extra.x ?? 960, dim: extra.dim ?? 0.6 }, t),
    bloom: 0.3, ...extra.shot };
}

// Wide room views.
export const WIDE_BACKLEFT = (t0, t1, extra = {}) => ({ t0, t1, pose: extra.pose || ((t) => resting()),
  cam: move([[0, [-1.15, 1.42, 1.55], [-0.05, 0.95, -0.25], 38], [1, [-1.08, 1.41, 1.45], [-0.05, 0.95, -0.25], 37]]), ...extra.shot });
export const SIDE = (t0, t1, extra = {}) => ({ t0, t1, pose: extra.pose || ((t) => resting()),
  cam: move([[0, [1.25, 1.12, 0.55], [0.05, 0.95, 0.10], 34], [1, [1.18, 1.11, 0.50], [0.05, 0.95, 0.10], 33]]), ...extra.shot });
export const FRONT = (t0, t1, extra = {}) => ({ t0, t1, pose: extra.pose || ((t) => resting()),
  cam: move([[0, [-0.25, 1.05, -0.48], [0.12, 1.08, 0.38], 36], [1, [-0.22, 1.06, -0.45], [0.12, 1.1, 0.38], 34]]), ...extra.shot });

// ---------------------------------------------------------------- contact
// Two close-up hands gripping the board's side edges (POV holds). Board mm
// frame: x right, y up, z out of the screens; edges at x = ±30, y = ±25.
export function boardGrip(c, o = {}) {
  const up = (x, y, z) => devPt(c, x, y, z);
  const yAxis = up(0, 1, 0).sub(up(0, 0, 0)).normalize();
  const xAxis = up(1, 0, 0).sub(up(0, 0, 0)).normalize();
  const zAxis = up(0, 0, 1).sub(up(0, 0, 0)).normalize();
  const hands = [];
  for (const [side, sx] of [['L', -1], ['R', 1]]) {
    const tip = up(sx * (31.5 + (o.spread ?? 0)), o.y ?? 6, o.z ?? -3);
    const dir = yAxis.clone().multiplyScalar(0.85).add(zAxis.clone().multiplyScalar(0.25)).normalize();
    const back = xAxis.clone().multiplyScalar(sx).add(zAxis.clone().multiplyScalar(-0.35)).normalize();
    hands.push(handAt(o.kind || 'hand_grip', side, tip, dir, back));
  }
  return hands;
}
// His (body) hands reaching the held board's edges, for third-person shots.
export function reachBoard(c, opts = {}) {
  return { handL: devPt(c, -33, -4, -6), handR: devPt(c, 33, -4, -6), ...opts };
}
// A held flat object (card) facing the camera: +Y face toward `toward`.
export function facingCard(pos, toward, up = V(0, 1, 0)) {
  const q = faceQuat(toward.clone().sub(pos).normalize(), up);
  return q.multiply(Q(Math.PI / 2, 0, 0));
}

// A card or plug held between finger and thumb, the hand entering from the
// side (side = +1 right, -1 left), parallel to the board so it never blocks it.
export function sideHand(c, kind, tip, side = 1) {
  const n = devNormal(c);
  const xAxis = devPt(c, 1, 0, 0).sub(devPt(c, 0, 0, 0)).normalize();
  // Fingers angle down toward what they hold, so the wrist stays above the desk.
  const dir = xAxis.clone().multiplyScalar(-side).add(n.clone().multiplyScalar(-0.2)).add(V(0, -0.4, 0)).normalize();
  return handAt(kind, side > 0 ? 'R' : 'L', tip.clone().add(n.clone().multiplyScalar(0.004)), dir, V(0, 1, 0).add(n.clone().multiplyScalar(0.25)).normalize());
}
