// SPDX-License-Identifier: Apache-2.0
// Film entry: load assets, build the world, and draw any frame from time t.
import * as THREE from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { createEngine } from './engine.js';
import { buildSet } from './set.js';
import { Device } from './device.js';
import { Character } from './character.js';
import { Film } from './film.js';
import { Overlay } from './overlay.js';
import { storyFromFilm } from './timewarp.js';

const canvas = document.getElementById('gl');
const E = createEngine(canvas);

function loadImage(src) { return new Promise((res, rej) => { const i = new Image(); i.onload = () => res(i); i.onerror = rej; i.src = src; }); }
function loadGltf(src) { return new Promise((res, rej) => new GLTFLoader().load(src, res, undefined, rej)); }

async function build() {
  await Promise.all([
    document.fonts.load('600 40px "Noto Sans SC"'), document.fonts.load('400 40px "Noto Sans SC"'),
    document.fonts.load('40px "LXGW WenKai"'),
  ]);
  const [eyeSheet, boardImg, body, hands] = await Promise.all([
    loadImage('assets/eyes/shaniu-cyan-v2.png'), loadImage('assets/textures/aidk_top.jpg'),
    loadGltf('assets/models/zhouyu.glb'), loadGltf('assets/models/hands.glb'),
  ]);
  const boardTex = new THREE.Texture(boardImg); boardTex.needsUpdate = true;
  const S = buildSet(E.scene);
  const device = new Device({ eyeSheet, boardTex });
  E.scene.add(device.group);
  const hero = new Character(body, hands);
  hero.addTo(E.scene);
  hero.attachLensReflections(device.canvases.L, device.canvases.R);
  const film = new Film(E, S, device, hero, new Overlay(document.getElementById('ui')));
  await film.ready();
  return film;
}

window.__hf = window.__hf || {};
window.__hf.buildReady = window.__hf.buildReady || {};
const ready = build();
window.__hf.buildReady.world = ready;
let film = null;
let pending = null;
// Optional offset so a range composition (index-range.html) can render part of the film.
const OFFSET = Number((document.getElementById('root').dataset.filmOffset) || 0);
const draw = (t) => { t += OFFSET; if (film) { film.render(storyFromFilm(t), t); return; } pending = t; };
window.addEventListener('hf-seek', (e) => draw(e.detail.time));
ready.then((f) => { film = f; window.__film = f; const tf = pending ?? 0; film.render(storyFromFilm(tf), tf); });
