// Same SR source/output, only original FXAA effect differs. No ground-truth quality score.
const fs=require('fs'),path=require('path'),assert=require('assert/strict'),crypto=require('crypto');
const [root]=process.argv.slice(2);assert(root,'Usage: node check-fxaa-comparison.cjs OFFICIAL_SR_DIR');
const out=path.join(root,'fxaa-checks-v2.json');assert(!fs.existsSync(out),'Refusing to overwrite FXAA evidence');
function raw(dir,name){const b=fs.readFileSync(path.join(root,dir,name+'.raw'));assert(b.length>=16);const [w,h,format,bpp]=Array.from({length:4},(_,i)=>b.readUInt32LE(i*4));assert.equal(b.length,16+w*h*bpp);return{b,w,h,format,bpp};}
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
function rgb(a,x,y){const v=a.b.readUInt32LE(16+(y*a.w+x)*4);if(a.format===24)return[v&1023,(v>>>10)&1023,(v>>>20)&1023].map(v=>v*255/1023);assert([87,90].includes(a.format));return[(v>>>16)&255,(v>>>8)&255,v&255];}
function differences(a,b,rect=[1280,0,2560,1440]){assert.deepEqual([a.w,a.h,a.format,a.bpp],[b.w,b.h,b.format,b.bpp]);let changed=0,outside=0,peak=0,sum=0;for(let y=0;y<a.h;++y)for(let x=0;x<a.w;++x){const i=16+(y*a.w+x)*4;if(a.b.readUInt32LE(i)!==b.b.readUInt32LE(i)){++changed;if(x<rect[0]||x>=rect[0]+rect[2]||y<rect[1]||y>=rect[1]+rect[3])++outside;}const p=rgb(a,x,y),q=rgb(b,x,y);for(let c=0;c<3;++c){const e=Math.abs(p[c]-q[c]);peak=Math.max(peak,e);sum+=e;}}return{changedPixels:changed,changedOutside:outside,meanAbsRgb8BitFullAllocation:sum/(a.w*a.h*3),meanAbsRgb8BitActive:sum/(rect[2]*rect[3]*3),maxAbsRgb8Bit:peak};}
const names=fs.readdirSync(path.join(root,'writeback-active')).filter(n=>n.endsWith('.raw')).map(n=>n.slice(0,-4));
for(const name of names)assert(raw('writeback-active',name).b.equals(raw('fxaa-retained',name).b),'Default path changed '+name);
const bypassDir='fxaa-bypass-v2';
const unchanged=[];for(const name of ['scene-hdr','luminance-reduction','exposure-before','exposure-after','tonemap-exposure','post-tonemap','ui-layer','ui-aux-849','ui-aux-850']){assert(raw('fxaa-retained',name).b.equals(raw(bypassDir,name).b),'Upstream/UI changed '+name);unchanged.push(name);}
const retainedAa=raw('fxaa-retained','post-fxaa'),bypassedAa=raw(bypassDir,'post-fxaa'),preAa=raw(bypassDir,'post-tonemap');
for(let y=0;y<1440;++y)for(let x=0;x<5120;++x){const at=16+(y*5120+x)*4;assert.equal(bypassedAa.b[at+3],retainedAa.b[at+3],'FXAA alpha changed');if(x>=1280&&x<3840)assert(bypassedAa.b.subarray(at,at+3).equals(preAa.b.subarray(at,at+3)),'Active RGB bypass mismatch');else assert(bypassedAa.b.subarray(at,at+4).equals(retainedAa.b.subarray(at,at+4)),'FXAA padding changed');}
const stages={};for(const name of ['post-fxaa','post-effects','composite','final']){stages[name]=differences(raw('fxaa-retained',name),raw(bypassDir,name));assert.equal(stages[name].changedOutside,0);}
assert(stages.final.changedPixels>0,'Bypass had no final effect');
const a=raw('fxaa-retained','post-effects'),b=raw(bypassDir,'post-effects');
const rois=[{name:'aircraft-tail',box:[1160,880,1288,1008]},{name:'aircraft-silhouette',box:[680,820,1850,1280]},{name:'sea-specular',box:[320,900,900,1350]},{name:'sky-cloud',box:[900,160,1800,700]}].map(({name,box})=>{
    let sa=0,sb=0,n=0,difference=0;const lum=(im,x,y)=>{const c=rgb(im,x+1280,y);return .2126*c[0]+.7152*c[1]+.0722*c[2]};
    for(let y=box[1];y<box[3]-1;++y)for(let x=box[0];x<box[2]-1;++x){const av=lum(a,x,y),bv=lum(b,x,y);sa+=Math.abs(av-lum(a,x+1,y))+Math.abs(av-lum(a,x,y+1));sb+=Math.abs(bv-lum(b,x+1,y))+Math.abs(bv-lum(b,x,y+1));difference+=Math.abs(av-bv);++n;}
    return{name,workingRectBox:box,meanAbsLumaDifference8Bit:difference/n,measurementStage:'post-effects before UI',retainedMeanAbsNeighborGradient8Bit:sa/(2*n),bypassMeanAbsNeighborGradient8Bit:sb/(2*n),retainedGradientOverBypass:sb?sa/sb:null,meaning:'Encoded-display neighbor variation; neither sharpness ground truth nor temporal/aliasing score'};
});
for(const [dir,bypass]of [['fxaa-retained',false],[bypassDir,true]]){const run=JSON.parse(fs.readFileSync(path.join(root,dir,'run.json')));assert.equal(run.status,0);assert.equal(run.bypass,bypass);assert.equal(run.stdout.includes('FXAA effect bypass:'),bypass);}
const result={sameSrPackedSha256:hash(fs.readFileSync(path.join(root,'writeback-input/sr-packed.raw'))),retainedPathBitExactCheckpoints:names,unchangedUpstreamAndUi:unchanged,bypassActiveRgbEqualsPostTonemap:true,bypassAlphaAndPaddingPreserved:true,stages,rois,preExposureAssumption:1,newModelRuns:0,gameRuns:0,limits:'One existing Reset/DLAA output at same size, original FXAA draw still executed then active RGB copied over via CPU staging with alpha/padding preserved. No performance, temporal shimmer, motion, upscaling or ground-truth verdict.'};
fs.writeFileSync(out,JSON.stringify(result,null,2));console.log(JSON.stringify(result,null,2));
