// SPDX-License-Identifier: Apache-2.0
// Renderer, post-processing and the lighting presets that carry the film's
// colour script (rain night → 3 a.m. dark → storm → dawn → clear morning).
import * as THREE from 'three';
import { EffectComposer } from 'three/addons/postprocessing/EffectComposer.js';
import { RenderPass } from 'three/addons/postprocessing/RenderPass.js';
import { UnrealBloomPass } from 'three/addons/postprocessing/UnrealBloomPass.js';
import { BokehPass } from 'three/addons/postprocessing/BokehPass.js';
import { ShaderPass } from 'three/addons/postprocessing/ShaderPass.js';
import { OutputPass } from 'three/addons/postprocessing/OutputPass.js';
import { RoomEnvironment } from 'three/addons/environments/RoomEnvironment.js';
import { lerp } from './util.js';

const GradeShader = {
  uniforms: {
    tDiffuse: { value: null }, time: { value: 0 }, vignette: { value: 0.28 }, grain: { value: 0.035 },
    lift: { value: new THREE.Vector3(0, 0, 0) }, gain: { value: new THREE.Vector3(1, 1, 1) }, sat: { value: 1 },
    black: { value: 0 }, white: { value: 0 }, bars: { value: 0 },
  },
  vertexShader: 'varying vec2 vUv; void main(){ vUv=uv; gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.0); }',
  fragmentShader: `uniform sampler2D tDiffuse; uniform float time, vignette, grain, sat, black, white, bars; uniform vec3 lift, gain; varying vec2 vUv;
    float h(vec2 p){ return fract(sin(dot(p, vec2(12.9898,78.233)) + time * 17.0) * 43758.5453); }
    void main(){
      vec3 c = texture2D(tDiffuse, vUv).rgb;
      c = c * gain + lift * (1.0 - c);
      float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
      c = mix(vec3(l), c, sat);
      vec2 d = vUv - 0.5; d.x *= 1.25;
      c *= 1.0 - vignette * smoothstep(0.25, 0.85, length(d));
      c += (h(vUv * 1000.0) - 0.5) * grain;
      c = mix(c, vec3(0.0), black); c = mix(c, vec3(1.0), white);
      float bar = step(vUv.y, bars * 0.12) + step(1.0 - bars * 0.12, vUv.y);
      c = mix(c, vec3(0.0), clamp(bar, 0.0, 1.0));
      gl_FragColor = vec4(c, 1.0);
    }`,
};

export function createEngine(canvas) {
  const renderer = new THREE.WebGLRenderer({ canvas, antialias: false, preserveDrawingBuffer: true, powerPreference: 'high-performance' });
  renderer.setPixelRatio(1);
  renderer.setSize(1920, 1080, false);
  renderer.shadowMap.enabled = true;
  renderer.shadowMap.type = THREE.PCFSoftShadowMap;
  renderer.toneMapping = THREE.AgXToneMapping;
  renderer.toneMappingExposure = 1.0;
  renderer.outputColorSpace = THREE.SRGBColorSpace;
  const scene = new THREE.Scene();
  const pmrem = new THREE.PMREMGenerator(renderer);
  scene.environment = pmrem.fromScene(new RoomEnvironment(), 0.04).texture;
  scene.environmentIntensity = 0.2;
  scene.background = new THREE.Color(0x05070b);
  const camera = new THREE.PerspectiveCamera(35, 16 / 9, 0.004, 60);
  const rt = new THREE.WebGLRenderTarget(1920, 1080, { type: THREE.HalfFloatType, samples: 4 });
  const composer = new EffectComposer(renderer, rt);
  const renderPass = new RenderPass(scene, camera);
  composer.addPass(renderPass);
  const bokeh = new BokehPass(scene, camera, { focus: 0.5, aperture: 0.004, maxblur: 0.008 });
  bokeh.enabled = false;
  composer.addPass(bokeh);
  const bloom = new UnrealBloomPass(new THREE.Vector2(960, 540), 0.5, 0.5, 0.86);
  composer.addPass(bloom);
  composer.addPass(new OutputPass());
  const grade = new ShaderPass(GradeShader);
  composer.addPass(grade);
  return { renderer, scene, camera, composer, renderPass, bokeh, bloom, grade };
}

// Lighting presets. Intensities are physical-ish (three r180 units).
export const LOOKS = {
  night: { lamp: 7.5, fill: 0.35, mon: 3.2, sun: 0.35, sunCol: 0x8fa8ff, sunPos: [0.8, 2.8, -3], hemi: 0.10, hemiSky: 0x5a6c96, hemiGround: 0x1a140f,
    skyTop: 0x080f1e, skyBot: 0x1d2a44, cityN: 1, cityD: 0, rain: 1, env: 0.10, exposure: 1.0,
    lift: [0.012, 0.016, 0.03], gain: [1.0, 0.98, 1.02], sat: 0.95 },
  late: { lamp: 0, fill: 0, mon: 1.4, sun: 0.22, sunCol: 0x7d93d8, sunPos: [0.8, 2.8, -3], hemi: 0.05, hemiSky: 0x40507a, hemiGround: 0x0c0a08,
    skyTop: 0x050a16, skyBot: 0x141d33, cityN: 0.7, cityD: 0, rain: 1, env: 0.05, exposure: 1.15,
    lift: [0.01, 0.014, 0.03], gain: [0.96, 0.98, 1.04], sat: 0.9 },
  overcast: { lamp: 0, fill: 0, mon: 1.6, sun: 1.6, sunCol: 0xdfe6ef, sunPos: [0.2, 3.2, -2.6], hemi: 0.75, hemiSky: 0xc9d4e2, hemiGround: 0x6d5a48,
    skyTop: 0x9eacbe, skyBot: 0xd6dce3, cityN: 0, cityD: 1, rain: 0.15, env: 0.4, exposure: 1.0,
    lift: [0.01, 0.01, 0.015], gain: [0.98, 1.0, 1.02], sat: 0.85 },
  evening: { lamp: 6.5, fill: 0.3, mon: 2.6, sun: 0.6, sunCol: 0xffb07a, sunPos: [1.6, 1.6, -3], hemi: 0.2, hemiSky: 0x7a6e9a, hemiGround: 0x2a1c12,
    skyTop: 0x2b2d55, skyBot: 0xc98a70, cityN: 0.5, cityD: 0.4, rain: 0, env: 0.15, exposure: 1.0,
    lift: [0.015, 0.01, 0.02], gain: [1.02, 0.99, 0.98], sat: 1.0 },
  dark: { lamp: 0, fill: 0, mon: 0, sun: 0.06, sunCol: 0x6f84c8, sunPos: [0.8, 2.8, -3], hemi: 0.012, hemiSky: 0x30405e, hemiGround: 0x050403,
    skyTop: 0x03060c, skyBot: 0x0b1222, cityN: 0.12, cityD: 0, rain: 1.15, env: 0.02, exposure: 1.25,
    lift: [0.004, 0.006, 0.016], gain: [0.95, 1.0, 1.06], sat: 0.85 },
  dawn: { lamp: 0, fill: 0, mon: 0.6, sun: 2.4, sunCol: 0xffa56a, sunPos: [2.2, 1.35, -3.2], hemi: 0.35, hemiSky: 0xa898c8, hemiGround: 0x4a2f22,
    skyTop: 0x5f74ad, skyBot: 0xffb48a, cityN: 0.25, cityD: 0.6, rain: 0, env: 0.2, exposure: 1.0,
    lift: [0.02, 0.012, 0.02], gain: [1.04, 0.99, 0.95], sat: 1.04 },
  morning: { lamp: 0, fill: 0, mon: 1.6, sun: 4.2, sunCol: 0xfff0d8, sunPos: [1.7, 2.6, -3.2], hemi: 0.85, hemiSky: 0xbcd6f2, hemiGround: 0x8a6a4a,
    skyTop: 0x7fb0e6, skyBot: 0xe4eef6, cityN: 0, cityD: 1, rain: 0, env: 0.45, exposure: 1.0,
    lift: [0.012, 0.012, 0.01], gain: [1.02, 1.0, 0.97], sat: 1.06 },
};

const _c1 = new THREE.Color(), _c2 = new THREE.Color();
function mixCol(a, b, k) { return _c1.set(a).lerp(_c2.set(b), k).clone(); }

export function blendLook(a, b, k) {
  const A = LOOKS[a], B = LOOKS[b];
  const out = {};
  for (const key of Object.keys(A)) {
    const x = A[key], y = B[key];
    if (Array.isArray(x)) out[key] = x.map((v, i) => lerp(v, y[i], k));
    else if (/Col|sky|hemiSky|hemiGround/.test(key) && typeof x === 'number' && x > 255) out[key] = mixCol(x, y, k);
    else out[key] = lerp(x, y, k);
  }
  return out;
}

export function applyLook(E, S, L, extra = {}) {
  const col = (v) => (v instanceof THREE.Color ? v : new THREE.Color(v));
  S.lamp.spot.intensity = L.lamp * (extra.lampMul ?? 1);
  S.lamp.fill.intensity = L.fill * (extra.lampMul ?? 1);
  S.lamp.bulb.material.color.setRGB(1, 0.85, 0.65).multiplyScalar(Math.min(1, L.lamp / 6) * (extra.lampMul ?? 1));
  S.monLight.intensity = L.mon * (extra.monMul ?? 1);
  // Screens stay just under the bloom threshold so white UI reads instead of glowing.
  S.monitor.mat.color.setScalar(Math.min(0.8, 0.25 + L.mon / 3.2) * (extra.monMul ?? 1));
  S.sun.intensity = L.sun; S.sun.color.copy(col(L.sunCol)); S.sun.position.set(...L.sunPos);
  S.hemi.intensity = L.hemi; S.hemi.color.copy(col(L.hemiSky)); S.hemi.groundColor.copy(col(L.hemiGround));
  S.skyU.top.value.copy(col(L.skyTop)); S.skyU.bottom.value.copy(col(L.skyBot));
  S.cityNight.opacity = L.cityN; S.cityDay.opacity = L.cityD;
  S.rainU.amount.value = L.rain;
  E.scene.environmentIntensity = L.env;
  E.renderer.toneMappingExposure = L.exposure * (extra.exposureMul ?? 1);
  E.grade.uniforms.lift.value.set(...L.lift);
  E.grade.uniforms.gain.value.set(...L.gain);
  E.grade.uniforms.sat.value = L.sat;
}
