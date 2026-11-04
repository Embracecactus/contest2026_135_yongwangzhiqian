// SPDX-License-Identifier: Apache-2.0
// Shared staging helpers: poses for 周屿, camera moves, eye-state builders.
import * as THREE from 'three';
import { SPOT, DESK } from './set.js';
import { clamp, easeInOut, lerp, noise1, range, smooth, track } from './util.js';

export const V = (x, y, z) => new THREE.Vector3(x, y, z);
export const Q = (x, y, z, order = 'XYZ') => new THREE.Quaternion().setFromEuler(new THREE.Euler(x, y, z, order));
export const HIPS = V(0.10, 0.60, 0.47);           // seated hips
export const EYE_POS = V(0.10, 1.215, 0.41);       // his eye point when seated upright

// Device eye centre (mean of the two screens) for a given desk spot.
export function shaniuEyes(spot) { return V(spot.x, spot.y + 0.052, spot.z + 0.006); }

// ---------------------------------------------------------------- poses
export function seated(opts = {}) {
  const lean = opts.lean ?? 0.16;
  return {
    pos: opts.pos || HIPS.clone(), yaw: opts.yaw ?? Math.PI, lean, twist: opts.twist ?? 0,
    headTurn: opts.headTurn ?? 0, headTilt: opts.headTilt ?? 0.08, legs: 'sit',
    armL: { target: opts.handL || V(0.0, DESK.y + 0.025, -0.08), pole: opts.poleL },
    armR: { target: opts.handR || V(0.22, DESK.y + 0.025, -0.08), pole: opts.poleR },
    povHide: !!opts.pov, hideArms: !!opts.hideArms, hands: opts.hands || [], visible: opts.visible !== false, lens: opts.lens || 0,
    face: opts.face, talk: opts.talk,
  };
}
export const typing = (t, o = {}) => seated({ ...o,
  handL: V(0.03 + 0.006 * Math.sin(t * 9.1), DESK.y + 0.03 + 0.004 * Math.abs(Math.sin(t * 7.3)), -0.12),
  handR: V(0.19 + 0.006 * Math.sin(t * 8.3 + 1), DESK.y + 0.03 + 0.004 * Math.abs(Math.sin(t * 6.1 + 2)), -0.12) });
export const resting = (o = {}) => seated({ handL: V(-0.02, DESK.y + 0.02, -0.02), handR: V(0.24, DESK.y + 0.02, -0.02), ...o });
// Camera over his left shoulder toward a target (Shaniu on her side of the desk).
export function otsLeft(target, opts = {}) {
  const back = opts.back ?? 0.42, up = opts.up ?? 0.12, side = opts.side ?? -0.16;
  return V(0.10 + side, 1.25 + up, 0.42 + back).lerp(target, opts.push ?? 0);
}
export const sleeping = (o = {}) => seated({ lean: 0.95, headTilt: 0.55, headTurn: 0.4, handL: V(-0.05, DESK.y + 0.03, -0.1), handR: V(0.2, DESK.y + 0.03, -0.12), ...o });

// ---------------------------------------------------------------- cameras
export function cam(pos, target, fov = 35, extra = {}) { return () => ({ pos: pos.clone ? pos : V(...pos), target: target.clone ? target : V(...target), fov, ...extra }); }
// Keyframed move: keys = [[k(0..1), [px,py,pz], [tx,ty,tz], fov?], ...] over the shot.
export function move(keys, extra = {}) {
  const pk = keys.map((k) => [k[0], k[1]]), tk = keys.map((k) => [k[0], k[2]]), fk = keys.map((k) => [k[0], k[3] ?? 35]);
  return (t, k) => {
    const out = { pos: V(...track(pk, k)), target: V(...track(tk, k)), fov: track(fk, k), ...extra };
    if (extra.shake) { const a = extra.shake; out.pos.x += noise1(t * 0.7, 1) * a; out.pos.y += noise1(t * 0.6, 2) * a * 0.7; out.target.x += noise1(t * 0.5, 3) * a * 0.6; }
    if (typeof extra.focus === 'function') out.focus = extra.focus(t, k);
    return out;
  };
}

// ---------------------------------------------------------------- eyes
// Eye timeline helpers: each returns a function (t, lt) → descriptor.
export const eyeIdle = (o = {}) => () => ({ mode: 'eye', expr: 'idle', brightness: 1, glow: 0.07, ...o });
export const eyeExpr = (expr, o = {}) => () => ({ mode: 'eye', expr, brightness: 1, glow: 0.07, ...o });
export const eyeOff = (o = {}) => () => ({ mode: 'eye', expr: 'blink_closed', brightness: 0, glow: 0, ...o });
export const eyeFade = (expr, dur, o = {}) => (t, lt) => ({ mode: 'eye', expr, brightness: smooth(lt / dur), glow: (o.glow ?? 0.07) * smooth(lt / dur), ...o, });
export const eyeFocus = (total, lt0Rate = 1, o = {}) => (t, lt) => {
  const shown = Math.max(0, total - lt * lt0Rate);
  return { mode: 'focus', seconds: Math.ceil(shown), segments: Math.min(32, Math.floor((total - shown) / total * 32)), brightness: 1, glow: 0.06, ...o };
};
export const eyeApproval = (dur, o = {}) => (t, lt) => ({ mode: 'approval', segments: Math.max(0, Math.ceil(32 * (1 - lt / dur))), brightness: 1, glow: 0.1, ...o });
export const eyeCheck = (o = {}) => () => ({ mode: 'check', segments: 32, brightness: 1, glow: 0.07, ...o });

// ---------------------------------------------------------------- device-relative staging
const MM = 0.001;
// A point on the board in world space (board millimetres from the board centre).
export function devPt(ctx, x, y, z = 0) { return ctx.device.board.localToWorld(V(x * MM, y * MM, z * MM)); }
export function devNormal(ctx) { const q = new THREE.Quaternion(); ctx.device.board.getWorldQuaternion(q); return V(0, 0, 1).applyQuaternion(q); }
export function devUp(ctx) { const q = new THREE.Quaternion(); ctx.device.board.getWorldQuaternion(q); return V(0, 1, 0).applyQuaternion(q); }
// Orient a close-up hand: fingers along dir, back of the hand toward up.
export function handAt(kind, side, tip, dir, up, tipLocal = V(-0.027, 0.004, 0.134)) {
  const z = dir.clone().normalize();
  const x = new THREE.Vector3().crossVectors(up, z).normalize();
  const y = new THREE.Vector3().crossVectors(z, x).normalize();
  const quat = new THREE.Quaternion().setFromRotationMatrix(new THREE.Matrix4().makeBasis(x, y, z));
  const off = tipLocal.clone(); if (side === 'L') off.x *= -1;
  return { kind, side, pos: tip.clone().sub(off.applyQuaternion(quat)), quat };
}
// Orientation that makes an object's +Z face `dir` with +Y toward `up`.
export function faceQuat(dir, up = V(0, 1, 0)) {
  const z = dir.clone().normalize();
  const x = new THREE.Vector3().crossVectors(up, z).normalize();
  const y = new THREE.Vector3().crossVectors(z, x).normalize();
  return new THREE.Quaternion().setFromRotationMatrix(new THREE.Matrix4().makeBasis(x, y, z));
}
