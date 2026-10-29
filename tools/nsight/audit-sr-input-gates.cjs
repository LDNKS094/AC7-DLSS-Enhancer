// Fixed normal-capture evidence only; does not manufacture pre-exposure or temporal history.
const fs=require('fs'),path=require('path'),assert=require('assert/strict');
const [directory,out]=process.argv.slice(2);
assert(directory&&out,'Usage: node audit-sr-input-gates.cjs NORMAL_CAPTURE_ARTIFACT_DIR NEW_REPORT_JSON');
assert(!fs.existsSync(out),'Report must be new');
const index=JSON.parse(fs.readFileSync(path.join(directory,'analysis/source-index.json'),'utf8'));
assert.equal(path.basename(index.capture),'Ace7Game__2026_10_28__02_30_38');
const prior=JSON.parse(fs.readFileSync(path.join(directory,'checkpoints/motion-analysis.json'),'utf8'));
const consumer=index.draws[prior.consumerDraw],entry=Object.entries(consumer.constantUpdates).find(([name,u])=>u.handle===prior.viewBlob&&Object.values(consumer.cb).includes(name));
assert(entry,'Verified view blob is not bound on the observed motion consumer');
const records=fs.readFileSync(path.join(index.capture,'data.bin.rec')),size=Number(records.readBigUInt64LE(prior.viewBlob*16)),offset=Number(records.readBigUInt64LE(prior.viewBlob*16+8));
assert(size>=115*16);
const bytes=Buffer.alloc(16),fd=fs.openSync(path.join(index.capture,'data.bin'),'r');
try{assert.equal(fs.readSync(fd,bytes,0,16,offset+114*16),16);}finally{fs.closeSync(fd);}
const jitter=[0,1,2,3].map(i=>bytes.readFloatLE(i*4));assert.deepEqual(jitter,prior.jitterCurrentPrevious);
function viewport(text){const flat=text.replace(/HexToFloat\(0x([0-9a-f]+)\/\*.*?\*\/\)/gi,(_,hex)=>{const b=Buffer.alloc(4);b.writeUInt32LE(parseInt(hex,16));return b.readFloatLE(0)+'f';});const values=[...flat.matchAll(/(-?\d+(?:\.\d+)?(?:e[+-]?\d+)?)f/gi)].map(m=>Number(m[1]));assert.equal(values.length,6);return {x:values[0],y:values[1],width:values[2],height:values[3],minDepth:values[4],maxDepth:values[5]};}
const passes=[467,468,481,482,483,525,526,528,545].map(id=>{const d=index.draws[id];return {draw:id,file:d.file,sourceLine:d.line,viewport:viewport(d.viewport),targets:d.targets.map(v=>{const r=index.views[v]?.resource,t=index.resources[r];return {name:index.names[r],resource:r,size:t?[t.width,t.height]:null,format:t?.format}})};});
const active=passes.find(p=>p.draw===467).viewport;assert.equal(active.x,1280);assert.equal(active.width,2560);assert.equal(active.height,1440);
assert.equal(passes.find(p=>p.draw===526).viewport.width,5120);
assert.equal(passes.find(p=>p.draw===482).viewport.width,1);
// Observed encoded HDR C and final gain G do not uniquely identify hidden pre-exposure P.
// Both candidate decompositions have exactly the same visible color C * G.
const observedHdr=8,observedGain=.5;
const ambiguity=[1,4].map(preExposure=>({preExposure,unexposedRadiance:observedHdr/preExposure,exposure:observedGain*preExposure,displayLinear:(observedHdr/preExposure)*(observedGain*preExposure)}));
assert.equal(ambiguity[0].displayLinear,ambiguity[1].displayLinear);
const report={capture:path.basename(index.capture),jitterEvidence:{consumerDraw:prior.consumerDraw,viewBuffer:entry[0],blob:prior.viewBlob,row:114,observedCurrentPrevious:jitter,scope:'Actual one-frame constant bytes; not a temporal sequence or proof of global jitter disable'},passes,preExposure:{status:'unresolved',knownExposureChannel:'Actual tonemap R gain already verified, not an independently identified pre-exposure factor',ambiguityExample:ambiguity,limit:'Illustrates underdetermination, not candidate values assigned to AC7'},gates:{sameSizeSrEvaluate:'Native calibrated acceptance held pending pre-exposure/input contract; explicit standalone Reset diagnostics under declared assumptions are allowed',temporal:'Real adjacent-frame matrices/jitter/MV/history required; repeat replay is not adjacent frames',higherOutput:'Allocation/viewports/subrect/UV and postprocess sampling need explicit remapping; existing central padding is not a demonstrated higher-output viewport'},actionsNotTaken:['No GPU/NGX or game launch','No renderer, SDK, model, driver or config changes','No default PreExposure=1 or synthetic jitter supplied']};
fs.mkdirSync(path.dirname(path.resolve(out)),{recursive:true});fs.writeFileSync(out,JSON.stringify(report,null,2));
console.log(JSON.stringify({capture:report.capture,jitter:jitter,passCount:passes.length,viewportClasses:[...new Set(passes.map(p=>`${p.viewport.x},${p.viewport.y}:${p.viewport.width}x${p.viewport.height}`))],preExposure:report.preExposure.status,gates:report.gates},null,2));
