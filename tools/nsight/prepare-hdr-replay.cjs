// Capture-specific HDR roundtrip and optional precomputed NR writeback; original source is never edited.
const fs=require('fs'),path=require('path'),cp=require('child_process'),assert=require('assert/strict');
const [capture,destination]=process.argv.slice(2);
assert(capture&&destination,'Usage: node prepare-hdr-replay.cjs CAPTURE_DIRECTORY NEW_DESTINATION');
const p=cp.spawnSync(process.execPath,[path.join(__dirname,'prepare-checkpoints.cjs'),capture,destination,path.join(__dirname,'checkpoints-hdr-writeback-20261028.json')],{encoding:'utf8'});
assert.equal(p.status,0,p.stderr);console.log(p.stdout.trim());
const file=path.join(destination,'replay-checkpoints.cpp');let text=fs.readFileSync(file,'utf8');
const anchor='    if (draw == 467) {';assert.equal(text.split(anchor).length,2);
text=text.replace('#include "Resources.h"','#include "Resources.h"\nvoid ValidateHdrReplay(ID3D11DeviceContext*, ID3D11Texture2D*);');
text=text.replace(anchor,anchor+'\n        ValidateHdrReplay(pID3D11DeviceContext_uid_30, pID3D11Texture2D_uid_1645);');
fs.writeFileSync(file,text);
const project=path.resolve(__dirname,'../..').split(path.sep).join('/');
fs.appendFileSync(path.join(destination,'CMakeLists.txt'),`\nadd_subdirectory("${project}" "\${CMAKE_CURRENT_BINARY_DIR}/ac7-hdr" EXCLUDE_FROM_ALL)
target_sources(Main PRIVATE "${project}/tests/d3d11/replay_hdr.cpp")
target_link_libraries(Main PRIVATE ac7_hdr)
`);
console.log('Prepared GPU HDR writeback after Draw467: neutral by default; optional NR output requires exact proxy-input verification. Explicit captured exposure is an offline diagnostic input.');
