// Offline HDR spatial diagnostics only; no model calls or reference-quality verdict.
const fs = require('fs');
const path = require('path');
const assert = require('assert/strict');
const crypto = require('crypto');
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const half = Float32Array.from({ length: 65536 }, (_, bits) => {
    const e = (bits >>> 10) & 31, m = bits & 1023, sign = bits & 32768 ? -1 : 1;
    return sign * (e === 31 ? (m ? NaN : Infinity) : e ? (1 + m / 1024) * 2 ** (e - 15) : m * 2 ** -24);
});
function read(file) {
    const bytes = fs.readFileSync(file);
    assert(bytes.length >= 16, 'Missing raw header');
    const header = Array.from({ length: 4 }, (_, i) => bytes.readUInt32LE(i * 4));
    assert.deepEqual(header, [2560, 1440, 10, 8]);
    assert.equal(bytes.length, 16 + 2560 * 1440 * 8);
    const rgb = new Float32Array(2560 * 1440 * 3);
    for (let i = 0; i < 2560 * 1440; ++i) for (let c = 0; c < 3; ++c) {
        const value = half[bytes.readUInt16LE(16 + i * 8 + c * 2)];
        assert(Number.isFinite(value), 'Nonfinite source or output RGB');
        rgb[i * 3 + c] = value;
    }
    return { bytes, rgb };
}
function spatial(a, b, w, h) {
    assert.equal(a.length, w * h * 3); assert.equal(a.length, b.length);
    let inputSum = 0, outputSum = 0, absSum = 0, xx = 0, xy = 0, yy = 0;
    let changed = 0, outputPeak = -Infinity, inputPeak = -Infinity, outputPeakCount = 0;
    let inputPeakAt = null, outputPeakAt = null, within = 0, below = 0, above = 0;
    const top = [], buckets = [0.1, 1, 10, 100, Infinity].map(upper => ({ upper: Number.isFinite(upper) ? upper : 'Infinity', pixels: 0, inputSum: 0, outputSum: 0, absoluteDifference: 0 }));
    const tiles = new Map();
    for (let y = 0; y < h; ++y) for (let x = 0; x < w; ++x) {
        const i = (y * w + x) * 3;
        const peak = Math.max(a[i], a[i + 1], a[i + 2]);
        const bucket = buckets[peak < 0.1 ? 0 : peak < 1 ? 1 : peak < 10 ? 2 : peak < 100 ? 3 : 4];
        ++bucket.pixels;
        const tileKey = `${x >>> 5},${y >>> 5}`;
        let tile = tiles.get(tileKey);
        if (!tile) { tile = { input: 0, output: 0, pixels: 0 }; tiles.set(tileKey, tile); }
        ++tile.pixels;
        let maxDiff = 0, inside = true;
        for (let c = 0; c < 3; ++c) {
            const v = a[i + c], n = b[i + c], error = Math.abs(v - n);
            inputSum += v; outputSum += n; absSum += error;
            bucket.inputSum += v; bucket.outputSum += n; bucket.absoluteDifference += error;
            tile.input += v; tile.output += n;
            xx += v * v; xy += v * n; yy += n * n;
            maxDiff = Math.max(maxDiff, error);
            if (v > inputPeak) { inputPeak = v; inputPeakAt = [x, y, c]; }
            if (n > outputPeak) { outputPeak = n; outputPeakAt = [x, y, c]; outputPeakCount = 1; }
            else if (n === outputPeak) ++outputPeakCount;
            let low = Infinity, high = -Infinity;
            for (let dy = -1; dy <= 1; ++dy) for (let dx = -1; dx <= 1; ++dx) {
                const j = (Math.max(0, Math.min(h - 1, y + dy)) * w + Math.max(0, Math.min(w - 1, x + dx))) * 3 + c;
                low = Math.min(low, a[j]); high = Math.max(high, a[j]);
            }
            const tolerance = Math.max(1e-5, Math.max(Math.abs(low), Math.abs(high)) * 0.002);
            if (n < low - tolerance) { ++below; inside = false; }
            if (n > high + tolerance) { ++above; inside = false; }
        }
        if (inside) ++within;
        if (maxDiff > 0) ++changed;
        if (top.length < 8 || maxDiff > top[top.length - 1].maxAbsoluteDifference) {
            top.push({ xy: [x, y], fullCaptureX: x + 1280, input: Array.from(a.slice(i, i + 3)), output: Array.from(b.slice(i, i + 3)), maxAbsoluteDifference: maxDiff });
            top.sort((v, n) => n.maxAbsoluteDifference - v.maxAbsoluteDifference);
            if (top.length > 8) top.pop();
        }
    }
    for (const t of top) {
        const [x, y] = t.xy;
        t.neighborhoods = [1, 2, 4, 8].map(radius => {
            let sumA = 0, sumB = 0, count = 0, peakA = -Infinity, peakB = -Infinity;
            for (let py = Math.max(0, y - radius); py <= Math.min(h - 1, y + radius); ++py) for (let px = Math.max(0, x - radius); px <= Math.min(w - 1, x + radius); ++px) {
                ++count;
                for (let c = 0; c < 3; ++c) { const j = (py * w + px) * 3 + c; sumA += a[j]; sumB += b[j]; peakA = Math.max(peakA, a[j]); peakB = Math.max(peakB, b[j]); }
            }
            return { side: radius * 2 + 1, pixels: count, inputSum: sumA, outputSum: sumB, ratio: sumA ? sumB / sumA : null, inputPeak: peakA, outputPeak: peakB };
        });
    }
    const ratios = [...tiles.values()].filter(t => t.input > 0).map(t => t.output / t.input).sort((a, b) => a - b);
    const quantile = q => ratios.length ? ratios[Math.floor((ratios.length - 1) * q)] : null;
    const gain = xx ? xy / xx : null;
    return {
        size: [w, h], inputPeak, inputPeakAt, outputPeak, outputPeakAt, outputPeakChannelCount: outputPeakCount,
        changedRgbPixels: changed, maxAbsoluteDifference: top[0].maxAbsoluteDifference, meanAbsoluteDifference: absSum / a.length,
        inputRgbSum: inputSum, outputRgbSum: outputSum, totalRgbSumRatio: inputSum ? outputSum / inputSum : null,
        bestScalarGainLeastSquares: gain, bestScalarRelativeResidualRms: yy ? Math.sqrt(Math.max(0, yy - xy * xy / xx) / yy) : null,
        inputPeakBins: buckets.map(v => ({ ...v, ratio: v.inputSum ? v.outputSum / v.inputSum : null, shareOfAbsoluteDifference: absSum ? v.absoluteDifference / absSum : 0 })),
        local3x3: { tolerance: 'max(1e-5, neighborhood absolute max *0.002)', withinPixels: within, withinPixelFraction: within / (w * h), channelsBelow: below, channelsAbove: above },
        tile32RgbSumRatios: { count: ratios.length, min: quantile(0), p01: quantile(0.01), median: quantile(0.5), p99: quantile(0.99), max: quantile(1) },
        worstPixels: top,
        interpretationLimit: 'RGB sums are numeric scene-domain measures, not calibrated photometric energy. Neighborhood membership, peak reduction and scalar fit cannot establish the private reconstruction algorithm, correct game exposure, temporal quality or ground truth.'
    };
}
function selfTest() {
    assert.equal(half[0x3c00], 1); assert.equal(half[0x0001], 2 ** -24); assert(Number.isNaN(half[0x7e00]));
    const a = new Float32Array(5 * 5 * 3).fill(2), identity = spatial(a, a, 5, 5);
    assert.equal(identity.changedRgbPixels, 0); assert.equal(identity.totalRgbSumRatio, 1); assert.equal(identity.local3x3.withinPixelFraction, 1);
    const doubled = spatial(a, Float32Array.from(a, v => v * 2), 5, 5);
    assert.equal(doubled.bestScalarGainLeastSquares, 2); assert.equal(doubled.bestScalarRelativeResidualRms, 0);
    for (let c = 0; c < 3; ++c) a[(2 * 5 + 2) * 3 + c] = 100;
    const blurred = a.slice();
    for (let y = 1; y <= 3; ++y) for (let x = 1; x <= 3; ++x) for (let c = 0; c < 3; ++c) blurred[(y * 5 + x) * 3 + c] = 116 / 9;
    const smooth = spatial(a, blurred, 5, 5);
    assert(smooth.outputPeak < smooth.inputPeak); assert(Math.abs(smooth.totalRgbSumRatio - 1) < 1e-6); assert.equal(smooth.local3x3.withinPixelFraction, 1);
}
selfTest();
if (process.argv[2] === '--self-test') { console.log('Half decode, identity, scalar gain and peak-reducing energy-conserving smoothing self-checks passed'); process.exit(0); }
assert.equal(process.argv.length, 4, 'Usage: node analyze_sr_reset.cjs OFFICIAL_SR_ARTIFACT_DIRECTORY NEW_REPORT_DIRECTORY');
const root = path.resolve(process.argv[2]), out = path.resolve(process.argv[3]);
assert(!fs.existsSync(out), 'Refusing to overwrite analysis');
fs.mkdirSync(out, { recursive: true });
for (const name of ['baseline', 'after-nr']) {
    const inputPath = path.join(root, `reset-diagnostic-${name}-input`), outputPath = path.join(root, `reset-diagnostic-${name}-output`);
    const contract = JSON.parse(fs.readFileSync(path.join(inputPath, 'diagnostic-contract.json')));
    const old = JSON.parse(fs.readFileSync(path.join(outputPath, 'checks.json')));
    assert.equal(contract.kind, 'EXPLICIT_STANDALONE_SR_RESET_DIAGNOSTIC'); assert.equal(contract.reset, 1);
    const a = read(path.join(inputPath, 'color.raw')), b = read(path.join(outputPath, 'sr-output.raw'));
    assert.equal(hash(a.bytes), contract.inputColorSha256); assert.equal(hash(b.bytes), old.outputSha256);
    const result = spatial(a.rgb, b.rgb, 2560, 1440);
    assert.equal(result.changedRgbPixels, old.changedRgbPixels); assert.equal(result.outputPeak, old.maxRgb);
    assert.equal(result.maxAbsoluteDifference, old.maxAbsoluteHdrDifference);
    assert(Math.abs(result.meanAbsoluteDifference - old.meanAbsoluteHdrDifference) < 1e-12);
    result.inputSha256 = hash(a.bytes); result.outputSha256 = hash(b.bytes);
    fs.writeFileSync(path.join(out, `${name}.json`), JSON.stringify(result, null, 2));
    console.log(JSON.stringify({ sample: name, inputPeakAt: result.inputPeakAt, outputPeakAt: result.outputPeakAt, totalRgbSumRatio: result.totalRgbSumRatio, local3x3: result.local3x3, bins: result.inputPeakBins.map(v => ({ upper: v.upper, pixels: v.pixels, ratio: v.ratio, diffShare: v.shareOfAbsoluteDifference })), worst: result.worstPixels[0] }, null, 2));
}
