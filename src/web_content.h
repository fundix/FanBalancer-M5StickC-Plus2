#pragma once

#include <pgmspace.h>

/**
 * Single-page web UI, embedded in flash.
 *
 * Fully self-contained (no CDN, no external fonts) because clients connect
 * to the device's own access point with no internet access. Served at "/";
 * live data arrives over the "/ws" WebSocket as JSON.
 */
static const char INDEX_HTML[] PROGMEM = R"idx(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>FanBalancer</title>
<style>
:root{--bg:#0f1115;--card:#181c23;--fg:#e8eaf0;--dim:#8a93a5;--ok:#27ae60;--warn:#c9a227;--bad:#c0392b;--acc:#4da3ff;--vib:#f0a24d}
*{box-sizing:border-box;margin:0}
body{background:var(--bg);color:var(--fg);font-family:system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;padding:16px;max-width:640px;margin:0 auto}
header{display:flex;justify-content:space-between;align-items:baseline;margin-bottom:12px}
h1{font-size:20px;letter-spacing:.02em}
#fw{color:var(--dim);font-size:12px}
.card{background:var(--card);border-radius:12px;padding:16px;margin-bottom:12px}
.lbl{color:var(--dim);font-size:11px;text-transform:uppercase;letter-spacing:.1em;text-align:center}
.heroes{display:flex;gap:12px}
.heroes .card{flex:1;margin-bottom:12px;min-width:0}
.big{font-size:56px;font-weight:700;text-align:center;font-variant-numeric:tabular-nums;line-height:1.1}
.big small{font-size:18px;font-weight:500;color:var(--dim);margin-left:4px}
#status{display:block;width:max-content;margin:6px auto 0;padding:4px 16px;border-radius:999px;font-weight:600;font-size:14px;background:#333;color:#fff}
.sub{color:var(--dim);font-size:12px;text-align:center;margin-top:8px;font-variant-numeric:tabular-nums}
canvas{width:100%;height:120px;display:block}
.imbal{display:flex;gap:16px;align-items:center;justify-content:center;flex-wrap:wrap}
#compass{width:220px;height:220px;flex:0 0 auto}
.imbaltext{min-width:150px}
.bar{height:8px;background:#2a2f38;border-radius:999px;margin:12px 0;overflow:hidden;display:none}
.bar>span{display:block;height:100%;width:0;background:var(--acc);transition:width .2s}
.wbtn{background:var(--acc);color:#08101c;border:0;border-radius:8px;padding:10px 18px;font-size:15px;font-weight:600;cursor:pointer;margin:0 4px}
.wbtn.ghost{background:#2a2f38;color:var(--fg)}
.wrow{display:flex;gap:16px;justify-content:center;flex-wrap:wrap;align-items:center}
.wrow input,.wrow select{background:#0f1115;color:var(--fg);border:1px solid #2a2f38;border-radius:6px;padding:6px 8px;font-size:15px}
.grid{display:grid;grid-template-columns:repeat(4,1fr);gap:12px}
.cell .v{font-size:17px;font-weight:600;text-align:center;font-variant-numeric:tabular-nums}
#conn{color:var(--dim);font-size:12px;text-align:center;margin-top:4px}
@media (max-width:480px){.grid{grid-template-columns:repeat(2,1fr)}.big{font-size:46px}}
</style>
</head>
<body>
<header><h1>FanBalancer</h1><span id="fw">&ndash;</span></header>

<div class="heroes">
  <div class="card">
    <div class="lbl">RPM</div>
    <div class="big" id="rpm">&ndash;</div>
    <span id="status">CONNECTING</span>
  </div>
  <div class="card">
    <div class="lbl">Vibration &middot; broadband</div>
    <div class="big" id="vib">&ndash;<small>g</small></div>
    <div class="sub" id="vibsub">&ndash;</div>
    <div class="sub" style="opacity:.7">compare only at the same RPM</div>
  </div>
</div>

<div class="card">
  <div class="lbl">Imbalance &ndash; 1&times; locked to the magnet</div>
  <div class="imbal">
    <canvas id="compass" width="220" height="220"></canvas>
    <div class="imbaltext">
      <div class="big" id="bmag" style="font-size:40px">&ndash;<small>mg</small></div>
      <div class="sub" id="bphase" style="text-align:left">&ndash;</div>
      <div class="sub" id="bhint" style="text-align:left;opacity:.7;margin-top:8px">spin the fan at a steady speed</div>
    </div>
  </div>
  <div class="sub" style="opacity:.7;margin-top:10px">indicative direction &middot; calibrate with a test weight for the exact blade &amp; grams (Phase 5)</div>
</div>

<div class="card">
  <div class="lbl">Balancing wizard &middot; Phase 5</div>
  <div id="wizmsg" class="sub" style="margin:10px 0">&ndash;</div>
  <div class="bar" id="wizprog"><span id="wizfill"></span></div>
  <div id="wizidle" style="text-align:center">
    <button id="wizstart" class="wbtn">Start balancing</button>
  </div>
  <div id="wizawait" style="display:none">
    <div class="wrow">
      <label>Test weight <input id="wizg" type="number" step="0.1" min="0.1" value="1.0"> g</label>
      <label>on <select id="wizb"></select></label>
    </div>
    <div style="text-align:center;margin-top:12px"><button id="wizmeas" class="wbtn">Measure with weight</button></div>
  </div>
  <div id="wizresult" style="display:none">
    <div id="wizrestext"></div>
    <div style="text-align:center;margin-top:12px">
      <button id="wizagain" class="wbtn">Run again</button>
      <button id="wizdone" class="wbtn ghost">Done</button>
    </div>
  </div>
</div>

<div class="card">
  <div class="lbl">RPM &ndash; last 60 s</div>
  <canvas id="chart" width="600" height="120"></canvas>
</div>

<div class="card">
  <div class="lbl">Vibration RMS &ndash; broadband, g &ndash; last 60 s</div>
  <canvas id="vchart" width="600" height="120"></canvas>
</div>

<div class="card">
  <div class="grid">
    <div class="cell"><div class="v" id="period">&ndash;</div><div class="lbl">Period</div></div>
    <div class="cell"><div class="v" id="vibpeak">&ndash;</div><div class="lbl">Vib peak</div></div>
    <div class="cell"><div class="v" id="ax">&ndash;</div><div class="lbl">Ax g</div></div>
    <div class="cell"><div class="v" id="ay">&ndash;</div><div class="lbl">Ay g</div></div>
    <div class="cell"><div class="v" id="az">&ndash;</div><div class="lbl">Az g</div></div>
    <div class="cell"><div class="v" id="imuhz">&ndash;</div><div class="lbl">IMU Hz</div></div>
    <div class="cell"><div class="v" id="pulses">&ndash;</div><div class="lbl">Pulses</div></div>
    <div class="cell"><div class="v" id="missed">&ndash;</div><div class="lbl">Missed</div></div>
    <div class="cell"><div class="v" id="glitches">&ndash;</div><div class="lbl">Glitches</div></div>
    <div class="cell"><div class="v" id="heap">&ndash;</div><div class="lbl">Free heap</div></div>
    <div class="cell"><div class="v" id="battery">&ndash;</div><div class="lbl">Battery</div></div>
    <div class="cell"><div class="v" id="uptime">&ndash;</div><div class="lbl">Uptime</div></div>
  </div>
</div>

<div id="conn">connecting&hellip;</div>
<div style="text-align:center;margin-top:8px"><a href="/update" style="color:var(--dim);font-size:12px">firmware update</a></div>

<script>
'use strict';
const $ = id => document.getElementById(id);
const hist = [];           // ~60 s of rpm samples at 4 Hz
const vibHist = [];        // ~60 s of vibration RMS samples
const HIST_MAX = 240;
let vibScale = 0.02;       // slowly-adapting y-scale for the vib chart (stable before/after)
let balScale = 0.005;      // slowly-adapting radius scale for the imbalance compass
let ws = null;
let lastMsgAt = 0;

function connect() {
  ws = new WebSocket('ws://' + location.host + '/ws');
  ws.onopen = () => { $('conn').textContent = 'live'; };
  ws.onclose = () => {
    $('conn').textContent = 'reconnecting…';
    $('status').textContent = 'OFFLINE';
    $('status').style.background = 'var(--bad)';
    setTimeout(connect, 2000);
  };
  ws.onmessage = e => {
    lastMsgAt = Date.now();
    try { render(JSON.parse(e.data)); }
    catch (err) { console.error('render failed', err); }
  };
}

// The device pushes at a strict 4 Hz, so a gap means the link went half-open
// (phone walked out of range / WiFi power-save). Force a reconnect rather than
// keep showing a frozen RPM as if it were live.
setInterval(() => {
  if (ws && ws.readyState === WebSocket.OPEN && lastMsgAt &&
      Date.now() - lastMsgAt > 2000) {
    $('conn').textContent = 'stale — reconnecting…';
    ws.close();
  }
}, 1000);

function render(d) {
  if (typeof d.rpm !== 'number') return;  // ignore malformed / partial frames
  const noSig = d.status === 'NO SIGNAL';
  $('rpm').textContent = noSig ? '—' : d.rpm.toFixed(1);
  const st = $('status');
  st.textContent = d.status;
  st.style.background = d.status === 'OK' ? 'var(--ok)'
                      : d.status === 'UNSTABLE' ? 'var(--warn)' : 'var(--bad)';

  const hasImu = d.imu_ok !== false && typeof d.vib_rms === 'number';
  $('vib').innerHTML  = hasImu ? d.vib_rms.toFixed(3) + '<small>g</small>' : 'no IMU';
  $('vibsub').textContent = hasImu
    ? 'peak ' + d.vib_peak.toFixed(3) + ' g · ' + Math.round(d.imu_hz) + ' Hz'
    : '—';

  // Imbalance (1x lock-in) — vector magnitude + heavy-spot direction.
  const balOk = d.bal_ok === true;
  const blades = d.blades || 3;
  if (balOk) {
    $('bmag').innerHTML  = (d.bal_mag * 1000).toFixed(0) + '<small>mg</small>';
    $('bphase').textContent = '1× peak ~' + Math.round(d.bal_phase) +
      '° after magnet · nearest blade ' + (d.bal_blade + 1) + '/' + blades;
    $('bhint').textContent = 'lower magnitude = better · compare only at the same RPM';
    balScale = Math.max(Math.max(0.005, d.bal_mag), balScale * 0.99);
  } else {
    $('bmag').innerHTML  = '&ndash;<small>mg</small>';
    $('bphase').textContent = '—';
    $('bhint').textContent = 'spin the fan at a steady speed (needs OK RPM)';
  }

  $('period').textContent   = d.period_ms ? d.period_ms.toFixed(1) + ' ms' : '—';
  $('vibpeak').textContent  = hasImu ? d.vib_peak.toFixed(3) : '—';
  $('ax').textContent       = hasImu ? d.ax.toFixed(3) : '—';
  $('ay').textContent       = hasImu ? d.ay.toFixed(3) : '—';
  $('az').textContent       = hasImu ? d.az.toFixed(3) : '—';
  $('imuhz').textContent    = hasImu ? Math.round(d.imu_hz) : '—';
  $('pulses').textContent   = d.pulses;
  $('missed').textContent   = d.missed;
  $('glitches').textContent = d.glitches;
  $('heap').textContent     = (d.heap / 1024).toFixed(0) + ' kB';
  $('battery').textContent  = d.battery_pct >= 0 ? d.battery_pct + ' %' : '—';
  $('uptime').textContent   = Math.floor(d.uptime_s / 60) + ':' + String(d.uptime_s % 60).padStart(2, '0');
  $('fw').textContent       = 'fw ' + d.fw;

  hist.push(noSig ? 0 : d.rpm);
  if (hist.length > HIST_MAX) hist.shift();
  vibHist.push(hasImu ? d.vib_rms : 0);
  if (vibHist.length > HIST_MAX) vibHist.shift();

  // Ratchet the vib scale up to recent peaks and let it decay slowly, so a real
  // drop in vibration (e.g. after adding a weight) visibly lowers the trace
  // instead of the chart silently rescaling to hide it.
  vibScale = Math.max(Math.max(0.02, ...vibHist), vibScale * 0.99);

  drawSeries($('chart'), hist, '#4da3ff', 20);
  drawSeries($('vchart'), vibHist, '#f0a24d', vibScale);
  drawCompass($('compass'), d.bal_phase || 0, d.bal_mag || 0, balScale, blades,
              d.bal_blade || 0, balOk, d.bs_state === 'result' ? d.bs_corr_deg : null);
  renderWizard(d);
}

// Drive the balancing wizard card from the telemetry session state.
let bladesInit = 0;
function renderWizard(d) {
  const st = d.bs_state || 'idle';
  const blades = d.blades || 3;
  const sel = $('wizb');
  if (sel && bladesInit !== blades) {
    bladesInit = blades; sel.innerHTML = '';
    for (let i = 1; i <= blades; i++) {
      const o = document.createElement('option');
      o.value = i; o.textContent = 'blade ' + i + (i === 1 ? ' (magnet)' : '');
      sel.appendChild(o);
    }
  }
  $('wizmsg').textContent = d.bs_msg || '';
  const capturing = st === 'baseline' || st === 'trial';
  $('wizprog').style.display = capturing ? 'block' : 'none';
  if (capturing) $('wizfill').style.width = (d.bs_prog || 0) + '%';
  $('wizidle').style.display   = (st === 'idle' || st === 'error') ? 'block' : 'none';
  $('wizawait').style.display  = st === 'await' ? 'block' : 'none';
  $('wizresult').style.display = st === 'result' ? 'block' : 'none';
  $('wizstart').textContent = st === 'error' ? 'Restart' : 'Start balancing';
  if (st === 'result' && d.bs_res) {
    const g = x => (x || 0).toFixed(1);
    $('wizrestext').innerHTML =
      '<div class="sub" style="text-align:left">Heavy spot: <b>' + (d.bs_heavy_g * 1000).toFixed(0) +
        ' mg</b> at ~' + Math.round(d.bs_heavy_deg) + '° from magnet</div>' +
      '<div style="margin-top:8px"><b>Remove the test weight,</b> then add (best):<br>' + g(d.bs_ga) +
        ' g on blade ' + d.bs_ba + ' + ' + g(d.bs_gb) + ' g on blade ' + d.bs_bb + '</div>' +
      '<div class="sub" style="text-align:left;margin-top:4px">or simply ' + g(d.bs_gs) +
        ' g on blade ' + d.bs_bs + ' (~' + Math.round(d.bs_improve) + '% better)</div>' +
      '<div class="sub" style="opacity:.7;margin-top:6px">at the same radius as the test weight · green mark on the dial = where to add</div>';
  }
}

function balPost(cmd, params) {
  fetch('/api/balance?cmd=' + cmd + (params || ''), { method: 'POST' }).catch(() => {});
}
$('wizstart').onclick = () => balPost('start');
$('wizagain').onclick = () => balPost('start');
$('wizdone').onclick  = () => balPost('reset');
$('wizmeas').onclick  = () => balPost('trial',
  '&grams=' + encodeURIComponent($('wizg').value) + '&blade=' + $('wizb').value);

// Draw one time series with faint gridlines, autoscaled to its own range.
// floorMax keeps a flat/quiet signal from filling the whole height.
function drawSeries(c, data, color, floorMax) {
  const x = c.getContext('2d'), w = c.width, h = c.height;
  x.clearRect(0, 0, w, h);
  if (!data.length) return;
  const max = Math.max(floorMax, ...data) * 1.15;

  x.strokeStyle = 'rgba(138,147,165,.25)';
  x.lineWidth = 1;
  x.beginPath();
  for (let g = 1; g <= 3; g++) {
    const gy = h - h * g / 4;
    x.moveTo(0, gy); x.lineTo(w, gy);
  }
  x.stroke();

  x.strokeStyle = color;  // canvas cannot resolve CSS variables
  x.lineWidth = 2;
  x.beginPath();
  data.forEach((v, i) => {
    const px = i / (HIST_MAX - 1) * w;
    const py = h - (v / max) * h;
    i === 0 ? x.moveTo(px, py) : x.lineTo(px, py);
  });
  x.stroke();
}

// Polar view of the imbalance vector: 0° = magnet at top, angle increases with
// rotation (clockwise), blades marked around the rim, arrow = 1x heavy-spot
// direction with length proportional to magnitude.
function drawCompass(c, phaseDeg, mag, scale, blades, bladeIdx, ok, corrDeg) {
  const x = c.getContext('2d'), w = c.width, h = c.height;
  x.clearRect(0, 0, w, h);
  const cx = w / 2, cy = h / 2, R = Math.min(w, h) / 2 - 24;
  const pt = (deg, r) => [cx + Math.sin(deg * Math.PI / 180) * r,
                          cy - Math.cos(deg * Math.PI / 180) * r];

  x.strokeStyle = 'rgba(138,147,165,.35)'; x.lineWidth = 1.5;
  x.beginPath(); x.arc(cx, cy, R, 0, 2 * Math.PI); x.stroke();
  x.strokeStyle = 'rgba(138,147,165,.15)';
  [0.66, 0.33].forEach(f => { x.beginPath(); x.arc(cx, cy, R * f, 0, 2 * Math.PI); x.stroke(); });

  x.font = '12px system-ui,sans-serif'; x.textAlign = 'center'; x.textBaseline = 'middle';
  for (let i = 0; i < blades; i++) {
    const deg = i * 360 / blades;
    const [bx, by] = pt(deg, R);
    const [lx, ly] = pt(deg, R + 14);
    const on = ok && i === bladeIdx;
    x.fillStyle = on ? '#4da3ff' : 'rgba(138,147,165,.7)';
    x.beginPath(); x.arc(bx, by, on ? 6 : 4, 0, 2 * Math.PI); x.fill();
    x.fillText(i === 0 ? 'magnet' : 'B' + (i + 1), lx, ly);
  }

  if (ok) {
    const rr = R * Math.max(0.05, Math.min(1, mag / Math.max(scale, 1e-6)));
    const [ax, ay] = pt(phaseDeg, rr);
    x.strokeStyle = '#f0a24d'; x.lineWidth = 3; x.lineCap = 'round';
    x.beginPath(); x.moveTo(cx, cy); x.lineTo(ax, ay); x.stroke();
    x.fillStyle = '#f0a24d';
    x.beginPath(); x.arc(ax, ay, 4.5, 0, 2 * Math.PI); x.fill();
  }
  // Correction direction from the wizard: where to add weight (green, dashed).
  if (corrDeg != null) {
    const [gx, gy] = pt(corrDeg, R * 0.82);
    x.strokeStyle = '#27ae60'; x.lineWidth = 3; x.setLineDash([5, 4]);
    x.beginPath(); x.moveTo(cx, cy); x.lineTo(gx, gy); x.stroke(); x.setLineDash([]);
    x.fillStyle = '#27ae60';
    x.beginPath(); x.arc(gx, gy, 5, 0, 2 * Math.PI); x.fill();
  }
  x.fillStyle = 'rgba(138,147,165,.9)';
  x.beginPath(); x.arc(cx, cy, 3, 0, 2 * Math.PI); x.fill();
}

connect();
</script>
</body>
</html>
)idx";

/**
 * OTA firmware update page, served at "/update".
 *
 * Lets the user flash a new firmware.bin over WiFi (join the AP, pick the
 * .bin, upload) without unplugging the device. Uploads via XHR so it can show
 * a progress percentage; the device reboots into the new image on success.
 */
static const char UPDATE_HTML[] PROGMEM = R"ota(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>FanBalancer &ndash; Update</title>
<style>
body{background:#0f1115;color:#e8eaf0;font-family:system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;padding:24px;max-width:520px;margin:0 auto}
h1{font-size:20px;margin-bottom:16px}
.card{background:#181c23;border-radius:12px;padding:20px}
input[type=file]{width:100%;margin-bottom:14px;color:#8a93a5}
button{background:#4da3ff;color:#08101c;border:0;border-radius:8px;padding:10px 18px;font-size:15px;font-weight:600;cursor:pointer}
button:disabled{opacity:.5;cursor:default}
.bar{height:8px;background:#2a2f38;border-radius:999px;margin-top:16px;overflow:hidden;display:none}
.bar>span{display:block;height:100%;width:0;background:#4da3ff;transition:width .2s}
#msg{margin-top:12px;font-size:14px;color:#8a93a5;min-height:20px}
a{color:#4da3ff}
</style>
</head>
<body>
<h1>Firmware update</h1>
<div class="card">
  <form id="f">
    <input type="file" id="file" name="firmware" accept=".bin" required>
    <button id="go" type="submit">Upload &amp; reboot</button>
    <div class="bar"><span id="pfill"></span></div>
    <div id="msg">Select firmware.bin from .pio/build/&hellip;</div>
  </form>
  <p style="margin-top:16px"><a href="/">&larr; back to dashboard</a></p>
</div>
<script>
'use strict';
const f = document.getElementById('f');
f.onsubmit = e => {
  e.preventDefault();
  const file = document.getElementById('file').files[0];
  if (!file) return;
  const bar = document.querySelector('.bar'), fill = document.getElementById('pfill'),
        msg = document.getElementById('msg'), go = document.getElementById('go');
  const fd = new FormData();
  fd.append('firmware', file);
  const x = new XMLHttpRequest();
  x.open('POST', '/update');
  bar.style.display = 'block'; go.disabled = true;
  x.upload.onprogress = ev => {
    if (!ev.lengthComputable) return;
    const p = Math.round(ev.loaded / ev.total * 100);
    fill.style.width = p + '%'; msg.textContent = 'Uploading… ' + p + '%';
  };
  x.onload = () => {
    if (x.status === 200) { msg.textContent = 'Done — rebooting. Reconnect the WiFi and reopen in ~10 s.'; }
    else { msg.textContent = 'Failed: ' + x.responseText; go.disabled = false; }
  };
  x.onerror = () => { msg.textContent = 'Upload error (connection lost).'; go.disabled = false; };
  x.send(fd);
};
</script>
</body>
</html>
)ota";
