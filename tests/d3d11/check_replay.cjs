const fs=require('fs'),path=require('path'),assert=require('assert/strict');
const [baseline,insertion]=process.argv.slice(2);
assert(baseline&&insertion,'Usage: node check_replay.cjs CAPTURE_ARTIFACT_DIRECTORY INSERTION_DIRECTORY');
const checks={};
for(const name of ['post-effects','composite','final']) {
  const a=fs.readFileSync(path.join(baseline,'checkpoints',name+'.raw'));
  const b=fs.readFileSync(path.join(insertion,name+'.raw'));
  assert(a.equals(b),'Identity insertion changed '+name);
  checks[name+'BitExact']=true;
}
for(const [name,reference]of [['motion','gpu-motion-cropped'],['depth','gpu-depth-cropped']]) {
  assert(fs.readFileSync(path.join(insertion,'insertion-'+name+'.raw')).equals(fs.readFileSync(path.join(baseline,'checkpoints',reference+'.raw'))),'Inserted '+name+' differs from standalone GPU normalization');
  checks[name+'BitExactWithStandalone']=true;
}
const b=fs.readFileSync(path.join(insertion,'insertion-validity.raw'));
assert.equal(b.readUInt32LE(8),42);assert.equal(b.readUInt32LE(12),4);
assert.equal(b.length,16+b.readUInt32LE(0)*b.readUInt32LE(4)*4);
checks.validity={camera:0,packed:0};
for(let i=16;i<b.length;i+=4){const value=b.readUInt32LE(i);assert(value===1||value===2,'Invalid normalized input in captured frame');checks.validity[value===1?'camera':'packed']++;}
fs.writeFileSync(path.join(insertion,'checks.json'),JSON.stringify(checks,null,2));
console.log('PASS '+JSON.stringify(checks));
