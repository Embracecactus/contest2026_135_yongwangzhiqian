// SPDX-License-Identifier: Apache-2.0
// The film as a pure function of time: world state + active shot → one frame.
import * as THREE from 'three';
import { applyLook, blendLook, LOOKS } from './engine.js';
import { SPOT, DESK } from './set.js';
import { clamp } from './util.js';
import { SHOTS } from './shots.js';
import { worldState, eyeAt } from './world.js';
import { phoneAt, monitorAt } from './screens.js';
import { drawPhone } from './phoneui.js';
import { drawMonitor } from './pcui.js';
import { Cutaway } from './cutaway.js';
import { SPEECH } from './speech.js';

// Loudness (0..1) of a speaker's line at time t, from the generated envelopes.
export function voiceLevel(list, t) {
  for (const [t0, lv] of list) {
    const i = Math.floor((t - t0) * SPEECH.fps);
    if (i >= 0 && i < lv.length) return +lv[i] / 9;
  }
  return 0;
}

export class Film {
  constructor(E, S, device, hero, overlay) {
    Object.assign(this, { E, S, device, hero, overlay });
    this.shots = SHOTS.slice().sort((a, b) => a.t0 - b.t0);
    this.cut = new Cutaway(E);
    this.ctx = { S, device, hero, film: this, SPOT, DESK };
    this._phoneKey = ''; this._monKey = '';
  }

  async ready() { await this.cut.ready(); }

  shotAt(t) {
    let s = this.shots[0];
    for (const x of this.shots) { if (x.t0 <= t) s = x; else break; }
    return s;
  }

  // t: story time (world, shots, eyes, screens); tf: film time (speech placed by the mixer).
  render(t, tf = t) {
    const { E, S, device, hero } = this;
    const shot = this.shotAt(t);
    const k = clamp((t - shot.t0) / Math.max(0.001, shot.t1 - shot.t0));
    let w = worldState(t);
    if (shot.world) w = shot.world(t, k, this.ctx, w) || w;
    this.w = w;
    if (shot.cutaway) {
      const look = LOOKS.night; applyLook(E, S, look);
      const labels = this.cut.render(shot.cutaway, t, k, shot);
      this.drawOverlay(t, k, shot, w, labels);
      return;
    }
    // Look (colour script) and weather.
    const look = w.lookB ? blendLook(w.lookA, w.lookB, w.lookK) : LOOKS[w.lookA];
    applyLook(E, S, look, { lampMul: w.lampMul ?? 1, monMul: w.monMul ?? 1, exposureMul: (shot.exposure ?? 1) * (w.exposureMul ?? 1) });
    S.rainU.time.value = t;
    S.skyU.flash.value = w.flash || 0;
    S.lightning.intensity = (w.flash || 0) * 5;
    // Props.
    S.plant.set(w.plantHealth ?? 0.2, w.plantGrow ?? 0);
    if (w.plantPos) S.plant.group.position.copy(w.plantPos);
    S.router.set(w.routerLeds ?? 1, t);
    S.parcel.group.visible = w.parcelVisible !== false;
    if (w.parcelPos) S.parcel.group.position.copy(w.parcelPos);
    S.parcel.group.rotation.y = w.parcelRot ?? 0.3;
    S.parcel.open(w.parcelOpen ?? 0);
    S.drawer.position.z = DESK.z1 - 0.005 + (w.drawerOpen ?? 0) * 0.22;
    S.card.visible = w.cardVisible !== false;
    // Every frame sets every prop explicitly: frames render out of order across workers.
    if (w.cardPos) S.card.position.copy(w.cardPos); else S.card.position.set(-0.56, DESK.y + 0.0006, -0.02);
    if (w.cardQuat) S.card.quaternion.copy(w.cardQuat); else S.card.rotation.set(0, 0.18, 0);
    S.doorCard.visible = !!w.doorCard;
    if (w.doorCard) {
      S.doorCard.position.copy(w.doorCard.pos);
      if (w.doorCard.quat) S.doorCard.quaternion.copy(w.doorCard.quat); else S.doorCard.rotation.set(...(w.doorCard.rot || [0, 0, 0]));
    }
    S.powerBank.visible = !!w.powerBank;
    if (w.powerBank) S.powerBank.position.copy(w.powerBank);
    S.powerBank.rotation.set(0, w.powerBankYaw ?? 0, 0);
    // Handled props: bottle and mugs follow a hand when a shot or the world says so.
    const pose = (obj, p) => { if (p) { obj.position.copy(p.pos); obj.quaternion.copy(p.quat); } else if (obj.userData.rest) { obj.position.copy(obj.userData.rest.pos); obj.quaternion.copy(obj.userData.rest.quat); } };
    pose(S.bottle, w.bottle);
    pose(S.fishMug, w.fishMugPose);
    pose(S.myMug, w.myMugPose);
    S.bottle.userData.water.scale.y = w.bottleWater ?? 1;
    S.bottle.userData.water.position.y = 0.003 + 0.055 * (w.bottleWater ?? 1);
    device.setAssembled(w.assembled !== false);
    S.mouse.position.copy(w.mousePos || S.mouse.userData.rest);
    S.stream.visible = !!w.stream;
    if (w.stream) {
      const a = w.stream.from, b = w.stream.to, d = b.clone().sub(a);
      S.stream.position.copy(a).add(b).multiplyScalar(0.5);
      S.stream.scale.set(1, d.length(), 1);
      S.stream.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), d.normalize());
    }
    S.fishMug.userData.liquid.visible = !!w.fishMugCoffee;
    if (S.fishMug.userData.sprig) S.fishMug.userData.sprig.visible = !!w.cuttings;
    S.myMug.userData.liquid.visible = !!w.myMugCoffee;
    S.doorGap.material.color.setHex(w.doorGapOn ? 0xffd9a0 : 0x050505);
    S.torch.intensity = w.torch ?? 0;
    S.usbPlug.visible = S.usbCable.visible = !!w.usbPlug;
    // Device placement.
    device.group.visible = !w.shaniuHidden;
    device.group.position.copy(w.shaniuPos || SPOT.shaniuLeft);
    device.group.rotation.set(0, w.shaniuYaw ?? 0, 0);
    device.board.rotation.set(w.shaniuFlat ? -Math.PI / 2 : -THREE.MathUtils.degToRad(24), 0, 0);
    device.board.position.set(0, w.shaniuFlat ? 0.0135 : 0.0345, w.shaniuFlat ? 0 : 0.006);
    device.stand.visible = !w.shaniuHeld && !w.shaniuFlat;
    if (w.shaniuHeld) {
      device.group.position.copy(w.shaniuHeld.pos); device.group.quaternion.copy(w.shaniuHeld.quat);
      device.board.position.set(0, 0, 0); device.board.rotation.set(0, 0, 0);
    }
    device.group.updateMatrixWorld(true);
    if (w.usbPlug) {
      const port = device.board.localToWorld(new THREE.Vector3(-5.5e-3, -25e-3 - (1 - w.usbPlug.k) * 0.03, 1.6e-3));
      const outward = device.board.localToWorld(new THREE.Vector3(-5.5e-3, -1, 1.6e-3)).sub(device.board.localToWorld(new THREE.Vector3(-5.5e-3, 0, 1.6e-3))).normalize();
      S.usbPlug.position.copy(port);
      S.usbPlug.quaternion.setFromUnitVectors(new THREE.Vector3(0, 0, 1), outward.clone().negate());
      const back = port.clone().add(outward.clone().multiplyScalar(0.018));
      S.setCable(back, (w.powerBank || port).clone().add(new THREE.Vector3(0.0, 0.006, 0.065)));
    }
    const eye = (shot.eye && shot.eye(t, k, this.ctx, w)) || eyeAt(t, w);
    const her = voiceLevel(SPEECH.her, tf);
    if (her > 0 && eye.glow) eye.glow *= 1 + 0.5 * her;   // her light breathes with her voice
    device.update(eye, t);
    // Character.
    const P = shot.pose ? shot.pose(t, k, this.ctx, w) : { ...this.defaultPose(t, w) };
    P.t = t;
    if (P.talk == null) P.talk = voiceLevel(SPEECH.him, tf);
    if (P.face == null) P.face = w.face || 'neutral';
    hero.pose(P);
    // Phone and monitor content (canvas textures redrawn only when they change).
    const held = shot.phone ? shot.phone(t, k, this.ctx, w) : null;
    if (held) { S.phone.group.position.copy(held.pos); S.phone.group.quaternion.copy(held.quat); }
    else { S.phone.group.position.set(0.27, DESK.y + 0.0045, -0.02); S.phone.group.rotation.set(-Math.PI / 2, 0, 0.3); }
    const ph = phoneAt(t, shot, w);
    const pk = drawPhone(S.phone.canvas, ph, t);
    if (pk !== this._phoneKey) { this._phoneKey = pk; S.phone.texture.needsUpdate = true; }
    S.phone.screen.material.color.setScalar(ph && ph.off ? 0 : 0.82);
    const mon = monitorAt(t, w);
    if (mon.app === 'workbench' && mon.panel === 'camera' && mon.frame) mon.frameCanvas = this.captureFrame();
    const mk = drawMonitor(S.monitor.canvas, mon, t, this);
    if (mk !== this._monKey) { this._monKey = mk; S.monitor.texture.needsUpdate = true; }
    // Camera.
    const cam = shot.cam(t, k, this.ctx, w);
    E.camera.position.copy(cam.pos);
    if (cam.quat) E.camera.quaternion.copy(cam.quat); else E.camera.lookAt(cam.target);
    if (cam.roll) E.camera.rotateZ(cam.roll);
    E.camera.fov = cam.fov || 35; E.camera.near = cam.near || 0.004; E.camera.updateProjectionMatrix();
    // Post.
    const dof = cam.focus != null;
    E.bokeh.enabled = dof;
    if (dof) {
      E.bokeh.uniforms.focus.value = cam.focus;
      E.bokeh.uniforms.aperture.value = cam.aperture ?? 0.004;
      E.bokeh.uniforms.maxblur.value = cam.maxblur ?? 0.01;
    }
    E.bloom.strength = (shot.bloom ?? 0.45) * (w.bloomMul ?? 1);
    E.grade.uniforms.time.value = Math.floor(t * 30) / 30;
    E.grade.uniforms.black.value = Math.max(w.black ?? 0, shot.black ? shot.black(t, k) : 0);
    E.grade.uniforms.white.value = shot.white ? shot.white(t, k) : 0;
    E.grade.uniforms.vignette.value = shot.vignette ?? 0.3;
    E.renderPass.scene = E.scene; E.renderPass.camera = E.camera;
    E.composer.render();
    this.drawOverlay(t, k, shot, w);
  }

  drawOverlay(t, k, shot, w, labels = null) {
    const o = this.overlay; if (!o) return;
    o.clear();
    if (labels) o.labels(labels, this.cut.camera);
    if (shot.overlay) shot.overlay(t, k, this.ctx, w, o);
    o.disclaimer(t, 0.8, 5.4);
    o.title(t, 541.6, 547.4);
    if (t >= 547.2) o.endCard(t, 547.2, 557, [this.device.canvases.L, this.device.canvases.R]);
  }

  // Her camera's view (640×480), rendered from the lens on the board.
  captureFrame() {
    const { E, device } = this, r = E.renderer;
    if (!this.capCam) {
      this.capCam = new THREE.PerspectiveCamera(58, 4 / 3, 0.01, 30);
      this.capCanvas = document.createElement('canvas'); this.capCanvas.width = 640; this.capCanvas.height = 480;
    }
    device.group.updateMatrixWorld(true);
    const lens = device.board.localToWorld(device.lensPos.clone());
    const ahead = device.board.localToWorld(device.lensPos.clone().add(new THREE.Vector3(0, 0, 1))).sub(lens).normalize();
    const cam = this.capCam; cam.position.copy(lens).add(ahead.clone().multiplyScalar(0.004));
    cam.lookAt(lens.clone().add(ahead)); cam.updateMatrixWorld(true);
    const pr = r.getPixelRatio(), size = r.getSize(new THREE.Vector2());
    this.hero.root.updateMatrixWorld(true);
    r.setRenderTarget(null);
    r.setViewport(0, 0, 640, 480); r.setScissor(0, 0, 640, 480); r.setScissorTest(true);
    r.render(E.scene, cam);
    r.setScissorTest(false); r.setViewport(0, 0, size.x, size.y);
    const g = this.capCanvas.getContext('2d');
    g.drawImage(r.domElement, 0, r.domElement.height - 480 * pr, 640 * pr, 480 * pr, 0, 0, 640, 480);
    return this.capCanvas;
  }

  defaultPose(t, w) {
    return { pos: new THREE.Vector3(0.10, 0.60, 0.47), yaw: Math.PI, lean: 0.16, legs: 'sit',
      armL: { target: new THREE.Vector3(0.02, DESK.y + 0.025, -0.08) }, armR: { target: new THREE.Vector3(0.22, DESK.y + 0.025, -0.08) } };
  }
}
