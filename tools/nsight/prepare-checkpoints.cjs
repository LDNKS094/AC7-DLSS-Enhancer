const fs = require('fs'), path = require('path'), assert = require('assert/strict');
const [capture, destination, manifestPath] = process.argv.slice(2);
assert(capture && destination, 'Usage: node prepare-checkpoints.cjs CAPTURE_DIRECTORY NEW_DESTINATION [MANIFEST_JSON]');
assert(!fs.existsSync(destination), 'Destination must be new; original capture is never modified');
const manifest = manifestPath ? JSON.parse(fs.readFileSync(manifestPath, 'utf8')) : null;
if (manifest) assert.equal(path.basename(capture), manifest.capture, 'Checkpoint IDs belong to a different capture');
const checkpoints = manifest ? manifest.checkpoints : [1401, 1427, 1428, 1437, 1443].map(draw => ({ draw, file: 'Frame0Part01.cpp' }));
fs.mkdirSync(destination, { recursive: true });
for (const name of fs.readdirSync(capture)) {
  if (/\.(cpp|h|hxx)$/.test(name) || name === 'CMakeLists.txt') fs.copyFileSync(path.join(capture, name), path.join(destination, name));
}
fs.symlinkSync(path.resolve(capture, 'nvapi'), path.join(destination, 'nvapi'), 'junction');
for (const file of new Set(checkpoints.map(c => c.file))) {
  assert(/^Frame0Part\d+\.cpp$/.test(file));
  const frame = path.join(destination, file);
  let source = 'void CaptureCheckpoint(int draw);\n' + fs.readFileSync(frame, 'utf8');
  for (const { draw } of checkpoints.filter(c => c.file === file)) {
    assert(Number.isSafeInteger(draw) && draw >= 0);
    const re = new RegExp('(// Draw #' + draw + ' \\[[^\\n]*\\n[^\\n]*My_ID3D11DeviceContext_Draw\\w*\\([^\\n]*;)', 'g');
    assert.equal([...source.matchAll(re)].length, 1, 'Checkpoint not found: ' + draw);
    source = source.replace(re, '$1\n    CaptureCheckpoint(' + draw + ');');
  }
  fs.writeFileSync(frame, source);
}
let observer = fs.readFileSync(path.join(__dirname, 'replay-checkpoints.cpp'), 'utf8');
if (manifest) {
  const body = [];
  for (const c of checkpoints) {
    body.push(`    if (draw == ${c.draw}) {`);
    for (const d of c.dumps) {
      assert(/^[a-z0-9-]+$/.test(d.name));
      if (d.resource) {
        assert(/^pID3D11(?:Texture2D|Resource)_uid_\d+$/.test(d.resource));
        body.push(`        Dump("${d.name}", ${d.resource});`);
      } else {
        assert(/^pIDXGISwapChain_uid_\d+$/.test(d.swapchain));
        body.push('        { ComPtr<ID3D11Texture2D> buffer;');
        body.push(`          Check(${d.swapchain}->GetBuffer(0, IID_PPV_ARGS(&buffer))); Dump("${d.name}", buffer.Get()); }`);
      }
    }
    body.push('    }');
  }
  observer = observer.slice(0, observer.indexOf('void CaptureCheckpoint(int draw)')) + 'void CaptureCheckpoint(int draw) {\n' + body.join('\n') + '\n}\n';
  observer = observer.replace('// Readback checkpoints for the 2026-10-27 07:41:15 capture only.', '// Readback checkpoints generated from a capture-specific manifest.');
}
fs.writeFileSync(path.join(destination, 'replay-checkpoints.cpp'), observer);
fs.appendFileSync(path.join(destination, 'CMakeLists.txt'), '\ntarget_sources(Main PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/replay-checkpoints.cpp")\n');
console.log('Prepared capture-specific checkpoint replay; run with original capture cwd and AC7_REPLAY_DUMP_DIR set.');
