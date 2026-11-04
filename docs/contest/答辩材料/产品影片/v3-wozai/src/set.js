// SPDX-License-Identifier: Apache-2.0
// 周屿's room: a desk under a rainy city window. Metres, Y up; he sits at +Z
// facing the window at -Z.
import * as THREE from 'three';
import { RoundedBoxGeometry } from 'three/addons/geometries/RoundedBoxGeometry.js';
import { RectAreaLightUniformsLib } from 'three/addons/lights/RectAreaLightUniformsLib.js';
import * as T from './textures.js';
import { mulberry32 } from './util.js';

const V3 = (x, y, z) => new THREE.Vector3(x, y, z);
export const DESK = { x0: -0.70, x1: 0.70, z0: -0.62, z1: 0.08, y: 0.75 };
export const SPOT = {
  shaniuLeft: new THREE.Vector3(-0.40, DESK.y, -0.22),
  shaniuRight: new THREE.Vector3(0.47, DESK.y, -0.14),
  plantSill: new THREE.Vector3(0.52, 0.925, -0.57),
  plantDesk: new THREE.Vector3(-0.22, DESK.y, -0.10),
};

function std(color, rough = 0.7, metal = 0, extra = {}) {
  return new THREE.MeshStandardMaterial({ color, roughness: rough, metalness: metal, ...extra });
}
function shadowed(o, cast = true, recv = true) { o.traverse((m) => { if (m.isMesh) { m.castShadow = cast; m.receiveShadow = recv; } }); return o; }

export function buildSet(scene) {
  RectAreaLightUniformsLib.init();
  const S = {};
  const root = new THREE.Group(); scene.add(root); S.root = root;

  // Room shell.
  const floorMat = std(0xffffff, 0.75, 0, { map: T.tex(T.wood(1024, 1024, [150, 104, 70], 11, 8), true, [3, 3]) });
  const floor = new THREE.Mesh(new THREE.PlaneGeometry(4, 4), floorMat);
  floor.rotation.x = -Math.PI / 2; floor.position.set(0.1, 0, 0.8); floor.receiveShadow = true; root.add(floor);
  const wallMat = std(0xffffff, 0.95, 0, { map: T.tex(T.plaster(512, 512, [222, 214, 202], 4), true, [4, 3]) });
  S.wallMat = wallMat;
  // Back wall with a window opening x -0.85..0.85, y 0.95..2.05.
  const wall = new THREE.Shape([new THREE.Vector2(-1.7, 0), new THREE.Vector2(1.9, 0), new THREE.Vector2(1.9, 2.7), new THREE.Vector2(-1.7, 2.7)]);
  const hole = new THREE.Path([new THREE.Vector2(-0.85, 0.95), new THREE.Vector2(0.85, 0.95), new THREE.Vector2(0.85, 2.05), new THREE.Vector2(-0.85, 2.05)]);
  wall.holes.push(hole);
  const back = new THREE.Mesh(new THREE.ShapeGeometry(wall), wallMat);
  back.position.z = -0.66; back.receiveShadow = true; root.add(back);
  const left = new THREE.Mesh(new THREE.PlaneGeometry(3.2, 2.7), wallMat);
  left.rotation.y = Math.PI / 2; left.position.set(-1.7, 1.35, 0.9); left.receiveShadow = true; root.add(left);
  const right = new THREE.Mesh(new THREE.PlaneGeometry(3.2, 2.7), wallMat);
  right.rotation.y = -Math.PI / 2; right.position.set(1.9, 1.35, 0.9); right.receiveShadow = true; root.add(right);
  const ceil = new THREE.Mesh(new THREE.PlaneGeometry(3.6, 3.2), std(0xe8e2d8, 1));
  ceil.rotation.x = Math.PI / 2; ceil.position.set(0.1, 2.7, 0.9); root.add(ceil);

  // Window frame, sill, reveal.
  const white = std(0xf1eee8, 0.5);
  const frameBars = [[0, 2.05, 1.76, 0.06], [0, 0.95, 1.76, 0.06], [-0.85, 1.5, 0.06, 1.16], [0.85, 1.5, 0.06, 1.16], [0, 1.5, 0.04, 1.1]];
  for (const [x, y, w, h] of frameBars) { const b = new THREE.Mesh(new THREE.BoxGeometry(w, h, 0.06), white); b.position.set(x, y, -0.69); shadowed(b); root.add(b); }
  const reveal = (x, y, w, h, d) => { const m = new THREE.Mesh(new THREE.BoxGeometry(w, h, d), wallMat); m.position.set(x, y, -0.76); root.add(m); };
  reveal(-0.88, 1.5, 0.06, 1.2, 0.2); reveal(0.88, 1.5, 0.06, 1.2, 0.2); reveal(0, 2.08, 1.82, 0.06, 0.2);
  const sill = new THREE.Mesh(new RoundedBoxGeometry(1.86, 0.035, 0.24, 2, 0.008), white);
  sill.position.set(0, 0.925 - 0.0175, -0.58); shadowed(sill); root.add(sill);

  // Sky and city behind the glass.
  const skyU = { top: { value: new THREE.Color(0x0b1324) }, bottom: { value: new THREE.Color(0x26324a) }, flash: { value: 0 } };
  S.skyU = skyU;
  const sky = new THREE.Mesh(new THREE.PlaneGeometry(14, 8), new THREE.ShaderMaterial({
    uniforms: skyU, depthWrite: false,
    vertexShader: 'varying vec2 vUv; void main(){ vUv=uv; gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.0); }',
    fragmentShader: 'uniform vec3 top; uniform vec3 bottom; uniform float flash; varying vec2 vUv; void main(){ vec3 c=mix(bottom, top, smoothstep(0.25,0.95,vUv.y)); c+=vec3(0.75,0.82,1.0)*flash; gl_FragColor=vec4(c,1.0); }',
  }));
  sky.position.set(0, 2.2, -7.5); root.add(sky);
  S.cityNight = new THREE.MeshBasicMaterial({ map: T.tex(T.cityNight(2048, 768, 21, false)), transparent: true, depthWrite: false });
  S.cityDay = new THREE.MeshBasicMaterial({ map: T.tex(T.cityNight(2048, 768, 21, true)), transparent: true, opacity: 0, depthWrite: false });
  const cityGeo = new THREE.PlaneGeometry(9, 3.4);
  const cityN = new THREE.Mesh(cityGeo, S.cityNight); cityN.position.set(0, 0.9, -6.0); root.add(cityN);
  const cityD = new THREE.Mesh(cityGeo, S.cityDay); cityD.position.set(0, 0.9, -5.99); root.add(cityD);

  // Rain on the glass: streaks + beads, seeded and time-driven.
  S.rainU = { time: { value: 0 }, amount: { value: 1 }, tint: { value: new THREE.Color(0x9fb8d8) } };
  const glass = new THREE.Mesh(new THREE.PlaneGeometry(1.7, 1.1), new THREE.ShaderMaterial({
    uniforms: S.rainU, transparent: true, depthWrite: false,
    vertexShader: 'varying vec2 vUv; void main(){ vUv=uv; gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.0); }',
    fragmentShader: `uniform float time; uniform float amount; uniform vec3 tint; varying vec2 vUv;
      float h(vec2 p){ return fract(sin(dot(p, vec2(127.1,311.7)))*43758.5453); }
      void main(){
        vec2 uv = vUv * vec2(1.7, 1.1) * 9.0;
        float a = 0.0;
        for (int L = 0; L < 3; L++) {
          float fl = float(L);
          vec2 g = uv * (1.0 + fl * 0.6) + vec2(fl * 3.1, 0.0);
          float col = floor(g.x * 3.0);
          float sp = 0.35 + h(vec2(col, fl)) * 0.9;
          float y = fract(g.y * 0.35 + time * sp * 0.6 + h(vec2(col, fl + 7.0)));
          float x = fract(g.x * 3.0) - 0.5;
          float streak = smoothstep(0.06, 0.0, abs(x)) * smoothstep(0.0, 0.25, y) * smoothstep(1.0, 0.6, y) * step(0.55, h(vec2(col, fl + 3.0)));
          a += streak * 0.35;
          vec2 cell = floor(g * vec2(1.0, 1.4)); vec2 f = fract(g * vec2(1.0, 1.4)) - 0.5;
          float bead = smoothstep(0.09, 0.03, length(f + (vec2(h(cell), h(cell + 3.3)) - 0.5) * 0.6)) * step(0.72, h(cell + fl));
          a += bead * 0.45;
        }
        gl_FragColor = vec4(tint, clamp(a, 0.0, 0.8) * amount);
      }`,
  }));
  glass.position.set(0, 1.5, -0.705); root.add(glass);
  S.glass = glass;

  // Desk.
  const deskMat = std(0xffffff, 0.48, 0, { map: T.tex(T.wood(2048, 1024, [178, 128, 84], 3, 5)) });
  const top = new THREE.Mesh(new RoundedBoxGeometry(1.4, 0.03, 0.70, 3, 0.006), deskMat);
  top.position.set(0, DESK.y - 0.015, (DESK.z0 + DESK.z1) / 2); shadowed(top); root.add(top);
  const metal = std(0x1c1d21, 0.4, 0.6);
  for (const [x, z] of [[-0.66, -0.58], [0.66, -0.58], [-0.66, 0.04], [0.66, 0.04]]) {
    const leg = new THREE.Mesh(new THREE.BoxGeometry(0.04, DESK.y - 0.03, 0.04), metal);
    leg.position.set(x, (DESK.y - 0.03) / 2, z); shadowed(leg); root.add(leg);
  }
  // A drawer under the left side (the door card lives here).
  S.drawer = new THREE.Group();
  const drawerBox = new THREE.Mesh(new THREE.BoxGeometry(0.42, 0.09, 0.5), std(0xc7a17a, 0.55, 0, { map: deskMat.map }));
  drawerBox.position.set(0, 0, -0.25); S.drawer.add(drawerBox);
  const knob = new THREE.Mesh(new THREE.CylinderGeometry(0.008, 0.008, 0.02, 16), metal);
  knob.rotation.x = Math.PI / 2; knob.position.set(0, 0, 0.012); S.drawer.add(knob);
  S.drawer.position.set(-0.40, DESK.y - 0.08, DESK.z1 - 0.005); shadowed(S.drawer); root.add(S.drawer);

  // Monitor with a live canvas.
  S.monitor = {};
  const mon = new THREE.Group();
  const bezel = new THREE.Mesh(new RoundedBoxGeometry(0.556, 0.326, 0.022, 2, 0.006), std(0x141416, 0.45, 0.3));
  mon.add(bezel);
  const mc = document.createElement('canvas'); mc.width = 1920; mc.height = 1080;
  const mtex = new THREE.CanvasTexture(mc); mtex.colorSpace = THREE.SRGBColorSpace; mtex.anisotropy = 8;
  const screen = new THREE.Mesh(new THREE.PlaneGeometry(0.54, 0.304), new THREE.MeshBasicMaterial({ map: mtex, toneMapped: false }));
  screen.position.z = 0.0115; mon.add(screen);
  const neck = new THREE.Mesh(new THREE.BoxGeometry(0.04, 0.16, 0.02), metal); neck.position.set(0, -0.2, -0.03); mon.add(neck);
  const foot = new THREE.Mesh(new RoundedBoxGeometry(0.22, 0.012, 0.16, 2, 0.004), metal); foot.position.set(0, -0.277, -0.0); mon.add(foot);
  mon.position.set(0.10, DESK.y + 0.283, -0.44); shadowed(mon); root.add(mon);
  S.monitor = { group: mon, canvas: mc, texture: mtex, screen, mat: screen.material };
  // Sticky note on the bezel.
  const note = new THREE.Mesh(new THREE.PlaneGeometry(0.06, 0.06), std(0xffffff, 0.9, 0, { map: T.tex(T.stickyNote()) }));
  note.position.set(-0.235, -0.165, 0.0125); note.rotation.z = 0.08; mon.add(note);
  S.note = note;

  // Monitor's soft light on the desk and on him.
  S.monLight = new THREE.RectAreaLight(0xcfe0ff, 0, 0.54, 0.30);
  S.monLight.position.set(0.10, DESK.y + 0.283, -0.425); S.monLight.lookAt(0.10, DESK.y + 0.2, 1.0); root.add(S.monLight);

  // Keyboard, mouse.
  const kb = new THREE.Mesh(new RoundedBoxGeometry(0.36, 0.018, 0.125, 2, 0.004), [std(0x2a2b30, 0.5), std(0x2a2b30, 0.5), std(0xffffff, 0.6, 0, { map: T.tex(T.keyboardTex()) }), std(0x2a2b30, 0.5), std(0x2a2b30, 0.5), std(0x2a2b30, 0.5)]);
  kb.position.set(0.10, DESK.y + 0.009, -0.13); shadowed(kb); root.add(kb);
  const mouse = new THREE.Mesh(new THREE.SphereGeometry(0.03, 24, 12), std(0x2c2d31, 0.4));
  mouse.scale.set(1, 0.5, 1.6); mouse.position.set(0.40, DESK.y + 0.012, -0.11); shadowed(mouse); root.add(mouse);
  S.mouse = mouse; mouse.userData.rest = mouse.position.clone();

  // Desk lamp (warm key), articulated.
  S.lamp = {};
  const lamp = new THREE.Group();
  const lampMat = std(0x2b2d33, 0.35, 0.5);
  const base = new THREE.Mesh(new THREE.CylinderGeometry(0.07, 0.08, 0.02, 32), lampMat); base.position.y = 0.01; lamp.add(base);
  const arm1 = new THREE.Mesh(new THREE.CylinderGeometry(0.008, 0.008, 0.42, 12), lampMat); arm1.position.set(-0.03, 0.22, 0.06); arm1.rotation.x = 0.35; arm1.rotation.z = 0.2; lamp.add(arm1);
  const arm2 = new THREE.Mesh(new THREE.CylinderGeometry(0.007, 0.007, 0.36, 12), lampMat); arm2.position.set(-0.13, 0.43, 0.2); arm2.rotation.x = 1.1; arm2.rotation.z = 0.45; lamp.add(arm2);
  const shade = new THREE.Mesh(new THREE.ConeGeometry(0.075, 0.11, 32, 1, true), lampMat);
  shade.material.side = THREE.DoubleSide; shade.position.set(-0.22, 0.46, 0.33); shade.rotation.x = 0.35; lamp.add(shade);
  const bulb = new THREE.Mesh(new THREE.SphereGeometry(0.025, 16, 10), new THREE.MeshBasicMaterial({ color: 0xffd9a6, toneMapped: false }));
  bulb.position.set(-0.22, 0.43, 0.34); lamp.add(bulb);
  lamp.position.set(0.58, DESK.y, -0.48); shadowed(lamp); root.add(lamp);
  S.lamp.bulb = bulb;
  S.lamp.spot = new THREE.SpotLight(0xffb468, 0, 3.2, 0.95, 0.65, 1.6);
  S.lamp.spot.position.set(0.36, DESK.y + 0.43, -0.15); S.lamp.spot.target.position.set(0.1, DESK.y, -0.05);
  S.lamp.spot.castShadow = true; S.lamp.spot.shadow.mapSize.set(2048, 2048); S.lamp.spot.shadow.bias = -0.0004; S.lamp.spot.shadow.radius = 4;
  root.add(S.lamp.spot); root.add(S.lamp.spot.target);
  S.lamp.fill = new THREE.PointLight(0xffb06a, 0, 4, 2); S.lamp.fill.position.set(0.3, DESK.y + 0.55, 0.1); root.add(S.lamp.fill);

  // Mugs.
  const mugProfile = [];
  for (let i = 0; i <= 12; i++) { const y = i / 12 * 0.095; mugProfile.push(new THREE.Vector2(0.038 + 0.002 * Math.sin(i / 12 * Math.PI), y)); }
  mugProfile.push(new THREE.Vector2(0.034, 0.095)); mugProfile.push(new THREE.Vector2(0.034, 0.006));
  const mugGeo = new THREE.LatheGeometry(mugProfile, 48);
  const makeMug = (mat) => {
    const g = new THREE.Group();
    const body = new THREE.Mesh(mugGeo, mat); g.add(body);
    const handle = new THREE.Mesh(new THREE.TorusGeometry(0.024, 0.006, 12, 24, Math.PI * 1.15), mat.clone());
    handle.material.map = null; handle.rotation.z = -Math.PI * 0.57; handle.position.set(0.04, 0.05, 0); g.add(handle);
    const liquid = new THREE.Mesh(new THREE.CircleGeometry(0.034, 32), std(0x3b2414, 0.15));
    liquid.rotation.x = -Math.PI / 2; liquid.position.y = 0.075; liquid.visible = false; g.add(liquid);
    g.userData.liquid = liquid;
    return shadowed(g);
  };
  S.fishMug = makeMug(std(0xffffff, 0.35, 0, { map: T.tex(T.fishMug()) }));
  // Pothos cuttings rooting in water (act three).
  const sprig = new THREE.Group();
  const heart2 = new THREE.Shape(); heart2.moveTo(0, 0); heart2.bezierCurveTo(0.018, 0.011, 0.027, 0.032, 0.0, 0.05); heart2.bezierCurveTo(-0.027, 0.032, -0.018, 0.011, 0, 0);
  const sprigLeaf = new THREE.ShapeGeometry(heart2, 8);
  for (let i = 0; i < 2; i++) {
    const stem = new THREE.Mesh(new THREE.CylinderGeometry(0.0016, 0.0016, 0.12, 6), std(0x6f9a4a, 0.6));
    stem.position.set(-0.006 + i * 0.012, 0.09, 0); stem.rotation.z = (i ? -1 : 1) * 0.18; sprig.add(stem);
    for (let j = 0; j < 2; j++) {
      const lf = new THREE.Mesh(sprigLeaf, new THREE.MeshStandardMaterial({ color: 0x5fa23f, roughness: 0.5, side: THREE.DoubleSide }));
      lf.position.set((i ? 0.02 : -0.02) + (j ? 0.012 : -0.008), 0.13 + j * 0.025, 0.004 * j);
      lf.rotation.set(-0.4, i ? 0.6 : -0.7, (i ? -1 : 1) * (0.7 + j * 0.3)); sprig.add(lf);
    }
  }
  sprig.visible = false; S.fishMug.add(sprig); S.fishMug.userData.sprig = sprig;
  S.fishMug.position.set(-0.13, DESK.y, -0.06); S.fishMug.rotation.y = 0.6; root.add(S.fishMug);
  S.fishMug.userData.rest = { pos: S.fishMug.position.clone(), quat: S.fishMug.quaternion.clone() };
  S.myMug = makeMug(std(0x2f4a63, 0.4));
  S.myMug.position.set(0.40, DESK.y, -0.33); S.myMug.rotation.y = -0.4; root.add(S.myMug);
  S.myMug.userData.rest = { pos: S.myMug.position.clone(), quat: S.myMug.quaternion.clone() };

  // Plant (pothos) with heart leaves; health 0 = drooping and yellowed.
  S.plant = buildPlant();
  S.plant.group.position.copy(SPOT.plantSill); root.add(S.plant.group);

  // Gift card, parcel box, door card, phone, power bank, router.
  S.card = new THREE.Mesh(new THREE.BoxGeometry(0.105, 0.0012, 0.074), [std(0xf3ecdf, 0.9), std(0xf3ecdf, 0.9), std(0xffffff, 0.85, 0, { map: T.tex(T.giftCard()) }), std(0xf3ecdf, 0.9), std(0xf3ecdf, 0.9), std(0xf3ecdf, 0.9)]);
  S.card.position.set(-0.56, DESK.y + 0.0006, -0.02); S.card.rotation.y = 0.18; shadowed(S.card); root.add(S.card);

  S.parcel = buildParcel(); S.parcel.group.position.set(-1.0, 0, 0.25); root.add(S.parcel.group);

  S.doorCard = new THREE.Mesh(new RoundedBoxGeometry(0.054, 0.0009, 0.0856, 2, 0.0004), [std(0xd6d6d2, 0.4), std(0xd6d6d2, 0.4), std(0xcfcfcb, 0.45, 0, { map: T.tex(T.doorCard()) }), std(0xeeeeea, 0.4), std(0xeeeeea, 0.4), std(0xeeeeea, 0.4)]);
  S.doorCard.visible = false; shadowed(S.doorCard); root.add(S.doorCard);

  S.phone = buildPhone(); S.phone.group.position.set(0.27, DESK.y + 0.004, -0.02); S.phone.group.rotation.x = -Math.PI / 2; root.add(S.phone.group);
  // Phone torch: points out of the back of the phone.
  S.torch = new THREE.SpotLight(0xf2f6ff, 0, 3, 0.55, 0.6, 1.8);
  S.torch.position.set(0, 0.06, -0.006); S.torch.target.position.set(0, 0.06, -1.0);
  S.phone.group.add(S.torch); S.phone.group.add(S.torch.target);

  // Water bottle on the desk (he waters the pothos with it, twice).
  S.bottle = new THREE.Group();
  const bodyMat = new THREE.MeshPhysicalMaterial({ color: 0xd8ecf4, roughness: 0.12, transparent: true, opacity: 0.42, clearcoat: 1, depthWrite: false });
  const body = new THREE.Mesh(new THREE.CylinderGeometry(0.032, 0.033, 0.17, 24), bodyMat); body.position.y = 0.085; S.bottle.add(body);
  const water = new THREE.Mesh(new THREE.CylinderGeometry(0.029, 0.030, 0.11, 24), new THREE.MeshStandardMaterial({ color: 0x9fc6d8, roughness: 0.1, transparent: true, opacity: 0.35, depthWrite: false }));
  water.position.y = 0.058; S.bottle.add(water); S.bottle.userData.water = water;
  const shoulder = new THREE.Mesh(new THREE.CylinderGeometry(0.013, 0.032, 0.03, 24), bodyMat); shoulder.position.y = 0.185; S.bottle.add(shoulder);
  const cap = new THREE.Mesh(new THREE.CylinderGeometry(0.0135, 0.0135, 0.016, 20), std(0x2d6fb5, 0.4)); cap.position.y = 0.208; S.bottle.add(cap);
  S.bottle.userData.cap = cap;
  S.bottle.position.set(0.27, DESK.y, -0.44); shadowed(S.bottle, true, false); root.add(S.bottle);
  S.bottle.userData.rest = { pos: S.bottle.position.clone(), quat: S.bottle.quaternion.clone() };
  // Water stream for the pours (a thin bright column between cap and pot).
  S.stream = new THREE.Mesh(new THREE.CylinderGeometry(0.0045, 0.0055, 1, 10, 1, true), new THREE.MeshStandardMaterial({ color: 0xe6f6ff, emissive: 0x335566, roughness: 0.05, transparent: true, opacity: 0.8, depthWrite: false }));
  S.stream.visible = false; root.add(S.stream);
  // A film of dust over the left of the desk, except the clean rectangle where
  // 晴's laptop used to stand (SPOT.shaniuLeft is in it).
  {
    const c = document.createElement('canvas'); c.width = 1024; c.height = 1024;
    const g = c.getContext('2d'); const r = mulberry32(1203);
    for (let i = 0; i < 9000; i++) { const x = r() * 1024, y = r() * 1024; g.fillStyle = `rgba(214,206,190,${0.05 + r() * 0.16})`; g.fillRect(x, y, 1 + r() * 2.2, 1 + r() * 2.2); }
    g.fillStyle = 'rgba(205,198,184,0.07)'; g.fillRect(0, 0, 1024, 1024);
    // Clean rectangle (a 34 x 24 cm laptop footprint), soft edged.
    g.globalCompositeOperation = 'destination-out';
    // canvas x: world x -0.70→0.00; canvas y: world z -0.62→0.08. Rectangle x -0.57..-0.23, z -0.34..-0.10.
    const X = (x) => (x + 0.70) / 0.70 * 1024, Z = (z) => (z + 0.62) / 0.70 * 1024;
    g.filter = 'blur(6px)'; g.fillStyle = '#000'; g.fillRect(X(-0.57), Z(-0.34), X(-0.23) - X(-0.57), Z(-0.10) - Z(-0.34)); g.filter = 'none';
    const t = new THREE.CanvasTexture(c); t.colorSpace = THREE.SRGBColorSpace;
    const dust = new THREE.Mesh(new THREE.PlaneGeometry(0.70, 0.70), new THREE.MeshStandardMaterial({ map: t, transparent: true, roughness: 0.95, depthWrite: false, polygonOffset: true, polygonOffsetFactor: -1 }));
    // Plane covers x -0.70..0.00, z -0.62..0.08; texture u→x, v→z.
    dust.rotation.x = -Math.PI / 2; dust.position.set(-0.35, DESK.y + 0.0003, -0.27); dust.receiveShadow = true; root.add(dust);
    S.deskDust = dust;
  }

  S.powerBank = new THREE.Mesh(new RoundedBoxGeometry(0.07, 0.016, 0.13, 2, 0.006), std(0xd9d6d0, 0.45));
  S.powerBank.visible = false; shadowed(S.powerBank); root.add(S.powerBank);

  // USB-C plug on a short cable from the power bank (blackout scene).
  S.usbPlug = new THREE.Group();
  const plugBody = new THREE.Mesh(new RoundedBoxGeometry(0.0085, 0.006, 0.018, 2, 0.0015), std(0x202124, 0.5));
  plugBody.position.z = -0.009; S.usbPlug.add(plugBody);
  const plugTip = new THREE.Mesh(new RoundedBoxGeometry(0.0084, 0.0026, 0.0065, 2, 0.001), std(0xb8bcc2, 0.3, 0.9));
  plugTip.position.z = 0.003; S.usbPlug.add(plugTip);
  S.usbPlug.visible = false; shadowed(S.usbPlug); root.add(S.usbPlug);
  S.usbCable = new THREE.Mesh(new THREE.TubeGeometry(new THREE.CatmullRomCurve3([V3(0, 0, 0), V3(0, 0, -0.1)]), 16, 0.0018, 8), std(0x1d1e22, 0.6));
  S.usbCable.visible = false; root.add(S.usbCable);
  S.setCable = (a, b) => {
    const mid = a.clone().lerp(b, 0.5); mid.y = Math.min(a.y, b.y) - 0.01;
    S.usbCable.geometry.dispose();
    S.usbCable.geometry = new THREE.TubeGeometry(new THREE.CatmullRomCurve3([a, a.clone().lerp(mid, 0.5).setY(a.y - 0.004), mid, b]), 24, 0.0018, 8);
  };

  S.router = buildRouter(); S.router.group.position.set(-0.66, 0.925, -0.57); root.add(S.router.group);

  // Chair.
  const chair = new THREE.Group();
  const fabric = std(0xffffff, 0.9, 0, { map: T.tex(T.linenTex(512, 512, [58, 66, 80]), true, [2, 2]) });
  const seat = new THREE.Mesh(new RoundedBoxGeometry(0.48, 0.07, 0.46, 3, 0.03), fabric); seat.position.y = 0.47; chair.add(seat);
  const backr = new THREE.Mesh(new RoundedBoxGeometry(0.46, 0.5, 0.06, 3, 0.03), fabric); backr.position.set(0, 0.8, 0.22); backr.rotation.x = -0.12; chair.add(backr);
  const post = new THREE.Mesh(new THREE.CylinderGeometry(0.025, 0.025, 0.4, 12), metal); post.position.y = 0.24; chair.add(post);
  const star = new THREE.Mesh(new THREE.CylinderGeometry(0.28, 0.28, 0.03, 5), metal); star.position.y = 0.05; chair.add(star);
  chair.position.set(0.10, 0, 0.52); shadowed(chair); root.add(chair); S.chair = chair;

  // Shelf on the left wall: books and a frame turned face down.
  const shelf = new THREE.Group();
  const board = new THREE.Mesh(new THREE.BoxGeometry(0.22, 0.02, 0.9), white); shelf.add(board);
  const rnd = mulberry32(4);
  let z = -0.4;
  const bookCols = [0x8a4b3a, 0x3e5a6e, 0xc9b38a, 0x6b7a4d, 0x2f2f38, 0xa86f4a, 0x5e6c7c];
  while (z < 0.25) {
    const th = 0.02 + rnd() * 0.03, hh = 0.18 + rnd() * 0.08;
    const b = new THREE.Mesh(new THREE.BoxGeometry(0.15, hh, th), std(bookCols[Math.floor(rnd() * bookCols.length)], 0.8));
    b.position.set(0, hh / 2 + 0.01, z + th / 2); shelf.add(b); z += th + 0.002;
  }
  const frame = new THREE.Mesh(new THREE.BoxGeometry(0.14, 0.012, 0.18), std(0x2a2522, 0.5)); frame.position.set(0, 0.017, 0.36); shelf.add(frame);
  shelf.position.set(-1.58, 1.25, -0.1); shadowed(shelf); root.add(shelf);

  // Front door on the right wall, for the knock that nobody makes.
  const door = new THREE.Group();
  const leaf = new THREE.Mesh(new RoundedBoxGeometry(0.9, 2.05, 0.045, 2, 0.006), std(0xd9d2c6, 0.6));
  leaf.position.y = 1.025; door.add(leaf);
  const handle = new THREE.Mesh(new THREE.BoxGeometry(0.12, 0.02, 0.03), metal); handle.position.set(-0.34, 1.0, 0.035); door.add(handle);
  const gap = new THREE.Mesh(new THREE.PlaneGeometry(0.9, 0.012), new THREE.MeshBasicMaterial({ color: 0xffd9a0, toneMapped: false }));
  gap.position.set(0, 0.006, 0.026); door.add(gap); S.doorGap = gap;
  door.rotation.y = -Math.PI / 2; door.position.set(1.875, 0, 1.45); shadowed(door); root.add(door);

  // Window light (moon / sun) and sky fill.
  S.sun = new THREE.DirectionalLight(0x9db4ff, 0);
  S.sun.position.set(0.6, 2.6, -2.8); S.sun.target.position.set(0, DESK.y, -0.1);
  S.sun.castShadow = true; S.sun.shadow.mapSize.set(2048, 2048); S.sun.shadow.bias = -0.0003;
  Object.assign(S.sun.shadow.camera, { left: -1.6, right: 1.6, top: 1.6, bottom: -1.6, near: 0.5, far: 7 });
  S.sun.shadow.radius = 3;
  root.add(S.sun); root.add(S.sun.target);
  S.hemi = new THREE.HemisphereLight(0x8aa0c8, 0x2a2018, 0.1); root.add(S.hemi);
  S.lightning = new THREE.DirectionalLight(0xdfe8ff, 0); S.lightning.position.set(-0.4, 2.2, -3); root.add(S.lightning);
  return S;
}

function buildPlant() {
  const group = new THREE.Group();
  const pot = new THREE.Mesh(new THREE.CylinderGeometry(0.06, 0.045, 0.1, 32), std(0xe9e4da, 0.5));
  pot.position.y = 0.05; group.add(pot);
  const soil = new THREE.Mesh(new THREE.CircleGeometry(0.055, 24), std(0x3a2a1e, 1));
  soil.rotation.x = -Math.PI / 2; soil.position.y = 0.095; group.add(soil);
  const heart = new THREE.Shape();
  heart.moveTo(0, 0); heart.bezierCurveTo(0.02, 0.012, 0.03, 0.035, 0.0, 0.055); heart.bezierCurveTo(-0.03, 0.035, -0.02, 0.012, 0, 0);
  const leafGeo = new THREE.ShapeGeometry(heart, 10);
  const leaves = [];
  const rnd = mulberry32(31);
  const vines = 5;
  for (let v = 0; v < vines; v++) {
    const ang = v / vines * Math.PI * 2 + rnd() * 0.6;
    const n = 4 + Math.floor(rnd() * 3);
    for (let i = 0; i < n; i++) {
      const mat = new THREE.MeshStandardMaterial({ color: 0x4f8f3a, roughness: 0.55, side: THREE.DoubleSide });
      const leaf = new THREE.Mesh(leafGeo, mat);
      leaf.userData = { ang, i, n, vine: v, s: 0.75 + rnd() * 0.5, twist: rnd() * 2 - 1 };
      leaf.castShadow = true; group.add(leaf); leaves.push(leaf);
    }
  }
  const newLeaf = new THREE.Mesh(leafGeo, new THREE.MeshStandardMaterial({ color: 0x9fd16a, roughness: 0.5, side: THREE.DoubleSide }));
  newLeaf.position.set(0.0, 0.16, 0.0); newLeaf.scale.setScalar(0); group.add(newLeaf);
  const set = (health, grow = 0) => {
    for (const leaf of leaves) {
      const { ang, i, n, s, twist } = leaf.userData;
      const droop = 1 - health;
      const r = 0.035 + i * 0.022;
      const y = 0.11 + 0.05 * Math.sin((i / n) * Math.PI * 0.9) - i * (0.012 + 0.02 * droop) * 1.0;
      leaf.position.set(Math.cos(ang) * r, y, Math.sin(ang) * r);
      leaf.rotation.set(-0.9 - droop * 0.9 + twist * 0.2, ang + Math.PI / 2, twist * 0.3);
      leaf.scale.setScalar(s * (0.9 - droop * 0.1));
      leaf.material.color.setRGB(0.31 + droop * 0.38, 0.56 - droop * 0.08, 0.23 - droop * 0.12);
    }
    newLeaf.scale.setScalar(0.8 * grow);
    newLeaf.rotation.set(-0.5, 0.3, 0);
  };
  set(0.2);
  return { group, set };
}

function buildParcel() {
  const group = new THREE.Group();
  const card = std(0xb98c5a, 0.85);
  const W = 0.30, D = 0.22, H = 0.12;
  // The top face doubles as the inside floor of the open box (seen from above).
  const box = new THREE.Mesh(new THREE.BoxGeometry(W, H, D), [card, card, std(0x7d5f3e, 0.95), card, card, card]);
  box.position.y = H / 2; group.add(box);
  // Kit parts that lie in the box: a loose acrylic plate, a bag of nylon
  // standoffs, a coiled USB-C cable (the board itself is the Device model).
  const inner = new THREE.Group(); inner.visible = false; group.add(inner);
  const acr = new THREE.Mesh(new THREE.BoxGeometry(0.066, 0.003, 0.056), new THREE.MeshPhysicalMaterial({ color: 0xdfeff2, roughness: 0.08, transparent: true, opacity: 0.35, clearcoat: 1, depthWrite: false }));
  acr.position.set(-0.075, H + 0.004, 0.055); acr.rotation.y = 0.2; inner.add(acr);
  const acr2 = acr.clone(); acr2.position.set(-0.07, H + 0.0075, 0.06); acr2.rotation.y = 0.32; inner.add(acr2);
  const bag = new THREE.Mesh(new THREE.BoxGeometry(0.05, 0.008, 0.04), new THREE.MeshStandardMaterial({ color: 0xf1f1ee, roughness: 0.3, transparent: true, opacity: 0.55, depthWrite: false }));
  bag.position.set(0.09, H + 0.005, -0.05); inner.add(bag);
  for (let i = 0; i < 4; i++) { const st = new THREE.Mesh(new THREE.CylinderGeometry(0.0026, 0.0026, 0.0096, 6), std(0xe6e3dc, 0.55)); st.rotation.z = Math.PI / 2; st.position.set(0.08 + i * 0.007, H + 0.004, -0.052 + (i % 2) * 0.01); inner.add(st); }
  const coil = new THREE.Mesh(new THREE.TorusGeometry(0.03, 0.0025, 8, 40), std(0x1d1d20, 0.5)); coil.rotation.x = Math.PI / 2; coil.position.set(0.085, H + 0.004, 0.06); inner.add(coil);
  const label = new THREE.Mesh(new THREE.PlaneGeometry(0.13, 0.08), std(0xffffff, 0.8, 0, { map: T.tex(T.parcelLabel()) }));
  label.rotation.x = -Math.PI / 2; label.position.set(0.06, H + 0.001, 0.03); group.add(label);
  const flapL = new THREE.Mesh(new THREE.BoxGeometry(W / 2, 0.002, D), card); flapL.geometry.translate(W / 4, 0, 0);
  const flapR = new THREE.Mesh(new THREE.BoxGeometry(W / 2, 0.002, D), card); flapR.geometry.translate(-W / 4, 0, 0);
  flapL.position.set(-W / 2, H, 0); flapR.position.set(W / 2, H, 0);
  group.add(flapL); group.add(flapR);
  shadowed(group);
  return { group, label, flapL, flapR, H, open: (k) => { flapL.rotation.z = k * 2.4; flapR.rotation.z = -k * 2.4; label.visible = k < 0.05; inner.visible = k > 0.3; } };
}

function buildPhone() {
  const group = new THREE.Group();
  const body = new THREE.Mesh(new RoundedBoxGeometry(0.0745, 0.161, 0.0082, 3, 0.006), std(0x1a1b1f, 0.3, 0.6));
  group.add(body);
  const c = document.createElement('canvas'); c.width = 720; c.height = 1560;
  const tex = new THREE.CanvasTexture(c); tex.colorSpace = THREE.SRGBColorSpace; tex.anisotropy = 8;
  const screen = new THREE.Mesh(new THREE.PlaneGeometry(0.0705, 0.1528), new THREE.MeshBasicMaterial({ map: tex, toneMapped: false }));
  screen.position.z = 0.0042; group.add(screen);
  const glass = new THREE.Mesh(new THREE.PlaneGeometry(0.0705, 0.1528), new THREE.MeshPhysicalMaterial({ color: 0x000000, transparent: true, opacity: 0.12, roughness: 0.05, clearcoat: 1, depthWrite: false }));
  glass.position.z = 0.0043; group.add(glass);
  shadowed(group);
  return { group, canvas: c, texture: tex, screen };
}

function buildRouter() {
  const group = new THREE.Group();
  const body = new THREE.Mesh(new RoundedBoxGeometry(0.16, 0.035, 0.11, 2, 0.008), std(0xf3f1ec, 0.5));
  body.position.y = 0.0175; group.add(body);
  const leds = [];
  for (let i = 0; i < 4; i++) {
    const led = new THREE.Mesh(new THREE.SphereGeometry(0.0025, 8, 6), new THREE.MeshBasicMaterial({ color: 0x55e07a, toneMapped: false }));
    led.position.set(-0.045 + i * 0.03, 0.03, 0.056); group.add(led); leds.push(led);
  }
  for (const s of [-1, 1]) { const ant = new THREE.Mesh(new THREE.CylinderGeometry(0.004, 0.004, 0.13, 8), std(0xf3f1ec, 0.5)); ant.position.set(s * 0.06, 0.1, -0.04); ant.rotation.z = s * 0.12; group.add(ant); }
  shadowed(group);
  return { group, leds, set: (k, t) => leds.forEach((l, i) => { l.visible = k > i / 4; l.material.color.setHex(i === 3 && Math.floor(t * 4) % 2 ? 0x2a6a3a : 0x55e07a); }) };
}
