/* Official installed SDK -> actual firmware C config RPC/compiler -> SDK.
 * The OS/Flash boundary is a host test double, not a connected device.
 * No official application files are modified and native drivers cannot open. */
const fs=require('node:fs');
const path=require('node:path');
const vm=require('node:vm');
const crypto=require('node:crypto');
const {spawn}=require('node:child_process');
const {createInterface}=require('node:readline');
const assert=require('node:assert/strict');
const sdkPath=process.argv[2];
if (!sdkPath) throw new Error('Pass the installed official SDK dist/index.js path');
const source=fs.readFileSync(sdkPath,'utf8');
class NoHardware { constructor(){throw new Error('Hardware disabled in SDK contract test');} }
const moduleObject={exports:{}};
const context={module:moduleObject,exports:moduleObject.exports,Buffer,console,setTimeout,clearTimeout,
  setInterval,clearInterval,TextEncoder,TextDecoder,URL,process,
  require:(name)=>name==='serialport'?{SerialPort:NoHardware}:name==='node-hid'?{HID:NoHardware,devices:()=>{throw new Error('Hardware disabled');}}:require(name)};
vm.runInNewContext(source,context,{filename:sdkPath});
const {WLRPCApi,noopLogger}=moduleObject.exports;
const child=spawn(path.resolve('firmware/codex-micro/.host-tests/test_config_rpc.exe'),['--serve'],{windowsHide:true,stdio:['pipe','pipe','inherit']});
const lines=createInterface({input:child.stdout});
const pending=new Map();let sequence=0,chain=Promise.resolve(),calls=0;
lines.on('line',line=>{
  const reply=JSON.parse(line),entry=pending.get(reply.id);assert(entry,'Unexpected C response id');pending.delete(reply.id);
  if(reply.error)entry.reject(new Error(JSON.stringify(reply.error)));else entry.resolve(reply);
});
child.on('error',err=>{for(const entry of pending.values())entry.reject(err);pending.clear();});
child.on('exit',code=>{if(code!==0)for(const entry of pending.values())entry.reject(new Error(`C service exited ${code}`));});
const api=Object.create(WLRPCApi.prototype);api.log=noopLogger;
api.rpcClient={sendRpcCall(request){
  const result=chain.then(()=>new Promise((resolve,reject)=>{
    const id=++sequence;++calls;pending.set(id,{resolve,reject});
    child.stdin.write(JSON.stringify({...request,id})+'\n');
  }));chain=result.catch(()=>{});return result;
}};
async function run(){
  const fixture=JSON.parse(fs.readFileSync('firmware/codex-micro/tests/fixtures/advanced_keymap.json','utf8'));
  const template=fixture.profiles[0];fixture.profiles=[];
  for(let p=0;p<6;p++){
    const profile=structuredClone(template);profile.id=p;profile.name=`Profile ${p+1}`;profile.layers=[];
    for(let l=0;l<6;l++){
      const layer=structuredClone(template.layers[0]);layer.id=l;layer.name=`Layer ${l+1}`;
      if(l){layer.layout.joystick={type:'RADIAL',sectors:[{k:'KI_X',a1:.1875,a2:.3125},{k:'KC_NONE',a1:.3125,a2:.1875}]};}
      layer.lights={backlight:{effect:'solid',brightness:1,speed:.5,magic:1,color:0xff8040},underglow:{effect:'rainbow',brightness:.5,speed:.5,magic:1}};
      profile.layers.push(layer);
    }fixture.profiles.push(profile);
  }
  const keymap=Buffer.from(JSON.stringify(fixture,null,1)),smart=fs.readFileSync('firmware/codex-micro/tests/fixtures/advanced_smart_actions.json');
  assert(keymap.length>8192 && keymap.length<=131072);
  const files=[{name:'keymap.json',data:keymap},{name:'smart_actions.json',data:smart}];
  const result=await api.sendMultipleFiles(files);assert.equal(result.ok,true,JSON.stringify(result));
  for(const file of files){const read=await api.readFileChunked(file.name);assert.equal(read.ok,true,JSON.stringify(read));assert(file.data.equals(read.value));}
  const baseline=await api.readFileChunked('keymap.json');assert.equal(baseline.ok,true);
  const bad=structuredClone(fixture);bad.profiles[0].layers[1].layout.joystick.sectors[1].a2=.25;
  const rejected=await api.sendMultipleFiles([{name:'keymap.json',data:Buffer.from(JSON.stringify(bad))}]);assert.equal(rejected.ok,false);
  const unchanged=await api.readFileChunked('keymap.json');assert.equal(unchanged.ok,true);assert(keymap.equals(unchanged.value));
  const testedSources=['components/config_store/config_rpc.c','components/device_services/keymap_model.c',
    'components/device_services/key_codes.c','components/device_services/light_engine.c','components/device_services/smart_actions.c',
    'components/device_services/include/keymap_model.h','components/device_services/include/smart_actions.h'];
  const evidence={passed:true,hardware_accessed:false,timestamp_utc:new Date().toISOString(),
    scope:'Official SDK + real C config RPC/compiler; host Flash mock; no USB/BLE/GUI acceptance',
    source_sha256:Object.fromEntries(testedSources.map(name=>[name,crypto.createHash('sha256').update(fs.readFileSync(path.join('firmware/codex-micro',name))).digest('hex')])),
    c_service_sha256:crypto.createHash('sha256').update(fs.readFileSync('firmware/codex-micro/.host-tests/test_config_rpc.exe')).digest('hex'),
    sdk_path:sdkPath,sdk_sha256:crypto.createHash('sha256').update(source).digest('hex'),sdk_chunk_raw:WLRPCApi.CHUNK_RAW_BYTE_SIZE,
    profiles:6,layers_per_profile:6,keymap_bytes:keymap.length,smart_bytes:smart.length,calls,
    checks:['SDK multifile transaction','3072-byte write/read chunks','six profiles and 36 layers','Input default RADIAL sectors','layer light DTO','macros/multitap/Smart references','byte-exact readback','invalid sector rollback']};
  const version=fs.readFileSync('firmware/codex-micro/CMakeLists.txt','utf8').match(/set\(PROJECT_VER "([^"]+)"\)/)[1];
  const evidencePath=`docs/firmware/evidence/candidate-${version}`;
  fs.mkdirSync(evidencePath,{recursive:true});
  fs.writeFileSync(`${evidencePath}/official-sdk-contract.json`,JSON.stringify(evidence,null,2)+'\n');
  console.log(JSON.stringify(evidence,null,2));
}
const watchdog=setTimeout(()=>{child.kill();console.error('SDK contract test timeout');process.exitCode=1;},45000);
run().catch(err=>{console.error(err);process.exitCode=1;}).finally(()=>{clearTimeout(watchdog);child.stdin.end();lines.close();});
