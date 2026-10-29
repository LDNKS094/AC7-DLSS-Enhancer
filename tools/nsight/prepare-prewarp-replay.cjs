// Observe and neutral-test before the original distortion application, never mutate original capture.
const fs=require('fs'),path=require('path'),cp=require('child_process'),assert=require('assert/strict');
const [capture,destination]=process.argv.slice(2);assert(capture&&destination,'Usage: node prepare-prewarp-replay.cjs CAPTURE NEW_DESTINATION');
const p=cp.spawnSync(process.execPath,[path.join(__dirname,'prepare-checkpoints.cjs'),capture,destination,path.join(__dirname,'checkpoints-prewarp-20261028.json')],{encoding:'utf8'});assert.equal(p.status,0,p.stderr);console.log(p.stdout.trim());
const file=path.join(destination,'replay-checkpoints.cpp');let text=fs.readFileSync(file,'utf8');
text=text.replace('#include "Resources.h"','#include "Resources.h"\n#include <cstring>\nvoid ValidateHdrReplay(ID3D11DeviceContext*, ID3D11Texture2D*);');
const anchor='    if (draw == 465) {';assert.equal(text.split(anchor).length,2);
text=text.replace(anchor,anchor+'\n        if (const char* neutral=std::getenv("AC7_PREWARP_NEUTRAL")) {\n            if (std::strcmp(neutral,"1") || std::getenv("AC7_HDR_NEURAL") || std::getenv("AC7_HDR_EXPECTED_PROXY")) throw std::runtime_error("Prewarp neutral test rejects model inputs or invalid mode");\n            ValidateHdrReplay(pID3D11DeviceContext_uid_30, pID3D11Texture2D_uid_1645);\n        }');
fs.writeFileSync(file,text);
const project=path.resolve(__dirname,'../..').split(path.sep).join('/');fs.appendFileSync(path.join(destination,'CMakeLists.txt'),`\nadd_subdirectory("${project}" "\${CMAKE_CURRENT_BINARY_DIR}/ac7-hdr" EXCLUDE_FROM_ALL)
target_sources(Main PRIVATE "${project}/tests/d3d11/replay_hdr.cpp")
target_link_libraries(Main PRIVATE ac7_hdr)
`);
console.log('Observe after465 before original466warp; optional AC7_PREWARP_NEUTRAL=1 runs identity HDR codec only. FXAA original retained for neutral equivalence.');
