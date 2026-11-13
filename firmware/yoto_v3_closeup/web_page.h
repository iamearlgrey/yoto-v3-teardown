// The web page served at http://cal-yoto.local (kept in flash).
#pragma once

const char PAGE[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>CAL Yoto</title>
<style>
:root{--bg:#141510;--panel:#1c1d17;--line:#33352b;--ink:#e9e6d8;--dim:#8f8d7f;--amber:#ffb340;--red:#ff6b5a;--green:#9fd36b;
--mono:ui-monospace,SFMono-Regular,Menlo,monospace;--sans:-apple-system,BlinkMacSystemFont,"Helvetica Neue",sans-serif}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font:14px/1.45 var(--sans)}
.wrap{max-width:980px;margin:0 auto;padding:20px 16px 40px}
header{display:flex;justify-content:space-between;align-items:flex-end;gap:12px;flex-wrap:wrap;border-bottom:1px solid var(--line);padding-bottom:14px;margin-bottom:16px}
h1{font:600 22px var(--mono);letter-spacing:.08em;margin:0;color:var(--amber)}
.sub{font:11px var(--mono);color:var(--dim);letter-spacing:.12em;text-transform:uppercase;margin-top:4px}
.stats{display:flex;gap:14px;flex-wrap:wrap;font:12px var(--mono);color:var(--dim)}
.stats b{color:var(--ink);font-weight:500}
.badge{font:600 11px var(--mono);padding:3px 8px;border-radius:3px;background:var(--red);color:#1a0d0a;letter-spacing:.06em}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:16px}
@media(max-width:760px){.grid{grid-template-columns:1fr}}
.card{background:var(--panel);border:1px solid var(--line);border-radius:6px;padding:16px;min-width:0}
.card.wide{grid-column:1/-1}
h2{font:600 11px var(--mono);letter-spacing:.16em;text-transform:uppercase;color:var(--dim);margin:0 0 12px}
button,input[type=text],input[type=password]{font:13px var(--sans);color:var(--ink);background:#10110e;border:1px solid var(--line);border-radius:4px;padding:7px 12px}
button{cursor:pointer}button:hover{border-color:var(--amber)}
button.primary{background:var(--amber);color:#1a1405;border-color:var(--amber);font-weight:600}
button.small{padding:3px 9px;font-size:12px}
button.danger:hover{border-color:var(--red);color:var(--red)}
.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
.now{font:13px var(--mono);color:var(--amber);min-height:20px;margin-bottom:10px;word-break:break-all}
input[type=range]{width:100%;accent-color:var(--amber)}
.vol{display:flex;justify-content:space-between;font:12px var(--mono);color:var(--dim);margin-top:12px}
.path{font:12px var(--mono);color:var(--dim);margin:10px 0 6px;word-break:break-all}
.list{border:1px solid var(--line);border-radius:4px;max-height:360px;overflow:auto}
.item{display:flex;align-items:center;gap:8px;padding:6px 10px;border-bottom:1px solid var(--line);font:13px var(--mono)}
.item:last-child{border-bottom:0}
.item .n{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.item .n.folder{color:var(--amber);cursor:pointer}
.item .s{color:var(--dim);font-size:12px;white-space:nowrap}
.drop{border:1px dashed var(--line);border-radius:4px;padding:14px;text-align:center;color:var(--dim);margin-top:10px;font-size:13px}
.drop.on{border-color:var(--amber);color:var(--amber)}
.prog{font:12px var(--mono);color:var(--amber);margin-top:6px;min-height:16px}
pre{font:12px/1.5 var(--mono);background:#0c0d0a;border:1px solid var(--line);border-radius:4px;padding:10px;margin:0;height:280px;overflow:auto;white-space:pre-wrap;word-break:break-all}
label{font-size:12px;color:var(--dim)}
.form{display:grid;gap:8px;max-width:360px}
.hint{color:var(--dim);font-size:12px;margin:8px 0 0}
</style></head><body><div class="wrap">
<header>
 <div><h1>CAL · YOTO</h1><div class="sub">Unit 54.2 · body console</div></div>
 <div class="stats" id="stats">connecting…</div>
</header>
<div class="grid">
 <section class="card" id="playerCard">
  <h2>Player</h2>
  <div class="now" id="now">Nothing playing</div>
  <div class="row"><button id="pause">Pause</button><button id="stop">Stop</button></div>
  <div class="vol"><span>Volume</span><span id="volV">−24 dB</span></div>
  <input type="range" id="vol" min="-48" max="0" step="6" value="-24">
  <div class="vol"><span>Boost (Smart 2 is the default)</span>
   <select id="boost" style="font:12px var(--mono);background:#10110e;color:var(--ink);border:1px solid var(--line);border-radius:4px;padding:3px 6px">
    <option value="0">Off</option><option value="1">Always on</option><option value="5">Smart 1</option><option value="6">Smart 2</option><option value="2" id="boostFound">As found</option>
   </select></div>
  <p class="hint">Plays 16-bit WAV files, mono or stereo, any sample rate. Left knob: volume, press to pause. Right knob: next/previous file in the folder (/music if there is one), press to play/stop.</p>
 </section>
 <section class="card" id="lightCard">
  <h2>Night light</h2>
  <div class="row">
   <input type="color" id="lc" value="#ffb340" style="width:44px;height:32px;padding:0;border:1px solid var(--line);border-radius:4px;background:none">
   <button id="lon" class="primary">Light</button>
   <button id="loff">Off</button>
   <button id="ltest">Channel test</button>
  </div>
  <div class="vol"><span>Brightness</span><span id="llV">128</span></div>
  <input type="range" id="ll" min="8" max="255" step="1" value="128">
  <details id="balBox" style="margin-top:12px"><summary style="cursor:pointer;color:var(--amber);font:12px var(--mono)">Colour balance</summary>
   <p class="hint">Press White, then lower the strongest colour until white looks white (a white wall helps). Check with a yellow from the picker, then Save.</p>
   <div class="vol"><span>Red</span><span id="brV">100%</span></div><input type="range" id="br" min="1" max="100" value="100">
   <div class="vol"><span>Green</span><span id="bgV">33%</span></div><input type="range" id="bg" min="1" max="100" value="33">
   <div class="vol"><span>Blue</span><span id="bbV">27%</span></div><input type="range" id="bb" min="1" max="100" value="27">
   <div class="row" style="margin-top:10px"><button id="lwhite">White</button><button id="bsave" class="primary">Save balance</button></div>
  </details>
  <p class="hint">Status colours: amber breathing while starting, blue while setting up WiFi or updating, red in safe mode, slow green in charging mode. Channel test lights each of the chip's three outputs alone for 2 s; the log asks what colour each one is.</p>
 </section>
 <section class="card">
  <h2>Board</h2>
  <div class="row">
   <button id="restart">Restart</button>
   <button id="safe">Restart in safe mode</button>
   <button id="off" class="danger">Power off</button>
  </div>
  <details id="wifiBox" style="margin-top:14px"><summary style="cursor:pointer;color:var(--amber);font:12px var(--mono)">WiFi settings</summary>
   <form class="form" id="wifiForm" style="margin-top:10px">
    <label>Network name<input type="text" name="ssid" list="nets" required autocapitalize="off" autocorrect="off" style="width:100%"></label>
    <datalist id="nets"></datalist>
    <label>Password<input type="password" name="pass" id="pass" autocapitalize="off" autocorrect="off" style="width:100%"></label>
    <label><input type="checkbox" onchange="pass.type=this.checked?'text':'password'"> show password</label>
    <label>Update password (optional)<input type="password" name="ota" style="width:100%"></label>
    <button class="primary">Save and restart</button>
   </form>
  </details>
  <p class="hint">Safe mode: hold a knob while switching on, or use the button above. Power off: hold the power button 3 s, then let go. The last session's log is saved as cal_log_prev.txt on the card.</p>
 </section>
 <section class="card wide">
  <h2>Display</h2>
  <div class="row">
   <input type="color" id="pc" value="#ffb340" style="width:44px;height:32px;padding:0;border:1px solid var(--line);border-radius:4px;background:none">
   <button class="small" id="pErase">Eraser</button>
   <button class="small" id="pFill">Fill</button>
   <button class="small" id="pClear">Clear</button>
   <button class="small" id="pPattern">Test pattern</button>
  </div>
  <div id="pGrid" style="display:grid;grid-template-columns:repeat(16,1fr);gap:2px;max-width:440px;margin-top:10px;touch-action:none;user-select:none"></div>
  <div class="vol"><span>Display brightness</span><span id="dbV">16/64</span></div>
  <input type="range" id="db" min="1" max="64" value="16">
  <p class="hint">Draw by clicking or dragging; the Yoto updates as you go. Test pattern: top row white, corners red (top left), green (top right), blue (bottom left), white (bottom right), a rainbow left to right, dim at the top to bright at the bottom.</p>
  <details style="margin-top:14px"><summary style="cursor:pointer;color:var(--amber);font:12px var(--mono)">Mapping tools</summary>
  <div class="row" style="margin-top:10px">
   <span class="s" style="font:12px var(--mono);color:var(--dim)">Whole chip</span>
   <button class="small" data-chip="0">0</button><button class="small" data-chip="1">1</button>
   <button class="small" data-chip="2">2</button><button class="small" data-chip="3">3</button>
   <button class="small" id="dAll">All on</button><button class="small" id="dOff">All off</button>
  </div>
  <div class="row" style="margin-top:10px">
   <label>chip <input type="text" id="dChip" value="0" style="width:44px"></label>
   <label>address (hex) <input type="text" id="dAddr" value="20" style="width:44px"></label>
   <label>block <input type="text" id="dBlock" value="1" style="width:44px"></label>
   <button class="small" id="dDot">Address</button>
   <button class="small" id="dBlk">Block</button>
   <button class="small" id="dRamp">Ramp</button>
  </div>
  <p class="hint">Each chip has memory addresses 00–FB (hex), one LED each, in 8 blocks of 20 (hex). Block 0 (00–1F) lights nothing on this board. "Whole chip" shows which band a chip covers; Address lights one LED; Block lights 32 addresses; Ramp grades brightness within each block. Starts dim on purpose.</p>
  <h2 style="margin-top:18px">Map a chip</h2>
  <div class="row">
   <label>chip <input type="text" id="mChip" value="0" style="width:44px"></label>
   <button class="small primary" id="mStart">Start mapping</button>
   <span id="mProg" style="font:12px var(--mono);color:var(--amber)"></span>
  </div>
  <p class="hint">One dot lights at a time. Click the pixel below that matches it, then its colour (or press R, G, B, N on the keyboard). Found pixels fill in as you go. Progress saves to the SD card.</p>
  <div id="mGrid" style="display:grid;grid-template-columns:repeat(16,1fr);gap:2px;max-width:440px;margin-top:6px"></div>
  <div class="row" style="margin-top:10px">
   <button id="mR" style="background:#7a1f16">Red</button><button id="mG" style="background:#1d5c33">Green</button>
   <button id="mB" style="background:#1d3f75">Blue</button><button id="mN">Nothing lit</button>
   <button id="mSkip">Rest of this block is dark</button><button id="mBack">Back</button>
  </div>
  </details>
 </section>
 <section class="card wide">
  <h2>SD card</h2>
  <div class="row">
   <button id="up" class="small">Up</button>
   <button id="mk" class="small">New folder</button>
   <label class="small" style="cursor:pointer"><input type="file" id="file" multiple style="display:none"><span style="border:1px solid var(--line);border-radius:4px;padding:4px 10px;color:var(--ink)">Upload files</span></label>
   <button id="refresh" class="small">Refresh</button>
  </div>
  <div class="path" id="path">/</div>
  <div class="list" id="list"></div>
  <div class="drop" id="drop">Drop files here to copy them into this folder</div>
  <div class="prog" id="prog"></div>
 </section>
 <section class="card wide">
  <div class="row" style="justify-content:space-between"><h2 style="margin:0">Log</h2>
   <label><input type="checkbox" id="follow" checked> follow</label></div>
  <pre id="log" style="margin-top:10px"></pre>
 </section>
</div></div>
<script>
const $=id=>document.getElementById(id);
let dir='/',logSince=0;
const post=(u)=>fetch(u,{method:'POST'});
const join=(d,n)=>(d==='/'?'':d)+'/'+n;
const size=b=>b<1024?b+' B':b<1048576?(b/1024).toFixed(1)+' KB':(b/1048576).toFixed(1)+' MB';

async function status(){
 try{const s=await (await fetch('/api/status')).json();
  let h='';
  if(s.safe)h+='<span class="badge">SAFE MODE</span>';
  if(s.hotspot)h+='<span class="badge">SETUP HOTSPOT</span>';
  h+=`<span>battery <b>${s.volts.toFixed(2)} V</b></span><span><b>${s.input}</b>, ${s.charge}</span>`;
  if(!s.hotspot)h+=`<span>wifi <b>${s.rssi} dBm</b></span>`;
  h+=`<span>${s.ip}</span><span>up ${Math.floor(s.uptime/60)} min</span>`;
  $('stats').innerHTML=h;
  if(s.hotspot)$('wifiBox').open=true;
  if(!s.light_ok)$('lightCard').style.opacity=.4;
  else if(document.activeElement!==$('ll')){$('ll').value=s.light_level;$('llV').textContent=s.light_level}
  if(s.balance&&!$('balBox').open)['br','bg','bb'].forEach((id,i)=>{$(id).value=s.balance[i];$(id+'V').textContent=s.balance[i]+'%'});
  if('playing' in s){$('now').textContent=s.charging_mode?'Charging mode: press a knob to wake':s.playing?(s.paused?'⏸ ':'▶ ')+s.playing:'Nothing playing';
   $('pause').textContent=s.paused?'Resume':'Pause';
   if(s.boost_found>=0){$('boostFound').value=s.boost_found;$('boostFound').textContent='As found ('+s.boost_found+')'}
   if(document.activeElement!==$('boost')&&s.boost>=0)$('boost').value=s.boost;
   if(document.activeElement!==$('vol')){$('vol').value=-s.volume_down;$('volV').textContent=(s.volume_down?'−':'')+s.volume_down+' dB'}}
  else $('playerCard').style.opacity=.4;
 }catch(e){$('stats').textContent='not responding'}
}
async function list(){
 $('path').textContent=dir;
 const r=await fetch('/api/list?dir='+encodeURIComponent(dir));
 if(!r.ok){$('list').innerHTML='<div class="item">Folder not found</div>';return}
 const j=await r.json();
 j.items.sort((a,b)=>(b.dir-a.dir)||a.name.localeCompare(b.name));
 $('list').innerHTML='';
 if(!j.items.length)$('list').innerHTML='<div class="item"><span class="n" style="color:var(--dim)">Empty folder</span></div>';
 for(const it of j.items){
  const p=join(dir,it.name),row=document.createElement('div');row.className='item';
  const n=document.createElement('span');n.className='n'+(it.dir?' folder':'');n.textContent=(it.dir?'▸ ':'')+it.name;
  if(it.dir)n.onclick=()=>{dir=p;list()};
  row.appendChild(n);
  if(!it.dir){const s=document.createElement('span');s.className='s';s.textContent=size(it.size);row.appendChild(s);
   if(/\.wav$/i.test(it.name)){const b=document.createElement('button');b.className='small primary';b.textContent='Play';
    b.onclick=async()=>{const r=await post('/api/play?path='+encodeURIComponent(p));if(!r.ok)alert(await r.text());setTimeout(status,300)};row.appendChild(b)}
   const a=document.createElement('a');a.href='/api/download?path='+encodeURIComponent(p);
   a.innerHTML='<button class="small">Download</button>';row.appendChild(a)}
  const d=document.createElement('button');d.className='small danger';d.textContent='Delete';
  d.onclick=async()=>{if(!confirm('Delete '+it.name+(it.dir?' and everything in it':'')+'?'))return;await post('/api/delete?path='+encodeURIComponent(p));list()};
  row.appendChild(d);$('list').appendChild(row)}
}
function upload(files){
 const q=[...files];let i=0;
 const next=()=>{if(i>=q.length){$('prog').textContent=q.length?'Done.':'';list();return}
  const f=q[i++],fd=new FormData();fd.append('file',f,f.name);
  const x=new XMLHttpRequest();x.open('POST','/api/upload?dir='+encodeURIComponent(dir));
  x.upload.onprogress=e=>{$('prog').textContent=`${f.name}: ${Math.round(e.loaded/e.total*100)}% (${i} of ${q.length})`};
  x.onload=()=>{if(x.status!==200)$('prog').textContent=f.name+': failed';next()};
  x.onerror=()=>{$('prog').textContent=f.name+': connection lost';};
  x.send(fd)};
 next();
}
async function pollLog(){
 try{const r=await fetch('/api/log?since='+logSince);const t=await r.text();
  logSince=+r.headers.get('X-Log-Total')||logSince;
  if(t){const l=$('log');l.textContent+=t;if(l.textContent.length>60000)l.textContent=l.textContent.slice(-40000);
   if($('follow').checked)l.scrollTop=l.scrollHeight}}catch(e){}
}
$('stop').onclick=async()=>{await post('/api/stop');setTimeout(status,300)};
$('pause').onclick=async()=>{await post('/api/pause');setTimeout(status,300)};
$('boost').onchange=e=>post('/api/boost?mode='+e.target.value);
const lightSet=()=>post('/api/light?color='+$('lc').value.slice(1)+'&level='+$('ll').value);
$('lon').onclick=lightSet;
$('lc').oninput=lightSet;
$('loff').onclick=()=>post('/api/light/off');
$('ltest').onclick=()=>post('/api/light/test');
$('ll').oninput=e=>{$('llV').textContent=e.target.value};
$('ll').onchange=lightSet;
const disp=(show,extra='')=>post('/api/disp?show='+show+extra);
const dsel=()=>'&chip='+$('dChip').value+'&addr='+$('dAddr').value+'&block='+$('dBlock').value;
document.querySelectorAll('[data-chip]').forEach(b=>b.onclick=()=>{$('dChip').value=b.dataset.chip;disp('chip','&chip='+b.dataset.chip)});
$('dAll').onclick=()=>disp('all');$('dOff').onclick=()=>disp('off');
$('dDot').onclick=()=>disp('addr',dsel());$('dBlk').onclick=()=>disp('block',dsel());
$('dRamp').onclick=()=>disp('ramp',dsel());
$('db').oninput=e=>{$('dbV').textContent=e.target.value+'/64'};
$('db').onchange=e=>post('/api/disp/bright?v='+e.target.value);
// ---- drawing grid ----
const frame=Array(256).fill('000000');const pcells=[];let painting=false,erase=false,sendT=null;
const sendFrame=()=>{sendT=null;fetch('/api/frame',{method:'POST',body:frame.join('')})};
const queueSend=()=>{if(!sendT)sendT=setTimeout(sendFrame,80)};
const paintCell=(i)=>{const col=erase?'000000':$('pc').value.slice(1);frame[i]=col;pcells[i].style.background='#'+(col==='000000'?'0c0d0a':col);queueSend()};
for(let i=0;i<256;i++){const c=document.createElement('div');
 c.style.cssText='aspect-ratio:1;background:#0c0d0a;border:1px solid var(--line);border-radius:2px;cursor:crosshair';
 c.onpointerdown=e=>{painting=true;c.releasePointerCapture&&c.releasePointerCapture(e.pointerId);paintCell(i)};
 c.onpointerenter=()=>{if(painting)paintCell(i)};
 pcells.push(c);$('pGrid').appendChild(c)}
document.addEventListener('pointerup',()=>{painting=false});
$('pErase').onclick=()=>{erase=!erase;$('pErase').textContent=erase?'Eraser (on)':'Eraser'};
$('pc').oninput=()=>{erase=false;$('pErase').textContent='Eraser'};
$('pClear').onclick=()=>{frame.fill('000000');pcells.forEach(c=>c.style.background='#0c0d0a');sendFrame()};
$('pFill').onclick=()=>{const col=$('pc').value.slice(1);frame.fill(col);pcells.forEach(c=>c.style.background='#'+col);sendFrame()};
$('pPattern').onclick=()=>disp('pattern');
// ---- click-mapping ----
let mSel=null;const cells=[];
for(let y=0;y<16;y++)for(let x=0;x<16;x++){const c=document.createElement('div');
 c.style.cssText='aspect-ratio:1;background:#0c0d0a;border:1px solid var(--line);border-radius:2px;cursor:pointer';
 c.onclick=()=>{if(mSel)mSel.style.outline='';mSel=c;c.style.outline='2px solid var(--amber)';c.dataset.x=x;c.dataset.y=y};
 cells.push(c);$('mGrid').appendChild(c)}
function mShow(s){
 if(s.chip<0){$('mProg').textContent='';return}
 const h=s.addr.toString(16).toUpperCase().padStart(2,'0');
 $('mProg').textContent=s.addr>=s.total?'Done: chip '+s.chip+' saved':'Chip '+s.chip+' · address '+h+' · block '+Math.floor(s.addr/32)+' ('+s.addr+' of '+s.total+')';
 const mix=Array(256).fill(0).map(()=>[0,0,0]);
 s.found.forEach(([x,y,c])=>{mix[y*16+x][c]=1});
 cells.forEach((c,i)=>{const m=mix[i];c.style.background=(m[0]||m[1]||m[2])?`rgb(${m[0]?230:25},${m[1]?200:25},${m[2]?230:25})`:'#0c0d0a'});
}
const mPost=async u=>{const r=await post(u);mShow(await r.json())};
function mRec(col){
 if(col!=='n'&&!mSel){alert('Click the pixel that lit up first.');return}
 const q=col==='n'?'':'&x='+mSel.dataset.x+'&y='+mSel.dataset.y;
 if(mSel){mSel.style.outline='';mSel=null}
 mPost('/api/map/record?c='+col+q)}
$('mStart').onclick=()=>mPost('/api/map/start?chip='+$('mChip').value);
$('mR').onclick=()=>mRec('r');$('mG').onclick=()=>mRec('g');$('mB').onclick=()=>mRec('b');$('mN').onclick=()=>mRec('n');
$('mSkip').onclick=()=>mPost('/api/map/skip');$('mBack').onclick=()=>mPost('/api/map/back');
document.addEventListener('keydown',e=>{if(e.target.tagName==='INPUT')return;const k=e.key.toLowerCase();if('rgbn'.includes(k)&&k.length===1)mRec(k)});
fetch('/api/map/state').then(r=>r.json()).then(mShow).catch(()=>{});
const balQ=()=>'/api/light/balance?r='+$('br').value+'&g='+$('bg').value+'&b='+$('bb').value;
['br','bg','bb'].forEach(id=>{$(id).oninput=e=>{$(id+'V').textContent=e.target.value+'%';post(balQ())}});
$('lwhite').onclick=()=>post('/api/light?color=ffffff&level='+$('ll').value);
$('bsave').onclick=async()=>{const r=await post(balQ()+'&save=1');alert(await r.text())};
$('vol').oninput=e=>{const v=-e.target.value;$('volV').textContent=(v?'−':'')+v+' dB'};
$('vol').onchange=e=>post('/api/volume?down='+(-e.target.value));
$('restart').onclick=()=>{if(confirm('Restart the Yoto?'))post('/api/restart')};
$('safe').onclick=()=>{if(confirm('Restart into safe mode? (WiFi, updates and this page only)'))post('/api/safemode')};
$('off').onclick=()=>{if(confirm('Switch the Yoto off? (Only works on battery.)'))post('/api/poweroff')};
$('up').onclick=()=>{if(dir!=='/'){dir=dir.slice(0,dir.lastIndexOf('/'))||'/';list()}};
$('mk').onclick=async()=>{const n=prompt('Folder name');if(n){await post('/api/mkdir?path='+encodeURIComponent(join(dir,n)));list()}};
$('refresh').onclick=list;
$('file').onchange=e=>upload(e.target.files);
const dz=$('drop');
['dragenter','dragover'].forEach(t=>dz.addEventListener(t,e=>{e.preventDefault();dz.classList.add('on')}));
['dragleave','drop'].forEach(t=>dz.addEventListener(t,e=>{e.preventDefault();dz.classList.remove('on')}));
dz.addEventListener('drop',e=>upload(e.dataTransfer.files));
$('wifiForm').onsubmit=async e=>{e.preventDefault();const r=await fetch('/api/wifi',{method:'POST',body:new URLSearchParams(new FormData(e.target))});alert(await r.text())};
fetch('/api/networks').then(r=>r.json()).then(a=>{for(const n of a){const o=document.createElement('option');o.value=n;$('nets').appendChild(o)}}).catch(()=>{});
status();list();pollLog();setInterval(status,4000);setInterval(pollLog,1000);
</script></body></html>)HTML";
