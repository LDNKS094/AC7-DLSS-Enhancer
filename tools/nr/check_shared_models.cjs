// Verify per-feature real NGX shared-resource results against the preserved eight-frame file baseline.
const fs=require('fs'),path=require('path'),assert=require('assert/strict'),crypto=require('crypto');
const [root,baseline,output]=process.argv.slice(2);assert(root&&baseline&&output,'Usage: check_shared_models.cjs SHARED_RUN FILE_BASELINE NEW_JSON');assert(!fs.existsSync(output),'Refusing prior evidence');
const half=Float64Array.from({length:65536},(_,i)=>{const sign=i&32768?-1:1,e=i>>10&31,m=i&1023;return e===31?NaN:sign*(e?2**(e-15)*(1+m/1024):2**-14*m/1024)});assert.equal(half[0x3c00],1);assert(Number.isNaN(half[0x7e00]));
const sha=b=>crypto.createHash('sha256').update(b).digest('hex'),runs=[];
for(const [mode,feature,input,role,expectedResets]of [
 ['nr-output','NR','nr-input','nr-output.raw',[1,0,0,0,0,0,0,0]],
 ['history','SR','sr-input','sr-output.raw',[1,0,0,0,0,0,0,0]],
 ['reset-control','SR','sr-input','sr-output.raw',[1,1,1,1,1,1,1,1]]]){
 const log=fs.readFileSync(path.join(root,mode==='nr-output'?'nr-process.log':mode+'-process.log'),'utf8');
 assert(log.includes('SHARED_BRIDGE: D3D11 input copies'));assert(log.includes('D3D12 input uploads=0'));
 const ids=[...log.matchAll(new RegExp(feature+'_FRAME index=(\\d+) Reset=(\\d) sharedFeature=(\\w+) colorResource=(\\w+) outputResource=(\\w+)','g'))];
 assert.equal(ids.length,8);assert.deepEqual(ids.map(m=>+m[1]),[0,1,2,3,4,5,6,7]);assert.deepEqual(ids.map(m=>+m[2]),expectedResets);
 for(const c of [3,4,5])for(const id of ids)assert.equal(id[c],ids[0][c]);
 const reads=feature==='NR'?24:32;assert.equal([...log.matchAll(/InputRoundtrip .* bit-exact/g)].length,reads);
 for(const pattern of [/EvaluateFeature result=0x1/g,/SharedOutputRoundtrip D3D12-to-D3D11 bit-exact/g,/D3D11DebugErrors=0/g,/D3D12DebugErrors=0/g,/Evaluate shared GPU fence completed/g])assert.equal([...log.matchAll(pattern)].length,8);
 for(const stage of ['ReleaseFeature','DestroyParameters','Shutdown'])assert(log.includes(stage+' result=0x1'));
 const contract=JSON.parse(fs.readFileSync(path.join(baseline,input,'sequence-contract.json')));assert.equal(contract.frames.length,8);
 const times=[...log.matchAll(/SR_FRAME_TIME milliseconds=([\d.e+-]+)/g)].map(m=>+m[1]);if(feature==='SR')assert.equal(times.length,8);
 const frames=[];
 for(let f=0;f<8;f++){
  for(const [name,hash]of Object.entries(contract.frames[f].hashes))assert.equal(sha(fs.readFileSync(path.join(baseline,input,`frame${f}`,name))),hash,'Source fixture mutated');
  if(feature==='SR'){const b=fs.readFileSync(path.join(baseline,input,`frame${f}/frame-time.bin`));assert(Math.abs(times[f]-b.readFloatLE(0))<.0005);}
  const a=fs.readFileSync(path.join(root,mode,`frame${f}`,role)),b=fs.readFileSync(path.join(baseline,mode,`frame${f}`,role));
  assert.deepEqual([0,4,8,12].map(i=>a.readUInt32LE(i)),[2560,1440,10,8]);assert.equal(a.length,16+2560*1440*8);
  let finiteRgb=0,max=0,changedRgbPixels=0,mean=0,maxDifference=0;
  for(let i=16;i<a.length;i+=8){let changed=false;for(let c=0;c<3;c++){const x=half[a.readUInt16LE(i+2*c)],y=half[b.readUInt16LE(i+2*c)];assert(Number.isFinite(x));if(feature==='NR')assert(x>=0&&x<=1);finiteRgb++;max=Math.max(max,x);const d=Math.abs(x-y);mean+=d;maxDifference=Math.max(maxDifference,d);changed||=a.readUInt16LE(i+2*c)!==b.readUInt16LE(i+2*c);}changedRgbPixels+=changed;assert(Number.isFinite(half[a.readUInt16LE(i+6)]));}
  frames.push({frame:f,reset:expectedResets[f],wholeFileExactToFileBaseline:a.equals(b),sha256:sha(a),finiteRgb,max,changedRgbPixels,meanAbsoluteHdrDifference:mean/finiteRgb,maxAbsoluteHdrDifference:maxDifference});
 }
 runs.push({mode,feature,resets:expectedResets,featureAndTexturesStable:true,inputReadbacksExact:reads,outputD3D12ToD3D11ReadbacksExact:8,d3d11DebugErrors:0,d3d12DebugErrors:0,frames});
}
const result={kind:'EIGHT_FRAME_PER_FEATURE_PUBLIC_NGX_SHARED_RESOURCE_EXECUTION',runs,newNrEvaluations:8,newSrEvaluations:16,allOutputsWholeFileExactToFileBaseline:runs.every(r=>r.frames.every(f=>f.wholeFileExactToFileBaseline)),actualFrameTimesVerified:true,completeGpuNrHdrSrChain:false,gameWriteback:false,performanceAccepted:false,qualityAccepted:false,limits:'D3D11 fixture textures feed public shared resources directly consumed by persistent NGX. Return to D3D11 verified. NR and SR run separately; existing prepared SR inputs include file-restored NR HDR. Diagnostic NaN output seed/readbacks/CPU polling are not a realtime performance path.'};
fs.writeFileSync(output,JSON.stringify(result,null,2));console.log(JSON.stringify({exactToBaseline:result.allOutputsWholeFileExactToFileBaseline,runs:runs.map(r=>({mode:r.mode,exact:r.frames.map(f=>f.wholeFileExactToFileBaseline),maxDiff:r.frames.map(f=>f.maxAbsoluteHdrDifference)})),newNrEvaluations:8,newSrEvaluations:16,completeGpuChain:false},null,2));
