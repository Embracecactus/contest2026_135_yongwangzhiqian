// SPDX-License-Identifier: Apache-2.0
// Story time vs. film time. Shots, eyes, screens and cue "at" values are
// authored in story time; EDIT.warp lists story-time ranges played slower
// (factor > 1) so a moment can breathe. Voices and effects keep their real
// length: the mixer places each cue at filmFromStory(at). Same maths as
// tools/editlib.py.
import { EDIT } from './edit.js';

const W = (EDIT.warp || []).map(([a, b, f]) => [a, b, f]).sort((x, y) => x[0] - y[0]);

export function filmFromStory(ts) {
  let tf = ts;
  for (const [a, b, f] of W) {
    if (ts <= a) break;
    tf += (Math.min(ts, b) - a) * (f - 1);
  }
  return tf;
}

export function storyFromFilm(tf) {
  let shift = 0;
  for (const [a, b, f] of W) {
    const fa = a + shift, fb = fa + (b - a) * f;
    if (tf <= fa) return tf - shift;
    if (tf < fb) return a + (tf - fa) / f;
    shift += (b - a) * (f - 1);
  }
  return tf - shift;
}

export const FILM_DURATION = filmFromStory(EDIT.duration);
