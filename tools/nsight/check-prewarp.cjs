// Fixed single-frame prewarp boundaries and actual distortion field measurements.
const fs=require('fs'),path=require('path'),assert=require('assert/strict'),crypto=require('crypto');
const [root,captureDir]=process.argv.slice(2);assert(root&&captureDir,'Usage: node check-prewarp.cjs PREWARP_ARTIFACT_DIR NORMAL_CAPTURE_ARTIFACT_DIR');
const out=path.join(root,'checks.json');assert(!fs.existsSync(out),'Refusing to overwrite prewarp evidence');
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
function raw(dir,name){const b=fs.readFileSync(path.join(dir,name+'.raw'));assert(b.length>=16);const [w,h,format,bpp]=Array.from({length:4},(_,i)=>b.readUInt32LE(i*4));assert.equal(b.length,16+w*h*bpp);return{b,w,h,format,bpp};}
function delta(a,b){assert.deepEqual([a.w,a.h,a.format,a.bpp],[b.w,b.h,b.format,b.bpp]);let inside=0,outside=0;for(let y=0;y<a.h;++y)for(let x=0;x<a.w;++x){const at=16+(y*a.w+x)*a.bpp;if(!a.b.subarray(at,at+a.bpp).equals(b.b.subarray(at,at+a.bpp))){if(x>=1280&&x<3840&&y<1440)++inside;else ++outside;}}return{changedInside:inside,changedOutside:outside};}
const base=path.join(root,'baseline'),neutral=path.join(root,'neutral'),names=fs.readdirSync(base).filter(n=>n.endsWith('.raw')).map(n=>n.slice(0,-4));
for(const n of names)assert(raw(base,n).b.equals(raw(neutral,n).b),'Neutral changed '+n);
const before=raw(base,'scene-before-distortion'),pre=raw(base,'scene-prewarp'),post=raw(base,'scene-postwarp');assert(before.b.equals(pre.b),'Unexpected scene change during distortion writers');
const da=raw(base,'depth-prewarp'),db=raw(base,'depth-postwarp');assert.deepEqual([da.w,da.h,da.format,da.bpp],[5120,1440,19,8]);assert.deepEqual([db.w,db.h,db.format,db.bpp],[5120,1440,19,8]);let stencilChanged=0;
for(let i=16;i<da.b.length;i+=8){assert.equal(da.b.readUInt32LE(i),db.b.readUInt32LE(i),'32bit depth changed during warp');stencilChanged+=da.b[i+4]!==db.b[i+4];}
assert(raw(base,'motion-prewarp').b.equals(raw(base,'motion-postwarp').b),'Motion changed during warp');
assert(post.b.equals(fs.readFileSync(path.join(captureDir,'color/scene-hdr.raw'))),'Postwarp differs from prior capture');assert(raw(base,'final').b.equals(fs.readFileSync(path.join(captureDir,'color/final.raw'))),'Readbacks changed original final');
const changes=delta(pre,post);assert.equal(changes.changedOutside,0);
const field=raw(base,'distortion-field');assert.deepEqual([field.w,field.h,field.format,field.bpp],[5120,1440,90,4]);
let nonzero=0,maxX=0,maxY=0,maxPixels=0,sum=0,bounds=[Infinity,Infinity,-Infinity,-Infinity],worst=null;const shifts=[];
for(let y=0;y<1440;++y)for(let x=1280;x<3840;++x){const at=16+(y*5120+x)*4,B=field.b[at],G=field.b[at+1],R=field.b[at+2],A=field.b[at+3];
    const dx=(R-B)/255*.25*5120,dy=(G-A)/255*.25*1440,len=Math.hypot(dx,dy);
    if(dx||dy){++nonzero;sum+=len;shifts.push(len);bounds=[Math.min(bounds[0],x-1280),Math.min(bounds[1],y),Math.max(bounds[2],x-1280),Math.max(bounds[3],y)];}
    maxX=Math.max(maxX,Math.abs(dx));maxY=Math.max(maxY,Math.abs(dy));if(len>maxPixels){maxPixels=len;worst={workingRectXY:[x-1280,y],rgba:[R,G,B,A],dx,dy};}
}
shifts.sort((a,b)=>a-b);
const index=JSON.parse(fs.readFileSync(path.join(captureDir,'analysis/source-index.json'))),source=fs.readFileSync(path.join(index.capture,'Frame0Part01.cpp'),'utf8').split('\n'),between=source.slice(index.draws[465].line-1,index.draws[466].line-1);
assert(!between.some(l=>/My_ID3D11DeviceContext_(Dispatch|Copy|Clear|Draw)/.test(l)&&!l.includes('DrawIndexed')),'Unexpected data mutation between draws');assert.equal(between.filter(l=>l.includes('My_ID3D11DeviceContext_DrawIndexed')).length,1);
for(const n of ['baseline','neutral'])assert.equal(JSON.parse(fs.readFileSync(path.join(root,n,'run.json'))).status,0);
const result={capture:path.basename(index.capture),candidateBoundary:'after465 before466 applies distortion',sceneLastDirectWriter:460,distortionWriters:[461,462,463,464,465],neutralBitExactCheckpoints:names,scene460Equals465:true,depth32AndMotionUnchangedAcrossWarp:true,stencilChangedPixelsAcrossWarp:stencilChanged,postwarpMatchesPriorCapture:true,finalMatchesPriorCapture:true,colorWarpChanges:changes,distortionField:{format:'BGRA8_TYPELESS sampled as normalized RGBA; delta=(xy-zw)*.25',nonzeroTexelsAtCenters:nonzero,activeTexelFraction:nonzero/(2560*1440),maxAbsoluteXInputPixels:maxX,maxAbsoluteYInputPixels:maxY,maxMagnitudeInputPixels:maxPixels,meanMagnitudeAmongNonzero:nonzero?sum/nonzero:0,p95MagnitudeAmongNonzero:nonzero?shifts[Math.floor((shifts.length-1)*.95)]:0,workingRectBounds:nonzero?bounds:null,worst,limit:'Texel-center displacement before bilinear sampling/bounds fallback; exact pixel shader warp/output requires separate reproduction'},candidatePrewarpSha256:hash(pre.b),priorPostwarpSha256:hash(post.b),declaredDecision:'Prefer before color-warp boundary for next NR/SR diagnostic; do NOT reuse old postwarp model results there. Original distortion pass applies after model.',limits:'Single frame, identity HDR codec only. Does not certify transparent/water/cloud guide coverage, all earlier color offsets, history/jitter, high-output distortion UV or real-time safety.',newModelRuns:0,gameRuns:0};
fs.writeFileSync(out,JSON.stringify(result,null,2));console.log(JSON.stringify(result,null,2));
