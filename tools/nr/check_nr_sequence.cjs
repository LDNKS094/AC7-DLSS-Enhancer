// Check bounded public NR sequence execution; Reset calls alone do not establish NR's internal temporal behavior.
const fs=require('fs'),path=require('path'),assert=require('assert/strict'),crypto=require('crypto');
const [experiment,output]=process.argv.slice(2);assert(experiment&&output,'Usage: check_nr_sequence.cjs EXPERIMENT NEW_JSON');assert(!fs.existsSync(output),'Refusing existing evidence');
const contract=JSON.parse(fs.readFileSync(path.join(experiment,'nr-input/sequence-contract.json'))),count=contract.frames.length;assert([2,8].includes(count));
const expected=Array.from({length:count},(_,i)=>i?0:1);assert.deepEqual(contract.resetSequence,expected);
const log=fs.readFileSync(path.join(experiment,'nr-process.log'),'utf8'),ids=[...log.matchAll(/NR_FRAME index=(\d+) Reset=(\d) sharedFeature=(\w+) colorResource=(\w+) outputResource=(\w+)/g)];
assert.equal(ids.length,count);assert.deepEqual(ids.map(m=>+m[1]),Array.from({length:count},(_,i)=>i));assert.deepEqual(ids.map(m=>+m[2]),expected);
for(const col of [3,4,5])for(const row of ids)assert.equal(row[col],ids[0][col]);
assert.equal([...log.matchAll(/EvaluateFeature result=0x1/g)].length,count);assert.equal([...log.matchAll(/InputRoundtrip .* bit-exact/g)].length,count*3);assert.equal([...log.matchAll(/D3D12DebugErrors=0/g)].length,count);
for(const stage of ['ReleaseFeature','DestroyParameters','Shutdown'])assert(log.includes(stage+' result=0x1'));
const half=Float64Array.from({length:65536},(_,i)=>{const sign=i&32768?-1:1,e=i>>10&31,m=i&1023;return e===31?NaN:sign*(e?2**(e-15)*(1+m/1024):2**-14*m/1024)});assert.equal(half[0x3c00],1);assert(Number.isNaN(half[0x7e00]));
const hash=b=>crypto.createHash('sha256').update(b).digest('hex'),frames=[];
for(let f=0;f<count;f++){
 for(const [name,sha]of Object.entries(contract.frames[f].hashes))assert.equal(hash(fs.readFileSync(path.join(experiment,`nr-input/frame${f}`,name))),sha);
 const b=fs.readFileSync(path.join(experiment,`nr-output/frame${f}/nr-output.raw`));assert.deepEqual([0,4,8,12].map(i=>b.readUInt32LE(i)),[2560,1440,10,8]);assert.equal(b.length,16+2560*1440*8);
 let min=Infinity,max=-Infinity;for(let i=16;i<b.length;i+=8){for(let c=0;c<3;c++){const v=half[b.readUInt16LE(i+c*2)];assert(Number.isFinite(v)&&v>=0&&v<=1,'NR RGB must be finite public SDR');min=Math.min(min,v);max=Math.max(max,v);}assert(Number.isFinite(half[b.readUInt16LE(i+6)]));}
 frames.push({frame:f,reset:expected[f],finiteRgb:11059200,min,max,sha256:hash(b)});
}
const result={kind:'BOUNDED_PUBLIC_NR_SEQUENCE_EXECUTION',frames,sameFeatureAndResources:true,inputReadbacksExact:3*count,debugErrors:0,internalNrTemporalBehaviorProven:false};fs.writeFileSync(output,JSON.stringify(result,null,2));console.log(JSON.stringify(result,null,2));
