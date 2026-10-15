/*
 * ═══════════════════════════════════════════════════════════════════════════
 *   SGP MESHTASTIC — web interface (served from PROGMEM)
 *   Tactical theme, English, mobile-first. Talks to /api/* on the device.
 * ═══════════════════════════════════════════════════════════════════════════
 */
const char PAGE_INDEX[] PROGMEM = R"HTML(<!DOCTYPE html><html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no,viewport-fit=cover">
<title>SGP MESHTASTIC</title>
<style>
:root{
--bg:#060a0f;--bg2:#0a1017;--card:#0d141d;--line:#1b2b3a;--line2:#2c4256;
--txt:#cfe0f0;--mut:#6f8aa3;--dim:#48617a;
--ac:#39d6a3;--ac2:#2fb9ff;--me:#0e2a24;--meln:#39d6a3;--them:#111a24;
--ok:#39d6a3;--bad:#ff5566;--amber:#ffb43a;--mesh:#9a7bff;
--mono:ui-monospace,'SF Mono',Menlo,'Roboto Mono','Courier New',monospace}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html,body{margin:0;padding:0;background:var(--bg);color:var(--txt);
font-family:var(--mono);font-size:15px;line-height:1.4;
background-image:radial-gradient(circle at 20% -10%,#0d1b24 0,transparent 55%),radial-gradient(circle at 100% 0,#101c2e 0,transparent 45%)}
#app{max-width:620px;margin:0 auto;min-height:100vh;display:flex;flex-direction:column;
background:linear-gradient(180deg,#070c12,#060a0f)}
/* top bar */
.top{position:sticky;top:0;z-index:30;padding:10px 14px;
background:rgba(7,12,18,.92);backdrop-filter:blur(8px);border-bottom:1px solid var(--line)}
.brand{display:flex;align-items:center;gap:9px;font-weight:700;letter-spacing:.14em;font-size:15px}
.brand .dot{width:9px;height:9px;border-radius:50%;background:var(--ac);box-shadow:0 0 10px var(--ac)}
.brand small{color:var(--mut);font-weight:400;letter-spacing:.06em}
.pills{display:flex;flex-wrap:wrap;gap:6px;margin-top:8px}
.pill{font-size:11px;letter-spacing:.05em;padding:3px 8px;border:1px solid var(--line2);border-radius:20px;
color:var(--mut);background:#0a121b;white-space:nowrap}
.pill b{color:var(--txt);font-weight:600}
.pill.on{border-color:var(--ac);color:var(--ac)}
.pill.bad{border-color:var(--bad);color:var(--bad)}
/* tabs */
.tabs{display:flex;gap:2px;padding:8px 8px 0;background:var(--bg);overflow-x:auto}
.tab{flex:0 0 auto;padding:9px 14px;font-size:12px;letter-spacing:.08em;color:var(--mut);
border:1px solid transparent;border-bottom:none;border-radius:10px 10px 0 0;cursor:pointer;white-space:nowrap}
.tab.sel{color:var(--ac);background:var(--card);border-color:var(--line)}
/* views */
.wrap{flex:1;display:flex;flex-direction:column;min-height:0}
.view{display:none;flex:1;flex-direction:column;min-height:0;padding:12px 14px 20px}
.view.sel{display:flex}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:14px;margin-bottom:12px;position:relative}
.card h3{margin:0 0 10px;font-size:12px;letter-spacing:.14em;color:var(--mut);font-weight:600;text-transform:uppercase}
.corner{position:absolute;width:9px;height:9px;border:1px solid var(--line2)}
.corner.tl{top:-1px;left:-1px;border-right:0;border-bottom:0}
.corner.tr{top:-1px;right:-1px;border-left:0;border-bottom:0}
.corner.bl{bottom:-1px;left:-1px;border-right:0;border-top:0}
.corner.br{bottom:-1px;right:-1px;border-left:0;border-top:0}
/* key/value */
.kv{display:flex;justify-content:space-between;gap:10px;padding:6px 0;border-bottom:1px dashed var(--line);font-size:13px}
.kv:last-child{border-bottom:none}
.kv .k{color:var(--mut)}
.kv .v{color:var(--txt);text-align:right}
.v.ac{color:var(--ac)}.v.am{color:var(--amber)}.v.bad{color:var(--bad)}
/* chat */
#chatScroll{flex:1;overflow-y:auto;display:flex;flex-direction:column;gap:7px;padding:4px 2px 8px}
.bub{max-width:82%;padding:8px 11px;border-radius:13px;font-size:14px;line-height:1.35;word-wrap:break-word}
.bub.them{align-self:flex-start;background:var(--them);border:1px solid var(--line)}
.bub.me{align-self:flex-end;background:var(--me);border:1px solid #1d5243}
.bub .meta{display:block;font-size:10px;color:var(--mut);margin-bottom:3px;letter-spacing:.04em}
.bub.me .meta{color:var(--meln)}
.empty{color:var(--dim);text-align:center;padding:30px 10px;font-size:13px}
.compose{display:flex;gap:8px;padding-top:8px;border-top:1px solid var(--line)}
.compose input{flex:1}
/* inputs */
input,select,button,textarea{font-family:var(--mono);font-size:15px}
input[type=text],input[type=number],select,textarea{
width:100%;padding:11px 12px;background:#070d14;border:1px solid var(--line2);border-radius:10px;
color:var(--txt);outline:none}
input:focus,select:focus,textarea:focus{border-color:var(--ac)}
label{display:block;font-size:11px;letter-spacing:.08em;color:var(--mut);margin:10px 0 5px;text-transform:uppercase}
button{padding:11px 15px;background:#0a1620;border:1px solid var(--line2);border-radius:10px;color:var(--txt);cursor:pointer;letter-spacing:.05em}
button:active{transform:translateY(1px)}
button.pri{background:linear-gradient(180deg,#123a30,#0e2a24);border-color:var(--ac);color:var(--ac)}
button.send{background:var(--ac);color:#04120d;border-color:var(--ac);font-weight:700;padding:11px 16px}
button.danger{border-color:var(--bad);color:var(--bad)}
.row{display:flex;gap:8px;flex-wrap:wrap}
.row>*{flex:1}
.hint{font-size:11px;color:var(--dim);margin-top:6px;line-height:1.5}
/* channel/node rows */
.lrow{display:flex;align-items:center;gap:10px;padding:10px;border:1px solid var(--line);border-radius:11px;margin-bottom:8px;background:#0a121b}
.lrow.sel{border-color:var(--ac);background:#0b1a16}
.lrow .nm{flex:1;min-width:0}
.lrow .nm b{display:block;font-size:14px;color:var(--txt);overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.lrow .nm small{color:var(--mut);font-size:11px}
.lock{font-size:11px;color:var(--amber);border:1px solid var(--amber);border-radius:6px;padding:1px 5px}
.open{font-size:11px;color:var(--dim);border:1px solid var(--dim);border-radius:6px;padding:1px 5px}
.bars{display:inline-flex;gap:2px;align-items:flex-end;height:14px}
.bars i{width:3px;background:var(--dim);border-radius:1px}
.sig b0{}
.chip{font-size:10px;padding:2px 6px;border-radius:12px;border:1px solid var(--line2);color:var(--mut)}
/* scan */
#scanCv{width:100%;height:200px;background:#060b11;border:1px solid var(--line);border-radius:10px}
.scanlbl{display:flex;justify-content:space-between;font-size:10px;color:var(--dim);margin-top:4px}
/* toast */
#toast{position:fixed;left:50%;bottom:22px;transform:translateX(-50%) translateY(40px);
background:#0c1a24;border:1px solid var(--ac);color:var(--ac);padding:9px 16px;border-radius:22px;
font-size:12px;letter-spacing:.05em;opacity:0;transition:.3s;z-index:60;pointer-events:none;max-width:90%}
#toast.show{opacity:1;transform:translateX(-50%) translateY(0)}
.seg{display:flex;border:1px solid var(--line2);border-radius:10px;overflow:hidden}
.seg button{flex:1;border:none;border-radius:0;background:transparent;color:var(--mut)}
.seg button.on{background:#0e2a24;color:var(--ac)}
/* intercept rows */
.irow{border:1px solid var(--line);border-radius:10px;padding:8px 10px;margin-bottom:7px;background:#0a121b;font-size:12px}
.irow .ih{display:flex;justify-content:space-between;gap:8px;align-items:center}
.irow .txt{margin-top:5px;color:var(--txt);font-size:13px;word-wrap:break-word}
.irow .enc{color:var(--dim);font-style:italic}
.irow .tag{font-size:10px;padding:1px 6px;border-radius:10px;border:1px solid var(--line2);color:var(--mut)}
.irow .tag.dec{border-color:var(--ac);color:var(--ac)}
.live{display:inline-block;width:8px;height:8px;border-radius:50%;background:var(--bad);margin-right:6px;vertical-align:middle}
.live.on{background:var(--ac);box-shadow:0 0 8px var(--ac);animation:pulse 1.2s infinite}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.35}}
.big{display:flex;align-items:baseline;gap:8px;margin:4px 0 10px}
.big b{font-size:26px;color:var(--ac)}.big span{color:var(--mut);font-size:12px}
/* ───────── DESKTOP / LARGE SCREEN ───────── */
@media(min-width:900px){
  body{background-attachment:fixed}
  #app{max-width:1180px;border-left:1px solid var(--line);border-right:1px solid var(--line)}
  .brand{font-size:17px}
  .pills{gap:8px}
  .tabs{padding:10px 16px 0}
  .tab{font-size:13px;padding:11px 20px}
  .view{padding:22px 30px 30px}
  /* card-heavy views → two columns (only while selected) */
  .view.sel[data-v="chan"],.view.sel[data-v="radio"],.view.sel[data-v="set"]{display:block;column-count:2;column-gap:24px}
  .view.sel[data-v="chan"] .card,.view.sel[data-v="radio"] .card,.view.sel[data-v="set"] .card{break-inside:avoid}
  .view.sel[data-v="nodes"],.view.sel[data-v="mon"]{display:block}
  #nodeList,#monLog{column-count:2;column-gap:16px}
  #nodeList .lrow,#monLog .irow{break-inside:avoid}
  /* chat: centered column, wider bubbles */
  .view.sel[data-v="chat"]{max-width:860px;margin:0 auto;width:100%}
  .bub{max-width:62%;font-size:15px}
  #scanCv{height:280px}
  .monctl{display:grid;grid-template-columns:1fr 1fr;gap:0 18px;align-items:end}
}
@media(min-width:1300px){
  #app{max-width:1340px}
  #nodeList,#monLog{column-count:3}
}
</style></head><body>
<div id="app">
  <div class="top">
    <div class="brand"><span class="dot"></span>SGP MESHTASTIC <small id="bId"></small></div>
    <div class="pills">
      <span class="pill" id="pRadio">RADIO</span>
      <span class="pill" id="pRegion">--</span>
      <span class="pill" id="pFreq">--</span>
      <span class="pill" id="pPreset">--</span>
      <span class="pill" id="pNodes">0 nodes</span>
      <span class="pill" id="pRep">RELAY OFF</span>
    </div>
  </div>

  <div class="tabs" id="tabs">
    <div class="tab sel" data-v="chat">◈ CHAT</div>
    <div class="tab" data-v="chan">⧉ CHANNELS</div>
    <div class="tab" data-v="nodes">⬡ NODES</div>
    <div class="tab" data-v="radio">≈ RADIO</div>
    <div class="tab" data-v="scan">▤ SCAN</div>
    <div class="tab" data-v="mon">◉ LISTEN</div>
    <div class="tab" data-v="img">📷 IMAGE</div>
    <div class="tab" data-v="sos">🆘 SOS</div>
    <div class="tab" data-v="set">⚙ SETTINGS</div>
  </div>

  <div class="wrap">
    <!-- CHAT -->
    <div class="view sel" data-v="chat">
      <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:8px">
        <div style="font-size:12px;color:var(--mut);letter-spacing:.08em">CHANNEL <b id="chatChan" style="color:var(--ac)">--</b></div>
        <button style="padding:6px 10px;font-size:11px" onclick="announce()">⟲ ANNOUNCE</button>
      </div>
      <div id="chatScroll"><div class="empty">No messages yet. Say hi to the mesh.</div></div>
      <div class="compose">
        <input id="msg" type="text" placeholder="Message the mesh…" maxlength="200"
          onkeydown="if(event.key==='Enter')doSend()">
        <button class="send" onclick="doSend()">SEND</button>
      </div>
    </div>

    <!-- CHANNELS -->
    <div class="view" data-v="chan">
      <div class="card"><span class="corner tl"></span><span class="corner tr"></span><span class="corner bl"></span><span class="corner br"></span>
        <h3>Channels</h3>
        <div id="chanList"></div>
        <div class="hint">The <b>primary</b> channel (first) sets the mesh frequency. Join the public mesh with
        primary <b>LongFast</b> and the default key. Add private channels with your own passphrase (AES).</div>
      </div>
      <div class="card"><span class="corner tl"></span><span class="corner tr"></span><span class="corner bl"></span><span class="corner br"></span>
        <h3>Add channel</h3>
        <label>Name</label><input id="caName" type="text" placeholder="e.g. secret" maxlength="11">
        <label>Passphrase (blank = default key · "none" = open)</label>
        <input id="caPass" type="text" placeholder="passphrase">
        <div class="row" style="margin-top:10px">
          <button class="pri" onclick="chanAdd()">+ ADD CHANNEL</button>
        </div>
      </div>
      <div class="card"><span class="corner tl"></span><span class="corner tr"></span><span class="corner bl"></span><span class="corner br"></span>
        <h3>Primary channel (frequency)</h3>
        <label>Primary name</label><input id="cpName" type="text" maxlength="11">
        <label>Primary passphrase (blank = default)</label><input id="cpPass" type="text" placeholder="leave blank for public key">
        <button class="pri" style="margin-top:10px" onclick="chanPrimary()">SET PRIMARY</button>
        <div class="hint">Changing this re-tunes the radio to a new slot. All nodes must share the same primary name + key + region + preset to talk.</div>
      </div>
    </div>

    <!-- NODES -->
    <div class="view" data-v="nodes">
      <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:8px">
        <div style="font-size:12px;color:var(--mut)"><b id="nCount" style="color:var(--ac)">0</b> nodes heard</div>
        <button style="padding:6px 10px;font-size:11px" onclick="announce()">⟲ REQUEST INFO</button>
      </div>
      <div id="nodeList"><div class="empty">Listening for nodes…</div></div>
    </div>

    <!-- RADIO -->
    <div class="view" data-v="radio">
      <div class="card"><span class="corner tl"></span><span class="corner tr"></span><span class="corner bl"></span><span class="corner br"></span>
        <h3>Live radio</h3>
        <div class="kv"><span class="k">Frequency</span><span class="v ac" id="rFreq">--</span></div>
        <div class="kv"><span class="k">Bandwidth</span><span class="v" id="rBw">--</span></div>
        <div class="kv"><span class="k">SF / CR</span><span class="v" id="rSfcr">--</span></div>
        <div class="kv"><span class="k">TX / RX / Relay</span><span class="v" id="rStats">--</span></div>
        <div class="kv"><span class="k">Last RSSI / SNR</span><span class="v" id="rSig">--</span></div>
      </div>
      <div class="card"><span class="corner tl"></span><span class="corner tr"></span><span class="corner bl"></span><span class="corner br"></span>
        <h3>Region &amp; preset</h3>
        <label>Region</label><select id="selRegion" onchange="radioSet()"></select>
        <label>Modem preset</label><select id="selPreset" onchange="radioSet()"></select>
        <div class="hint">Match these to the mesh you want to join (US / EU_868 and LongFast are the common defaults).</div>
      </div>
      <div class="card"><span class="corner tl"></span><span class="corner tr"></span><span class="corner bl"></span><span class="corner br"></span>
        <h3>Power &amp; options</h3>
        <label>TX power <span id="pwrVal" style="color:var(--ac)"></span> dBm (max <span id="pwrMax"></span>)</label>
        <input id="selPwr" type="range" min="2" max="22" step="1" oninput="pwrVal.textContent=this.value" onchange="radioSet()" style="width:100%">
        <div class="row" style="margin-top:12px">
          <div class="seg"><button id="boostOff" onclick="setBoost(0)">GAIN NORM</button><button id="boostOn" onclick="setBoost(1)">RX BOOST</button></div>
        </div>
        <div class="row" style="margin-top:10px">
          <div class="seg"><button id="repOff" onclick="setRep(0)">RELAY OFF</button><button id="repOn" onclick="setRep(1)">RELAY / REPEATER</button></div>
        </div>
        <div class="hint">RX Boost improves sensitivity (slightly more current). Relay mode rebroadcasts other nodes' packets to extend the mesh.</div>
      </div>
      <div class="card"><span class="corner tl"></span><span class="corner tr"></span><span class="corner bl"></span><span class="corner br"></span>
        <h3>Frequency override (advanced)</h3>
        <label>Exact MHz (0 = use channel slot)</label>
        <div class="row"><input id="ovFreq" type="number" step="0.001" placeholder="0"><button class="pri" onclick="setOverride()">SET</button></div>
        <div class="hint">Forces a verbatim frequency, ignoring the channel-slot math. Use only if you know the exact mesh frequency.</div>
      </div>
    </div>

    <!-- SCAN -->
    <div class="view" data-v="scan">
      <div class="card"><span class="corner tl"></span><span class="corner tr"></span><span class="corner bl"></span><span class="corner br"></span>
        <h3>Spectrum scan — <span id="scanRegion" style="color:var(--ac)"></span></h3>
        <canvas id="scanCv"></canvas>
        <div class="scanlbl"><span id="scanLo"></span><span id="scanMid"></span><span id="scanHi"></span></div>
        <button class="pri" style="margin-top:12px;width:100%" onclick="doScan()">▶ RUN SWEEP</button>
        <div class="hint">RSSI sweep across the region band. The dashed line marks the current operating frequency. Higher bars = more energy.</div>
      </div>
    </div>

    <!-- SUPER LISTEN / MONITOR -->
    <div class="view" data-v="mon">
      <div class="card"><span class="corner tl"></span><span class="corner tr"></span><span class="corner bl"></span><span class="corner br"></span>
        <h3><span class="live" id="monLive"></span>Super Listen</h3>
        <div class="big"><b id="monFreq">--</b><span>MHz &nbsp;·&nbsp; slot <span id="monSlot">0</span>/<span id="monSlots">0</span> &nbsp;·&nbsp; <span id="monReg">--</span></span></div>
        <div class="kv"><span class="k">Intercepted</span><span class="v ac" id="monTotal">0</span></div>
        <div class="kv"><span class="k">Decrypted</span><span class="v am" id="monDec">0</span></div>
        <div class="monctl">
          <div>
            <label>Monitor preset (bandwidth to listen on)</label>
            <select id="monPreset" onchange="monApply()"></select>
          </div>
          <div>
            <label>Coverage</label>
            <div class="seg"><button id="hopOff" onclick="monHop(0)">DEFAULT SLOT</button><button id="hopOn" onclick="monHop(1)">HOP ALL SLOTS</button></div>
          </div>
        </div>
        <div class="row" style="margin-top:14px">
          <button class="pri" id="monBtn" onclick="monToggle()" style="flex:2">▶ START LISTENING</button>
          <button onclick="monClear()">CLEAR</button>
        </div>
        <div class="hint">Captures Meshtastic traffic and tries to decrypt it with every known key (the public LongFast keys + your channels).
        <b>Default slot</b> stays on the main frequency (catches nearly all normal traffic). <b>Hop all slots</b> sweeps every
        channel slot in the region to discover other meshes — one radio can only listen to one frequency at a time, so it jumps between them.</div>
      </div>
      <div class="card"><span class="corner tl"></span><span class="corner tr"></span><span class="corner bl"></span><span class="corner br"></span>
        <h3>Intercept log</h3>
        <div id="monLog"><div class="empty">Press START to begin listening.</div></div>
      </div>
    </div>

    <!-- IMAGE over LoRa -->
    <div class="view" data-v="img">
      <div class="card"><span class="corner tl"></span><span class="corner tr"></span><span class="corner bl"></span><span class="corner br"></span>
        <h3>Send image</h3>
        <input id="imgFile" type="file" accept="image/*" style="margin-bottom:10px" onchange="imgPick(event)">
        <div class="monctl">
          <div>
            <label>Resolution</label>
            <select id="imgRes"><option value="32">32 × 32 (tiny)</option><option value="48" selected>48 × 48</option><option value="64">64 × 64</option></select>
          </div>
          <div>
            <label>Quality</label>
            <select id="imgFmt"><option value="1">1-bit B/W (smallest)</option><option value="0" selected>4-bit grayscale</option></select>
          </div>
        </div>
        <div style="display:flex;gap:14px;align-items:center;margin-top:12px;flex-wrap:wrap">
          <div><div style="font-size:10px;color:var(--mut);margin-bottom:4px">PREVIEW (what is sent)</div>
            <canvas id="imgPrev" width="192" height="192" style="width:150px;height:150px;image-rendering:pixelated;border:1px solid var(--line);border-radius:8px;background:#000"></canvas></div>
          <div style="flex:1;min-width:140px">
            <div class="hint" id="imgInfo">Pick an image. It is shrunk and reduced to very low quality so it fits in a handful of LoRa packets.</div>
          </div>
        </div>
        <div class="row" style="margin-top:12px">
          <button class="pri" id="imgSendBtn" onclick="imgSend()" style="flex:2">▶ SEND OVER LoRa</button>
        </div>
        <div id="imgTxBar" style="display:none;margin-top:10px">
          <div style="height:8px;background:#0a1620;border:1px solid var(--line2);border-radius:5px;overflow:hidden"><div id="imgTxFill" style="height:100%;width:0;background:var(--ac)"></div></div>
          <div id="imgTxTxt" style="font-size:11px;color:var(--mut);margin-top:5px"></div>
        </div>
        <div class="hint" style="margin-top:10px">Sending a picture ties up the radio for a few seconds (one packet per chunk). Both nodes must share the same channel + radio settings.</div>
      </div>
      <div class="card"><span class="corner tl"></span><span class="corner tr"></span><span class="corner bl"></span><span class="corner br"></span>
        <h3><span class="live" id="imgLive"></span>Incoming image</h3>
        <div style="display:flex;gap:14px;align-items:flex-start;flex-wrap:wrap">
          <canvas id="imgRxCv" width="256" height="256" style="width:200px;height:200px;image-rendering:pixelated;border:1px solid var(--line);border-radius:8px;background:#060b11"></canvas>
          <div style="flex:1;min-width:140px">
            <div class="kv"><span class="k">From</span><span class="v" id="imgFrom">—</span></div>
            <div class="kv"><span class="k">Size</span><span class="v" id="imgSize">—</span></div>
            <div class="kv"><span class="k">Chunks</span><span class="v ac" id="imgChunks">0 / 0</span></div>
            <button style="margin-top:10px" onclick="imgClear()">CLEAR</button>
          </div>
        </div>
        <div class="hint">The node switches to receive mode <b>automatically</b> when an image starts arriving. Missing pieces show as dim blocks; it fills in live as packets land.</div>
      </div>
    </div>

    <!-- SUPER SOS -->
    <div class="view" data-v="sos">
      <div class="card" style="border-color:#5a1f1f">
        <span class="corner tl" style="border-color:var(--bad)"></span><span class="corner tr" style="border-color:var(--bad)"></span><span class="corner bl" style="border-color:var(--bad)"></span><span class="corner br" style="border-color:var(--bad)"></span>
        <h3 style="color:var(--bad)"><span class="live" id="sosLive"></span>Super SOS beacon</h3>
        <div class="big"><b id="sosState" style="color:var(--bad)">OFF</b><span id="sosSub">emergency distress beacon</span></div>
        <label>Emergency message (optional)</label>
        <textarea id="sosMsg" rows="2" maxlength="120" placeholder="e.g. Need help — injured, no water"></textarea>
        <div class="row" style="margin-top:10px;align-items:center">
          <button id="sosGpsBtn" onclick="sosGetLoc()" style="flex:2">📍 ADD MY LOCATION (GPS)</button>
          <button onclick="sosClrLoc()" style="flex:1">CLEAR</button>
        </div>
        <div class="hint" id="sosLocTxt" style="margin-top:6px">Location not added. Tapping GPS asks your phone/browser for your position.</div>
        <div class="row" style="margin-top:14px">
          <button class="pri" id="sosBtn" onclick="sosToggle()" style="flex:1;background:linear-gradient(180deg,#5a1414,#3a0e0e);border-color:var(--bad);color:#ff8a8a;font-weight:700;font-size:16px;padding:16px">🆘 START SUPER SOS</button>
        </div>
        <div class="kv" style="margin-top:14px"><span class="k">Transmissions</span><span class="v bad" id="sosCount">0</span></div>
        <div class="kv"><span class="k">Now on</span><span class="v" id="sosNow">—</span></div>
        <div class="hint">Broadcasts <b>SOS</b> (plus your message and location) over LoRa, sweeping <b>every frequency slot and every modem type</b> of the <span id="sosReg">current</span> region, on the public Meshtastic channel so any node can hear it. It keeps transmitting <b>until you press stop or power off</b>. The LED flashes red and the buzzer chirps on each burst. Use only in a real emergency.</div>
      </div>
    </div>

    <!-- SETTINGS -->
    <div class="view" data-v="set">
      <div class="card"><span class="corner tl"></span><span class="corner tr"></span><span class="corner bl"></span><span class="corner br"></span>
        <h3>Wi-Fi network</h3>
        <label>Network name (SSID)</label>
        <input id="wSsid" type="text" maxlength="31" placeholder="SGP_MESH_xxxx">
        <label>Password <span style="color:var(--dim)">(blank = open · 8-63 chars for a password)</span></label>
        <div class="row" style="align-items:center">
          <input id="wPass" type="password" maxlength="63" placeholder="leave blank for open network" style="flex:4">
          <button onclick="wShow()" id="wShowBtn" style="flex:1;min-width:64px">SHOW</button>
        </div>
        <button class="pri" id="wSaveBtn" style="margin-top:12px" onclick="saveWifi()">SAVE WI-FI &amp; REBOOT</button>
        <div class="hint">Saved permanently. The card reboots to apply the new network — you'll need to reconnect your phone to the new Wi-Fi (and re-open http://192.168.4.1).</div>
      </div>
      <div class="card"><span class="corner tl"></span><span class="corner tr"></span><span class="corner bl"></span><span class="corner br"></span>
        <h3>Feedback</h3>
        <label>Buzzer + LED on send / receive</label>
        <div class="seg"><button id="sndOff" onclick="setSound(0)">MUTED</button><button id="sndOn" onclick="setSound(1)">SOUND + LED</button></div>
        <div class="hint">When on, the card chirps and blinks cyan when you send a message and green when one arrives.</div>
      </div>
      <div class="card"><span class="corner tl"></span><span class="corner tr"></span><span class="corner bl"></span><span class="corner br"></span>
        <h3>Identity</h3>
        <div class="kv"><span class="k">Node ID</span><span class="v ac" id="sId">--</span></div>
        <label>Long name (shown in the mesh)</label><input id="sLn" type="text" maxlength="39">
        <label>Short name (≤4 chars)</label><input id="sSn" type="text" maxlength="4">
        <button class="pri" style="margin-top:12px" onclick="saveName()">SAVE &amp; ANNOUNCE</button>
      </div>
      <div class="card"><span class="corner tl"></span><span class="corner tr"></span><span class="corner bl"></span><span class="corner br"></span>
        <h3>Diagnostics</h3>
        <div id="diagBox"><div class="empty">—</div></div>
        <div class="row" style="margin-top:12px">
          <button onclick="loadDiag()">RUN CHECK</button>
          <button class="pri" onclick="reinit()">RE-INIT RADIO</button>
        </div>
      </div>
      <div class="card"><span class="corner tl"></span><span class="corner tr"></span><span class="corner bl"></span><span class="corner br"></span>
        <h3>About</h3>
        <div class="hint">
          <b style="color:var(--txt)">SGP MESHTASTIC</b> — a Meshtastic-compatible node for the SGP Card Mini
          (ESP32-S3 + SX1262). It speaks the real Meshtastic LoRa protocol: 16-byte header, channel-hash
          addressing, AES-CTR channel encryption and protobuf text/node-info messages. Join an existing mesh
          by matching region, preset and the primary channel name + key.<br><br>
          <span id="aFw" style="color:var(--mut)"></span>
        </div>
      </div>
    </div>
  </div>
</div>
<div id="toast"></div>

<script>
var S={},REG={regions:[],presets:[]},curView="chat",lastChatLen=-1;
function $(id){return document.getElementById(id)}
function toast(t){var e=$("toast");e.textContent=t;e.classList.add("show");clearTimeout(e._t);e._t=setTimeout(function(){e.classList.remove("show")},1800)}
function j(u){return fetch(u).then(function(r){return r.json()})}

// tabs
document.querySelectorAll('.tab').forEach(function(t){t.onclick=function(){
  document.querySelectorAll('.tab').forEach(function(x){x.classList.remove('sel')});
  document.querySelectorAll('.view').forEach(function(x){x.classList.remove('sel')});
  t.classList.add('sel');
  var v=t.getAttribute('data-v');curView=v;
  document.querySelector('.view[data-v="'+v+'"]').classList.add('sel');
  if(v==='chan')loadChannels();
  if(v==='nodes')loadNodes();
  if(v==='radio')loadRadio();
  if(v==='mon')loadMonitor();
  if(v==='img')loadImage();
  if(v==='sos')loadSos();
  if(v==='set')loadDiag();
}});

function sig(rssi){var n=0;if(rssi>-60)n=4;else if(rssi>-80)n=3;else if(rssi>-100)n=2;else if(rssi>-115)n=1;
  var h='<span class="bars">';for(var i=0;i<4;i++){var on=i<n;h+='<i style="height:'+(4+i*3)+'px;background:'+(on?'var(--ac)':'var(--dim)')+'"></i>'}return h+'</span>'}

// ── status (poll) ──
function poll(){ j('/api/status').then(function(d){S=d;
  $("bId").textContent=d.id||'';
  $("pRadio").textContent=d.init?'RADIO OK':'RADIO FAIL';$("pRadio").className='pill '+(d.init?'on':'bad');
  $("pRegion").innerHTML='<b>'+d.region+'</b>';
  $("pFreq").innerHTML='<b>'+(d.freq||0).toFixed(3)+'</b> MHz';
  $("pPreset").innerHTML='<b>'+d.preset+'</b>';
  $("pNodes").innerHTML='<b>'+d.nodes+'</b> nodes';
  $("pRep").textContent=d.repeater?'RELAY ON':'RELAY OFF';$("pRep").className='pill '+(d.repeater?'on':'');
  $("chatChan").textContent=d.chan;
  // radio tab live
  $("rFreq").textContent=(d.freq||0).toFixed(3)+' MHz';
  $("rBw").textContent=d.bw+' kHz';
  $("rSfcr").textContent='SF'+d.sf+' / CR4:'+d.cr;
  $("rStats").textContent=d.tx+' / '+d.rx+' / '+d.relay;
  $("rSig").textContent=d.rssi+' dBm · '+d.snr+' dB';
  $("sId").textContent=d.id;
  $("aFw").textContent=d.fw;
}).catch(function(){}); }

// ── chat ──
function loadChat(){ j('/api/chat').then(function(d){
  var sc=$("chatScroll");
  if(!d.msgs.length){sc.innerHTML='<div class="empty">No messages on <b>'+d.chan+'</b> yet.</div>';lastChatLen=0;return}
  if(d.msgs.length===lastChatLen)return; // no change, keep scroll
  var stick=sc.scrollTop+sc.clientHeight>sc.scrollHeight-40;
  var h='';d.msgs.forEach(function(m){
    var meta=m.m?'You':(m.f+' · '+m.r+'dBm'+(m.h?(' · '+m.h+'h'):''));
    h+='<div class="bub '+(m.m?'me':'them')+'"><span class="meta">'+esc(meta)+'</span>'+esc(m.t)+'</div>';
  });
  sc.innerHTML=h;lastChatLen=d.msgs.length;
  if(stick)sc.scrollTop=sc.scrollHeight;
}).catch(function(){}); }
function esc(s){return (s+'').replace(/[&<>"]/g,function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]})}
function doSend(){var t=$("msg").value.trim();if(!t)return;$("msg").value='';
  fetch('/api/send?t='+encodeURIComponent(t)).then(function(r){return r.json()}).then(function(d){
    if(d.ok){lastChatLen=-1;loadChat()}else toast('TX failed');});}
function announce(){fetch('/api/announce').then(function(){toast('Announced to mesh')})}

// ── channels ──
function loadChannels(){ j('/api/channels').then(function(d){
  var h='';d.list.forEach(function(c){
    h+='<div class="lrow '+(c.i===d.active?'sel':'')+'" onclick="chanSel('+c.i+')">';
    h+='<div class="nm"><b>'+esc(c.name)+(c.primary?' ★':'')+'</b><small>hash '+c.hash+(c.enc?(' · AES-'+c.bits):' · open')+'</small></div>';
    h+=c.enc?'<span class="lock">'+c.bits+'</span>':'<span class="open">OPEN</span>';
    if(!c.primary)h+='<button class="danger" style="padding:6px 9px;font-size:11px" onclick="event.stopPropagation();chanDel('+c.i+')">✕</button>';
    h+='</div>';
  });
  $("chanList").innerHTML=h;
  if(S.primary){$("cpName").value=S.primary}
}); }
function chanSel(i){fetch('/api/chan/sel?i='+i).then(function(){loadChannels();lastChatLen=-1;loadChat();toast('Active channel set')})}
function chanAdd(){var n=$("caName").value.trim();if(!n){toast('Name required');return}
  fetch('/api/chan/add?name='+encodeURIComponent(n)+'&pass='+encodeURIComponent($("caPass").value)).then(function(){
    $("caName").value='';$("caPass").value='';loadChannels();toast('Channel added')});}
function chanDel(i){fetch('/api/chan/del?i='+i).then(function(){loadChannels();toast('Channel removed')})}
function chanPrimary(){var n=$("cpName").value.trim();if(!n){toast('Name required');return}
  fetch('/api/chan/primary?name='+encodeURIComponent(n)+'&pass='+encodeURIComponent($("cpPass").value)).then(function(){
    toast('Primary set · radio re-tuned');poll();});}

// ── nodes ──
function loadNodes(){ j('/api/nodes').then(function(d){
  $("nCount").textContent=d.count;
  if(!d.list.length){$("nodeList").innerHTML='<div class="empty">No nodes heard yet.</div>';return}
  d.list.sort(function(a,b){return a.ago-b.ago});
  var h='';d.list.forEach(function(n){
    var ago=n.ago<60?n.ago+'s':(n.ago<3600?Math.floor(n.ago/60)+'m':Math.floor(n.ago/3600)+'h');
    h+='<div class="lrow"><div class="nm"><b>'+esc(n.name)+'</b><small>'+esc(n.id)+(n.short?(' · '+esc(n.short)):'')+' · '+n.hops+' hops · '+ago+' ago</small></div>';
    h+='<div style="text-align:right">'+sig(n.rssi)+'<div style="font-size:10px;color:var(--mut);margin-top:2px">'+n.rssi+' · '+n.snr+'dB</div></div></div>';
  });
  $("nodeList").innerHTML=h;
}); }

// ── radio ──
function loadRadio(){ j('/api/regions').then(function(d){REG=d;
  var rs='';d.regions.forEach(function(r,i){rs+='<option value="'+i+'"'+(i===d.regionIdx?' selected':'')+'>'+r+'</option>'});
  $("selRegion").innerHTML=rs;
  var ps='';d.presets.forEach(function(p,i){ps+='<option value="'+i+'"'+(i===d.presetIdx?' selected':'')+'>'+p+'</option>'});
  $("selPreset").innerHTML=ps;
  $("selPwr").value=S.pwr||22;$("pwrVal").textContent=S.pwr||22;$("pwrMax").textContent=S.maxpwr||22;
  $("ovFreq").value=S.override>0?S.override:'';
  segSel("boost",S.boost);segSel("rep",S.repeater);
}); }
function segSel(name,on){$(name+"On").classList.toggle('on',!!on);$(name+"Off").classList.toggle('on',!on)}
function radioSet(){
  var u='/api/radio?region='+$("selRegion").value+'&preset='+$("selPreset").value+'&pwr='+$("selPwr").value;
  fetch(u).then(function(r){return r.json()}).then(function(){toast('Radio updated');poll();});}
function setBoost(v){fetch('/api/radio?boost='+v).then(function(){segSel("boost",v);toast(v?'RX boost on':'Gain normal');poll()})}
function setRep(v){fetch('/api/radio?repeater='+v).then(function(){segSel("rep",v);toast(v?'Relay on':'Relay off');poll()})}
function setOverride(){var f=parseFloat($("ovFreq").value)||0;fetch('/api/radio?override='+f).then(function(){toast(f?'Override '+f+' MHz':'Override cleared');poll()})}

// ── super listen / monitor ──
var monInited=false;
function loadMonitor(){
  if(!monInited&&REG.presets&&REG.presets.length){
    var ps='';REG.presets.forEach(function(p,i){ps+='<option value="'+i+'">'+p+'</option>'});$("monPreset").innerHTML=ps;monInited=true;
  } else if(!monInited){ j('/api/regions').then(function(d){REG=d;var ps='';d.presets.forEach(function(p,i){ps+='<option value="'+i+'">'+p+'</option>'});$("monPreset").innerHTML=ps;monInited=true;}); }
  refreshMonitor();
}
function refreshMonitor(){ j('/api/monitor').then(function(d){
  $("monLive").className='live '+(d.on?'on':'');
  $("monBtn").textContent=d.on?'■ STOP LISTENING':'▶ START LISTENING';
  $("monBtn").classList.toggle('danger',d.on);
  $("monFreq").textContent=(d.freq||0).toFixed(3);
  $("monSlot").textContent=d.slot;$("monSlots").textContent=d.slots;$("monReg").textContent=d.region;
  $("monTotal").textContent=d.total;$("monDec").textContent=d.decoded;
  if(monInited)$("monPreset").value=d.presetIdx;
  segSel("hop",d.hop);
  if(!d.log.length){$("monLog").innerHTML='<div class="empty">'+(d.on?'Listening… waiting for traffic.':'Press START to begin listening.')+'</div>';return}
  var h='';d.log.forEach(function(m){
    var ago=m.ago<60?m.ago+'s':(Math.floor(m.ago/60)+'m');
    h+='<div class="irow"><div class="ih"><span>'+esc(m.from)+' <span style="color:var(--dim)">→ '+esc(m.to)+'</span></span>';
    h+='<span>'+(m.dec?'<span class="tag dec">'+m.port+'</span>':'<span class="tag">enc</span>')+'</span></div>';
    h+='<div class="ih" style="margin-top:3px;color:var(--mut);font-size:10px"><span>'+m.f.toFixed(3)+' MHz · '+m.hash+' · '+m.hops+'h · '+m.rssi+'dBm/'+m.snr+'dB</span><span>'+ago+'</span></div>';
    if(m.dec&&m.text)h+='<div class="txt">'+esc(m.text)+'</div>';
    else if(!m.dec)h+='<div class="txt enc">[encrypted — unknown key]</div>';
    h+='</div>';
  });
  $("monLog").innerHTML=h;
}); }
function monToggle(){var on=($("monBtn").textContent.indexOf('STOP')>=0)?0:1;
  fetch('/api/monitor/set?on='+on+'&preset='+$("monPreset").value+'&hop='+(($("hopOn").classList.contains('on'))?1:0))
  .then(function(){toast(on?'Super listen ON':'Stopped');refreshMonitor();poll();});}
function monHop(v){segSel("hop",v);fetch('/api/monitor/set?hop='+v).then(function(){refreshMonitor()})}
function monApply(){fetch('/api/monitor/set?preset='+$("monPreset").value).then(function(){refreshMonitor()})}
function monClear(){fetch('/api/monitor/clear').then(function(){refreshMonitor();toast('Log cleared')})}

// ── sound / LED feedback ──
function setSound(v){fetch('/api/sound?on='+v).then(function(){segSel("snd",v);toast(v?'Sound + LED on':'Muted');poll()})}

// ── super SOS ──
var sosLocVal="";
function loadSos(){ $("sosReg").textContent=S.region||'current'; refreshSos(); }
function sosGetLoc(){
  if(!navigator.geolocation){toast('No GPS in this browser');return}
  $("sosGpsBtn").textContent='📍 LOCATING…';
  navigator.geolocation.getCurrentPosition(function(p){
    sosLocVal=p.coords.latitude.toFixed(5)+','+p.coords.longitude.toFixed(5);
    $("sosLocTxt").innerHTML='Location: <b style="color:var(--ac)">'+sosLocVal+'</b> (±'+Math.round(p.coords.accuracy)+'m)';
    $("sosGpsBtn").textContent='📍 LOCATION ADDED ✓';
    toast('Location added');
  },function(e){$("sosGpsBtn").textContent='📍 ADD MY LOCATION (GPS)';toast('GPS failed: '+e.message);},{enableHighAccuracy:true,timeout:10000});
}
function sosClrLoc(){sosLocVal="";$("sosLocTxt").textContent='Location not added.';$("sosGpsBtn").textContent='📍 ADD MY LOCATION (GPS)';}
function sosToggle(){
  if($("sosBtn").getAttribute('data-on')==='1'){ fetch('/api/sos/stop').then(function(){toast('SOS stopped');refreshSos()}); return; }
  var u='/api/sos/start?msg='+encodeURIComponent($("sosMsg").value)+'&loc='+encodeURIComponent(sosLocVal);
  fetch(u).then(function(){toast('SUPER SOS ON — transmitting on all bands');refreshSos()});
}
function refreshSos(){ j('/api/sos').then(function(d){
  var on=d.on;
  $("sosLive").className='live '+(on?'on':'');
  $("sosState").textContent=on?'ON':'OFF';
  $("sosState").style.color=on?'var(--bad)':'var(--dim)';
  $("sosSub").textContent=on?'transmitting — sweeping all frequencies':'emergency distress beacon';
  $("sosBtn").setAttribute('data-on',on?'1':'0');
  $("sosBtn").textContent=on?'■ STOP SOS':'🆘 START SUPER SOS';
  $("sosCount").textContent=d.count;
  $("sosNow").textContent=on?((d.freq||0).toFixed(3)+' MHz · '+d.preset):'—';
  if(!sosLocVal&&d.loc){sosLocVal=d.loc;$("sosLocTxt").innerHTML='Location: <b style="color:var(--ac)">'+d.loc+'</b>';}
}); }

// ── image over LoRa ──
var imgSrc=null,imgBytes=null,imgW=0,imgH=0,imgFmt=0,imgSending=false;
function loadImage(){ refreshImgRx(); }
function imgPick(ev){var f=ev.target.files[0];if(!f)return;var r=new FileReader();
  r.onload=function(){var im=new Image();im.onload=function(){imgSrc=im;imgQuant();};im.src=r.result;};r.readAsDataURL(f);}
function imgQuant(){
  if(!imgSrc)return;
  var W=parseInt($("imgRes").value),H=W;imgFmt=parseInt($("imgFmt").value);
  var c=document.createElement('canvas');c.width=W;c.height=H;var x=c.getContext('2d');
  x.drawImage(imgSrc,0,0,W,H);var d=x.getImageData(0,0,W,H).data;
  var gray=new Uint8Array(W*H);
  for(var i=0;i<W*H;i++){gray[i]=Math.round(0.3*d[i*4]+0.59*d[i*4+1]+0.11*d[i*4+2]);}
  // pack
  var bytes;
  if(imgFmt===1){ // 1-bit
    bytes=new Uint8Array(Math.ceil(W*H/8));
    for(var p=0;p<W*H;p++){if(gray[p]>128)bytes[p>>3]|=(0x80>>(p&7));}
  }else{ // 4-bit
    bytes=new Uint8Array(Math.ceil(W*H/2));
    for(var p=0;p<W*H;p++){var v=gray[p]>>4;if(p&1)bytes[p>>1]|=v;else bytes[p>>1]|=(v<<4);}
  }
  imgBytes=bytes;imgW=W;imgH=H;
  // preview (draw quantized back)
  var pv=$("imgPrev");pv.width=W;pv.height=H;var px=pv.getContext('2d');var id=px.createImageData(W,H);
  for(var p=0;p<W*H;p++){var g;if(imgFmt===1)g=(bytes[p>>3]&(0x80>>(p&7)))?255:0;else{var nib=(p&1)?(bytes[p>>1]&0xf):(bytes[p>>1]>>4);g=nib*17;}
    id.data[p*4]=id.data[p*4+1]=id.data[p*4+2]=g;id.data[p*4+3]=255;}
  px.putImageData(id,0,0);
  var cs=180,n=Math.ceil(bytes.length/cs);
  $("imgInfo").innerHTML='<b>'+W+'×'+H+'</b> · '+(imgFmt===1?'1-bit B/W':'4-bit gray')+' · <b>'+bytes.length+'</b> bytes · <b>'+n+'</b> LoRa packets (~'+Math.ceil(n*0.6)+'s to send)';
}
$("imgRes").onchange=imgQuant;$("imgFmt").onchange=imgQuant;
function hex2(b){var h='';for(var i=0;i<b.length;i++)h+=('0'+b[i].toString(16)).slice(-2);return h;}
function sleep(ms){return new Promise(function(r){setTimeout(r,ms)});}
async function imgSend(){
  if(!imgBytes){toast('Pick an image first');return}
  if(imgSending)return;imgSending=true;$("imgSendBtn").disabled=true;
  var cs=180,n=Math.ceil(imgBytes.length/cs),id=Math.floor(Math.random()*250)+1;
  $("imgTxBar").style.display='block';
  function prog(i,msg){$("imgTxFill").style.width=Math.round(i/(n+1)*100)+'%';$("imgTxTxt").textContent=msg||('Sending chunk '+i+' / '+n);}
  prog(0,'Announcing image — waiting for the other node to get ready…');
  await fetch('/api/img/tx?t=h&id='+id+'&iw='+imgW+'&ih='+imgH+'&fmt='+imgFmt+'&n='+n+'&cs='+cs);
  for(var i=0;i<n;i++){
    var chunk=imgBytes.subarray(i*cs,Math.min((i+1)*cs,imgBytes.length));
    await fetch('/api/img/tx?t=d&id='+id+'&idx='+i+'&data='+hex2(chunk));
    prog(i+2,'Sending chunk '+(i+1)+' / '+n);
    await sleep(120);
  }
  $("imgTxTxt").textContent='Done — '+(n+1)+' packets sent.';
  imgSending=false;$("imgSendBtn").disabled=false;toast('Image sent');
}
function imgClear(){fetch('/api/img/clear').then(function(){refreshImgRx();toast('Cleared')})}
function refreshImgRx(){ j('/api/img/rx').then(function(d){
  $("imgLive").className='live '+(d.active&&d.got<d.n?'on':'');
  if(!d.active){$("imgChunks").textContent='0 / 0';$("imgFrom").textContent='—';$("imgSize").textContent='—';return}
  $("imgFrom").textContent=d.from;$("imgSize").textContent=d.w+'×'+d.h+' · '+(d.fmt==1?'1-bit':'4-bit');
  $("imgChunks").textContent=d.got+' / '+d.n;
  // decode hex
  var bytes=new Uint8Array(d.data.length/2);for(var i=0;i<bytes.length;i++)bytes[i]=parseInt(d.data.substr(i*2,2),16);
  var W=d.w,H=d.h,cv=$("imgRxCv");cv.width=W;cv.height=H;var x=cv.getContext('2d');var id=x.createImageData(W,H);
  for(var p=0;p<W*H;p++){
    var byteIdx=(d.fmt==1)?(p>>3):(p>>1);var chunk=Math.floor(byteIdx/d.cs);
    var have=d.map&&d.map[chunk]==='1';
    var g;
    if(!have){g=-1;}
    else if(d.fmt==1){g=(bytes[p>>3]&(0x80>>(p&7)))?255:0;}
    else{var nib=(p&1)?(bytes[p>>1]&0xf):(bytes[p>>1]>>4);g=nib*17;}
    if(g<0){id.data[p*4]=20;id.data[p*4+1]=26;id.data[p*4+2]=34;}
    else{id.data[p*4]=id.data[p*4+1]=id.data[p*4+2]=g;}
    id.data[p*4+3]=255;
  }
  x.putImageData(id,0,0);
}); }

// ── scan ──
function doScan(){toast('Sweeping…');j('/api/scan').then(function(d){drawScan(d)})}
function drawScan(d){
  var cv=$("scanCv");var w=cv.width=cv.clientWidth*2,h=cv.height=400;var x=cv.getContext('2d');
  x.fillStyle='#060b11';x.fillRect(0,0,w,h);
  $("scanRegion").textContent=d.region;
  if(!d.points.length)return;
  var lo=-130,hi=-30;
  // grid
  x.strokeStyle='#13202c';x.lineWidth=1;for(var g=0;g<=4;g++){var yy=h*g/4;x.beginPath();x.moveTo(0,yy);x.lineTo(w,yy);x.stroke();}
  var n=d.points.length,bw=w/n;
  for(var i=0;i<n;i++){var p=d.points[i];var v=(p.r-lo)/(hi-lo);if(v<0)v=0;if(v>1)v=1;
    var bh=v*(h-10);var hue=120-v*120;
    x.fillStyle='hsl('+hue+',80%,'+(35+v*25)+'%)';
    x.fillRect(i*bw+1,h-bh,bw-2,bh);}
  // current freq marker
  var f0=d.points[0].f,f1=d.points[n-1].f;
  if(d.freq>=f0&&d.freq<=f1){var mx=(d.freq-f0)/(f1-f0)*w;x.strokeStyle='#39d6a3';x.setLineDash([6,6]);x.lineWidth=2;
    x.beginPath();x.moveTo(mx,0);x.lineTo(mx,h);x.stroke();x.setLineDash([]);}
  $("scanLo").textContent=f0.toFixed(1)+' MHz';
  $("scanMid").textContent='● '+d.freq.toFixed(3);
  $("scanHi").textContent=f1.toFixed(1)+' MHz';
}

// ── settings ──
function loadDiag(){
  $("sLn").value=S.name||'';$("sSn").value=S.short||'';
  $("wSsid").value=S.apssid||'';$("wPass").value=S.appass||'';
  segSel("snd",S.sound);
  j('/api/diag').then(function(d){
    var h='';
    h+=kv('Radio init',d.init?'OK':'FAIL',d.init?'ac':'bad');
    h+=kv('SPI bus test',d.busTest?'PASS':'FAIL',d.busTest?'ac':'bad');
    h+=kv('TCXO voltage',d.tcxo+' V','');
    h+=kv('Primary hash',d.primaryHash,'am');
    h+=kv('Status',d.error,d.init?'ac':'bad');
    h+='<div class="hint" style="margin-top:8px">'+d.pins+'</div>';
    $("diagBox").innerHTML=h;
  });
}
function kv(k,v,c){return '<div class="kv"><span class="k">'+k+'</span><span class="v '+(c||'')+'">'+esc(v)+'</span></div>'}
function saveName(){fetch('/api/name?ln='+encodeURIComponent($("sLn").value)+'&sn='+encodeURIComponent($("sSn").value))
  .then(function(){toast('Saved · announced');poll()})}
function wShow(){var p=$("wPass");if(p.type==='password'){p.type='text';$("wShowBtn").textContent='HIDE'}else{p.type='password';$("wShowBtn").textContent='SHOW'}}
var wifiArmed=false;
function saveWifi(){
  var ss=$("wSsid").value.trim(),pw=$("wPass").value;
  if(ss.length<1){toast('Name required');return}
  if(pw.length>0&&(pw.length<8||pw.length>63)){toast('Password: 8-63 chars or blank');return}
  var b=$("wSaveBtn");
  if(!wifiArmed){ // two-tap confirm (no browser confirm() — blocked in captive portals)
    wifiArmed=true;b.textContent='TAP AGAIN TO SAVE & REBOOT';b.classList.add('danger');
    setTimeout(function(){wifiArmed=false;b.textContent='SAVE WI-FI & REBOOT';b.classList.remove('danger')},4000);
    return;
  }
  wifiArmed=false;b.textContent='SAVING…';b.disabled=true;b.classList.remove('danger');
  fetch('/api/wifi?ssid='+encodeURIComponent(ss)+'&pass='+encodeURIComponent(pw)).then(function(r){return r.json()}).then(function(d){
    if(d.ok)toast('Saved — rebooting. Reconnect to "'+ss+'"'+(pw.length?' (password set)':''));
    else {toast(d.err||'Failed');b.textContent='SAVE WI-FI & REBOOT';b.disabled=false;}
  }).catch(function(){toast('Rebooting — reconnect to "'+ss+'"')});
}
function reinit(){toast('Re-initialising…');fetch('/api/reinit').then(function(){loadDiag();poll()})}

// loops
poll();loadChat();
setInterval(function(){poll();if(curView==='chat')loadChat();if(curView==='nodes')loadNodes();if(curView==='mon')refreshMonitor();if(curView==='img')refreshImgRx();if(curView==='sos')refreshSos();},2500);
</script>
</body></html>)HTML";
