// Compare two real-frame SR runs; differences indicate history-sensitive output, not a quality score.
const fs=require('fs'),path=require('path'),assert=require('assert/strict'),crypto=require('crypto');
const [directory,out]=process.argv.slice(2);assert(directory&&out,'Usage: node check_sr_pair.cjs EXPERIMENT_DIRECTORY NEW_JSON');assert(!fs.existsSync(out),'Refusing existing evidence');
const half=Float64Array.from({length:65536},(_,i)=>{const s=i&32768?-1:1,e=(i>>10)&31,m=i&1023;return e===31?(m?NaN:s*Infinity):s*(e?2**(e-15)*(1+m/1024):2**-14*m/1024);});
assert.equal(half[0x3c00],1);assert.equal(half[0x4000],2);assert(Number.isNaN(half[0x7e00]));
const input=fs.existsSync(path.join(directory,'sr-input/sequence-contract.json'))?'sr-input':'input';
const contract=JSON.parse(fs.readFileSync(path.join(directory,input,'sequence-contract.json'))),stats=[],comparisons=[];
const frameCount=contract.frames.length;assert([2,8].includes(frameCount));
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
for(const mode of ['history','reset-control']) {
 const log=fs.readFileSync(path.join(directory,mode+'-process.log'),'utf8');
 const identities=[...log.matchAll(/SR_FRAME index=(\d) Reset=(\d) sharedFeature=(\w+) colorResource=(\w+) outputResource=(\w+)/g)];
 assert.equal(identities.length,frameCount);assert.deepEqual(identities.map(m=>Number(m[1])),Array.from({length:frameCount},(_,i)=>i));assert.deepEqual(identities.map(m=>Number(m[2])),Array.from({length:frameCount},(_,i)=>mode==='history'&&i>0?0:1));
 for(const col of [3,4,5])for(const identity of identities)assert.equal(identities[0][col],identity[col],'Resource/feature identity changed');
 assert.equal([...log.matchAll(/InputRoundtrip .* bit-exact/g)].length,4*frameCount);assert.equal([...log.matchAll(/D3D12DebugErrors=0/g)].length,frameCount);
 assert.equal([...log.matchAll(/EvaluateFeature result=0x1/g)].length,frameCount);assert(log.includes('ReleaseFeature result=0x1')&&log.includes('DestroyParameters result=0x1')&&log.includes('Shutdown result=0x1'));
 if(frameCount===8){
  const times=[...log.matchAll(/SR_FRAME_TIME milliseconds=([\d.e+-]+)/g)].map(m=>Number(m[1]));assert.equal(times.length,8);
  for(let f=0;f<8;f++){const b=fs.readFileSync(path.join(directory,`${input}/frame${f}/frame-time.bin`));assert.equal(b.length,4);assert(Math.abs(times[f]-b.readFloatLE(0))<.0005,'Captured SR frame time mismatch');}
 }
 for(let f=0;f<frameCount;f++){
  for(const [name,sha]of Object.entries(contract.frames[f].hashes))assert.equal(hash(fs.readFileSync(path.join(directory,`${input}/frame${f}`,name))),sha,'Prepared input changed');
  const b=fs.readFileSync(path.join(directory,mode,`frame${f}/sr-output.raw`));assert.deepEqual([0,4,8,12].map(i=>b.readUInt32LE(i)),[2560,1440,10,8]);assert.equal(b.length,16+2560*1440*8);
  let finite=0,invalid=0,aboveOne=0,negative=0,min=Infinity,max=-Infinity,invalidAlpha=0;
  for(let i=16;i<b.length;i+=8){for(let c=0;c<3;c++){const v=half[b.readUInt16LE(i+c*2)];if(!Number.isFinite(v)){invalid++;continue;}finite++;min=Math.min(min,v);max=Math.max(max,v);aboveOne+=v>1;negative+=v<0;}invalidAlpha+=!Number.isFinite(half[b.readUInt16LE(i+6)]);}
  assert.equal(invalid,0);assert.equal(finite,11059200);assert(aboveOne>0);assert.equal(invalidAlpha,0);
  stats.push({mode,frame:f,reset:Number(identities[f][2]),sha256:hash(b),finiteRgb:finite,invalidRgb:invalid,aboveOne,negative,min,max,invalidAlpha});
 }
}
for(let f=0;f<frameCount;f++){
 const a=fs.readFileSync(path.join(directory,'history',`frame${f}/sr-output.raw`)),b=fs.readFileSync(path.join(directory,'reset-control',`frame${f}/sr-output.raw`));
 let pixels=0,sum=0,max=0;
 for(let i=16;i<a.length;i+=8){let different=false;for(let c=0;c<3;c++){const x=a.readUInt16LE(i+c*2),y=b.readUInt16LE(i+c*2),delta=Math.abs(half[x]-half[y]);different||=x!==y;sum+=delta;max=Math.max(max,delta);}pixels+=different;}
 comparisons.push({frame:f,wholeFileExact:a.equals(b),changedRgbPixels:pixels,meanAbsoluteHdrDifference:sum/11059200,maxAbsoluteHdrDifference:max,interpretation:'Diagnostic difference only, not error or quality score'});
}
assert(comparisons[0].wholeFileExact,'First-frame control differs; cannot attribute second-frame differences only to Reset');
const result={kind:'REAL_PAIR_SAME_FEATURE_SR_HISTORY_EXECUTION',frames:contract.frames.map(f=>f.sourceFrameCandidate),stats,comparisons,historyBranchSecondFrameReset:0,resetControlSecondFrameReset:1,featureAndTextureIdentitiesStableWithinEachRun:true,inputReadbacksExactPerRun:4*frameCount,debugErrors:0,preExposureAssumption:1,jitter:[0,0],nrApplied:contract.nrApplied===true,historySensitiveOutputObserved:comparisons.slice(1).some(c=>c.changedRgbPixels>0),qualityAccepted:false,limits:contract.limits};
fs.writeFileSync(out,JSON.stringify(result,null,2));console.log(JSON.stringify(result,null,2));
