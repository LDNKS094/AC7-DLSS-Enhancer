// shortcut: parse Nsight 2025.5 generated D3D11 syntax only; extend assertions before accepting a different generator layout.
const fs = require('fs');
const path = require('path');
const assert = require('assert/strict');
const [capture, output] = process.argv.slice(2);
assert(capture && output, 'Usage: node inspect-capture.cjs CAPTURE_DIRECTORY OUTPUT_DIRECTORY');
fs.mkdirSync(output, { recursive: true });
const resources = {}, views = {}, shaders = {}, names = {}, arrays = {}, descriptors = {};
function args(text) {
  const parts = []; let depth = 0, start = 0;
  for (let i = 0; i < text.length; i++) {
    if ('({['.includes(text[i])) depth++;
    if (')}]'.includes(text[i])) depth--;
    if (text[i] === ',' && depth === 0) { parts.push(text.slice(start, i).trim()); start = i + 1; }
  }
  parts.push(text.slice(start).trim());
  return parts;
}
const uid = text => text.match(/\b(p\w+_uid_\d+)\b/)?.[1] || null;
function pointers(text) {
  if (arrays[text]) return arrays[text];
  if (/\bNULL\b|\bnullptr\b/.test(text)) return [null];
  assert(uid(text), 'Unknown pointer expression: ' + text);
  return [uid(text)];
}
function declarations(line) {
  let m = line.match(/static (D3D11_\w+) (\w+)(?:\[\d+\])? = (.*);/);
  if (m) descriptors[m[2]] = { type: m[1], text: m[3] };
  m = line.match(/static ID3D11\w+\* (\w+)\[(\d+)\] = \{(.*)\};/);
  if (m) {
    const values = args(m[3]).map(uid);
    arrays[m[1]] = Array.from({ length: Number(m[2]) }, (_, i) => values[i] || null);
  }
}
function call(line) { const m = line.match(/My_(\w+)\((.*)\);/); return m && { name: m[1], a: args(m[2]) }; }
for (const file of fs.readdirSync(capture).filter(n => /^Resources\d+\.cpp$/.test(n)).sort()) {
  for (const line of fs.readFileSync(path.join(capture, file), 'utf8').split(/\r?\n/)) {
    declarations(line);
    const named = line.match(/NV_SET_OBJECT_NAME\((\w+), "([^"]+)"\)/);
    if (named) names[named[1]] = named[2];
    const c = call(line); if (!c) continue;
    const { name, a } = c;
    if (name === 'ID3D11Device_CreateTexture2D') {
      const d = descriptors[a[1].replace('&', '')]; assert(d, line);
      const fields = args(d.text.slice(1, -1));
      resources[uid(a[3])] = { width: parseInt(fields[0]), height: parseInt(fields[1]), mips: fields[2], arraySize: fields[3], format: fields[4], samples: fields[5], usage: fields[6], bind: fields[7], source: file };
    }
    if (/ID3D11Device_Create(RenderTarget|ShaderResource|DepthStencil|UnorderedAccess)View$/.test(name)) {
      views[uid(a[3])] = { resource: uid(a[1]), descriptor: descriptors[a[2].replace('&', '')] || null };
    }
    if (/ID3D11Device_Create(Pixel|Vertex|Compute)Shader$/.test(name)) {
      const handle = a[1].match(/NV_GET_RESOURCE\(const void\*, (\d+)\)/); assert(handle, line);
      shaders[uid(a[4])] = { stage: name.match(/Create(\w+)Shader/)[1], handle: Number(handle[1]), size: parseInt(a[2]), source: file };
    }
  }
}
const draws = [], updates = {}, ps = {}, vs = {}, cb = {}, vsCb = {}, mappedBuffers = {};
let targets = [], depth = null, pixelShader = null, vertexShader = null, viewport = null, blend = null, drawId = null;
const contexts = new Set();
for (const file of fs.readdirSync(capture).filter(n => /^Frame0Part\d+\.cpp$/.test(n)).sort()) {
  const text = fs.readFileSync(path.join(capture, file), 'utf8');
  for (const m of text.matchAll(/static ID3D11\w+\* \w+\[\d+\] = \{[^}]*\};/g)) declarations(m[0].replace(/[\r\n]/g, ' '));
  const lines = text.split(/\r?\n/);
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i]; declarations(line);
    const marker = line.match(/\/\/ Draw #(\d+)/); if (marker) drawId = Number(marker[1]);
    const copy = line.match(/NV_MEMCPY_IN_FRAME\((\w+)\.pData, NV_GET_RESOURCE\(void\*, (\d+)\), (\d+)ull\)/);
    if (copy && mappedBuffers[copy[1]]) updates[mappedBuffers[copy[1]]] = { file, line: i + 1, handle: Number(copy[2]), bytes: Number(copy[3]), method: 'mapped-copy' };
    const c = call(line); if (!c) continue; const { name, a } = c;
    if (name === 'ID3D11DeviceContext_Map') mappedBuffers[a[5].replace('&', '')] = uid(a[1]);
    if (name.startsWith('ID3D11DeviceContext_')) contexts.add(a[0]);
    if (name === 'ID3D11DeviceContext_OMSetRenderTargets') { targets = pointers(a[2]).slice(0, parseInt(a[1])); depth = uid(a[3]); }
    if (name === 'ID3D11DeviceContext_PSSetShader') pixelShader = uid(a[1]);
    if (name === 'ID3D11DeviceContext_VSSetShader') vertexShader = uid(a[1]);
    if (name === 'ID3D11DeviceContext_OMSetBlendState') blend = uid(a[1]);
    if (name === 'ID3D11DeviceContext_RSSetViewports') viewport = descriptors[a[2]]?.text || null;
    for (const [method, state] of [['PSSetShaderResources', ps], ['VSSetShaderResources', vs], ['PSSetConstantBuffers', cb], ['VSSetConstantBuffers', vsCb]]) {
      if (name === 'ID3D11DeviceContext_' + method) {
        const values = pointers(a[3]), count = parseInt(a[2]), start = parseInt(a[1]);
        assert(values.length >= count, 'Incomplete binding array: ' + line);
        for (let j = 0; j < count; j++) state[start + j] = values[j];
      }
    }
    if (name === 'ID3D11DeviceContext_UpdateSubresource') updates[uid(a[1])] = { file, line: i + 1, handle: Number(a[4].match(/, (\d+)\)/)?.[1]) };
    if (/^ID3D11DeviceContext_Draw\w*$/.test(name)) {
      assert.equal(drawId, draws.length, 'Draw marker sequence mismatch');
      draws.push({ id: drawId, file, line: i + 1, call: name, arguments: a.slice(1), targets: [...targets], depth, pixelShader, vertexShader, viewport, blend, ps: { ...ps }, vs: { ...vs }, cb: { ...cb }, vsCb: { ...vsCb }, constantUpdates: Object.fromEntries([...new Set([...Object.values(cb), ...Object.values(vsCb)])].filter(x => x && updates[x]).map(x => [x, updates[x]])) });
    }
  }
}
assert.equal(contexts.size, 1, 'Multiple contexts require separate state tracking');
assert(draws.length > 0 && Object.keys(resources).length > 0);
const records = fs.readFileSync(path.join(capture, 'data.bin.rec'));
assert.equal(records.length % 16, 0);
const fd = fs.openSync(path.join(capture, 'data.bin'), 'r'), dataSize = fs.fstatSync(fd).size;
fs.mkdirSync(path.join(output, 'shaders'), { recursive: true });
for (const [id, shader] of Object.entries(shaders)) {
  const offset = shader.handle * 16; assert(offset + 16 <= records.length);
  const size = Number(records.readBigUInt64LE(offset)), position = Number(records.readBigUInt64LE(offset + 8));
  assert.equal(size, shader.size); assert(position + size <= dataSize);
  const bytes = Buffer.alloc(size); assert.equal(fs.readSync(fd, bytes, 0, size, position), size);
  assert.equal(bytes.subarray(0, 4).toString(), 'DXBC');
  fs.writeFileSync(path.join(output, 'shaders', id + '.dxbc'), bytes);
}
fs.closeSync(fd);
const result = { capture, note: 'Source draw ordinals are not Nsight event IDs. Binding presence does not prove shader usage. Initial FrameSetup bindings and implicit D3D11 hazard unbinding are not modeled; confirm candidates through shader disassembly and replay.', resources, views, shaders, names, draws };
fs.writeFileSync(path.join(output, 'source-index.json'), JSON.stringify(result, null, 2));
console.log(JSON.stringify({ textures: Object.keys(resources).length, views: Object.keys(views).length, shaders: Object.keys(shaders).length, draws: draws.length, contexts: contexts.size, output }, null, 2));
