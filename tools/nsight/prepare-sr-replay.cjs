// Derived same-size NR→SR replay. Original capture is never edited.
const fs=require('fs'),path=require('path'),cp=require('child_process'),assert=require('assert/strict');
const [capture,destination]=process.argv.slice(2);assert(capture&&destination,'Usage: node prepare-sr-replay.cjs CAPTURE NEW_DESTINATION');
const p=cp.spawnSync(process.execPath,[path.join(__dirname,'prepare-hdr-replay.cjs'),capture,destination],{encoding:'utf8'});assert.equal(p.status,0,p.stderr);console.log(p.stdout.trim());
const file=path.join(destination,'replay-checkpoints.cpp');let text=fs.readFileSync(file,'utf8');
const anchor='        ValidateHdrReplay(pID3D11DeviceContext_uid_30, pID3D11Texture2D_uid_1645);';assert.equal(text.split(anchor).length,2);
text=text.replace('void ValidateHdrReplay(ID3D11DeviceContext*, ID3D11Texture2D*);','void ValidateHdrReplay(ID3D11DeviceContext*, ID3D11Texture2D*);\nvoid ValidateSrReplay(ID3D11DeviceContext*, ID3D11Texture2D*);\nvoid ValidateFxaaReplay(ID3D11DeviceContext*, ID3D11Texture2D*, ID3D11Texture2D*);');
text=text.replace(anchor,anchor+'\n        ValidateSrReplay(pID3D11DeviceContext_uid_30, pID3D11Texture2D_uid_1645);');
text=text.replace('void CaptureCheckpoint(int draw) {','void CaptureCheckpoint(int draw) {\n    if (draw == 526) {\n        ValidateFxaaReplay(pID3D11DeviceContext_uid_30, pID3D11Texture2D_uid_1633, pID3D11Texture2D_uid_1639);\n        Dump("post-fxaa", pID3D11Texture2D_uid_1639);\n    }');
fs.writeFileSync(file,text);
const frame=path.join(destination,'Frame0Part01.cpp');let source=fs.readFileSync(frame,'utf8');
const re=/(\/\/ Draw #526 \[[^\n]*\n[^\n]*My_ID3D11DeviceContext_Draw\w*\([^\n]*;)/g;assert.equal([...source.matchAll(re)].length,1);source=source.replace(re,'$1\n    CaptureCheckpoint(526);');fs.writeFileSync(frame,source);
const project=path.resolve(__dirname,'../..').split(path.sep).join('/');fs.appendFileSync(path.join(destination,'CMakeLists.txt'),`\ntarget_sources(Main PRIVATE "${project}/tests/d3d11/replay_sr.cpp")\n`);
console.log('Added SR packed handoff after NR restore at467 with full-source identity gate; original spatial AA526 retained by default; explicit AC7_SR_BYPASS_FXAA=1 replaces active RGB with exact pre-AA color, preserving original alpha/padding, for comparison, no timing claim.');
