// Capture-specific numerical check of the observed exposure reduction/adaptation shader.
const fs=require('fs'),path=require('path'),assert=require('assert/strict');
const [indexFile,directory]=process.argv.slice(2);
assert(indexFile&&directory,'Usage: node analyze-exposure-20261028.cjs SOURCE_INDEX COLOR_READBACK_DIRECTORY');
const index=JSON.parse(fs.readFileSync(indexFile,'utf8'));
assert.equal(path.basename(index.capture),'Ace7Game__2026_10_28__02_30_38');
const records=fs.readFileSync(path.join(index.capture,'data.bin.rec')),fd=fs.openSync(path.join(index.capture,'data.bin'),'r');
function constant(draw,stage,slot){const binding=index.draws[draw][stage][slot],update=index.draws[draw].constantUpdates[binding];assert(update);const size=Number(records.readBigUInt64LE(update.handle*16)),offset=Number(records.readBigUInt64LE(update.handle*16+8));assert(offset+size<=fs.fstatSync(fd).size);const b=Buffer.alloc(size);assert.equal(fs.readSync(fd,b,0,size,offset),size);return {handle:update.handle,row:i=>[0,1,2,3].map(k=>b.readFloatLE(i*16+k*4)),uint:i=>[0,1,2,3].map(k=>b.readUInt32LE(i*16+k*4))};}
function raw(name,format,bpp){const b=fs.readFileSync(path.join(directory,name+'.raw'));assert.equal(b.readUInt32LE(8),format);assert.equal(b.readUInt32LE(12),bpp);const w=b.readUInt32LE(0),h=b.readUInt32LE(4);assert.equal(b.length,16+w*h*bpp);return {b,w,h};}
function half(n){const e=(n>>>10)&31,m=n&1023,sign=(n&32768)?-1:1;return sign*(e===31?(m?NaN:Infinity):e===0?m*2**-24:(1+m/1024)*2**(e-15));}
function unsignedFloat(n,bits){const e=n>>>bits,m=n&((1<<bits)-1);return e===31?(m?NaN:Infinity):e===0?m*2**(-14-bits):(1+m/2**bits)*2**(e-15);}
assert.equal(half(0x3c00),1);assert.equal(half(0xbc00),-1);assert.equal(half(1),2**-24);assert.equal(unsignedFloat(15<<6,6),1);
const before=raw('exposure-before',16,8),after=raw('exposure-after',16,8),used=raw('tonemap-exposure',16,8),reduced=raw('luminance-reduction',10,8),hdr=raw('scene-hdr',26,4);
assert.equal(before.w*before.h,1);assert.equal(after.w*after.h,1);assert(after.b.equals(used.b),'Tonemap exposure changed after adaptation');
const c=constant(482,'cb',0),[minX,minY,maxX,maxY]=c.uint(21),p18=c.row(18),p19=c.row(19),p20=c.row(20);
assert(minX<maxX&&minY<maxY&&maxX<=reduced.w&&maxY<=reduced.h);
let weighted=0,weight=0;
for(let x=minX;x<maxX;x++)for(let y=minY;y<maxY;y++){
  const wx=Math.max(1-2*p20[3]*Math.abs(x/(maxX-minX)-.5),0),wy=Math.max(1-2*p20[3]*Math.abs(y/(maxY-minY)-.5),0),w=Math.max(wx*wy,.05);
  const alpha=half(reduced.b.readUInt16LE(16+(y*reduced.w+x)*8+6));assert(Number.isFinite(alpha));weighted+=w*alpha;weight+=w;
}
const clamp=v=>Math.min(p18[3],Math.max(p18[2],v));
const encodedMean=weighted/weight,rawTarget=2**((encodedMean-p20[1])/p20[0])*6.25,target=clamp(rawTarget);
const old=[before.b.readFloatLE(16),before.b.readFloatLE(20)],actual=[after.b.readFloatLE(16),after.b.readFloatLE(20)];
const previous=p19[0]/(old[0]||1),delta=target-previous,rate=delta>0?p19[2]:p19[3],blend=1-2**(-rate*p19[1]);
const adapted=clamp(previous+delta*blend),predicted=[p19[0]/Math.max(adapted,.0001),p19[0]/Math.max(target,.0001)];
const error=predicted.map((v,i)=>Math.abs(v-actual[i]));assert(error.every(v=>v<5e-7),'Exposure CPU reproduction disagrees with GPU');
const tonemap=index.draws[525];assert.equal(index.names[index.views[tonemap.vs[0]].resource],'Texture2D 2020');
const tonemapCb=constant(525,'cb',0),mode=tonemapCb.uint(34)[0];
let lo=Infinity,hi=0,total=0,count=0,aboveOne=0,invalid=0;const luminance=[];
for(let y=0;y<1440;y++)for(let x=1280;x<3840;x++){
  const word=hdr.b.readUInt32LE(16+(y*hdr.w+x)*4),rgb=[unsignedFloat(word&2047,6),unsignedFloat((word>>>11)&2047,6),unsignedFloat(word>>>22,5)];
  for(const v of rgb){if(!Number.isFinite(v))invalid++;else{lo=Math.min(lo,v);hi=Math.max(hi,v);total+=v;count++;if(v>1)aboveOne++;}}
  if((y*2560+x-1280)%8===0)luminance.push(rgb[0]*.2126+rgb[1]*.7152+rgb[2]*.0722);
}
assert.equal(invalid,0);luminance.sort((a,b)=>a-b);
const baseline=path.resolve(directory,'../checkpoints/final.raw');assert(fs.readFileSync(path.join(directory,'final.raw')).equals(fs.readFileSync(baseline)),'Exposure readbacks changed final rendering');
const result={capture:path.basename(index.capture),exposure:{before:old,after:actual,tonemapVertexSlot0:'Texture2D 2020 red',statsBlob:c.handle,sampleRect:[minX,minY,maxX,maxY],encodedWeightedMean:encodedMean,unclampedTargetLuminance:rawTarget,clampedTargetLuminance:target,previousAdaptedLuminance:previous,adaptationBlend:blend,cpuPredicted:predicted,absoluteError:error,stopsFromActualRed:Math.log2(actual[0]),redMeaning:'scale / temporally adapted luminance; applied by tonemap vertex TEXCOORD1.x',greenMeaning:'scale / instantaneous clamped target luminance'},color:{tonemapMode:mode,branch:mode<3?'log-LUT input branch (not PQ input branch)':'other',hdr:{format:'R11G11B10_FLOAT',activeRect:[1280,0,2560,1440],finiteChannels:count,nonfiniteChannels:invalid,min:lo,max:hi,meanChannel:total/count,channelsAboveOne:aboveOne,sampledLuminanceP50:luminance[Math.floor(luminance.length*.5)],sampledLuminanceP95:luminance[Math.floor(luminance.length*.95)]}},finalBitExact:true,limits:'One frame with exposure near unity: identifies channel usage and verifies arithmetic here, not independent dynamic adaptation calibration or complete pre-exposure/color-space contract. CPU uses double arithmetic against float shader; tolerance 5e-7.'};
fs.writeFileSync(path.join(directory,'analysis.json'),JSON.stringify(result,null,2));fs.closeSync(fd);console.log(JSON.stringify(result,null,2));
