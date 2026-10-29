// Offline writeback receipts, not a temporal or quality score.
const fs=require('fs'),path=require('path'),assert=require('assert/strict');
const [baseline,neutralDir,activeDir]=process.argv.slice(2);
assert(baseline&&neutralDir&&activeDir,'Usage: node check-hdr-writeback.cjs CAPTURE_ARTIFACT_DIR NEUTRAL_DIR ACTIVE_DIR');
function raw(dir,name){const b=fs.readFileSync(path.join(dir,name+'.raw')),header=[0,4,8,12].map(i=>b.readUInt32LE(i));assert.equal(b.length,16+header[0]*header[1]*header[3]);return {b,w:header[0],h:header[1],format:header[2],bpp:header[3],header};}
function base(name){return raw(path.join(baseline,['post-effects','composite'].includes(name)?'checkpoints':'color'),name);}
const names=['scene-hdr','exposure-before','luminance-reduction','exposure-after','tonemap-exposure','post-tonemap','post-effects','composite','final'];
const receipts=names.map(name=>{const a=base(name),b=raw(neutralDir,name),c=raw(activeDir,name);assert.deepEqual(a.header,b.header);assert.deepEqual(a.header,c.header);assert(a.b.equals(b.b),'Neutral replay differs: '+name);return {name,neutralBitExact:true,activeBitExact:a.b.equals(c.b)};});
function uf(n,bits){const e=n>>>bits,m=n&((1<<bits)-1);return e===31?(m?NaN:Infinity):e===0?m*2**(-14-bits):(1+m/2**bits)*2**(e-15);}
function hdr(word){return [uf(word&2047,6),uf((word>>>11)&2047,6),uf(word>>>22,5)];}
const original=base('scene-hdr'),active=raw(activeDir,'scene-hdr');assert.equal(active.format,26);assert.equal(active.w,5120);assert.equal(active.h,1440);
const exp=raw(activeDir,'tonemap-exposure');const gain=exp.b.readFloatLE(16);assert(Number.isFinite(gain)&&gain>0);
let changed=0,outsideChanged=0,invalid=0,min=Infinity,max=0,fullyProtected=0,protectedChanged=0,maxAbsoluteHdrChange=0;
for(let y=0;y<1440;y++)for(let x=0;x<5120;x++){
 const p=16+(y*5120+x)*4,a=original.b.readUInt32LE(p),b=active.b.readUInt32LE(p);const inside=x>=1280&&x<3840;
 if(a!==b){changed++;if(!inside)outsideChanged++;}
 if(!inside)continue;
 const src=hdr(a),dst=hdr(b);for(let c=0;c<3;c++){if(!Number.isFinite(dst[c]))invalid++;else{min=Math.min(min,dst[c]);max=Math.max(max,dst[c]);maxAbsoluteHdrChange=Math.max(maxAbsoluteHdrChange,Math.abs(src[c]-dst[c]));}}
 if(Math.max(...src)*gain>=8){fullyProtected++;if(a!==b)protectedChanged++;}
}
assert(changed>0);assert.equal(outsideChanged,0);assert.equal(invalid,0);assert.equal(protectedChanged,0);
function rgb(image,p){const v=image.b.readUInt32LE(p);if(image.format===90)return [(v>>>16)&255,(v>>>8)&255,v&255];assert.equal(image.format,24);return [v&1023,(v>>>10)&1023,(v>>>20)&1023];}
function imageDelta(name){const a=base(name),b=raw(activeDir,name),scale=b.format===24?255/1023:1;let changed=0,sum=0,peak=0,outside=0;for(let i=0;i<a.w*a.h;i++){const p=16+i*4;if(a.b.readUInt32LE(p)!==b.b.readUInt32LE(p)){changed++;const x=i%a.w;if(x<1280||x>=3840)outside++;}const x=rgb(a,p),y=rgb(b,p);for(let c=0;c<3;c++){const e=Math.abs(x[c]-y[c])*scale;sum+=e;peak=Math.max(peak,e);}}return {changedPixels:changed,changedOutsideActiveRect:outside,meanAbsRgbChange8Bit:sum/(a.w*a.h*3),maxRgbChange8Bit:peak};}
const post=raw(activeDir,'post-effects'),composite=raw(activeDir,'composite'),final=raw(activeDir,'final');assert.equal(post.format,90);assert.equal(composite.format,24);
const basePost=base('post-effects'),baseComposite=base('composite');
let uiPixels=0,baseUiPixels=0,uiMaskDifference=0,finalAfterComposite=0;
for(let i=0;i<post.w*post.h;i++){
 const p=16+i*4;
 // A tolerance identifies compositing changes despite BGRA8 -> R10 quantization; not an alpha/layer recovery.
 const different=(a,b)=>a.some((v,c)=>Math.abs(v-b[c]*255/1023)>2);
 const ui=different(rgb(post,p),rgb(composite,p)),prior=different(rgb(basePost,p),rgb(baseComposite,p));uiPixels+=ui;baseUiPixels+=prior;uiMaskDifference+=ui!==prior;
 finalAfterComposite+=composite.b.readUInt32LE(p)!==final.b.readUInt32LE(p);
}
const exposure=Object.fromEntries(['exposure-before','exposure-after','tonemap-exposure'].map(n=>{const a=base(n),b=raw(activeDir,n);return [n,{baseline:[a.b.readFloatLE(16),a.b.readFloatLE(20)],active:[b.b.readFloatLE(16),b.b.readFloatLE(20)],bitExact:a.b.equals(b.b)}]}));
const uiResources=['ui-layer','ui-aux-849','ui-aux-850'].map(name=>{const a=raw(neutralDir,name),b=raw(activeDir,name);assert.deepEqual(a.header,b.header);assert(a.b.equals(b.b),'UI resource altered: '+name);return {name,width:a.w,height:a.h,format:a.format,bitExact:true};});
const result={uiResources,receipts,hdr:{changedPixels:changed,changedOutsideActiveRect:outsideChanged,nonfiniteChannels:invalid,min,max,maxAbsoluteHdrChange,fullyProtectedPixels:fullyProtected,fullyProtectedChangedPixels:protectedChanged},exposure,images:Object.fromEntries(['post-tonemap','post-effects','composite','final'].map(n=>[n,imageDelta(n)])),uiDiagnostic:{pixelsChangedByComposite:uiPixels,baselinePixelsChangedByComposite:baseUiPixels,maskDifferencePixels:uiMaskDifference,pixelsChangedAfterComposite:finalAfterComposite,method:'Approximate compositor-change mask from scene vs composite with 2/255 RGB tolerance; not proof of exact HUD alpha or separate UI-resource equality'},limits:'Single reset frame, baseline exposure explicitly supplied offline. Changed luminance statistics with unchanged adapted exposure do not independently establish dynamic exposure feedback; final image changes are not quality errors. File transport is not live GPU interop.'};
// Reproduce the captured exposure shader's reduction/target clamp using its actual CB bytes.
const index=JSON.parse(fs.readFileSync(path.join(baseline,'analysis/source-index.json'),'utf8'));
assert.equal(path.basename(index.capture),'Ace7Game__2026_10_28__02_30_38');
const binding=index.draws[482].cb[0],update=index.draws[482].constantUpdates[binding];assert(update);
const records=fs.readFileSync(path.join(index.capture,'data.bin.rec')),size=Number(records.readBigUInt64LE(update.handle*16)),offset=Number(records.readBigUInt64LE(update.handle*16+8)),fd=fs.openSync(path.join(index.capture,'data.bin'),'r');
const constants=Buffer.alloc(size);try{assert.equal(fs.readSync(fd,constants,0,size,offset),size);}finally{fs.closeSync(fd);}
const row=n=>[0,1,2,3].map(k=>constants.readFloatLE(n*16+k*4)),p18=row(18),p19=row(19),p20=row(20),rect=[0,1,2,3].map(k=>constants.readUInt32LE(21*16+k*4));
function half(v){const e=(v>>>10)&31,m=v&1023;return (v&32768?-1:1)*(e===31?(m?NaN:Infinity):e===0?m*2**-24:(1+m/1024)*2**(e-15));}
function adaptation(dir){const image=raw(dir,'luminance-reduction');assert.equal(image.format,10);const [x0,y0,x1,y1]=rect;assert(x0<x1&&y0<y1&&x1<=image.w&&y1<=image.h);let sum=0,weights=0;for(let x=x0;x<x1;x++)for(let y=y0;y<y1;y++){const wx=Math.max(1-2*p20[3]*Math.abs(x/(x1-x0)-.5),0),wy=Math.max(1-2*p20[3]*Math.abs(y/(y1-y0)-.5),0),w=Math.max(wx*wy,.05),a=half(image.b.readUInt16LE(16+(y*image.w+x)*8+6));assert(Number.isFinite(a));sum+=w*a;weights+=w;}const mean=sum/weights,unclamped=2**((mean-p20[1])/p20[0])*6.25,clamp=v=>Math.min(p18[3],Math.max(p18[2],v)),target=clamp(unclamped),previous=raw(dir,'exposure-before').b.readFloatLE(16),old=p19[0]/previous,delta=target-old,rate=delta>0?p19[2]:p19[3],blend=1-2**(-rate*p19[1]),predicted=p19[0]/Math.max(clamp(old+delta*blend),.0001),actual=raw(dir,'exposure-after').b.readFloatLE(16);assert(Math.abs(predicted-actual)<5e-7);return {weightedMean:mean,unclampedTargetLuminance:unclamped,clampedTargetLuminance:target,predictedRedGain:predicted,actualRedGain:actual,absoluteGainError:Math.abs(predicted-actual)};}
result.exposureAdaptation={constantBlob:update.handle,targetClamp:[p18[2],p18[3]],baseline:adaptation(neutralDir),active:adaptation(activeDir),limit:'Current-frame arithmetic only, not dynamic exposure or live input exposure timing.'};
assert(result.images.final.changedPixels>0);assert.equal(finalAfterComposite,0);fs.writeFileSync(path.join(activeDir,'writeback-checks.json'),JSON.stringify(result,null,2));console.log(JSON.stringify(result,null,2));
