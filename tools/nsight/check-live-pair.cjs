// Read-only pair audit. Candidate field names follow the locally checked old UE4 View layout.
const fs = require('fs'), path = require('path'), assert = require('assert/strict'), crypto = require('crypto');
const [input, output] = process.argv.slice(2);
assert(input && output, 'Usage: node check-live-pair.cjs INPUT_PAIR_DIRECTORY NEW_REPORT_JSON');
assert(!fs.existsSync(output), 'Refusing to overwrite audit evidence');
assert(!fs.existsSync(path.join(input, 'INCOMPLETE')), 'Incomplete transport');
const manifest = JSON.parse(fs.readFileSync(path.join(input, 'capture.json')));
assert(manifest.transportComplete && manifest.frames === 2);
const [width, height] = manifest.allocation, [rx, ry, rw, rh] = manifest.rect;
assert([width, height, rx, ry, rw, rh].every(Number.isSafeInteger));
assert(width > 0 && height > 0 && rx >= 0 && ry >= 0 && rw > 0 && rh > 0 && rx + rw <= width && ry + rh <= height);
const provenance = [];
function read(name) {
    const bytes = fs.readFileSync(path.join(input, name));
    provenance.push({ name, bytes: bytes.length, sha256: crypto.createHash('sha256').update(bytes).digest('hex') });
    return bytes;
}
const views = [0, 1].map(f => { const b = read(`frame${f}-view.bin`); assert.equal(b.length, 4096); return b; });
const row = (b, n) => Array.from({ length: 4 }, (_, c) => b.readFloatLE(n * 16 + c * 4));
const metadata = views.map(b => ({
    counter: b.readUInt32LE(137 * 16 + 4), mod8: b.readUInt32LE(137 * 16 + 8),
    jitter: row(b, 114), previousTimes: row(b, 133).slice(2), currentTimes: row(b, 136).slice(2),
    rectMin: row(b, 126).slice(0, 2), viewSize: row(b, 127).slice(0, 2), allocation: row(b, 128).slice(0, 2)
}));
for (const m of metadata) {
    assert(m.jitter.every(Number.isFinite));
    assert.deepEqual(m.rectMin, [rx, ry]); assert.deepEqual(m.viewSize, [rw, rh]); assert.deepEqual(m.allocation, [width, height]);
    assert(m.previousTimes.concat(m.currentTimes).every(Number.isFinite));
}
const time = {
    gameDeltaBetweenSamples: metadata[1].currentTimes[0] - metadata[0].currentTimes[0],
    realDeltaBetweenSamples: metadata[1].currentTimes[1] - metadata[0].currentTimes[1],
    gameChainResidual: metadata[1].previousTimes[0] - metadata[0].currentTimes[0],
    realChainResidual: metadata[1].previousTimes[1] - metadata[0].currentTimes[1],
    withinFrameSteps: metadata.map(m => m.currentTimes.map((v, i) => v - m.previousTimes[i]))
};
const matrices = [['TranslatedWorldToClip', 0, 79], ['WorldToClip', 4, 63], ['ViewToClip', 24, 71],
    ['ClipToView', 28, 75], ['TranslatedWorldToView', 8, 83], ['ViewToTranslatedWorld', 12, 87]].map(([name, currentRow, previousRow]) => {
    let maxAbs = 0;
    for (let i = 0; i < 16; i++) {
        const a = views[0].readFloatLE(currentRow * 16 + i * 4), b = views[1].readFloatLE(previousRow * 16 + i * 4);
        assert(Number.isFinite(a) && Number.isFinite(b)); maxAbs = Math.max(maxAbs, Math.abs(a - b));
    }
    return { name, currentRow, previousRow, maxAbs, bitExact: views[0].subarray(currentRow * 16, (currentRow + 4) * 16).equals(views[1].subarray(previousRow * 16, (previousRow + 4) * 16)) };
});
const trace = read('candidates.jsonl').toString('utf8').trim().split(/\r?\n/).map(JSON.parse);
assert(trace.length <= 64);
const order = [0, 1].map(f => trace.filter(e => e.completedIntervals === f));
for (const events of order) {
    const roles = events.map(e => e.role);
    assert(roles.includes(0) && roles.includes(1) && roles.includes(2));
    assert(roles.indexOf(0) < roles.indexOf(1) && roles.indexOf(1) < roles.indexOf(2));
}
const resources = {};
for (const [role, format, bpp] of [['scene', 26, 4], ['depth', 19, 8], ['velocity', 35, 4], ['exposure', 16, 8]]) {
    const buffers = [0, 1].map(f => read(`frame${f}-${role}.raw`));
    const w = role === 'exposure' ? 1 : width, h = role === 'exposure' ? 1 : height;
    for (const b of buffers) { assert.deepEqual([0, 4, 8, 12].map(i => b.readUInt32LE(i)), [w, h, format, bpp]); assert.equal(b.length, 16 + w * h * bpp); }
    let changedActive = 0, changedOutside = 0, packed = 0, overPoint1Pixel = 0, maxPackedMotionPixels = 0;
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
        const offset = 16 + (y * w + x) * bpp;
        const a = buffers[0], b = buffers[1], active = role === 'exposure' || (x >= rx && y >= ry && x < rx + rw && y < ry + rh);
        const changed = role === 'depth' ? (a.readUInt32LE(offset) !== b.readUInt32LE(offset) || a[offset + 4] !== b[offset + 4]) :
            role === 'exposure' ? !a.subarray(offset, offset + bpp).equals(b.subarray(offset, offset + bpp)) : a.readUInt32LE(offset) !== b.readUInt32LE(offset);
        if (changed) { if (active) changedActive++; else changedOutside++; }
        if (role === 'velocity' && active && b.readUInt16LE(offset) > 0) {
            packed++;
            const vx = (b.readUInt16LE(offset) / 65535 * 4.008016 - 2.003978) * rw / 2;
            const vy = (b.readUInt16LE(offset + 2) / 65535 * 4.008016 - 2.003978) * rh / 2;
            const magnitude = Math.hypot(vx, vy); maxPackedMotionPixels = Math.max(maxPackedMotionPixels, magnitude);
            if (magnitude > 0.1) overPoint1Pixel++;
        }
    }
    resources[role] = { changedActive, changedOutside, wholeFileExact: buffers[0].equals(buffers[1]) };
    if (role === 'velocity') resources[role].frame1ObjectMotion = { packedPixels: packed, overPoint1Pixel, maxPixels: maxPackedMotionPixels, note: 'Packed object path only; not a complete camera/disocclusion validation' };
    if (role === 'exposure') resources[role].values = buffers.map(b => [b.readFloatLE(16), b.readFloatLE(20)]);
}
const counterStep1 = metadata[1].counter === ((metadata[0].counter + 1) >>> 0);
const cameraMatrixBlockUnchanged = views[0].subarray(0, 114 * 16).equals(views[1].subarray(0, 114 * 16));
const adjacentRenderedIntervalsSupported = counterStep1 && Math.abs(time.realChainResidual) < 1e-6 && time.realDeltaBetweenSamples > 0 && matrices.every(m => m.bitExact);
const simulationAdvanceObserved = time.gameDeltaBetweenSamples > 1e-6;
const result = {
    source: path.resolve(input), provenance, manifestUnmodified: manifest, viewMetadataCandidates: metadata, timeCandidates: time,
    sampleIndexCandidate: { values: metadata.map(m => m.mod8), advancesModulo8: metadata[1].mod8 === (metadata[0].mod8 + 1) % 8,
        frameCounterModuloMatches: metadata.map(m => m.counter % 8 === m.mod8), note: 'Independent sample-index candidate; equality to frame counter modulo8 was only a coincidence in the original single frame.' },
    matrixCandidateChains: matrices, candidateOrder: order, resources,
    qualification: { transportComplete: true, counterStep1, adjacentRenderedIntervalsSupported, cameraMatrixBlockUnchanged,
        simulationAdvanceObserved, dynamicTemporalQualified: false, actualJitterZero: metadata.every(m => m.jitter.every(v => v === 0)),
        reason: adjacentRenderedIntervalsSupported && !simulationAdvanceObserved ? 'Consecutive rendered intervals supported, simulation/camera held. Suitable for transport/static history only; not dynamic temporal quality.' : 'Inspect times/matrices/resources independently; pair is not full dynamic quality acceptance.',
        fieldLimits: 'Names/offsets follow old UE4 reference layout and observed shader bindings; real chain+counter+matrix matches support this pair, not all game paths. Paused PrevFrameGameTime may be CurrentWorldTime-DeltaWorldTime rather than previous sample WorldTime.' }
};
fs.mkdirSync(path.dirname(path.resolve(output)), { recursive: true }); fs.writeFileSync(output, JSON.stringify(result, null, 2));
console.log(JSON.stringify({ counters: metadata.map(m => m.counter), time, matrixChainsExact: matrices.filter(m => m.bitExact).length, resources, qualification: result.qualification }, null, 2));
