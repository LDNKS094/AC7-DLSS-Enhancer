const fs = require('fs'), path = require('path'), assert = require('assert/strict');
const [directory, screenshot, manifestPath] = process.argv.slice(2);
assert(directory && screenshot, 'Usage: node check-readbacks.cjs READBACK_DIRECTORY ORIGINAL_SCREENSHOT_BMP [MANIFEST_JSON]');
const config = manifestPath ? JSON.parse(fs.readFileSync(manifestPath, 'utf8')).readbacks : {
  final: '1443-final', composite: '1437-composite', velocity: '1401-velocity', depth: '1401-depth',
  previews: ['1427-post-tonemap', '1428-post-spatial-aa', '1437-composite', '1443-final']
};
function load(name) {
  const b = fs.readFileSync(path.join(directory, name + '.raw'));
  const [w, h, format, bpp] = [0, 4, 8, 12].map(i => b.readUInt32LE(i));
  assert.equal(b.length, 16 + w * h * bpp);
  return { b, w, h, format, bpp };
}
function rgb(image, x, y) {
  const offset = 16 + (y * image.w + x) * image.bpp;
  if (image.format === 90) return [image.b[offset + 2], image.b[offset + 1], image.b[offset]];
  assert.equal(image.format, 24);
  const value = image.b.readUInt32LE(offset);
  return [value & 1023, (value >>> 10) & 1023, (value >>> 20) & 1023].map(v => Math.round(v * 255 / 1023));
}
function preview(image, name) {
  const w = 1600, h = Math.round(image.h * w / image.w), stride = Math.ceil(w * 3 / 4) * 4;
  const b = Buffer.alloc(54 + stride * h);
  b.write('BM'); b.writeUInt32LE(b.length, 2); b.writeUInt32LE(54, 10); b.writeUInt32LE(40, 14);
  b.writeInt32LE(w, 18); b.writeInt32LE(h, 22); b.writeUInt16LE(1, 26); b.writeUInt16LE(24, 28);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    const color = rgb(image, Math.floor(x * image.w / w), Math.floor(y * image.h / h));
    const offset = 54 + (h - y - 1) * stride + x * 3;
    b[offset] = color[2]; b[offset + 1] = color[1]; b[offset + 2] = color[0];
  }
  fs.writeFileSync(path.join(directory, name + '-preview.bmp'), b);
}
const final = load(config.final), composite = load(config.composite);
for (const name of config.previews) preview(load(name), name);
const original = fs.readFileSync(screenshot), offset = original.readUInt32LE(10), originalW = original.readInt32LE(18), originalH = original.readInt32LE(22);
assert.equal(original.readUInt16LE(28), 24); assert.equal(original.readUInt32LE(30), 0);
assert.equal(originalW, final.w); assert.equal(Math.abs(originalH), final.h);
const stride = Math.ceil(final.w * 3 / 4) * 4;
let sum = 0, max = 0, overOne = 0, changedByLastDraws = 0;
for (let y = 0; y < final.h; y++) for (let x = 0; x < final.w; x++) {
  const c = rgb(final, x, y), p = offset + (originalH > 0 ? final.h - y - 1 : y) * stride + x * 3;
  for (let k = 0; k < 3; k++) { const error = Math.abs(c[k] - original[p + 2 - k]); sum += error; max = Math.max(max, error); if (error > 1) overOne++; }
  const pos = 16 + (y * final.w + x) * 4;
  if (final.b.readUInt32LE(pos) !== composite.b.readUInt32LE(pos)) changedByLastDraws++;
}
const velocity = load(config.velocity); assert.equal(velocity.format, 35);
let nonzeroVelocityPixels = 0, outsideVelocityPixels = 0;
const rect = config.validRect || { x: 0, y: 0, width: velocity.w, height: velocity.h };
assert(rect.x >= 0 && rect.y >= 0 && rect.width > 0 && rect.height > 0 && rect.x + rect.width <= velocity.w && rect.y + rect.height <= velocity.h);
for (let p = 16; p < velocity.b.length; p += 4) if (velocity.b.readUInt32LE(p)) {
  nonzeroVelocityPixels++;
  const index = (p - 16) / 4, x = index % velocity.w, y = Math.floor(index / velocity.w);
  if (x < rect.x || x >= rect.x + rect.width || y < rect.y || y >= rect.y + rect.height) outsideVelocityPixels++;
}
const depth = load(config.depth); assert.equal(depth.format, 19);
let depthMin = Infinity, depthMax = -Infinity, nonfiniteDepth = 0;
for (let p = 16; p < depth.b.length; p += 8) { const v = depth.b.readFloatLE(p); if (!Number.isFinite(v)) nonfiniteDepth++; else { depthMin = Math.min(depthMin, v); depthMax = Math.max(depthMax, v); } }
const result = { screenshotComparison: { width: final.w, height: final.h, meanAbsoluteRgbError8Bit: sum / (final.w * final.h * 3), maxChannelError8Bit: max, channelsWithErrorAboveOne: overOne }, pixelsChangedAfterComposite: changedByLastDraws, nonzeroVelocityPixels, outsideVelocityPixels, validRect: rect, depth: { min: depthMin, max: depthMax, nonfinite: nonfiniteDepth }, note: '8-bit comparison uses nearest conversion from R10G10B10A2. A small quantization difference is expected; no shader tone mapping was added.' };
assert.equal(result.screenshotComparison.channelsWithErrorAboveOne, 0, 'Replay image differs from captured screenshot beyond 8-bit quantization');
fs.writeFileSync(path.join(directory, 'checks.json'), JSON.stringify(result, null, 2));
console.log(JSON.stringify(result, null, 2));
