// The insertion point and UIDs are deliberately pinned to the verified 2026-10-28 capture.
const fs=require('fs'),path=require('path'),cp=require('child_process'),assert=require('assert/strict');
const [capture,destination]=process.argv.slice(2);
assert(capture&&destination,'Usage: node prepare-insertion-20261028.cjs CAPTURE_DIRECTORY NEW_DESTINATION');
const prepared=cp.spawnSync(process.execPath,[path.join(__dirname,'prepare-checkpoints.cjs'),capture,destination,path.join(__dirname,'checkpoints-20261028.json')],{encoding:'utf8'});
assert.equal(prepared.status,0,prepared.stderr);console.log(prepared.stdout.trim());
const observer=path.join(destination,'replay-checkpoints.cpp');let text=fs.readFileSync(observer,'utf8');
const anchor='    if (draw == 528) {';assert.equal(text.split(anchor).length,2);
text=text.replace('#include "Resources.h"','#include "Resources.h"\nvoid ValidateReplayInsertion(ID3D11DeviceContext*, ID3D11Texture2D*, ID3D11Resource*, ID3D11Texture2D*);');
text=text.replace(anchor,anchor+'\n        ValidateReplayInsertion(pID3D11DeviceContext_uid_30, pID3D11Texture2D_uid_57543, pID3D11Resource_uid_1541, pID3D11Texture2D_uid_1688);');
fs.writeFileSync(observer,text);
const project=path.resolve(__dirname,'../..').split(path.sep).join('/');
fs.appendFileSync(path.join(destination,'CMakeLists.txt'),`\nadd_library(ac7_guides_check STATIC "${project}/src/d3d11/guide_normalizer.cpp")
target_include_directories(ac7_guides_check PUBLIC "${project}/src/d3d11")
target_compile_features(ac7_guides_check PUBLIC cxx_std_17)
target_compile_definitions(ac7_guides_check PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(ac7_guides_check PUBLIC d3d11 d3dcompiler)
target_sources(Main PRIVATE "${project}/tests/d3d11/replay_insertion.cpp")
target_link_libraries(Main PRIVATE ac7_guides_check)
`);
console.log('Inserted real guide normalization and identity scene handoff after Draw528 in the derived source only.');
