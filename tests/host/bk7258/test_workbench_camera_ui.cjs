#!/usr/bin/env node
/* SPDX-License-Identifier: Apache-2.0 */
/* Execute the actual camera browser controller against a minimal DOM/fetch peer. */
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const root = path.resolve(__dirname, '../../..');
const html = fs.readFileSync(path.join(root, 'tools/bk7258/_lib/workbench_web/index.html'), 'utf8');
const script = fs.readFileSync(path.join(root, 'tools/bk7258/_lib/workbench_web/app.js'), 'utf8');
/* 2x2 RGB synthetic JPEG from fixtures/camera-synthetic-jpeg.h. */
const JPEG = '/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAgGBgcGBQgHBwcJCQgKDBQNDAsLDBkSEw8UHRofHh0aHBwgJC4nICIsIxwcKDcpLDAxNDQ0Hyc5PTgyPC4zNDL/2wBDAQkJCQwLDBgNDRgyIRwhMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjL/wAARCAACAAIDASIAAhEBAxEB/8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx8vP09fb3+Pn6/8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/8QAtREAAgECBAQDBAcFBAQAAQJ3AAECAxEEBSExBhJBUQdhcRMiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4uPk5ebn6Onq8vP09fb3+Pn6/9oADAMBAAIRAxEAPwDg6KKK988E/9k=';

function makeHarness() {
  const elements = {}, sent = [], downloads = [];
  let deferredPreview = null, failStart = false;
  function element(id = '') {
    return {
      id, value: '', disabled: false, textContent: '', hidden: false, src: '',
      naturalWidth: 0, naturalHeight: 0, children: [], dataset: {}, files: [],
      addEventListener(type, callback) { this[`on${type}`] = callback; },
      appendChild(child) { this.children.push(child); },
      replaceChildren(...children) { this.children = children; },
      removeAttribute(name) { if (name === 'src') this.src = ''; },
      setAttribute(name, value) { this[name] = value; },
      click() {
        if (this.disabled) return undefined;
        if (typeof this.onclick === 'function') return this.onclick();
        if (this.download) downloads.push(this);
        return undefined;
      },
    };
  }
  for (const match of html.matchAll(/\bid="([^"]+)"/g)) elements[match[1]] = element(match[1]);
  const buttons = [...html.matchAll(/<button[^>]*id="([^"]+)"/g)].map((match) => elements[match[1]]);
  async function fetch(url, options = {}) {
    const body = options.body ? JSON.parse(options.body) : null;
    sent.push({ url, method: options.method || 'GET', body });
    if (url === '/api/start' && failStart) return { ok: false, json: async () => ({ error: '本机请求失败' }) };
    if (url === '/api/camera-preview' && deferredPreview) return deferredPreview.promise;
    if (url === '/api/camera-preview') return { ok: true, json: async () => ({
      id: body.id, image_base64: JPEG, valid: true, release_confirmed: true,
    }) };
    return { ok: true, json: async () => ({ job: null }) };
  }
  const context = {
    console, fetch, setTimeout() {}, clearTimeout() {},
    crypto: { randomUUID: () => '01000000-0000-0000-0000-000000000000' },
    location: { hash: '' }, sessionStorage: { getItem: () => '', setItem() {} },
    history: { replaceState() {} }, URL: { createObjectURL() {}, revokeObjectURL() {} }, Blob,
    document: {
      getElementById(id) { return elements[id] || null; },
      querySelectorAll(selector) { return selector === 'button' ? buttons : []; },
      createElement() { return element(); },
    },
  };
  vm.createContext(context);
  vm.runInContext(script, context, { filename: 'app.js' });
  return {
    context, elements, sent, downloads,
    run: (source) => vm.runInContext(source, context),
    deferPreview() {
      deferredPreview = {};
      deferredPreview.promise = new Promise((resolve) => { deferredPreview.resolve = resolve; });
      return deferredPreview;
    },
    failStart() { failStart = true; },
  };
}

async function showAccepted(harness, id = '11'.repeat(16)) {
  harness.context.input = {
    id, operation: 'camera-capture', phase: 'returned',
    result: { state: 'ready', valid: true, release_confirmed: true, preview_available: true },
  };
  await harness.run('show(input)');
  const preview = harness.elements['camera-preview'];
  preview.naturalWidth = 2;
  preview.naturalHeight = 2;
  if (typeof preview.onload === 'function') preview.onload();
}

(async () => {
  const failures = [];
  const expect = (condition, message) => { if (!condition) failures.push(message); };
  {
    const h = makeHarness();
    expect(h.elements['camera-save'], 'missing camera-save control');
    expect(h.elements['camera-clear'], 'missing camera-clear control');
    await showAccepted(h);
    const calls = h.sent.filter((call) => call.url === '/api/camera-preview');
    expect(calls.length === 1, 'accepted capture fetches exactly once');
    expect(calls[0] && calls[0].method === 'POST' && calls[0].body.id === '11'.repeat(16),
      'preview read binds the accepted capture id');
    expect(h.elements['camera-preview'].src.startsWith('data:image/jpeg;base64,'), 'valid JPEG displays after load');
    expect(h.elements['camera-save'] && !h.elements['camera-save'].disabled, 'image load enables save');
    await h.run('show(input)');
    expect(h.sent.filter((call) => call.url === '/api/camera-preview').length === 1, 'polling does not refetch');
  }
  {
    const h = makeHarness();
    const pending = h.deferPreview();
    h.context.loading = { id: '44'.repeat(16), operation: 'camera-capture', phase: 'returned', result: { state: 'ready', valid: true, release_confirmed: true, preview_available: true } };
    const loading = h.run('show(loading)');
    await Promise.resolve();
    expect(!h.elements['camera-clear'].disabled, 'clear remains enabled while preview HTTP read is pending');
    await h.elements['camera-clear'].click();
    pending.resolve({ ok: true, json: async () => ({ id: h.context.loading.id, image_base64: JPEG, valid: true, release_confirmed: true }) });
    await loading;
    expect(!h.elements['camera-preview'].src, 'clear during preview HTTP read prevents a late image');
  }
  {
    const h = makeHarness();
    h.context.loading = { id: '55'.repeat(16), operation: 'camera-capture', phase: 'returned', result: { state: 'ready', valid: true, release_confirmed: true, preview_available: true } };
    await h.run('show(loading)');
    expect(h.elements['camera-preview'].src.startsWith('data:'), 'preview source is assigned before decode completes');
    expect(!h.elements['camera-clear'].disabled, 'clear remains enabled while image decode is pending');
    await h.elements['camera-clear'].click();
    if (typeof h.elements['camera-preview'].onload === 'function') h.elements['camera-preview'].onload();
    expect(!h.elements['camera-preview'].src, 'clear during decode prevents a late image');
  }
  {
    const h = makeHarness();
    for (const [id, result] of [['66'.repeat(16), { state: 'ready', valid: true, release_confirmed: true, preview_available: false }], ['67'.repeat(16), { state: 'ready', valid: false, release_confirmed: true, preview_available: true }], ['68'.repeat(16), { state: 'ready', valid: true, release_confirmed: false, preview_available: true }]]) {
      h.context.unavailable = { id, operation: 'camera-capture', phase: 'returned', result };
      await h.run('show(unavailable)');
      expect(!/有效单帧/.test(h.elements['camera-state'].textContent), 'unavailable capture must not claim a valid preview');
    }
    expect(h.sent.filter((call) => call.url === '/api/camera-preview').length === 0, 'unavailable capture never reads preview');
  }
  {
    const h = makeHarness();
    h.failStart();
    await h.run('submit("camera-capture")');
    expect(!/正在请求/.test(h.elements['camera-state'].textContent), 'failed start does not leave camera requesting');
    expect(/失败|未知/.test(h.elements['camera-state'].textContent), 'failed start gives camera-specific Chinese feedback');
  }
  {
    const h = makeHarness();
    await showAccepted(h);
    await h.run('submit("camera-status")');
    expect(h.elements['camera-preview'].src.startsWith('data:'), 'status preserves displayed preview');
  }
  {
    const h = makeHarness();
    const pending = h.deferPreview();
    h.context.old = { id: '22'.repeat(16), operation: 'camera-capture', phase: 'returned', result: { state: 'ready', valid: true, release_confirmed: true, preview_available: true } };
    const old = h.run('show(old)');
    await Promise.resolve();
    expect(h.sent.filter((call) => call.url === '/api/camera-preview').length === 1,
      'old accepted capture starts one explicit preview read');
    await h.run('submit("camera-capture")');
    pending.resolve({ ok: true, json: async () => ({ id: h.context.old.id, image_base64: JPEG, valid: true, release_confirmed: true }) });
    await old;
    expect(!h.elements['camera-preview'].src, 'old async preview cannot revive after new capture');
  }
  {
    const h = makeHarness();
    await showAccepted(h);
    if (typeof h.elements['camera-preview'].onerror === 'function') {
      h.elements['camera-preview'].onerror();
    } else {
      failures.push('missing image decode-error handler');
    }
    expect(/解码/.test(h.elements.message.textContent), 'decode event provides Chinese failure feedback');
    expect(h.elements['camera-save'] && h.elements['camera-save'].disabled, 'decode failure disables save');
    await h.run('show(input)');
    expect(/解码失败/.test(h.elements.message.textContent), 'same terminal poll preserves decode feedback');
  }
  {
    const h = makeHarness();
    for (const [id, state, feedback] of [
      ['77'.repeat(16), 'expired', /过期/],
      ['78'.repeat(16), 'canceled', /取消/],
      ['79'.repeat(16), 'unknown', /未知/],
    ]) {
      h.context.terminal = { id, operation: 'camera-capture', phase: 'returned', result: { state, preview_available: false } };
      await h.run('show(terminal)');
      await h.run('show(terminal)');
      expect(feedback.test(h.elements.message.textContent), `same terminal poll preserves ${state} feedback`);
    }
    h.context.unavailable = { id: '7a'.repeat(16), operation: 'camera-capture', phase: 'returned', result: { state: 'ready', valid: false, release_confirmed: true, preview_available: false } };
    await h.run('show(unavailable)');
    await h.run('show(unavailable)');
    expect(/不可读取/.test(h.elements.message.textContent), 'same terminal poll preserves unavailable feedback');
  }
  {
    const h = makeHarness();
    await showAccepted(h);
    if (h.elements['camera-save'] && h.elements['camera-clear']) {
      const calls = h.sent.length;
      await h.elements['camera-save'].click();
      expect(h.downloads.length === 1, 'save downloads displayed data URL');
      await h.elements['camera-clear'].click();
      expect(!h.elements['camera-preview'].src, 'clear removes displayed data URL');
      expect(h.sent.length === calls, 'save and clear do not fetch or capture');
    }
  }
  {
    const h = makeHarness();
    for (const state of ['canceled', 'expired', 'unknown', 'unauthorized']) {
      h.context.blocked = { id: '33'.repeat(16), operation: 'camera-capture', phase: state, result: { state, preview_available: true } };
      await h.run('show(blocked)');
    }
    expect(h.sent.filter((call) => call.url === '/api/camera-preview').length === 0, 'terminal failures never fetch a preview');
  }
  assert.deepEqual(failures, []);
  console.log('CONTRACT_PASS actual browser camera preview lifecycle');
})().catch((error) => { console.error(error.stack || error); process.exitCode = 1; });
