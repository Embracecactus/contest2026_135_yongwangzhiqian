// SPDX-License-Identifier: Apache-2.0
// Global world state over the whole film: light, weather, props, where
// Shaniu stands and what her eyes show. Shots (scenes*.js) frame it and may
// override props they handle (a held card, a lifted board).
//
// Her eyes follow the product firmware, not invention:
//   wake ack / listening  → activity 1 → cached look_left frame
//   capture quiescent      → activity 2 → blink_half ("thinking")
//   reply audio            → speaking frame, or the reply expression (F21)
//   lifted / set down      → 800 ms look_left / blink_half (motion companion)
//   offline wake           → consumed silently: no ack, no eye change
// (bk7258_agent_product.c, bk7258_display_companion.inc,
//  bk7258_agent_companion.inc, bk7258_agent_trigger.c)
import * as THREE from 'three';
import { SPOT, DESK } from './set.js';
import { clamp, easeInOut, lerp, pulse, range, smooth, stateAt } from './util.js';
import { V, Q, eyeIdle, eyeExpr, eyeOff, eyeFade, eyeFocus, eyeApproval, eyeCheck } from './rig.js';

// Lightning: short double flashes.
const FLASHES = [342.2, 348.1, 352.0, 357.0, 366.2, 376.4, 381.6, 388.5];
function flash(t) {
  let f = 0;
  for (const a of FLASHES) { const d = t - a; if (d > 0 && d < 0.5) f = Math.max(f, Math.exp(-d * 14) + 0.6 * Math.exp(-Math.abs(d - 0.18) * 30)); }
  return f;
}

function look(t) {
  const L = (a, b = null, k = 0) => ({ lookA: a, lookB: b, lookK: k });
  if (t < 157.8) return L('night');
  if (t < 175.4) return L('late');
  if (t < 231) return L('overcast');
  if (t < 240) return L('night');
  if (t < 244) return L('night', 'dawn', smooth(range(t, 240.6, 244)));
  if (t < 248) return L('dawn', 'morning', smooth(range(t, 244, 247)));
  if (t < 272.6) return L('evening');
  if (t < 306.4) return L('night');
  if (t < 334) return L('late');
  if (t < 342) { // weeks pass: daylight sweeps over the dark desk again and again
    const p = (t - 334) / 8 * 4; const c = 0.5 - 0.5 * Math.cos(p * Math.PI * 2);
    return L('late', 'overcast', c);
  }
  if (t < 344.6) return L('night');
  if (t < 404.6) return L('dark');
  if (t < 409) return L('dark', 'dawn', smooth(range(t, 404.6, 408.6)));
  if (t < 423) return L('dawn', 'morning', smooth(range(t, 409, 423)));
  return L('morning');
}

export function worldState(t) {
  const w = { ...look(t), flash: flash(t) };
  // Lamp: on in the rainy nights; flicker before the blackout; off when he sleeps.
  w.lampMul = 1;
  if (t >= 343.4 && t < 344.6) w.lampMul = [1, 0.15, 0.9, 0.05, 0.7, 0][Math.floor((t - 343.4) / 0.2)] ?? 0;
  if (t >= 240.6 && t < 244) w.lampMul = 0;
  w.monMul = 1;
  if (t >= 304 && t < 334) w.monMul = 0;            // he switched the screen off
  if (t >= 334 && t < 342) w.monMul = 0.25;
  if (t >= 344.6 && t < 409.2) w.monMul = 0;        // blackout, then the PC boots
  // Router LEDs die with the power and come back one by one.
  w.routerLeds = t >= 344.6 && t < 405 ? 0 : (t >= 405 && t < 407 ? range(t, 405, 407) : 1);
  w.doorGapOn = !(t >= 344.6 && t < 404.6);
  // Parcel: on the floor, then on the desk, opened; gone once she is built.
  if (t < 21.4) { w.parcelPos = V(-0.66, 0, 0.36); w.parcelRot = 0.35; w.parcelOpen = 0; }
  else if (t < 52) { w.parcelPos = V(-0.30, DESK.y, -0.30); w.parcelRot = 0.12; w.parcelOpen = smooth(range(t, 24.0, 25.2)); }
  else w.parcelVisible = false;
  // Gift card: in the open box, in his hand (shot), then on the desk corner.
  w.cardVisible = t >= 24.4;
  if (t < 29.6) { w.cardPos = V(-0.24, DESK.y + 0.1225, -0.33); w.cardQuat = new THREE.Quaternion().setFromEuler(new THREE.Euler(0, 0.4, 0)); }
  // Door card: in the drawer while it is open, then near her.
  if (t >= 40.4 && t < 43.6) w.doorCard = { pos: V(-0.36, DESK.y - 0.08 + 0.046, DESK.z1 + 0.13), rot: [0, 0.3, 0] };
  else if (t >= 146.0 && t < 387.0) w.doorCard = { pos: V(-0.24, DESK.y + 0.0006, -0.03), rot: [0, -0.35, 0] };
  else if (t >= 387.0) w.doorCard = { pos: V(0.31, DESK.y + 0.0006, 0.0), rot: [0, 0.25, 0] };
  w.drawerOpen = t >= 40.6 && t < 43.3 ? smooth(range(t, 40.6, 41.4)) * (1 - smooth(range(t, 43.0, 43.3))) : 0;
  // Mugs: her mug holds his two-sugar coffee from scene 6 until he roots cuttings in it.
  w.myMugCoffee = true;
  w.fishMugCoffee = t >= 127.8 && t < 449.4;
  w.cuttings = t >= 450.6;
  // Bottle water level drops each time he waters the plant.
  w.bottleWater = t < 221.6 ? 1 : (t < 391.8 ? 0.72 : 0.45);
  // Plant: dry, watered (she told him), dries while she is off, watered again
  // (nobody told him), then thrives and grows a new leaf.
  let h = 0.18;
  if (t >= 221.2) h = lerp(0.18, 0.75, smooth(range(t, 221.2, 226)));
  if (t >= 334) h = lerp(0.75, 0.28, smooth(range(t, 334, 342)));
  if (t >= 391.6) h = lerp(0.28, 0.42, smooth(range(t, 391.6, 400)));
  if (t >= 449) h = lerp(0.42, 0.85, smooth(range(t, 449, 473)));
  w.plantHealth = h;
  w.plantGrow = smooth(range(t, 481, 515));
  w.plantPos = (t >= 204.4 && t < 231) ? SPOT.plantDesk.clone().add(V(0.0, 0, 0.02)) : SPOT.plantSill.clone();
  // Shaniu: in the box (bare board) → held (his decision) → assembly → her side
  // of the desk (晴's old place) → lifted in the blackout → beside his own work.
  w.assembled = t >= 46.4;
  if (t < 21.4) w.shaniuHidden = true;
  else if (t < 40.4) { w.shaniuPos = V(-0.30, DESK.y + 0.1235, -0.31); w.shaniuFlat = true; w.shaniuYaw = 0.12; }
  else if (t < 74.6) { w.shaniuPos = V(0.02, DESK.y, -0.02); w.shaniuFlat = true; }
  else if (t < 387.0) { w.shaniuPos = SPOT.shaniuLeft.clone(); w.shaniuYaw = 0.42; }
  else { w.shaniuPos = SPOT.shaniuRight.clone(); w.shaniuYaw = -0.38; }
  // Power bank and USB-C: plugged in by torchlight, moved with her, unplugged at dawn.
  if (t >= 347.4 && t < 387.0) { w.powerBank = V(-0.27, DESK.y + 0.008, -0.33); w.powerBankYaw = 0.5; }
  else if (t >= 387.0 && t < 412.0) { w.powerBank = V(0.58, DESK.y + 0.008, -0.33); w.powerBankYaw = -0.2; }
  else w.powerBank = null;
  if (t >= 348.6 && t < 412.0) w.usbPlug = { k: 1 };
  // Making space for her beside his keyboard: the mouse moves aside.
  if (t >= 384.6) w.mousePos = V(0.40, DESK.y + 0.012, -0.11).lerp(V(0.30, DESK.y + 0.012, 0.03), smooth(range(t, 384.6, 385.3)));
  // The pothos comes into her light on the desk for the night, back on the sill by morning.
  if (t >= 390.4 && t < 404.6) w.plantPos = V(0.62, DESK.y, 0.0);   // cut from the sill shot
  // Dust settles on her screens while she is off; he wipes it with a finger.
  w.dust = t >= 334 && t < 374 ? smooth(range(t, 334, 341)) * (1 - smooth(range(t, 372.4, 373.8))) : 0;
  w.bloomMul = 1;
  return w;
}

// ---------------------------------------------------------------- eyes
// Ordered [start, fn(t, lt)] keys; fn returns the screen descriptor.
const motion = (expr, o = {}) => eyeExpr(expr, o);          // 800 ms companion frame
export const EYES = [
  [0, eyeOff()],
  [49.8, (t, lt) => ({ mode: 'qr', brightness: smooth(lt / 0.6), glow: 0.05 })],
  [72.2, (t, lt) => ({ mode: 'eye', expr: lt < 0.4 ? 'blink_closed' : (lt < 0.8 ? 'blink_half' : 'neutral'), brightness: smooth(lt / 0.4), glow: 0.07 })],
  [73.6, eyeIdle()],
  // Stood up on her side of the desk: set down → half blink.
  [76.5, motion('blink_half')], [77.3, eyeIdle()],
  // First "我在": wake ack + listening → thinking → speaking → follow-up.
  [80.6, eyeExpr('look_left')], [84.6, eyeExpr('blink_half')], [85.9, eyeExpr('speaking')], [88.9, eyeIdle()],
  [91.1, eyeExpr('blink_half')], [103.4, eyeIdle()],
  // Memory scene: "小鱼" answered with a shy reply expression (F21).
  [118.1, eyeExpr('look_left')], [122.0, eyeExpr('blink_half')], [122.8, eyeExpr('shy')], [124.5, eyeIdle()],
  [132.6, eyeExpr('blink_half')], [133.3, eyeExpr('speaking')], [135.0, eyeIdle()],
  // 02:13: the whisper is too quiet; the second wakes her; he says nothing.
  [157.8, eyeIdle({ glow: 0.09 })], [167.6, eyeExpr('look_left', { glow: 0.09 })], [171.2, eyeIdle({ glow: 0.09 })],
  // Lighthouse lecture: half duplex.
  [175.4, eyeIdle()], [178.1, eyeExpr('look_left')], [181.9, eyeExpr('blink_half')], [182.6, eyeExpr('speaking')], [196.0, eyeIdle()],
  // Plant: "看看这个" — capture while thinking, then the answer.
  [209.5, eyeExpr('look_left')], [213.3, eyeExpr('blink_half')], [214.8, eyeExpr('speaking')], [219.2, eyeIdle()],
  // Lifted to wipe the desk, set down.
  [226.4, motion('look_left')], [227.2, eyeIdle()], [229.7, motion('blink_half')], [230.5, eyeIdle()],
  // Render done in the morning: check + 40 ms pulse.
  [246.2, eyeCheck()], [247.6, eyeIdle()],
  // OTA: reboot into the new version, then "我在" again.
  [263.8, eyeOff()], [264.4, eyeFade('neutral', 0.6)], [265.0, eyeIdle()], [269.2, eyeExpr('look_left')], [272.4, eyeIdle()],
  // The photo folder: amber "!" and its ring run out (edited time).
  [282.3, eyeApproval(15.4)], [297.7, eyeIdle()],
  // 3 a.m.: a sad reply expression (F21).
  [309.3, eyeExpr('look_left', { glow: 0.1 })], [313.5, eyeExpr('blink_half', { glow: 0.1 })], [314.4, eyeExpr('sad', { glow: 0.1 })], [318.4, eyeIdle({ glow: 0.1 })],
  [328.4, (t, lt) => ({ mode: 'eye', expr: lt < 0.3 ? 'blink_half' : 'blink_closed', brightness: 1 - smooth((lt - 0.2) / 0.8), glow: 0.1 * (1 - smooth(lt / 1)) })],
  [329.4, eyeOff()],
  // Blackout: K2 starts her; offline, she cannot answer — no ack, no eye change.
  [351.0, eyeFade('neutral', 1.0, { glow: 0.13 })], [352.0, eyeIdle({ glow: 0.13 })],
  // Lifted (look_left 800 ms); set down beside his work (blink_half 800 ms).
  [370.9, motion('look_left', { glow: 0.13 })], [371.7, eyeIdle({ glow: 0.13 })],
  [387.1, motion('blink_half', { glow: 0.13 })], [387.9, eyeIdle({ glow: 0.13 })],
  // Morning: Wi-Fi changed (offline until re-provisioned), then the health check.
  [404.6, eyeIdle()], [421.9, eyeExpr('look_left')], [427.5, eyeExpr('blink_half')], [428.4, eyeExpr('speaking')], [433.2, eyeIdle()],
  // New eyes: 30 s trial (neutral) → wake → local command "换成开心表情" → default.
  [461.8, eyeIdle({ pack: 'warm' })], [464.9, eyeExpr('look_left', { pack: 'warm' })], [468.3, eyeExpr('happy', { pack: 'warm' })], [471.2, eyeIdle({ pack: 'warm' })],
  // Focus by card (25 min, edited), render done, two approvals.
  [477.3, eyeFocus(1500, 1500 / 4.3)], [481.6, eyeCheck()], [483.0, eyeIdle({ pack: 'warm' })],
  [487.0, eyeCheck()], [488.2, eyeIdle({ pack: 'warm' })],
  [489.5, eyeApproval(120)], [492.8, eyeIdle({ pack: 'warm' })],
  [499.9, eyeApproval(120)], [503.2, eyeIdle({ pack: 'warm' })],
];

export function eyeAt(t, w) {
  let k = EYES[0], idx = 0;
  for (let i = 0; i < EYES.length; i++) { if (EYES[i][0] <= t) { k = EYES[i]; idx = i; } else break; }
  const d = { ...k[1](t, t - k[0]) };
  if (d.mode === 'approval' && d.segments === undefined) d.segments = 32;
  d.dust = w ? w.dust : 0;
  if (!d.pack && t >= 461.8) d.pack = 'warm';
  return d;
}
