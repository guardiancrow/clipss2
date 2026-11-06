// Copyright (c) 2026 guardiancrow. MIT.
// Node.js built-ins + an installed Chromium-based browser; no downloads.
import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawn } from 'node:child_process';
import { inflateSync } from 'node:zlib';
import { createHash } from 'node:crypto';
import assert from 'node:assert/strict';
import { parseArgs } from 'node:util';

const assetRoot = path.dirname(fileURLToPath(import.meta.url));
const { values } = parseArgs({ options: { output: { type: 'string' } } });
assert.ok(values.output, 'Usage: node src/platform/windows/assets/generate.mjs --output build/<fresh-directory>');
const root = path.resolve(values.output);
const buildRoot = path.resolve(assetRoot, '../../../..', 'build');
const relativeOutput = path.relative(buildRoot, root);
assert.ok(relativeOutput && !relativeOutput.startsWith('..') && !path.isAbsolute(relativeOutput), 'Output must be a new directory under build/');
const sizes = [16, 24, 32, 48, 64, 128, 256];
const browser = process.env.ICON_BROWSER ?? path.join(process.env['ProgramFiles(x86)'] ?? process.env.ProgramFiles ?? '', 'Microsoft/Edge/Application/msedge.exe');
const profile = path.resolve(root, '..', `icon-renderer-${process.pid}`);
await fs.access(browser);
await fs.mkdir(path.dirname(root), { recursive: true });
await fs.mkdir(root); // Reject existing output, even if it is empty.
await fs.mkdir(path.join(root, 'master'));
await fs.copyFile(path.join(assetRoot, 'clipss2.svg'), path.join(root, 'master', 'clipss2.svg'));
for (const dir of ['png', 'ico', 'preview', 'notes']) await fs.mkdir(path.join(root, dir), { recursive: true });
await fs.mkdir(profile); // Never reuse another browser session's profile.
const child = spawn(browser, [
  '--headless=new', '--disable-gpu', '--no-first-run', '--no-default-browser-check',
  '--disable-extensions', '--disable-background-networking', '--disable-component-update',
  '--remote-debugging-port=0', '--remote-debugging-address=127.0.0.1',
  `--user-data-dir=${profile}`, 'about:blank',
], { windowsHide: true, stdio: 'ignore' });
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
let socket;
const pending = new Map();
let serial = 0;
const command = (method, params = {}) => new Promise((resolve, reject) => {
  const id = ++serial;
  const timer = setTimeout(() => { pending.delete(id); reject(new Error(`CDP timeout: ${method}`)); }, 20000);
  pending.set(id, { resolve, reject, timer });
  socket.send(JSON.stringify({ id, method, params }));
});
async function evaluate(expression) {
  const result = await command('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true });
  if (result.exceptionDetails) throw new Error(JSON.stringify(result.exceptionDetails));
  return result.result.value;
}
async function render(html, width, height) {
  await command('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: 1, mobile: false });
  await command('Emulation.setDefaultBackgroundColorOverride', { color: { r: 0, g: 0, b: 0, a: 0 } });
  await evaluate(`(async () => {
    document.open(); document.write(${JSON.stringify(html)}); document.close();
    await Promise.all([...document.images].map(img => img.decode()));
    await document.fonts.ready;
    await new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));
    return true;
  })()`);
  const result = await command('Page.captureScreenshot', { format: 'png', clip: { x: 0, y: 0, width, height, scale: 1 } });
  return Buffer.from(result.data, 'base64');
}

// Decode the browser's 8-bit RGB/RGBA PNG independently of the renderer.
function decodePng(buffer) {
  assert.equal(buffer.subarray(0, 8).toString('hex'), '89504e470d0a1a0a');
  let offset = 8, width, height, channels;
  const chunks = [];
  while (offset < buffer.length) {
    const length = buffer.readUInt32BE(offset);
    const type = buffer.toString('ascii', offset + 4, offset + 8);
    const data = buffer.subarray(offset + 8, offset + 8 + length);
    if (type === 'IHDR') {
      width = data.readUInt32BE(0); height = data.readUInt32BE(4);
      assert.equal(data[8], 8); assert.equal(data[12], 0);
      assert.ok([2, 6].includes(data[9])); channels = data[9] === 6 ? 4 : 3;
    }
    if (type === 'IDAT') chunks.push(data);
    offset += length + 12;
  }
  const raw = inflateSync(Buffer.concat(chunks));
  const stride = width * channels;
  assert.equal(raw.length, (stride + 1) * height);
  const pixels = Buffer.alloc(stride * height);
  const paeth = (a, b, c) => {
    const p = a + b - c, da = Math.abs(p - a), db = Math.abs(p - b), dc = Math.abs(p - c);
    return da <= db && da <= dc ? a : db <= dc ? b : c;
  };
  for (let y = 0; y < height; y++) {
    const filter = raw[y * (stride + 1)];
    assert.ok(filter <= 4);
    for (let x = 0; x < stride; x++) {
      const index = y * stride + x;
      const a = x >= channels ? pixels[index - channels] : 0;
      const b = y ? pixels[index - stride] : 0;
      const c = y && x >= channels ? pixels[index - stride - channels] : 0;
      const predictor = [0, a, b, Math.floor((a + b) / 2), paeth(a, b, c)][filter];
      pixels[index] = (raw[y * (stride + 1) + 1 + x] + predictor) & 255;
    }
  }
  return { width, height, pixel(x, y) {
    const i = (y * width + x) * channels;
    return [...pixels.subarray(i, i + 3), channels === 4 ? pixels[i + 3] : 255];
  } };
}
function validatePng(bytes, size) {
  const image = decodePng(bytes);
  assert.equal(image.width, size); assert.equal(image.height, size);
  let transparent = 0, partial = 0;
  for (let y = 0; y < size; y++) for (let x = 0; x < size; x++) {
    const [r, g, b, a] = image.pixel(x, y);
    if (!a) transparent++;
    if (a > 0 && a < 255) {
      partial++;
      // Only the deep-blue plate touches transparency: reject white/cyan matte.
      // Allow one premultiplied 8-bit quantum near alpha=0 (unpremultiplication
      // can otherwise turn a legitimate deep-blue edge into e.g. 0,0,255,1).
      assert.ok(r * a <= 30 * a + 255 && g * a <= 90 * a + 255 && b * a <= 150 * a + 255,
        `Unexpected edge color at ${size}: ${r},${g},${b},${a}`);
    }
    if (x === 0 || y === 0 || x === size - 1 || y === size - 1) assert.equal(a, 0);
  }
  const center = image.pixel(Math.floor(size / 2), Math.floor(size / 2));
  assert.equal(center[3], 255); assert.ok(center[0] < 30 && center[1] < 90 && center[2] > 50);
  const at = (x, y) => image.pixel(Math.floor(x * size / 256), Math.floor(y * size / 256));
  const tl = at(68, 68), tr = at(188, 68), bl = at(68, 188), br = at(188, 188);
  assert.ok(tl[0] > 150 && br[0] > 150, 'White diagonal must remain visible');
  assert.ok(tr[1] > 150 && bl[1] > 150 && tr[0] < 150 && bl[0] < 150, 'Cyan diagonal must remain visible');
  // All four open gaps should show opaque blue, even at 16 px.
  for (const [x, y] of [[128, 64], [64, 128], [192, 128], [128, 192]]) {
    const gap = at(x, y);
    assert.equal(gap[3], 255); assert.ok(gap[0] < 40 && gap[1] < 100);
  }
  return { size, mode: 'RGBA', transparentPixels: transparent, antialiasedEdgePixels: partial,
    perimeterTransparent: true, centerOpaqueBlue: true, edgeWithoutWhiteMatte: true,
    fourFramesVisible: true, fourGapsOpen: true };
}
function makeIco(images) {
  const header = Buffer.alloc(6 + images.length * 16);
  header.writeUInt16LE(1, 2); header.writeUInt16LE(images.length, 4);
  let position = header.length;
  images.forEach(({ size, bytes }, index) => {
    const i = 6 + index * 16;
    header[i] = header[i + 1] = size === 256 ? 0 : size;
    header.writeUInt16LE(1, i + 4); header.writeUInt16LE(32, i + 6);
    header.writeUInt32LE(bytes.length, i + 8); header.writeUInt32LE(position, i + 12);
    position += bytes.length;
  });
  return Buffer.concat([header, ...images.map(image => image.bytes)]);
}
function previewHtml(images, smallOnly = false) {
  const shown = smallOnly ? images.slice(0, 3) : images;
  const cells = (dark) => shown.map(({ size, bytes }) => {
    const src = `data:image/png;base64,${bytes.toString('base64')}`;
    const zoom = size <= 32 ? `<img class="zoom" src="${src}" width="${size * 6}" height="${size * 6}" alt="${size} px, 6x nearest-neighbor">` : '';
    return `<div class="cell"><b>${size} × ${size}</b><div class="actual"><img src="${src}" width="${size}" height="${size}" alt="${size} px native"></div>${zoom}<span>${size <= 32 ? 'Native above · 6× pixels below' : 'Native size'}</span></div>`;
  }).join('');
  return `<!doctype html><html lang="en"><meta charset="utf-8"><title>clipss2 icon asset review</title>
  <style>*{box-sizing:border-box}body{margin:0;padding:24px;background:#e8edf3;color:#172b40;font:14px Arial,sans-serif}h1{font-size:22px;margin:0 0 8px}p{margin:0 0 20px}section{padding:20px;border-radius:12px;margin-bottom:16px}h2{font-size:16px;margin:0 0 16px}.light{background:#f5f7fa}.dark{background:#151c28;color:#eef3fa}.grid{display:grid;grid-template-columns:${smallOnly ? 'repeat(3,1fr)' : 'repeat(4,1fr)'};gap:12px}.cell{min-width:0;display:flex;align-items:center;flex-direction:column;gap:12px;padding:12px;border:1px solid #8594a544;border-radius:8px}.actual{height:${smallOnly ? 48 : 268}px;display:flex;align-items:center;justify-content:center}.zoom{image-rendering:pixelated}span{font-size:11px;opacity:.8}footer{font-size:12px;margin:8px 0}img{display:block}</style>
  <h1>clipss2 · icon asset review</h1><p>Original capture frame · transparent outer margin · white/cyan diagonals · no size-specific variants</p>
  <section class="light"><h2>Light background · #F5F7FA</h2><div class="grid">${cells(false)}</div></section>
  <section class="dark"><h2>Dark background · #151C28</h2><div class="grid">${cells(true)}</div></section>
  <footer>Native sizes are CSS pixels at 100% browser zoom. Enlarged small icons use nearest-neighbor pixels for inspection.<br>Generated asset previews · verify Windows integration separately.</footer></html>`;
}

try {
  let port;
  for (let attempt = 0; attempt < 200; attempt++) {
    try { port = Number((await fs.readFile(path.join(profile, 'DevToolsActivePort'), 'utf8')).split('\n')[0]); break; }
    catch { if (child.exitCode !== null) throw new Error(`Browser exited ${child.exitCode}`); await delay(100); }
  }
  assert.ok(port, 'Browser did not start');
  const targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
  const target = targets.find(target => target.type === 'page');
  assert.ok(target?.webSocketDebuggerUrl);
  socket = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((resolve, reject) => { socket.onopen = resolve; socket.onerror = reject; });
  socket.onerror = event => console.error('Browser connection error:', event.message);
  socket.onclose = event => {
    for (const [id, item] of pending) {
      clearTimeout(item.timer); item.reject(new Error(`Browser connection closed: ${event.code}`)); pending.delete(id);
    }
  };
  socket.onmessage = ({ data }) => {
    const message = JSON.parse(data);
    const item = pending.get(message.id);
    if (!item) return;
    pending.delete(message.id); clearTimeout(item.timer);
    if (message.error) item.reject(new Error(JSON.stringify(message.error))); else item.resolve(message.result);
  };
  const version = await command('Browser.getVersion');
  await command('Page.enable'); await command('Runtime.enable');
  const svg = await fs.readFile(path.join(root, 'master', 'clipss2.svg'), 'utf8');
  const svgCheck = await evaluate(`(() => {
    const doc = new DOMParser().parseFromString(${JSON.stringify(svg)}, 'image/svg+xml');
    return { valid: !doc.querySelector('parsererror'), root: doc.documentElement.localName,
      viewBox: doc.documentElement.getAttribute('viewBox'), textElements: doc.querySelectorAll('text').length,
      externalElements: doc.querySelectorAll('image,script,foreignObject,use').length,
      paths: doc.querySelectorAll('path').length };
  })()`);
  assert.deepEqual(svgCheck, { valid: true, root: 'svg', viewBox: '0 0 256 256', textElements: 0, externalElements: 0, paths: 4 });
  const images = [], pngChecks = [];
  for (const size of sizes) {
    const html = `<html><style>html,body{margin:0;padding:0;background:transparent}img{display:block}</style><img width="${size}" height="${size}" src="data:image/svg+xml;base64,${Buffer.from(svg).toString('base64')}"></html>`;
    const bytes = await render(html, size, size);
    pngChecks.push(validatePng(bytes, size));
    await fs.writeFile(path.join(root, 'png', `clipss2-${size}.png`), bytes);
    images.push({ size, bytes });
  }
  const ico = makeIco(images);
  assert.equal(ico.readUInt16LE(0), 0); assert.equal(ico.readUInt16LE(2), 1); assert.equal(ico.readUInt16LE(4), 7);
  const icoChecks = images.map(({ size, bytes }, index) => {
    const i = 6 + index * 16;
    assert.equal(ico[i] || 256, size); assert.equal(ico[i + 1] || 256, size);
    const length = ico.readUInt32LE(i + 8), offset = ico.readUInt32LE(i + 12);
    assert.deepEqual(ico.subarray(offset, offset + length), bytes);
    return { size, format: 'PNG', bitDepth: 32, exactPngMatch: true };
  });
  await fs.writeFile(path.join(root, 'ico', 'clipss2.ico'), ico);
  for (const [name, smallOnly, width] of [['all-sizes', false, 1280], ['small-sizes', true, 960]]) {
    const html = previewHtml(images, smallOnly);
    await fs.writeFile(path.join(root, 'preview', `${name}.html`), html);
    // Measure after layout; PNG includes the full page without scrollbars.
    await render(html, width, 1000);
    const height = await evaluate('Math.ceil(document.documentElement.scrollHeight)');
    const png = await render(html, width, height);
    await fs.writeFile(path.join(root, 'preview', `${name}.png`), png);
  }
  const report = { result: 'PASS', renderer: version.product, node: process.version,
    svg: svgCheck, png: pngChecks, ico: icoChecks, sizeSpecificAdjustments: 'none',
    visualInspection: 'Automated checks do not replace Owner visual review.' };
  await fs.writeFile(path.join(root, 'notes', 'validation.json'), JSON.stringify(report, null, 2) + '\n');
  const manifest = [];
  for (const dir of ['master', 'png', 'ico', 'preview']) for (const filename of (await fs.readdir(path.join(root, dir))).sort()) {
    const relative = `${dir}/${filename}`, bytes = await fs.readFile(path.join(root, relative));
    manifest.push(`${createHash('sha256').update(bytes).digest('hex')}  ${relative}`);
  }
  await fs.writeFile(path.join(root, 'notes', 'SHA256SUMS.txt'), manifest.join('\n') + '\n');
  console.log(JSON.stringify({ result: 'PASS', renderer: version.product, pngSizes: sizes, icoEntries: icoChecks.length, previews: ['all-sizes', 'small-sizes'] }));
} finally {
  if (socket?.readyState === WebSocket.OPEN) {
    try { await command('Browser.close'); } catch { /* Closing may drop the reply. */ }
    socket.close();
  }
  for (const item of pending.values()) clearTimeout(item.timer);
  if (child.exitCode === null) await Promise.race([new Promise(resolve => child.once('exit', resolve)), delay(3000)]);
  if (child.exitCode === null) child.kill();
  // Verify the exact temporary directory before recursive removal.
  const buildRoot = path.resolve(root, '..');
  assert.equal(path.dirname(profile), buildRoot);
  assert.equal(path.basename(profile), `icon-renderer-${process.pid}`);
  await fs.rm(profile, { recursive: true, force: true, maxRetries: 10, retryDelay: 300 });
}
