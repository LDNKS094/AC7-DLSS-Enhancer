// Read only a bounded View CB slice and compare named reference field sequences.
const fs=require('fs'),path=require('path'),assert=require('assert/strict'),crypto=require('crypto');
const [captureDir,referenceDir,out]=process.argv.slice(2);
assert(captureDir&&referenceDir&&out,'Usage: node audit-view-layout.cjs CAPTURE_ARTIFACT_DIR REFERENCE_DIR NEW_JSON');
assert(!fs.existsSync(out),'Refusing to overwrite evidence');
const index=JSON.parse(fs.readFileSync(path.join(captureDir,'analysis/source-index.json')));
const prior=JSON.parse(fs.readFileSync(path.join(captureDir,'checkpoints/motion-analysis.json')));
const consumer=index.draws[prior.consumerDraw];
assert(Object.entries(consumer.constantUpdates).some(([b,u])=>u.handle===prior.viewBlob&&Object.values(consumer.cb).includes(b)));
const rec=fs.readFileSync(path.join(index.capture,'data.bin.rec')),size=Number(rec.readBigUInt64LE(prior.viewBlob*16)),offset=Number(rec.readBigUInt64LE(prior.viewBlob*16+8));
assert(size>=138*16);const bytes=Buffer.alloc(24*16),fd=fs.openSync(path.join(index.capture,'data.bin'),'r');
try{assert.equal(fs.readSync(fd,bytes,0,bytes.length,offset+114*16),bytes.length)}finally{fs.closeSync(fd)}
const row=n=>Array.from({length:4},(_,c)=>bytes.readFloatLE((n-114)*16+c*4));
assert.deepEqual(row(114),prior.jitterCurrentPrevious);assert.deepEqual(row(126),[1280,0,0,0]);
assert.equal(row(127)[0],2560);assert.equal(row(127)[1],1440);assert.equal(row(128)[0],5120);assert.equal(row(128)[1],1440);
const source=(file)=>{const text=fs.readFileSync(path.join(referenceDir,file),'utf8');return{text,sha256:crypto.createHash('sha256').update(text).digest('hex')}};
function fields(text){return text.split('\n').flatMap((l,i)=>{const m=l.match(/VIEW_UNIFORM_BUFFER_MEMBER(?:_EX)?\((\w+),\s*(\w+)/);return m?[{type:m[1],name:m[2],line:i+1}]:[]})}
const old=source('old-view-layout.h'),newer=source('new-view-layout.h'),oldFields=fields(old.text),newFields=fields(newer.text);
const segment=f=>f.slice(f.findIndex(x=>x.name==='FieldOfViewWideAngles'),f.findIndex(x=>x.name==='DiffuseOverrideParameter')+1);
assert(!oldFields.some(x=>x.name==='PreExposure'));assert(newFields.some(x=>x.name==='PreExposure'));
assert.deepEqual(segment(oldFields).map(x=>x.name),['FieldOfViewWideAngles','PrevFieldOfViewWideAngles','ViewRectMin','ViewSizeAndInvSize','BufferSizeAndInvSize','NumSceneColorMSAASamples','ExposureScale','DiffuseOverrideParameter']);
assert(newFields.findIndex(x=>x.name==='BufferBilinearUVMinMax')<newFields.findIndex(x=>x.name==='PreExposure'));
const renderer=source('old-scenerendering.txt');assert(renderer.text.includes('ViewUniformShaderParameters.ExposureScale = ExposureScale; // Only used for MobileHDR == false'));
const writers=index.draws.filter(d=>d.targets.some(t=>index.views[t]?.resource==='pID3D11Texture2D_uid_1645'));
const shaders=new Map();let missing=0;
for(const d of writers){const slot=Object.entries(d.cb).find(([s,b])=>b==='pID3D11Buffer_uid_58215')?.[0];if(slot===undefined)continue;const key=d.pixelShader+':'+slot;if(shaders.has(key))continue;const file=path.join(captureDir,'analysis/shaders',d.pixelShader+'.asm');if(!fs.existsSync(file)){++missing;continue}const lines=fs.readFileSync(file,'utf8').split('\n');const hits=lines.flatMap((l,i)=>new RegExp('cb'+slot+'\\[129\\]').test(l)?[{line:i+1,instruction:l.trim()}]:[]);shaders.set(key,{shader:d.pixelShader,slot,firstDraw:d.id,hits});}
const report={capture:path.basename(index.capture),sourceIdentity:{oldHeader:old.sha256,newHeader:newer.sha256,oldRenderer:renderer.sha256,oldVersion:JSON.parse(fs.readFileSync(path.join(referenceDir,'old-version.txt')))},referenceKind:'Public third-party UE4.18 GameWorks and UE4.22 mirror; NOT authenticated AC7 source',oldSegment:segment(oldFields),newSegment:segment(newFields),observedRows:[114,115,125,126,127,128,129,130,131,132,133,134,135,136,137].map(n=>({row:n,float:row(n),uint:n===129?Array.from({length:4},(_,c)=>bytes.readUInt32LE((n-114)*16+c*4)):undefined})),layoutInference:{oldFieldCandidate:'NumSceneColorMSAASamples at129.x; old ExposureScale at129.y; overrides130-132',oldExposureScaleCandidate:row(129)[1],numSamplesCandidate:bytes.readUInt32LE((129-114)*16),status:'Local sequence agrees with older reference; not full AC7 layout identity',newerLayoutRejected:'New reference has BufferBilinearUVMinMax and PreExposure/OneOverPreExposure; direct transplant incompatible with old sequence',customDifference:'AC7 contains additional values/matrix-like rows116-124 before FOV125; do not apply global offsets'},writerScan:{directDraws:writers.length,viewBoundShaderSlotPairs:shaders.size,missingAsm:missing,row129References:[...shaders.values()].filter(s=>s.hits.length),scope:'Literal row129 reference scan only, no dynamic indexing/indirect resource/compute dataflow proof'},conclusion:'Old ExposureScale candidate1.333333 is NOT identified PreExposure; source comment mobile-only. Native P remains unresolved; newer offset extraction must not be implemented.',actions:['No GPU/NGX/game launch','Only384bytes read from actual bound View blob','No capture, SDK, game or config modifications']};
fs.mkdirSync(path.dirname(path.resolve(out)),{recursive:true});fs.writeFileSync(out,JSON.stringify(report,null,2));
console.log(JSON.stringify({oldVersion:report.sourceIdentity.oldVersion.MinorVersion,oldExposureCandidate:report.layoutInference.oldExposureScaleCandidate,numSamples:report.layoutInference.numSamplesCandidate,viewBoundShaderSlotPairs:shaders.size,row129References:report.writerScan.row129References.length,newerLayout:'not directly usable',nativeP:'unresolved'},null,2));
