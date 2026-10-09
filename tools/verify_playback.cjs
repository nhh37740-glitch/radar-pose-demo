// Server-side control regression harness. Fixtures stay outside the public data pack.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const root = path.resolve(__dirname, "..");
const sample = JSON.parse(fs.readFileSync(path.join(root, "web/data/pose-excerpt.js"), "utf8")
  .trim().replace(/^window\.RADAR_POSE_METHOD_DATA=/, "").replace(/;$/, ""));
// Unique mock timestamps identify requested frame numbers; real website assets are untouched.
const rows = Array.from({ length: 7203 }, (_, index) => {
  const row = sample.frames[index % sample.frames.length].slice();
  row[0] = index;
  row[1] = 1000000 + index;
  row[31] = 2000000 + index;
  return row;
});
const pages = Array.from({ length: 31 }, (_, index) => ({
  startFrame: index * 240, count: Math.min(240, rows.length - index * 240),
  file: `chunks/page-${String(index).padStart(5, "0")}.json`,
}));
const manifest = {
  formatVersion: 1, pageSize: 240, pages,
  metadata: { sampleCount: rows.length, recordedDurationSeconds: 1800.02 },
  overviewRows: [rows[0], rows.at(-1)],
  globalBounds: { minNorth: 5735000, maxNorth: 5737000, minEast: 619000, maxEast: 621000 },
};
const drawing = new Proxy({}, { get: () => () => {} });
class Element {
  constructor() { this.events = {}; this.textContent = ""; this.value = "0"; this.disabled = false; this.width = 600; this.height = 500; this.hidden = false; this.classList = { add() {}, toggle() {} }; }
  addEventListener(name, fn) { this.events[name] = fn; }
  setAttribute(name, value) { this[name] = value; }
  removeAttribute(name) { delete this[name]; }
  getContext() { return drawing; }
  remove() {}
}
const elements = new Map();
const el = id => {
  if (!elements.has(id)) elements.set(id, new Element());
  return elements.get(id);
};
let now = 0;
let animation;
let nextTimer = 0;
const timers = new Map();
const imageRequests = [];
const delayedImages = new Set([0]);
const pendingImages = new Map();
const failedImages = new Set(["radar:1", "radar:30"]);
const delayedPages = new Set();
const failedPages = new Set();
const pendingPages = new Map();
function imageFrame(src) {
  const match = /\/(radar|stereo)\/(\d+)\.jpg$/.exec(src);
  return match ? { sensor: match[1], frame: Number(match[2]) - (match[1] === "radar" ? 1000000 : 2000000) } : null;
}
class MockImage {
  set src(value) {
    this.url = value;
    const requested = imageFrame(value);
    if (!requested) return;
    imageRequests.push(requested);
    const settle = () => {
      if (failedImages.has(`${requested.sensor}:${requested.frame}`)) this.onerror?.();
      else this.onload?.();
    };
    if (delayedImages.has(requested.frame)) {
      if (!pendingImages.has(requested.frame)) pendingImages.set(requested.frame, []);
      pendingImages.get(requested.frame).push(settle);
    } else queueMicrotask(settle);
  }
  get src() { return this.url; }
}
const context = {
  document: { getElementById: el, createElement: () => new Element(), head: { appendChild() {} } },
  window: { addEventListener() {}, requestAnimationFrame: fn => { animation = fn; } },
  console: { error() {}, warn() {} }, Image: MockImage, AbortController,
  performance: { now: () => now },
  setTimeout: (fn, delay) => { const id = ++nextTimer; timers.set(id, { fn, due: now + delay }); return id; },
  clearTimeout: id => timers.delete(id),
  fetch: async (url, options) => {
    if (url.endsWith("manifest.json")) return { ok: true, json: async () => manifest };
    const page = Number(/page-(\d+)\.json/.exec(url)[1]);
    if (failedPages.has(page)) return { ok: false, status: 404 };
    if (delayedPages.has(page)) await new Promise((resolve, reject) => {
      pendingPages.set(page, resolve);
      options.signal.addEventListener("abort", () => reject(new Error("Data request timed out")));
    });
    return { ok: true, json: async () => ({ startFrame: pages[page].startFrame, frames: rows.slice(page * 240, page * 240 + pages[page].count) }) };
  },
};
vm.createContext(context);
vm.runInContext(fs.readFileSync(path.join(root, "web/config.js"), "utf8"), context);
vm.runInContext(fs.readFileSync(path.join(root, "web/app.js"), "utf8"), context);
async function flush() { for (let count = 0; count < 8; count += 1) await new Promise(resolve => setImmediate(resolve)); }
async function advance(ms) {
  now += ms;
  for (const [id, timer] of [...timers]) if (timer.due <= now) { timers.delete(id); timer.fn(); }
  animation(now);
  await flush();
}
async function click() { el("playback-toggle").events.click(); await flush(); }
async function seek(frame) { el("frame-seek").value = String(frame); el("frame-seek").events.change(); await flush(); }
async function releaseImages(frame) {
  delayedImages.delete(frame);
  (pendingImages.get(frame) || []).forEach(fn => fn());
  pendingImages.delete(frame);
  await flush();
}
const frame = () => Number(/^Frame (\d+)/.exec(el("frame-label").textContent)?.[1] ?? -1);

(async () => {
  await flush();
  assert.equal(el("playback-toggle").disabled, false, "controls work before initial images load");
  await click();
  assert.equal(el("playback-toggle").textContent, "Resume");
  await releaseImages(0);
  await advance(1000);
  assert.equal(frame(), 0, "initial loading cannot override paused state");
  await click();
  await advance(99);
  assert.equal(frame(), 0, "resume resets interval instead of jumping immediately");
  await advance(1);
  assert.equal(frame(), 10);
  assert.deepEqual([...new Set(imageRequests.map(item => item.frame))], [0, 10], "skipped/preload404 frame1 is never requested");

  delayedImages.add(20);
  await advance(100);
  assert.equal(frame(), 10);
  await click();
  await releaseImages(20);
  await advance(1000);
  assert.equal(frame(), 10, "late images cannot advance a paused display");
  await click();
  await advance(99);
  assert.equal(frame(), 10);
  await advance(1);
  assert.equal(frame(), 20);

  await advance(100);
  assert.equal(frame(), 30);
  assert.equal(el("radar-image").hidden, true, "failed current sensor clears old image");
  assert.equal(el("stereo-image").hidden, false);
  assert.match(el("radar-image-status").textContent, /unavailable/);
  assert.equal(el("playback-toggle").disabled, false, "active image404 never disables control");
  await click(); await click();
  await advance(100);
  assert.equal(frame(), 40);
  assert.equal(el("radar-image").hidden, false);
  assert.equal(el("radar-image-status").hidden, true, "later successful frame clears missing-image state");

  await seek(239); await click(); await advance(100);
  assert.equal(frame(), 249, "10-frame advance crosses pose-page boundary");
  delayedPages.add(2);
  await seek(480);
  await seek(7202);
  pendingPages.get(2)(); delayedPages.delete(2); await flush();
  assert.equal(frame(), 7202, "stale page cannot override a more recent seek");
  await seek(7200); await click(); await advance(100);
  assert.equal(frame(), 7202, "step clamps to final frame rather than skipping it");
  await advance(100);
  assert.equal(frame(), 0, "final frame wraps to zero");

  delayedPages.add(1);
  await advance(100); // frame10
  el("frame-seek").value = "240";
  el("frame-seek").events.change(); await flush();
  await click(); await click(); // resume then pause while page is loading
  pendingPages.get(1)(); delayedPages.delete(1); await flush();
  assert.equal(frame(), 10, "pause also invalidates a pending page load");

  failedPages.add(2);
  await seek(480);
  assert.equal(el("playback-toggle").textContent, "Resume");
  assert.match(el("status").textContent, /Resume to retry/);
  failedPages.delete(2);
  await click();
  assert.equal(frame(), 480, "resume retries the failed target page");

  delayedImages.add(490);
  await advance(100);
  await advance(15000);
  assert.equal(frame(), 490, "image timeout recovers instead of locking playback");
  assert.equal(el("playback-toggle").disabled, false);
  await advance(100);
  assert.equal(frame(), 500);
  assert.equal(el("radar-image-status").hidden, true);
  console.log("PASS: target-only frameStep10, preload404 avoidance, active404 recovery, pause during image/page/initial load, stale seek, resume timing, final-frame wrap, page retry, timeout recovery");
})().catch(error => { console.error(error); process.exitCode = 1; });
