// One synthetic Reset frame distinguishes spatial response from fixed HDR amplitude compression.
const fs = require('fs'), path = require('path'), assert = require('assert/strict'), crypto = require('crypto');
const hash = b => crypto.createHash('sha256').update(b).digest('hex');
const w = 2560, h = 1440;
const plates = [{ value: 0.125, bits: 0x3000 }, { value: 1, bits: 0x3c00 }, { value: 10, bits: 0x4900 }, { value: 100, bits: 0x5640 }, { value: 166, bits: 0x5930 }];
const spots = [1, 3, 9, 65, 129].map((side, i) => ({ side, center: [128 + 512 * i, 1080], value: 160, bits: 0x5900 }));
function half(bits) {
    const e = (bits >>> 10) & 31, m = bits & 1023;
    return (bits & 32768 ? -1 : 1) * (e === 31 ? (m ? NaN : Infinity) : e ? (1 + m / 1024) * 2 ** (e - 15) : m * 2 ** -24);
}
for (const item of [...plates, ...spots]) assert.equal(half(item.bits), item.value);
function raw(file) {
    const b = fs.readFileSync(file);
    assert.equal(b.length, 16 + w * h * 8);
    assert.deepEqual(Array.from({ length: 4 }, (_, i) => b.readUInt32LE(i * 4)), [w, h, 10, 8]);
    return b;
}
function pixel(b, x, y) { return Array.from({ length: 3 }, (_, c) => half(b.readUInt16LE(16 + (y * w + x) * 8 + c * 2))); }
function set(b, x, y, bits) {
    const at = 16 + (y * w + x) * 8;
    for (let c = 0; c < 3; ++c) b.writeUInt16LE(bits, at + c * 2);
    b.writeUInt16LE(0x3c00, at + 6);
}
const [mode, input, output, report] = process.argv.slice(2);
assert(['prepare', 'check'].includes(mode), 'Usage: sr_spatial_control.cjs prepare BASELINE_INPUT NEW_INPUT | check CONTROL_INPUT GPU_OUTPUT NEW_REPORT_DIR');
if (mode === 'prepare') {
    assert.equal(process.argv.length, 5); assert(!fs.existsSync(output), 'Refusing to overwrite control input');
    const base = raw(path.join(input, 'color.raw')), b = Buffer.alloc(base.length); base.copy(b, 0, 0, 16);
    for (let y = 0; y < h; ++y) for (let x = 0; x < w; ++x) set(b, x, y, y < 720 ? plates[Math.floor(x / 512)].bits : plates[0].bits);
    for (const spot of spots) { const r = (spot.side - 1) / 2; for (let y = spot.center[1] - r; y <= spot.center[1] + r; ++y) for (let x = spot.center[0] - r; x <= spot.center[0] + r; ++x) set(b, x, y, spot.bits); }
    for (let i = 0; i < plates.length; ++i) assert.deepEqual(pixel(b, 256 + i * 512, 360), Array(3).fill(plates[i].value));
    for (const spot of spots) assert.deepEqual(pixel(b, ...spot.center), [160, 160, 160]);
    fs.mkdirSync(output, { recursive: true }); fs.writeFileSync(path.join(output, 'color.raw'), b);
    const guideHashes = {};
    for (const name of ['depth.raw', 'motion.raw', 'exposure.raw']) { const file = fs.readFileSync(path.join(input, name)); fs.writeFileSync(path.join(output, name), file, { flag: 'wx' }); guideHashes[name] = hash(file); }
    const parent = JSON.parse(fs.readFileSync(path.join(input, 'diagnostic-contract.json')));
    const contract = { ...parent, sourceScene: 'SYNTHETIC flat HDR plates and differently sized high-contrast squares; NOT an AC7 frame', sourceSha256: hash(b), inputColorSha256: hash(b), derivedFromDiagnosticInputSha256: hash(base), sourceKind: 'SYNTHETIC_SPATIAL_CONTROL', inputMaxRgb: 166, inputChannelsAboveOne: (512 * 720 * 3 + spots.reduce((n, s) => n + s.side ** 2, 0)) * 3, plates, spots, unchangedGuideHashes: guideHashes, purpose: 'Single Reset frame, unchanged guide/exposure bytes. Test amplitude-only compression versus spatial response; does not establish game exposure or temporal quality.' };
    fs.writeFileSync(path.join(output, 'diagnostic-contract.json'), JSON.stringify(contract, null, 2));
    console.log(JSON.stringify({ control: output, sourceKind: contract.sourceKind, peak: 166, unchangedGuideHashes: guideHashes }));
} else {
    assert.equal(process.argv.length, 6); assert(!fs.existsSync(report), 'Refusing to overwrite control report');
    const contract = JSON.parse(fs.readFileSync(path.join(input, 'diagnostic-contract.json')));
    assert.equal(contract.sourceKind, 'SYNTHETIC_SPATIAL_CONTROL'); assert.equal(contract.reset, 1);
    const a = raw(path.join(input, 'color.raw')), b = raw(path.join(output, 'sr-output.raw'));
    assert.equal(hash(a), contract.inputColorSha256);
    for (const [name, expected] of Object.entries(contract.unchangedGuideHashes)) assert.equal(hash(fs.readFileSync(path.join(input, name))), expected);
    let min = Infinity, max = -Infinity, invalid = 0;
    for (let i = 0; i < w * h; ++i) for (let c = 0; c < 3; ++c) { const v = half(b.readUInt16LE(16 + i * 8 + c * 2)); if (!Number.isFinite(v)) ++invalid; else { min = Math.min(min, v); max = Math.max(max, v); } }
    function window(cx, cy, radius) {
        let inputSum = 0, outputSum = 0, outputMin = Infinity, outputMax = -Infinity;
        for (let y = cy - radius; y <= cy + radius; ++y) for (let x = cx - radius; x <= cx + radius; ++x) {
            const p = pixel(a, x, y), q = pixel(b, x, y);
            for (let c = 0; c < 3; ++c) { inputSum += p[c]; outputSum += q[c]; outputMin = Math.min(outputMin, q[c]); outputMax = Math.max(outputMax, q[c]); }
        }
        return { side: radius * 2 + 1, inputSum, outputSum, ratio: outputSum / inputSum, outputMin, outputMax };
    }
    const result = { kind: contract.sourceKind, reset: 1, preExposureAssumption: 1, exposureScaleAssumption: 1, inputSha256: hash(a), outputSha256: hash(b), nonfiniteRgbChannels: invalid, minRgb: min, maxRgb: max,
        plates: plates.map((p, i) => ({ input: p.value, xy: [256 + i * 512, 360], centerOutput: pixel(b, 256 + i * 512, 360), interior: window(256 + i * 512, 360, 64) })),
        spots: spots.map(s => ({ input: s.value, side: s.side, xy: s.center, centerOutput: pixel(b, ...s.center), neighborhoods: [0, 1, 2, 4, 8].map(r => window(...s.center, r)) })),
        limits: 'One synthetic Reset control with captured guide/exposure bytes. Distinguishes spatial versus amplitude-only response here; does NOT prove AC7 calibration, photometric correctness, private algorithm or temporal quality.' };
    fs.mkdirSync(report, { recursive: true }); fs.writeFileSync(path.join(report, 'checks.json'), JSON.stringify(result, null, 2));
    assert.equal(invalid, 0); assert(max > 1);
    console.log(JSON.stringify({ nonfiniteRgb: invalid, range: [min, max], plates: result.plates.map(p => ({ input: p.input, output: p.centerOutput, interiorRatio: p.interior.ratio })), spots: result.spots.map(s => ({ side: s.side, output: s.centerOutput, ratio17x17: s.neighborhoods[4].ratio })) }, null, 2));
}
