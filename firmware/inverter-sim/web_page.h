#pragma once
// Live dashboard served by the simulator at http://<ip>/  (polls /api/state every second)
const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Sinag Simulator</title>
<style>
:root{--bg:#17110d;--card:#221a14;--line:#3a2c22;--ink:#f6ece3;--mute:#a8968a;--sun:#f5a524;--acc:#e8590c;
--ok:#3ccf7a;--bad:#ff5d5d;--warn:#ffb020;--batt:#4dabf7;--grid:#b197fc}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
.wrap{max-width:1040px;margin:0 auto;padding:16px}
header{display:flex;flex-wrap:wrap;gap:10px;align-items:center;justify-content:space-between;margin-bottom:14px}
h1{font-size:19px;margin:0;letter-spacing:.2px}h1 b{color:var(--acc)}
.pills{display:flex;gap:6px;flex-wrap:wrap}
.pill{padding:4px 10px;border-radius:99px;font-size:12.5px;font-weight:600;background:#2c221b;color:var(--mute);border:1px solid var(--line)}
.pill.ok{color:var(--ok);border-color:#24603f}.pill.bad{color:var(--bad);border-color:#6b2a2a}.pill.warn{color:var(--warn);border-color:#6b4f1a}
.modebar{display:flex;align-items:center;gap:12px;flex-wrap:wrap;margin-bottom:12px}
.grid{display:grid;gap:12px}
.flow{grid-template-columns:repeat(4,1fr)}
@media(max-width:760px){.flow{grid-template-columns:repeat(2,1fr)}}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:14px}
.k{font-size:12px;text-transform:uppercase;letter-spacing:.8px;color:var(--mute)}
.v{font-size:28px;font-weight:700;font-variant-numeric:tabular-nums;margin-top:2px;white-space:nowrap}
.v small{font-size:14px;color:var(--mute);font-weight:500;margin-left:3px}
.sub{font-size:12.5px;color:var(--mute);margin-top:2px;min-height:18px}
.bar{height:6px;border-radius:9px;background:#33271f;margin-top:8px;overflow:hidden}
.bar i{display:block;height:100%;border-radius:9px;transition:width .4s}
.two{grid-template-columns:1.1fr .9fr;margin-top:12px}
@media(max-width:760px){.two{grid-template-columns:1fr}}
h2{font-size:14px;margin:0 0 10px;color:var(--mute);font-weight:600;text-transform:uppercase;letter-spacing:.8px}
.row{display:flex;align-items:center;gap:10px;margin:10px 0}
.row label{width:72px;color:var(--mute);font-size:13px}
.row output{width:80px;text-align:right;font-variant-numeric:tabular-nums}
input[type=range]{flex:1;min-width:0;accent-color:var(--acc)}
.btns{display:flex;gap:8px;flex-wrap:wrap;margin-top:6px}
button,select{font:inherit;color:var(--ink);background:#2c221b;border:1px solid var(--line);border-radius:10px;padding:8px 12px;cursor:pointer}
button.on{background:var(--acc);border-color:var(--acc);color:#fff}
button.danger.on{background:var(--bad);border-color:var(--bad)}
.seg{display:inline-flex;border:1px solid var(--line);border-radius:10px;overflow:hidden;flex-wrap:wrap}
.seg button{border:0;border-radius:0}
table{width:100%;border-collapse:collapse;font-variant-numeric:tabular-nums;font-size:13.5px}
td,th{padding:6px 4px;border-bottom:1px solid var(--line);text-align:left}
th{color:var(--mute);font-weight:500;font-size:12px}
td.n{text-align:right;white-space:nowrap}td.raw{color:var(--mute);font-family:ui-monospace,Menlo,monospace;text-align:right;white-space:nowrap}
td.reg{font-family:ui-monospace,Menlo,monospace;font-size:12.5px}
.mono{font-family:ui-monospace,Menlo,monospace;font-size:12px;color:var(--mute);word-break:break-all}
.stats{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;margin-bottom:8px}
@media(max-width:520px){.stats{grid-template-columns:repeat(2,1fr)}}
.stats div{background:#2a2019;border-radius:10px;padding:8px}
.stats b{display:block;font-size:18px;font-variant-numeric:tabular-nums}
.hint{font-size:12px;color:var(--mute);margin-top:8px}
footer{margin-top:14px;font-size:12px;color:var(--mute);text-align:center}
.hide{display:none!important}
</style></head><body><div class="wrap">
<header>
  <h1><b>Sinag</b> · <span id="title">Simulator</span></h1>
  <div class="pills"><span id="pLink" class="pill">Modbus: —</span><span id="pGrid" class="pill">Grid: —</span><span id="pFault" class="pill">—</span><span id="pWeb" class="pill">Web: —</span></div>
</header>

<div class="modebar">
  <span class="k">Simulating</span>
  <div class="seg"><button id="m_inverter">Hybrid inverter</button><button id="m_meter">Submeter (SDM120)</button><button id="m_meralco">Meralco meter (C12.18)</button></div>
  <span class="sub" id="modeSub" style="margin:0"></span>
</div>

<div class="grid flow">
  <div class="card"><div class="k" id="t0k"></div><div class="v" id="t0v">—</div><div class="sub" id="t0s"></div><div class="bar"><i id="t0b"></i></div></div>
  <div class="card"><div class="k" id="t1k"></div><div class="v" id="t1v">—</div><div class="sub" id="t1s"></div><div class="bar"><i id="t1b"></i></div></div>
  <div class="card"><div class="k" id="t2k"></div><div class="v" id="t2v">—</div><div class="sub" id="t2s"></div><div class="bar"><i id="t2b"></i></div></div>
  <div class="card"><div class="k" id="t3k"></div><div class="v" id="t3v">—</div><div class="sub" id="t3s"></div><div class="bar"><i id="t3b"></i></div></div>
</div>

<div class="grid two">
  <div class="card">
    <h2>Controls</h2>
    <div class="row"><label>Sun</label><input id="sun" type="range" min="0" max="100"><output id="sunO"></output></div>
    <div class="row"><label>Load</label><input id="load" type="range" min="0" max="100"><output id="loadO"></output></div>
    <div class="row" id="voltRow"><label>Voltage</label><input id="volt" type="range" min="180" max="260" step="0.5"><output id="voltO"></output></div>
    <div class="row"><label>Knob</label><div class="seg" id="knobs"></div></div>
    <div class="sub" id="knobSub"></div>
    <div class="btns">
      <button id="bBrown">Brownout</button>
      <button id="bFault" class="danger">Fault 21</button>
      <select id="speed" title="Simulation speed"><option value="1">Speed 1×</option><option value="10">10×</option><option value="60">60×</option><option value="300">300×</option><option value="1800">1800×</option></select>
      <button id="bSet">Set…</button>
    </div>
    <div class="hint" id="panelHint"></div>
  </div>
  <div class="card">
    <h2 id="regTitle">Modbus registers</h2>
    <div class="sub" id="mapSub" style="margin:-6px 0 6px"></div>
    <table><thead><tr><th>Reg</th><th>Field</th><th class="n">Value</th><th class="n">Raw</th></tr></thead><tbody id="regs"></tbody></table>
  </div>
</div>

<div class="card" style="margin-top:12px">
  <h2>Link to Sinag device</h2>
  <div class="stats">
    <div><span class="k">Requests</span><b id="mReq">0</b></div>
    <div><span class="k">Last poll</span><b id="mAge">—</b></div>
    <div><span class="k">CRC errors</span><b id="mCrc">0</b></div>
    <div><span class="k">Exceptions</span><b id="mExc">0</b></div>
  </div>
  <div class="mono">RX ← <span id="mRx">—</span></div>
  <div class="mono">TX → <span id="mTx">—</span></div>
  <div class="hint" id="mHint"></div>
</div>
<footer id="foot"></footer>
</div>
<script>
const $=id=>document.getElementById(id);
const KNOB_LABEL={sun:"Sun",load:"Load",soc:"SoC",speed:"Speed",volt:"Voltage"};
let S=null,drag={},timers={},knobKey="";
const fmt=(n,d=0)=>Number(n).toLocaleString(undefined,{minimumFractionDigits:d,maximumFractionDigits:d});
const sgn=n=>(n>0?"+":"")+fmt(n);
function pill(id,txt,cls){const e=$(id);e.textContent=txt;e.className="pill "+(cls||"")}
function tile(i,k,v,unit,sub,pct,color){$("t"+i+"k").textContent=k;$("t"+i+"v").innerHTML=v+"<small>"+unit+"</small>";
  $("t"+i+"s").textContent=sub;const b=$("t"+i+"b");b.style.width=Math.max(0,Math.min(100,pct))+"%";b.style.background=color}
function setSlider(id,val,label){if(!drag[id])$(id).value=val;$(id+"O").textContent=label}
function render(s){
  S=s;const c=s.ctl,m=s.mb,meter=s.mode!="inverter",ansi=s.mode=="meralco";
  $("title").textContent=ansi?"Meralco Smart Meter Simulator":meter?"Submeter Simulator":"Inverter Simulator";document.title="Sinag "+(ansi?"Meralco":meter?"Meter":"Inverter")+" Sim";
  ["inverter","meter","meralco"].forEach(k=>$("m_"+k).className=s.mode==k?"on":"");
  $("modeSub").textContent=ansi?"Aclara I-210+c-style utility meter · ANSI C12.18 packets · C12.19 tables":meter?"SDM120-style customer submeter · Modbus slave "+m.slave_id+" · float32 registers":"Hybrid solar inverter · Modbus slave "+m.slave_id+" · Contract A map";
  if(meter){const e=s.meter;
    tile(0,"Voltage",fmt(e.v,1),"V",e.hz.toFixed(2)+" Hz",e.v/2.6,e.v<200&&e.v>0?"var(--warn)":"var(--grid)");
    tile(1,"Net power",sgn(e.p),"W",(e.p>0.5?"Importing":e.p<-0.5?"Exporting":"Idle")+" · PF "+e.pf.toFixed(2),Math.abs(e.p)/50,e.p<0?"var(--ok)":"var(--acc)");
    tile(2,"Current",fmt(e.i,2),"A",fmt(e.va)+" VA · "+fmt(e.var)+" VAr",e.i/0.3,"var(--batt)");
    tile(3,"Meter reading",fmt(e.imp,2),"kWh","Export "+fmt(e.exp,2)+" kWh",0,"var(--sun)");
    $("t1s").textContent+=" · load "+fmt(e.load_w)+" W, solar "+fmt(e.pv_w)+" W";
  }else{const r=s.inv;
    tile(0,"☀ Solar",fmt(r.pv_w),"W","Sun "+c.sun+"%",r.pv_w/50,"var(--sun)");
    tile(1,"⌂ Home load",fmt(r.load_w),"W","Load knob "+c.load+"%",r.load_w/40,"var(--acc)");
    tile(2,"▮ Battery",r.batt_soc,"%",r.batt_w>0?"Discharging "+fmt(r.batt_w)+" W":r.batt_w<0?"Charging "+fmt(-r.batt_w)+" W":"Idle",r.batt_soc,"var(--batt)");
    tile(3,"⚡ Grid",sgn(r.grid_w),"W",(!r.grid_on?"Brownout — grid off":r.grid_w>0?"Importing":r.grid_w<0?"Exporting":"Idle")+" · "+r.inv_temp_c+" °C",r.inv_temp_c/0.9,r.inv_temp_c>60?"var(--bad)":"var(--grid)");
  }
  setSlider("sun",c.sun,c.sun+"%");setSlider("load",c.load,c.load+"%");setSlider("volt",c.volt,c.volt.toFixed(1)+" V");
  $("voltRow").classList.toggle("hide",!meter);
  const key=c.knobs.join();if(key!==knobKey){knobKey=key;$("knobs").innerHTML=c.knobs.map(k=>`<button data-k="${k}">${KNOB_LABEL[k]}</button>`).join("");
    $("knobs").querySelectorAll("button").forEach(b=>b.onclick=()=>send({knob:b.dataset.k}))}
  $("knobs").querySelectorAll("button").forEach(b=>b.className=b.dataset.k==c.knob?"on":"");
  $("knobSub").textContent="Physical knob at "+c.pot+"% · "+(c.knob_locked?"turn it to take over":"in control");
  $("bBrown").className=c.brownout?"on":"";$("bBrown").textContent=c.brownout?"Brownout ON":"Brownout";
  const evOn=meter?c.sag:c.fault;$("bFault").className="danger"+(evOn?" on":"");
  $("bFault").textContent=(meter?"Low voltage":"Fault 21")+(evOn?" ON":"");
  $("bSet").textContent=meter?"Set reading…":"Set SoC…";
  if(document.activeElement!==$("speed"))$("speed").value=c.speed;
  $("panelHint").textContent="Physical panel: tap = next knob target ("+c.knobs.map(k=>KNOB_LABEL[k]).join(" → ")+") · hold 1–3 s = Brownout · hold 3–6 s = "+(meter?"Low voltage":"Fault")+" · hold >6 s = next mode.";
  pill("pLink",m.linked?"Modbus: polled":"Modbus: "+(m.never?"waiting":"no polls"),m.linked?"ok":m.never?"":"bad");
  pill("pGrid",c.brownout?"Grid: BROWNOUT":"Grid: ON",c.brownout?"warn":"ok");
  if(meter)pill("pFault",c.sag?"Voltage: LOW":"Voltage: normal",c.sag?"warn":"ok");
  else pill("pFault",c.fault?"Fault: 21":"Fault: none",c.fault?"bad":"ok");
  $("regTitle").textContent=ansi?"C12.19 tables":"Modbus registers · slave "+m.slave_id;
  $("mapSub").textContent=ansi?"Read after logon · little-endian · Wh, V×100, A×1000":meter?"Function 04 · float32, high word first":"Function 03 · int16 / uint16";
  $("regs").innerHTML=s.table.map(r=>`<tr><td class="reg">${r[0]}</td><td>${r[1]}</td><td class="n">${r[2]}</td><td class="raw">${r[3]}</td></tr>`).join("");
  $("mReq").textContent=m.requests;$("mCrc").textContent=m.crc_errors;$("mExc").textContent=m.exceptions;
  $("mAge").textContent=m.never?"never":m.last_age_s+" s ago";$("mRx").textContent=m.last_rx||"—";$("mTx").textContent=m.last_tx||"—";
  $("mHint").textContent=m.never?(m.crc_errors?"Bytes arrive but CRC fails — check baud (9600) and GND.":"No requests yet — check TX↔RX are crossed and GND is shared."+(m.rs485?" With RS485, try swapping A/B.":"")):
    (m.linked?"Sinag device is reading "+(ansi?"the C12.19 tables":"slave "+m.slave_id)+(m.rs485?" over RS485.":" over direct UART."):"Polls stopped — is the Sinag device running?");
  const sys=s.sys;$("foot").textContent=`fw ${sys.fw} · ${sys.ap?"AP mode":"Wi-Fi "+sys.rssi+" dBm"} · ${sys.ip} · up ${Math.floor(sys.uptime_s/60)} min · ${m.baud} 8N1`;
}
async function poll(){try{const r=await fetch("/api/state",{cache:"no-store"});render(await r.json());pill("pWeb","Web: live","ok")}catch(e){pill("pWeb","Web: offline","bad")}}
function send(p){return fetch("/api/control",{method:"POST",body:new URLSearchParams(p)}).then(r=>r.json()).then(render).catch(()=>{})}
["sun","load","volt"].forEach(id=>{const el=$(id);
  el.addEventListener("pointerdown",()=>drag[id]=1);
  el.addEventListener("input",()=>{drag[id]=1;clearTimeout(timers[id]);timers[id]=setTimeout(()=>send({[id]:el.value}),120)});
  el.addEventListener("change",()=>{send({[id]:el.value}).then(()=>drag[id]=0)});
});
["inverter","meter","meralco"].forEach(k=>$("m_"+k).onclick=()=>send({mode:k}));
$("bBrown").onclick=()=>S&&send({brownout:S.ctl.brownout?0:1});
$("bFault").onclick=()=>S&&(S.mode=="meter"?send({sag:S.ctl.sag?0:1}):send({fault:S.ctl.fault?0:1}));
$("speed").onchange=e=>send({speed:e.target.value});
$("bSet").onclick=()=>{if(!S)return;
  if(S.mode=="meter"){const v=prompt("Import reading (kWh)",S.meter.imp.toFixed(2));if(v!==null&&v!=="")send({imp:v})}
  else{const v=prompt("Battery charge (0–100 %)",S.inv.batt_soc);if(v!==null&&v!=="")send({soc:v})}};
poll();setInterval(poll,1000);
</script></body></html>)HTML";
