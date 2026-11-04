const fs=require('fs'),vm=require('vm'),assert=require('assert');
const html=fs.readFileSync('experiments/v7_small/main/index.html','utf8');
const ids=new Set([...html.matchAll(/id="([^"]+)"/g)].map(m=>m[1]));const elements=new Map(),requests=[];
function el(){return {value:'',checked:false,disabled:false,textContent:'',children:[],style:{},handlers:{},classList:{toggle(){}},addEventListener(e,f){this.handlers[e]=f},append(v){this.children.push(v)},focus(){},getContext(){return {clearRect(){},beginPath(){},arc(){},fill(){}}},width:640,height:480}}
let saved={email_enabled:false,webhook_enabled:false,email:'driver@gmail.com',recipient:'friend@example.com',password_saved:true,webhook_saved:true,events:1,hold_ms:[2000,4000,3000,3500,5000],cooldown:60};
const sample={age_ms:0,face:false,calibrated:false,calibration_running:false,calibration_waiting:false,calibration_step:1,calibration_samples:0,calibration_target:12,overlay:[],inference_ms:500,notifications:{email_status:'Email accepted by Gmail',webhook_status:'Not sent yet',clock_ready:true},wifi_connected:true,internet_verified:true};
let fail=false;
const ctx={document:{getElementById(id){assert(ids.has(id),'Missing id '+id);if(!elements.has(id))elements.set(id,el());return elements.get(id)},createElement:el},setInterval(){},AbortSignal,TextEncoder,confirm:()=>true,fetch:async(url,options)=>{
 if(url==='/api/status')return {ok:true,json:async()=>sample};
 const body=JSON.parse(options.body);requests.push(body);
 return {ok:!fail,status:fail?400:200,json:async()=>fail?{error:'Invalid settings'}:body.action==='load'?saved:{message:'Saved'}};
}};
vm.createContext(ctx);vm.runInContext(html.split('<script>')[1].split('</script>')[0].replace('poll();preview();setInterval(poll,1300);setInterval(preview,1800);',''),ctx);
(async()=>{
 const $=id=>ctx.document.getElementById(id),click=async id=>$(id).handlers.click();
 assert(!ids.has('key'));await click('loadAlerts');assert.equal(requests.length,1);assert.equal(requests[0].key,undefined);
 await click('restart');assert.equal(requests.at(-1).key,undefined);assert.equal(requests.at(-1).action,undefined);requests.length=0;
 await click('loadAlerts');assert.equal($('emailFrom').value,saved.email);assert.equal($('appPassword').value,'');assert.equal($('webhookUrl').value,'');assert.match($('appPassword').placeholder,/Saved/);
 assert.equal($('hold0').value,2);$('hold0').value='4.5';
 $('emailEnabled').checked=true;$('appPassword').value='abcd efgh ijkl mnop';await click('saveAlerts');let sent=requests.at(-1);
 assert.equal(sent.app_password,'abcdefghijklmnop');assert.equal(sent.events,1);assert.deepEqual(sent.hold_ms,[4500,4000,3000,3500,5000]);assert.equal(sent.webhook_url,'');assert.equal($('appPassword').value,'');
 await click('saveAlerts');assert.equal(requests.at(-1).app_password,'');assert.equal(requests.at(-1).webhook_url,'');
 fail=true;$('appPassword').value='keepme';await click('saveAlerts');assert.equal($('appPassword').value,'keepme');assert.equal($('alertMessage').textContent,'Invalid settings');
 vm.runInContext('update('+JSON.stringify(sample)+')',ctx);assert.equal($('emailDelivery').textContent,'Email accepted by Gmail');assert.match($('deliveryNetwork').textContent,/ready/);
 sample.wifi_connected=false;vm.runInContext('update('+JSON.stringify(sample)+')',ctx);assert.match($('deliveryNetwork').textContent,/Offline/);
 fail=false;$('ssid').value='Test network';$('pwd').value='test-password';await click('connect');assert.equal(requests.at(-1).ssid,'Test network');await click('forget');await click('testEmail');assert.equal(requests.at(-1).action,'test_email');await click('testWebhook');assert.equal(requests.at(-1).action,'test_webhook');
 assert(requests.every(r=>!Object.hasOwn(r,'key')));
 sample.sharing_status='Hotspot sharing ready';sample.internet_sharing=true;vm.runInContext('update('+JSON.stringify(sample)+')',ctx);assert.equal($('sharingInfo').textContent,'Hotspot sharing ready');
 sample.sharing_status='Home Wi-Fi overlaps hotspot';sample.internet_sharing=false;vm.runInContext('update('+JSON.stringify(sample)+')',ctx);assert.equal($('sharingInfo').textContent,sample.sharing_status);
 $('apName').value='My SAFE ROAD';$('apPassword').value='short';const before=requests.length;await click('applyHotspot');assert.equal(requests.length,before);$('apPassword').value='';await click('applyHotspot');assert.equal(requests.at(-1).ssid,'My SAFE ROAD');assert.equal(requests.at(-1).password,'');assert.equal($('apPassword').value,'');
 console.log('PASS: keyless settings, calibration, secret-preserving saves, app-password normalization, errors and offline/delivery status');
})().catch(e=>{console.error(e);process.exitCode=1});
