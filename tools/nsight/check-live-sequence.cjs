// Audit fixed8 real outputs without inventing temporal qualification from successful transport.
const fs=require('fs'),path=require('path'),assert=require('assert/strict'),crypto=require('crypto');
const [input,output]=process.argv.slice(2);assert(input&&output,'Usage: node check-live-sequence.cjs CAPTURE8 NEW_AUDIT_JSON');assert(!fs.existsSync(output),'Refusing prior audit');
assert(!fs.existsSync(path.join(input,'INCOMPLETE'))&&!fs.existsSync(path.join(input,'failure.txt')),'Incomplete capture');
const manifest=JSON.parse(fs.readFileSync(path.join(input,'capture.json')));assert(manifest.transportComplete&&manifest.frames===8);assert(manifest.maxInFlightSlots===2&&manifest.peakInFlightLogicalBytes<=256*1024*1024&&manifest.logicalBytes<=1024*1024*1024);
const [w,h]=manifest.allocation,[x,y,rw,rh]=manifest.rect;assert([w,h,x,y,rw,rh].every(Number.isSafeInteger));assert(w>0&&h>0&&x>=0&&y>=0&&rw>0&&rh>0&&x+rw<=w&&y+rh<=h);
const provenance=[],frames=[],pairs=[];let previous=null,total=0;
function read(name){const b=fs.readFileSync(path.join(input,name));provenance.push({name,bytes:b.length,sha256:crypto.createHash('sha256').update(b).digest('hex')});return b;}
const row=(b,n)=>Array.from({length:4},(_,c)=>b.readFloatLE(n*16+c*4));
for(let f=0;f<8;f++){
 const v=read(`frame${f}-view.bin`);assert.equal(v.length,4096);total+=4096;
 const info={frame:f,counter:v.readUInt32LE(137*16+4),sampleIndex:v.readUInt32LE(137*16+8),jitter:row(v,114),previousTimes:row(v,133).slice(2),currentTimes:row(v,136).slice(2)};
 assert(info.jitter.concat(info.previousTimes,info.currentTimes).every(Number.isFinite));assert.deepEqual(row(v,126).slice(0,2),[x,y]);assert.deepEqual(row(v,127).slice(0,2),[rw,rh]);assert.deepEqual(row(v,128).slice(0,2),[w,h]);
 for(const [role,fmt,bpp]of [['scene',26,4],['depth',19,8],['velocity',35,4],['exposure',16,8]]){
  const b=read(`frame${f}-${role}.raw`),width=role==='exposure'?1:w,height=role==='exposure'?1:h;
  assert.deepEqual([0,4,8,12].map(i=>b.readUInt32LE(i)),[width,height,fmt,bpp]);assert.equal(b.length,16+width*height*bpp);total+=b.length-16;
  if(role==='exposure'){info.exposure=[b.readFloatLE(16),b.readFloatLE(20)];assert(info.exposure.every(Number.isFinite)&&info.exposure[0]>0);}
 }
 const metadata=JSON.parse(read(`frame${f}-scene-metadata.json`).toString());assert.deepEqual(metadata.allocation,[w,h]);assert.deepEqual(metadata.rect,manifest.rect);
 if(previous){
  const a=frames[f-1],gameStep=info.currentTimes[0]-a.currentTimes[0],realStep=info.currentTimes[1]-a.currentTimes[1];
  const matrixChains=[['TranslatedWorldToClip',0,79],['WorldToClip',4,63],['ViewToClip',24,71],['ClipToView',28,75],['TranslatedWorldToView',8,83],['ViewToTranslatedWorld',12,87]].map(([name,cur,prev])=>{
   let maxAbs=0;for(let i=0;i<16;i++){const aa=previous.readFloatLE(cur*16+i*4),bb=v.readFloatLE(prev*16+i*4);assert(Number.isFinite(aa)&&Number.isFinite(bb));maxAbs=Math.max(maxAbs,Math.abs(aa-bb));}return{name,maxAbs,exact:previous.subarray(cur*16,(cur+4)*16).equals(v.subarray(prev*16,(prev+4)*16))};
  });
  pairs.push({from:f-1,to:f,counterStep1:info.counter===((a.counter+1)>>>0),gameStep,realStep,gameChainResidual:info.previousTimes[0]-a.currentTimes[0],realChainResidual:info.previousTimes[1]-a.currentTimes[1],matrixChains,cameraChanged:!previous.subarray(0,114*16).equals(v.subarray(0,114*16))});
 }
 frames.push(info);previous=v;
}
assert.equal(total,manifest.logicalBytes);assert.deepEqual(frames.map(f=>f.counter),manifest.frameNumberCandidates);
const trace=read('candidates.jsonl').toString().trim().split(/\r?\n/).map(JSON.parse);assert(trace.length<=64);
for(let f=0;f<8;f++){const events=trace.filter(e=>e.completedIntervals===f),roles=events.map(e=>e.role);assert(roles.includes(0)&&roles.includes(1)&&roles.includes(2));assert(roles.indexOf(0)<roles.indexOf(1)&&roles.indexOf(1)<roles.indexOf(2));}
const adjacentInputSupported=pairs.every(p=>p.counterStep1&&p.realStep>0&&Math.abs(p.realChainResidual)<1e-6&&p.matrixChains.every(m=>m.exact));
const dynamicInputSupported=adjacentInputSupported&&pairs.every(p=>p.gameStep>1e-6&&Math.abs(p.gameChainResidual)<1e-6);
const result={source:path.resolve(input),manifestUnmodified:manifest,provenance,frames,pairs,qualification:{transportComplete:true,adjacentInputSupported,dynamicInputSupported,actualJitterZero:frames.every(f=>f.jitter.every(v=>v===0)),temporalModelQualityAccepted:false,limits:'Old-layout candidate offsets plus actual shader/rect evidence; counter/time/matrix matches support sequence only. Disk stalls perturb timing; this is not performance or full guide/occlusion/quality acceptance.'}};
fs.mkdirSync(path.dirname(path.resolve(output)),{recursive:true});fs.writeFileSync(output,JSON.stringify(result,null,2));console.log(JSON.stringify({counters:frames.map(f=>f.counter),peakLogicalBytes:manifest.peakInFlightLogicalBytes,writeMilliseconds:manifest.writeMilliseconds,qualification:result.qualification,steps:pairs.map(p=>({game:p.gameStep,real:p.realStep}))},null,2));
