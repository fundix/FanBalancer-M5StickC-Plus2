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
    <div class="lbl">Vibration</div>
    <div class="big" id="vib">&ndash;<small>g</small></div>
    <div class="sub" id="vibsub">&ndash;</div>
  </div>
</div>

<div class="card">
  <div class="lbl">RPM &ndash; last 60 s</div>
  <canvas id="chart" width="600" height="120"></canvas>
</div>

<div class="card">
  <div class="lbl">Vibration (RMS, g) &ndash; last 60 s</div>
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

<script>
'use strict';
const $ = id => document.getElementById(id);
const hist = [];           // ~60 s of rpm samples at 4 Hz
const vibHist = [];        // ~60 s of vibration RMS samples
const HIST_MAX = 240;
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

  drawSeries($('chart'), hist, '#4da3ff', 20);
  drawSeries($('vchart'), vibHist, '#f0a24d', 0.02);
}

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

connect();
</script>
</body>
</html>
)idx";
