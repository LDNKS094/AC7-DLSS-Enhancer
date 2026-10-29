// Validate the offline two-frame GPU proxy/NR/HDR/SR source chain; preserve original evidence.
const fs=require('fs'),path=require('path'),assert=require('assert/strict'),crypto=require('crypto');
const [source,experiment,out]=process.argv.slice(2);assert(source&&experiment&&out,'Usage: node check_live_chain.cjs LIVE_PAIR EXPERIMENT NEW_JSON');assert(!fs.existsSync(out),'Refusing existing evidence');
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const audit=JSON.parse(fs.readFileSync(path.join(source,'audit.json')));for(const p of audit.provenance)assert.equal(hash(fs.readFileSync(path.join(source,p.name))),p.sha256);
const nr=JSON.parse(fs.readFileSync(path.join(experiment,'nr-input/sequence-contract.json'))),sr=JSON.parse(fs.readFileSync(path.join(experiment,'sr-checks.json'))),nrChecks=JSON.parse(fs.readFileSync(path.join(experiment,'nr-checks.json')));
assert(sr.nrApplied&&sr.featureAndTextureIdentitiesStableWithinEachRun&&nrChecks.sameFeatureAndResources);
function raw(p,header){const b=fs.readFileSync(p);assert.deepEqual([0,4,8,12].map(i=>b.readUInt32LE(i)),header);assert.equal(b.length,16+header[0]*header[1]*header[3]);return b;}
const unsignedFloat=(bits,mantissa)=>{const e=bits>>mantissa,m=bits&((1<<mantissa)-1);return e===31?(m?NaN:Infinity):e?2**(e-15)*(1+m/2**mantissa):m*2**(-14-mantissa);};
const eleven=Float64Array.from({length:2048},(_,i)=>unsignedFloat(i,6)),ten=Float64Array.from({length:1024},(_,i)=>unsignedFloat(i,5));assert.equal(eleven[15<<6],1);assert.equal(ten[15<<5],1);
const frameCount=nr.frames.length;assert([2,8].includes(frameCount));assert.equal(sr.frames.length,frameCount);assert.equal((nrChecks.frames||nrChecks.stats).length,frameCount);
const frames=[];
for(let f=0;f<frameCount;f++){
 const gpu=path.join(experiment,fs.existsSync(path.join(experiment,'gpu-prepare/complete.txt'))?'gpu-prepare':'gpu-prepare-v2',`frame${f}`),restored=path.join(experiment,'gpu-restore',`frame${f}`),input=path.join(experiment,'nr-input',`frame${f}`);
 for(const [name,sha] of Object.entries(nr.frames[f].hashes))assert.equal(hash(fs.readFileSync(path.join(input,name))),sha);
 const original=raw(path.join(source,`frame${f}-scene.raw`),[5120,1440,26,4]),neutral=raw(path.join(gpu,'scene-restored.raw'),[5120,1440,26,4]),active=raw(path.join(restored,'scene-restored.raw'),[5120,1440,26,4]);assert(original.equals(neutral));
 assert(fs.readFileSync(path.join(gpu,'proxy.raw')).equals(fs.readFileSync(path.join(input,'color.raw'))));assert(fs.readFileSync(path.join(restored,'proxy.raw')).equals(fs.readFileSync(path.join(input,'color.raw'))));
 const mask=raw(path.join(gpu,'validity.raw'),[2560,1440,42,4]);let camera=0,object=0;for(let i=16;i<mask.length;i+=4){const value=mask.readUInt32LE(i);assert(value===1||value===2);if(value===1)camera++;else object++;}
 let changed=0,changedOutside=0,invalid=0,max=0;
 for(let y=0;y<1440;y++)for(let x=0;x<5120;x++){
  const at=16+(y*5120+x)*4,a=original.readUInt32LE(at),b=active.readUInt32LE(at);if(a!==b){if(x>=1280&&x<3840)changed++;else changedOutside++;}
  if(x>=1280&&x<3840)for(const v of [eleven[b&2047],eleven[(b>>>11)&2047],ten[b>>>22]]){if(!Number.isFinite(v))invalid++;max=Math.max(max,v);}
 }
 assert.equal(changedOutside,0);assert.equal(invalid,0);assert(changed>0);
 frames.push({frame:f,sourceFrame:(audit.frames||audit.viewMetadataCandidates)[f].counter,neutralFullSceneExact:true,proxyActualNrSourceExact:true,cameraGuidePixels:camera,objectGuidePixels:object,invalidGuidePixels:0,restoredChangedActivePixels:changed,restoredChangedOutsidePixels:changedOutside,restoredNonfiniteRgb:invalid,restoredMaxRgb:max,restoredSha256:hash(active),gpuVsCpuHalfMotionMaxError:nr.frames[f].maxGpuVsCpuHalfPixelError});
}
assert(!fs.existsSync(path.join(experiment,'reject-wrong-proxy/frame0/scene-restored.raw')));assert(fs.readFileSync(path.join(experiment,'reject-wrong-proxy.log'),'utf8').includes('refusing restored scene publication'));
const result={kind:'REAL_SEQUENCE_OFFLINE_GPU_PROXY_NR_HDR_SR_CHAIN',frames,nr:nrChecks,sr,wrongProxyRefusedBeforeRestoredPublication:true,newNrEvaluations:frameCount,newSrEvaluations:2*frameCount,preExposureAssumption:1,actualJitter:[0,0],liveInterop:false,gameWriteback:false,longSequenceQualityAccepted:false,limits:'Fixed 2/8 actual frames with file/staging D3D11-D3D12 handoff. Public NR Reset1/0 execution does not prove NR temporal algorithm internals. Need bounded longer sequence, complete guide semantics and live resource bridge before real-time game processing.'};
fs.writeFileSync(out,JSON.stringify(result,null,2));console.log(JSON.stringify({frames,srComparisons:sr.comparisons,wrongProxyRefused:true,liveInterop:false},null,2));
