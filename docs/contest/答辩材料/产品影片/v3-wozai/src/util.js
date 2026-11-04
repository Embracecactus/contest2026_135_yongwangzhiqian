// SPDX-License-Identifier: Apache-2.0
// Pure helpers: every visible value is a function of film time t.

export const clamp = (x, a = 0, b = 1) => Math.min(b, Math.max(a, x));
export const lerp = (a, b, k) => a + (b - a) * k;
export const smooth = (k) => { k = clamp(k); return k * k * (3 - 2 * k); };
export const smoother = (k) => { k = clamp(k); return k * k * k * (k * (k * 6 - 15) + 10); };
export const easeOut = (k) => 1 - Math.pow(1 - clamp(k), 3);
export const easeIn = (k) => Math.pow(clamp(k), 3);
export const easeInOut = (k) => { k = clamp(k); return k < 0.5 ? 4 * k * k * k : 1 - Math.pow(-2 * k + 2, 3) / 2; };
export const range = (t, a, b) => clamp((t - a) / (b - a));
// 0 → 1 → 0 window with soft edges.
export const pulse = (t, a, b, fade = 0.3) => Math.min(smooth(range(t, a, a + fade)), 1 - smooth(range(t, b - fade, b)));

export function mulberry32(seed) {
  let a = seed >>> 0;
  return () => {
    a |= 0; a = (a + 0x6D2B79F5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

export function hash1(n) {
  const x = Math.sin(n * 127.1 + 311.7) * 43758.5453;
  return x - Math.floor(x);
}

// Smooth 1D value noise, deterministic in t.
export function noise1(t, seed = 0) {
  const i = Math.floor(t), f = t - i;
  const a = hash1(i + seed * 101.3), b = hash1(i + 1 + seed * 101.3);
  return lerp(a, b, smooth(f)) * 2 - 1;
}

// Keyframe track: keys = [[t, value, ease?], ...]; values are numbers or arrays.
export function track(keys, t) {
  if (t <= keys[0][0]) return keys[0][1];
  const last = keys[keys.length - 1];
  if (t >= last[0]) return last[1];
  for (let i = 0; i < keys.length - 1; i++) {
    const [t0, v0] = keys[i];
    const [t1, v1, e] = keys[i + 1];
    if (t >= t0 && t < t1) {
      const k = (e || easeInOut)((t - t0) / (t1 - t0));
      if (Array.isArray(v0)) return v0.map((x, j) => lerp(x, v1[j], k));
      return lerp(v0, v1, k);
    }
  }
  return last[1];
}

// Discrete state track: [[t, state], ...] → active state and its local time.
export function stateAt(keys, t) {
  let cur = keys[0], idx = 0;
  for (let i = 0; i < keys.length; i++) { if (keys[i][0] <= t) { cur = keys[i]; idx = i; } else break; }
  const next = keys[idx + 1];
  return { state: cur[1], local: t - cur[0], start: cur[0], end: next ? next[0] : Infinity, args: cur[2] || {} };
}

export function hexColor(h) { return parseInt(h.replace('#', ''), 16); }
