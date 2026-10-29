// Reproduce the observed PS251 reprojection/packed-MV selection for this capture.
// shortcut: this is a shader-derived reference field, not validated DLSS input; verify temporal direction and coverage with adjacent frames before runtime use.
const fs = require('fs'), path = require('path'), assert = require('assert/strict');
const [indexFile, directory] = process.argv.slice(2);
assert(indexFile && directory, 'Usage: node analyze-motion-20261028.cjs SOURCE_INDEX CHECKPOINT_DIRECTORY');
const index = JSON.parse(fs.readFileSync(indexFile, 'utf8'));
assert.equal(path.basename(index.capture), 'Ace7Game__2026_10_28__02_30_38');
const draw = index.draws[427], records = fs.readFileSync(path.join(index.capture, 'data.bin.rec'));
const fd = fs.openSync(path.join(index.capture, 'data.bin'), 'r');
function constant(slot) {
  const update = draw.constantUpdates[draw.cb[slot]]; assert(update && Number.isSafeInteger(update.handle));
  const size = Number(records.readBigUInt64LE(update.handle * 16)), offset = Number(records.readBigUInt64LE(update.handle * 16 + 8));
  assert(offset + size <= fs.fstatSync(fd).size);
  const bytes = Buffer.alloc(size); assert.equal(fs.readSync(fd, bytes, 0, size, offset), size);
  return { handle: update.handle, row: i => Array.from({ length: 4 }, (_, k) => bytes.readFloatLE(i * 16 + k * 4)) };
}
const view = constant(1), params = constant(0), matrix = [110, 111, 112, 113].map(i => view.row(i));
const screen = params.row(16), extent = params.row(0), jitter = view.row(114);
assert.deepEqual(extent.slice(0, 2), [5120, 1440]);
assert.deepEqual(screen, [1280, -720, 2559.5, 719.5]);
assert(matrix.flat().every(Number.isFinite));
const motion = fs.readFileSync(path.join(directory, 'velocity.raw')), depth = fs.readFileSync(path.join(directory, 'depth.raw'));
const width = motion.readUInt32LE(0), height = motion.readUInt32LE(4);
assert.equal(width, 5120); assert.equal(height, 1440);
assert.equal(motion.readUInt32LE(8), 35); assert.equal(depth.readUInt32LE(8), 19);
assert.equal(motion.length, 16 + width * height * 4); assert.equal(depth.length, 16 + width * height * 8);
function fallback(x, y, z, m) {
  const cx = (x - screen[2]) / screen[0], cy = (y - screen[3]) / screen[1];
  const previous = [0, 1, 3].map(k => cx * m[0][k] + cy * m[1][k] + z * m[2][k] + m[3][k]);
  return [cx - previous[0] / previous[2], cy - previous[1] / previous[2]];
}
assert.deepEqual(fallback(2560, 720, 0.5, [[1,0,0,0],[0,1,0,0],[0,0,1,0],[0,0,0,1]]), [0,0]);
const encode = v => v * 0.2495 + 0.499992;
const decode = v => (v / 65535) * 4.008016 - 2.003978;
assert(Math.abs(decode(Math.round(encode(0) * 65535))) < 0.0001);
const output = Buffer.alloc(16 + width * height * 8);
[width, height, 16, 8].forEach((v, i) => output.writeUInt32LE(v, i * 4));
const groups = { packed: { count: 0, minX: Infinity, maxX: -Infinity, minY: Infinity, maxY: -Infinity, samples: [] }, camera: { count: 0, minX: Infinity, maxX: -Infinity, minY: Infinity, maxY: -Infinity, samples: [] } };
let minX = width, minY = height, maxX = -1, maxY = -1;
for (let y = 0; y < height; y++) for (let x = 1280; x < 3840; x++) {
  const pixel = y * width + x, at = 16 + pixel * 4;
  const rx = motion.readUInt16LE(at), ry = motion.readUInt16LE(at + 2), z = depth.readFloatLE(16 + pixel * 8);
  assert(Number.isFinite(z));
  const packed = rx > 0, velocity = packed ? [decode(rx), decode(ry)] : fallback(x, y, z, matrix);
  assert(velocity.every(Number.isFinite), 'Nonfinite reconstructed motion');
  output.writeFloatLE(velocity[0], 16 + pixel * 8); output.writeFloatLE(velocity[1], 20 + pixel * 8);
  const px = velocity[0] * 1280, py = velocity[1] * -720, group = groups[packed ? 'packed' : 'camera'];
  group.count++; group.minX = Math.min(group.minX, px); group.maxX = Math.max(group.maxX, px); group.minY = Math.min(group.minY, py); group.maxY = Math.max(group.maxY, py);
  if (pixel % 8 === 0) group.samples.push(Math.hypot(px, py));
  if (packed) { minX = Math.min(minX, x); maxX = Math.max(maxX, x); minY = Math.min(minY, y); maxY = Math.max(maxY, y); }
}
for (const group of Object.values(groups)) {
  group.samples.sort((a, b) => a - b);
  group.magnitudePixels = Object.fromEntries([0.5, 0.95, 0.99].map(q => [String(q), group.samples[Math.floor((group.samples.length - 1) * q)]]));
  delete group.samples;
}
fs.writeFileSync(path.join(directory, 'reference-motion-ndc.raw'), output);
const result = { consumerDraw: 427, viewBlob: view.handle, paramsBlob: params.handle, jitterCurrentPrevious: jitter, currentClipToPreviousClipCandidate: matrix, packedPixelBounds: { minX, maxX, minY, maxY }, groups, direction: 'Observed shader computes current NDC minus previous NDC; pixel estimates multiply by (1280,-720), the active view half extent.', limitations: 'Shader-derived reference, not independently verified temporal ground truth. Transparent clouds/water/particles and disocclusions may lack object motion. Zero jitter in this frame does not prove jitter is always disabled. Output is full allocation with zeros outside the valid rect.' };
fs.writeFileSync(path.join(directory, 'motion-analysis.json'), JSON.stringify(result, null, 2));
const parameters = Buffer.alloc(80);
[1280, 0, 2560, 1440].forEach((v, i) => parameters.writeUInt32LE(v, i * 4));
matrix.flat().forEach((v, i) => parameters.writeFloatLE(v, 16 + i * 4));
fs.writeFileSync(path.join(directory, 'normalizer-parameters.bin'), parameters);
fs.closeSync(fd);
console.log(JSON.stringify(result, null, 2));
