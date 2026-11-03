const fs=require('fs'),vm=require('vm'),assert=require('assert');
const html=fs.readFileSync('experiments/v7_live/main/index.html','utf8');const ids=new Set([...html.matchAll(/id="([^"]+)"/g)].map(m=>m[1]));const elements=new Map();
function el(){return {textContent:'',value:'',disabled:false,children:[],style:{},classList:{toggle(){}},addEventListener(){},append(v){this.children.push(v)},focus(){},getContext(){return {clearRect(){},beginPath(){},arc(){},fill(){}}},width:640,height:480}}
const ctx={document:{getElementById(id){assert(ids.has(id),'Missing DOM id '+id);if(!elements.has(id))elements.set(id,el());return elements.get(id)},createElement:el},setInterval(){},AbortSignal,confirm:()=>true,fetch:async()=>{throw Error('offline')}};
let js=html.split('<script>')[1].split('</script>')[0].replace('poll();preview();setInterval(poll,1300);setInterval(preview,1800);','');vm.createContext(ctx);vm.runInContext(js,ctx);
const base={age_ms:0,face:false,calibrated:false,calibration_running:false,calibration_waiting:false,calibration_step:1,calibration_samples:0,calibration_target:12,overlay:[],inference_ms:700};
function update(extra){ctx.sample={...base,...extra};vm.runInContext('update(sample)',ctx)}
update({});assert.equal(elements.get('calState').textContent,'Ready when you are');assert.match(elements.get('engineState').textContent,/700 ms/);
update({calibration_running:true,calibration_waiting:true});assert.equal(elements.get('ready').disabled,false);assert.equal(elements.get('restart').disabled,true);
update({face:true,calibrated:true,calibration_saved:true,eye:'OPEN-LIKE',mouth:'SHUT-LIKE',away:'NO',tilt:'NO',overlay:[.4,.4,.6,.4]});assert.equal(elements.get('eyeState').textContent,'OPEN-LIKE');assert.match(elements.get('calNote').textContent,/Saved on device/);
update({engine_error:'V7 model loading failed'});assert.equal(elements.get('engineState').textContent,'V7 model loading failed');
update({calibration_running:true,calibration_preparing:true,calibration_wait_s:0,face_reason:'Landmark face confidence too low'});assert.match(elements.get('calNote').textContent,/Landmark face confidence too low/);
console.log('PASS: actual dashboard DOM ids, idle/ready/saved states, landmark overlay and model error feedback');
