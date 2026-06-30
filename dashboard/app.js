// ===== Configuration =====
const BROKER_URL = 'ws://72.62.126.85:9001';
const DATA_TOPIC = 'kutai/fleet/data';
const DEFAULT_DT_ID = 'DT01';

// API Base URL (Relative path since Nginx proxies /api/ to our backend server)
const API_BASE = window.location.origin.includes('localhost') || window.location.origin.includes('127.0.0.1')
  ? 'http://72.62.126.85/api' // fallback during local development/preview
  : '/api';

// ===== State =====
let countDT = 0;
let countEXCA = 0;
let countACK = 0;
const fleetMarkers = {};
const fleetData = {};
let historyPolyline = null;
let historyMarkersGroup = L.featureGroup();

// Playback State
let playbackInterval = null;
let playbackIndex = 0;
let playbackActive = false;
let playbackMarker = null;
let historicalDataList = [];

// ===== DOM Elements =====
const connStatusEl = document.getElementById('conn-status');
const countDtEl = document.getElementById('count-dt');
const countExcaEl = document.getElementById('count-exca');
const countAckEl = document.getElementById('count-ack');
const autoAckToggle = document.getElementById('auto-ack-toggle');
const logStreamEl = document.getElementById('log-stream');
const telemetryRowsEl = document.getElementById('telemetry-rows');
const clearLogBtn = document.getElementById('clear-log-btn');
const clockEl = document.getElementById('live-clock');

// History Elements
const historyDeviceSelect = document.getElementById('history-device-select');
const historyRangeSelect = document.getElementById('history-range-select');
const loadHistoryBtn = document.getElementById('load-history-btn');
const clearHistoryBtn = document.getElementById('clear-history-btn');
const wipeDeviceDbBtn = document.getElementById('wipe-device-db-btn');
const historyStatsEl = document.getElementById('history-stats');
const histPtsVal = document.getElementById('hist-pts-val');
const histSpdVal = document.getElementById('hist-spd-val');
const liveBadge = document.getElementById('live-badge');

const playbackControls = document.getElementById('history-playback-wrap');
const playHistoryBtn = document.getElementById('play-history-btn');

// ===== Live Clock =====
function updateClock() {
  if (clockEl) {
    const now = new Date();
    clockEl.textContent = now.toLocaleTimeString('id-ID', {
      hour: '2-digit',
      minute: '2-digit',
      second: '2-digit',
      hour12: false
    });
  }
}
setInterval(updateClock, 1000);
updateClock();

// ===== Map Initialization =====
const map = L.map('map', {
  zoomControl: true,
  attributionControl: false,
}).setView([-0.95, 117.0], 12);

// Colorful standard tiles for light theme
L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
  attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors',
  maxZoom: 19
}).addTo(map);

// Add history group to map
historyMarkersGroup.addTo(map);

// Clear track history when clicking on empty map space
map.on('click', () => {
  clearHistoryTrack();
  if (historyDeviceSelect) {
    historyDeviceSelect.value = "";
  }
});

// Custom Marker Icons
const dtIcon = L.divIcon({
  className: 'map-marker-dt',
  html: '<div class="marker-dot blue"><i class="fa-solid fa-truck"></i></div>',
  iconSize: [36, 36],
  iconAnchor: [18, 18]
});

const excaIcon = L.divIcon({
  className: 'map-marker-exca',
  html: '<div class="marker-dot amber"><i class="fa-solid fa-helmet-safety"></i></div>',
  iconSize: [36, 36],
  iconAnchor: [18, 18]
});

// Inject marker styles for light theme
const markerStyle = document.createElement('style');
markerStyle.textContent = `
  .marker-dot {
    width: 36px;
    height: 36px;
    border-radius: 50%;
    display: flex;
    align-items: center;
    justify-content: center;
    border: 3px solid #ffffff;
    font-size: 13px;
    color: #fff;
    box-shadow: 0 4px 6px rgba(0,0,0,0.15);
  }
  .marker-dot.blue {
    background: #0284c7;
    box-shadow: 0 0 12px rgba(2, 132, 199, 0.4);
  }
  .marker-dot.amber {
    background: #d97706;
    box-shadow: 0 0 12px rgba(217, 119, 6, 0.4);
  }
`;
document.head.appendChild(markerStyle);

// ===== API / Database Initial Load =====
async function initializeData() {
  try {
    // 1. Fetch latest active devices state
    const resDevices = await fetch(`${API_BASE}/devices`);
    if (resDevices.ok) {
      const devices = await resDevices.json();
      
      // Clear dropdown
      historyDeviceSelect.innerHTML = '<option value="">-- Choose Unit --</option>';
      
      devices.forEach(device => {
        // Populate local state
        const src = device.src;
        const isDT = src.toUpperCase().startsWith('DT');
        
        // Populate dropdown options
        const opt = document.createElement('option');
        opt.value = src;
        opt.textContent = src;
        historyDeviceSelect.appendChild(opt);
        
        // Store in fleetData
        fleetData[src] = {
          timestamp: device.ts,
          ignition: device.ign,
          speed: device.spd,
          battery: device.bat,
          msgId: device.id
        };

        // Render markers
        if (device.lat && device.lon) {
          updateMapMarker(src, isDT, device.lat, device.lon, device.spd, device.ts);
        }
      });
      
      updateTelemetryTableFromState();
    }

    // 2. Fetch recent ingest messages for logging
    const resRecent = await fetch(`${API_BASE}/recent?limit=25`);
    if (resRecent.ok) {
      const recentLogs = await resRecent.json();
      // Reverse logs to show older first as we append to bottom
      recentLogs.reverse().forEach(log => {
        const isDT = log.src.toUpperCase().startsWith('DT');
        addLogEntry(log.src, isDT ? 'dt' : 'exca', JSON.stringify(log.raw_payload));
      });
    }

  } catch (err) {
    console.error('Failed to pre-populate dashboard statistics from API:', err);
    addLogSystem(`Gagal memuat history data awal: ${err.message}`, 'warn');
  }
}

// Trigger initial API load
initializeData();

// ===== MQTT Connection =====
console.log(`Connecting to MQTT Broker: ${BROKER_URL}`);
const client = mqtt.connect(BROKER_URL, {
  keepalive: 60,
  reconnectPeriod: 2000,
});

client.on('connect', () => {
  console.log('MQTT Connected');
  updateStatus(true);
  client.subscribe(DATA_TOPIC, (err) => {
    if (!err) {
      addLogSystem(`Connected & subscribed to: <code>${DATA_TOPIC}</code>`);
    } else {
      addLogSystem(`Subscription error: ${err.message}`, 'error');
    }
  });
});

client.on('close', () => {
  console.warn('MQTT Connection closed');
  updateStatus(false);
});

client.on('error', (err) => {
  console.error('MQTT Error:', err);
  addLogSystem(`Connection error: ${err.message}`, 'error');
  updateStatus(false);
});

const partialBuffers = {};

function tryMergeChunks(s1, s2) {
  // 1. Try overlap matching
  const maxLen = Math.min(s1.length, s2.length);
  for (let k = maxLen; k > 0; k--) {
    const prefix = s2.substring(0, k);
    if (s1.endsWith(prefix)) {
      const merged = s1 + s2.substring(k);
      try {
        JSON.parse(merged);
        return merged;
      } catch (e) {}
    }
  }

  // 2. Try candidate bridges (direct concatenation and common gap fillers)
  const candidates = ["", "t\":\"", "\":\"", "st\":\"", "\":", ",", "\":{", "\":[\"", "t\":"];
  for (const bridge of candidates) {
    const merged = s1 + bridge + s2;
    try {
      JSON.parse(merged);
      return merged;
    } catch (e) {}
  }
  return null;
}

client.on('message', (topic, message) => {
  const rawPayload = message.toString().replace(/\r/g, '').replace(/\n/g, '').trim();
  try {
    const data = JSON.parse(rawPayload);
    const src = data.src || data.source;
    if (src) {
      delete partialBuffers[src];
    }
    handleIncomingData(data, rawPayload);
  } catch (e) {
    let recovered = false;
    if (rawPayload.startsWith('{')) {
      const match = rawPayload.match(/"src"\s*:\s*"([^"]+)"/);
      const srcKey = match ? match[1] : 'unknown';
      
      // If we already had a pending partial buffer for this device, it means it was orphaned
      const orphaned = partialBuffers[srcKey];
      if (orphaned) {
        addLogSystem(`Non-JSON message on <code>${topic}</code> (Orphaned): ${orphaned}`, 'warn');
      }
      
      partialBuffers[srcKey] = rawPayload;
      recovered = true; // Avoid warning for this chunk yet
    } else {
      for (const srcKey in partialBuffers) {
        const pending = partialBuffers[srcKey];
        const merged = tryMergeChunks(pending, rawPayload);
        if (merged) {
          try {
            const data = JSON.parse(merged);
            delete partialBuffers[srcKey];
            handleIncomingData(data, merged);
            recovered = true;
            break;
          } catch (err) {
            // Still not valid JSON
          }
        }
      }
    }

    if (!recovered) {
      addLogSystem(`Non-JSON message on <code>${topic}</code>: ${rawPayload}`, 'warn');
    }
  }
});

// ===== Status Update =====
function updateStatus(isConnected) {
  const dot = connStatusEl.querySelector('.conn-dot');
  const text = connStatusEl.querySelector('.status-text');

  if (isConnected) {
    dot.className = 'conn-dot online';
    text.textContent = 'Broker Connected';
  } else {
    dot.className = 'conn-dot offline';
    text.textContent = 'Broker Offline';
  }
}

// ===== Data Handler =====
function handleIncomingData(data, rawJson) {
  const id = data.id || data.msg_id;
  let src = data.src || data.source || 'UNKNOWN';
  
  if (src === 'UNKNOWN' && id) {
    const parts = id.split('-');
    if (parts.length > 0 && parts[0].trim() !== '') {
      src = parts[0];
    }
  }

  const timestamp = data.ts || data.timestamp;
  const latitude = parseFloat(data.lat || data.latitude);
  const longitude = parseFloat(data.lon || data.longitude);

  // Validation guard: reject messages with missing/zero coordinates or invalid timestamp format
  if (isNaN(latitude) || isNaN(longitude) || latitude === 0 || longitude === 0 || !timestamp || typeof timestamp !== 'string' || !timestamp.includes('T')) {
    console.warn(`Ignoring invalid/incomplete telemetry data for ${src}`);
    return;
  }

  const speed = parseFloat(data.spd || data.speed || 0);
  const battery = parseFloat(data.bat || data.external || 0);
  const ignition = data.ign !== undefined ? data.ign : (data.ignition !== undefined ? data.ignition : -1);

  const isDT = src.toUpperCase().startsWith('DT');

  // Update counters
  if (isDT) {
    countDT++;
    countDtEl.textContent = countDT;
    animateStat('stat-dt');
  } else {
    countEXCA++;
    countExcaEl.textContent = countEXCA;
    animateStat('stat-exca');
  }

  // Add option to dropdown if it doesn't exist
  if (![...historyDeviceSelect.options].some(opt => opt.value === src)) {
    const opt = document.createElement('option');
    opt.value = src;
    opt.textContent = src;
    historyDeviceSelect.appendChild(opt);
  }

  // Log entry
  addLogEntry(src, isDT ? 'dt' : 'exca', rawJson);

  // Map marker (only update if not currently focused on history route)
  if (!isNaN(latitude) && !isNaN(longitude)) {
    updateMapMarker(src, isDT, latitude, longitude, speed, timestamp);
  }

  // Telemetry state update & render
  fleetData[src] = { timestamp, ignition, speed, battery, msgId: id };
  updateTelemetryTableFromState();

  // Auto-ACK
  if (autoAckToggle.checked && id) {
    sendAutoACK(id, src);
  }
}

// ===== Stat Animation =====
function animateStat(elementId) {
  const el = document.getElementById(elementId);
  if (!el) return;
  el.style.transform = 'scale(1.02)';
  setTimeout(() => {
    el.style.transform = '';
  }, 150);
}

// ===== Auto-ACK =====
function sendAutoACK(msgId, src) {
  const targetTopics = [];
  if (src.toUpperCase().startsWith('DT')) {
    targetTopics.push(`kutai/fleet/ack/${src}`);
  } else {
    targetTopics.push(`kutai/fleet/ack/${DEFAULT_DT_ID}`);
    targetTopics.push(`kutai/fleet/ack/${src}`);
  }

  const ackPayload = JSON.stringify({ id: msgId, status: 'ok' });

  targetTopics.forEach(topic => {
    client.publish(topic, ackPayload, { qos: 0 }, (err) => {
      if (!err) {
        countACK++;
        countAckEl.textContent = countACK;
        animateStat('stat-ack');
        addLogEntry('ACK', 'ack', `→ ${topic}: ${ackPayload}`);
      } else {
        addLogSystem(`Failed ACK to ${topic}: ${err.message}`, 'error');
      }
    });
  });
}

// ===== Map Marker =====
function updateMapMarker(src, isDT, lat, lon, speed, timestamp) {
  if (fleetMarkers[src]) {
    fleetMarkers[src].setLatLng([lat, lon]);
    fleetMarkers[src].getPopup().setContent(popupHTML(src, lat, lon, speed, timestamp));
  } else {
    const marker = L.marker([lat, lon], { icon: isDT ? dtIcon : excaIcon }).addTo(map);
    marker.bindPopup(popupHTML(src, lat, lon, speed, timestamp));
    
    // Auto-load history route and play controls on click
    marker.on('click', (e) => {
      if (e && e.originalEvent) {
        e.originalEvent.stopPropagation();
      }
      L.DomEvent.stopPropagation(e);
      
      if (historyDeviceSelect) {
        historyDeviceSelect.value = src;
        if (loadHistoryBtn) {
          loadHistoryBtn.click();
        }
      }
    });
    
    fleetMarkers[src] = marker;
  }
}

function popupHTML(src, lat, lon, speed, ts) {
  return `
    <div style="font-family:'Outfit',sans-serif;color:#0f172a;font-size:12px;line-height:1.6;min-width:140px">
      <strong style="font-size:13px;color:#0284c7;display:block;margin-bottom:4px">${src}</strong>
      <b>Lat:</b> ${lat.toFixed(6)}<br>
      <b>Lon:</b> ${lon.toFixed(6)}<br>
      <b>Speed:</b> ${speed.toFixed(1)} km/h<br>
      <span style="color:#64748b;font-size:10px;display:block;margin-top:6px">${ts}</span>
    </div>`;
}

// ===== Telemetry Table updates =====
function updateTelemetryTableFromState() {
  const keys = Object.keys(fleetData).sort();

  if (keys.length === 0) {
    telemetryRowsEl.innerHTML = '<tr class="empty-row"><td colspan="6">Menunggu data…</td></tr>';
    return;
  }

  let html = '';
  keys.forEach(key => {
    const r = fleetData[key];
    const isDT = key.toUpperCase().startsWith('DT');
    const badge = isDT ? 'badge-dt' : 'badge-exca';

    let ignHtml = '<span class="ign-state ign-off"><i class="fa-solid fa-circle-xmark"></i> Off</span>';
    if (r.ignition === 1) {
      ignHtml = '<span class="ign-state ign-on"><i class="fa-solid fa-circle"></i> On</span>';
    } else if (r.ignition === 0) {
      ignHtml = '<span class="ign-state ign-off"><i class="fa-solid fa-circle-half-stroke"></i> Idle</span>';
    }

    let time = r.timestamp || '—';
    if (typeof time === 'string' && time.includes('T')) {
      const parts = time.split('T');
      const datePart = parts[0].split('-').reverse().join('/');
      const timePart = parts[1].substring(0, 8);
      time = `${datePart} ${timePart}`;
    }
    const shortId = r.msgId ? r.msgId.split('-').pop() : '—';
    const displaySpeed = (typeof r.speed === 'number' && !isNaN(r.speed)) ? r.speed.toFixed(1) : '0.0';
    const displayBattery = (typeof r.battery === 'number' && !isNaN(r.battery)) ? r.battery.toFixed(1) : '0.0';

    html += `<tr data-device="${key}" style="cursor: pointer;">
      <td><span class="badge ${badge}">${key}</span></td>
      <td style="font-family:var(--mono); font-size:0.8rem;">${time}</td>
      <td>${ignHtml}</td>
      <td><strong>${displaySpeed}</strong> <span style="color:var(--text-secondary)">km/h</span></td>
      <td>${displayBattery} <span style="color:var(--text-secondary)">V</span></td>
      <td style="font-family:var(--mono);color:var(--text-muted);font-size:0.7rem">${shortId}</td>
    </tr>`;
  });

  telemetryRowsEl.innerHTML = html;

  // Add click listeners to rows to center map on the device
  telemetryRowsEl.querySelectorAll('tr[data-device]').forEach(row => {
    row.addEventListener('click', () => {
      const devId = row.getAttribute('data-device');
      const marker = fleetMarkers[devId];
      if (marker) {
        map.panTo(marker.getLatLng());
        marker.openPopup();
      }
    });
  });
}

// ===== Log Utilities =====
function addLogEntry(src, type, message) {
  clearEmptyLog();
  const entry = document.createElement('div');
  entry.className = `log-entry ${type}`;
  const time = new Date().toLocaleTimeString('id-ID', { hour: '2-digit', minute: '2-digit', second: '2-digit', hour12: false });
  const label = type === 'ack' ? 'ACK' : src;
  entry.innerHTML = `
    <div class="log-meta"><span>${label}</span><span>${time}</span></div>
    <div class="log-body">${message}</div>`;
  logStreamEl.appendChild(entry);
  logStreamEl.scrollTop = logStreamEl.scrollHeight;
  while (logStreamEl.children.length > 50) logStreamEl.removeChild(logStreamEl.firstChild);
}

function addLogSystem(message, level = 'info') {
  clearEmptyLog();
  const entry = document.createElement('div');
  entry.className = 'log-entry';
  entry.style.borderLeftColor = level === 'error' ? 'var(--red)' : 'var(--text-muted)';
  const time = new Date().toLocaleTimeString('id-ID', { hour: '2-digit', minute: '2-digit', second: '2-digit', hour12: false });
  entry.innerHTML = `
    <div class="log-meta"><span>${level.toUpperCase()}</span><span>${time}</span></div>
    <div class="log-body" style="color:${level === 'error' ? 'var(--red)' : 'var(--text-secondary)'}">${message}</div>`;
  logStreamEl.appendChild(entry);
  logStreamEl.scrollTop = logStreamEl.scrollHeight;
}

function clearEmptyLog() {
  const empty = logStreamEl.querySelector('.log-empty');
  if (empty) empty.remove();
}

clearLogBtn.addEventListener('click', () => {
  logStreamEl.innerHTML = '<div class="log-empty">Waiting for data on <code>kutai/fleet/data</code>…</div>';
});

// ===== History Path Drawing =====
loadHistoryBtn.addEventListener('click', async () => {
  const src = historyDeviceSelect.value;
  const days = historyRangeSelect.value;
  
  if (!src) {
    alert('Silakan pilih unit terlebih dahulu.');
    return;
  }

  try {
    loadHistoryBtn.disabled = true;
    loadHistoryBtn.innerHTML = '<i class="fa-solid fa-spinner fa-spin"></i> Loading...';

    const res = await fetch(`${API_BASE}/history?src=${src}&days=${days}`);
    if (!res.ok) throw new Error('API Request error');
    const data = await res.json();

    if (data.length === 0) {
      alert(`Tidak ada data koordinat historis untuk ${src} dalam ${days} hari terakhir.`);
      clearHistoryTrack();
      return;
    }

    // Process coordinates
    const coords = data.map(item => [item.lat, item.lon]);
    
    // Clear previous history
    clearHistoryTrack();

    // Draw Polyline
    historyPolyline = L.polyline(coords, {
      color: src.toUpperCase().startsWith('DT') ? '#0284c7' : '#d97706',
      weight: 4,
      opacity: 0.8,
      dashArray: '5, 10'
    }).addTo(map);

    // Zoom map to fit track
    map.fitBounds(historyPolyline.getBounds(), { padding: [30, 30] });

    // Show start and end markers
    const startPoint = coords[0];
    const endPoint = coords[coords.length - 1];

    L.marker(startPoint, {
      icon: L.divIcon({
        className: 'hist-pin-icon',
        html: '<div style="background:#16a34a;color:#fff;border-radius:50%;width:20px;height:20px;display:grid;place-items:center;font-size:10px;font-weight:bold;border:2px solid #fff">S</div>',
        iconSize: [20, 20]
      })
    }).bindPopup(`<b>Start Location</b><br>${data[0].ts}`).addTo(historyMarkersGroup);

    L.marker(endPoint, {
      icon: L.divIcon({
        className: 'hist-pin-icon',
        html: '<div style="background:#dc2626;color:#fff;border-radius:50%;width:20px;height:20px;display:grid;place-items:center;font-size:10px;font-weight:bold;border:2px solid #fff">E</div>',
        iconSize: [20, 20]
      })
    }).bindPopup(`<b>End Location</b><br>${data[data.length - 1].ts}`).addTo(historyMarkersGroup);

    // Calculate Average Speed
    let sumSpd = 0;
    data.forEach(item => sumSpd += item.spd);
    const avgSpd = sumSpd / data.length;

    // Display Stats
    histPtsVal.textContent = data.length;
    histSpdVal.textContent = `${avgSpd.toFixed(1)} km/h`;
    historyStatsEl.style.display = 'flex';

    // Update map panel badge
    liveBadge.textContent = `Track: ${src}`;
    liveBadge.style.background = 'var(--blue-dim)';
    liveBadge.style.color = 'var(--blue)';

    addLogSystem(`Loaded historical track for ${src} (${data.length} points)`, 'info');

    // Enable Replay Simulation
    historicalDataList = data;
    playbackControls.style.display = 'block';
    resetPlayback();

  } catch (err) {
    console.error('Failed to load history:', err);
    alert('Terjadi kesalahan saat memuat data historis.');
  } finally {
    loadHistoryBtn.disabled = false;
    loadHistoryBtn.innerHTML = '<i class="fa-solid fa-map-pin"></i> Load Track';
  }
});

clearHistoryBtn.addEventListener('click', () => {
  clearHistoryTrack();
  addLogSystem('Historical track cleared', 'info');
});

if (wipeDeviceDbBtn) {
  wipeDeviceDbBtn.addEventListener('click', async () => {
    const src = historyDeviceSelect.value;
    if (!src) {
      alert('Silakan pilih unit terlebih dahulu.');
      return;
    }

    const confirmed = confirm(`Apakah Anda yakin ingin menghapus semua data telemetry untuk unit "${src}" dari database? Tindakan ini tidak dapat dibatalkan.`);
    if (!confirmed) return;

    try {
      wipeDeviceDbBtn.disabled = true;
      wipeDeviceDbBtn.innerHTML = '<i class="fa-solid fa-spinner fa-spin"></i> Wiping...';

      const res = await fetch(`${API_BASE}/clear`, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json'
        },
        body: JSON.stringify({ src })
      });

      if (!res.ok) throw new Error('API Request error');
      const result = await res.json();

      if (result.success) {
        alert(`Berhasil menghapus ${result.deleted_rows} baris data telemetry untuk unit "${src}" dari database.`);
        addLogSystem(`Database telemetry wiped for unit ${src} (${result.deleted_rows} rows)`, 'info');
        if (historyDeviceSelect.value === src) {
          clearHistoryTrack();
        }
        initializeData();
      } else {
        alert(`Gagal menghapus data: ${result.error || 'Unknown error'}`);
      }
    } catch (err) {
      console.error('Failed to wipe device data:', err);
      alert(`Terjadi kesalahan: ${err.message}`);
    } finally {
      wipeDeviceDbBtn.disabled = false;
      wipeDeviceDbBtn.innerHTML = '<i class="fa-solid fa-trash-can"></i> Wipe DB Data';
    }
  });
}

function clearHistoryTrack() {
  if (historyPolyline) {
    map.removeLayer(historyPolyline);
    historyPolyline = null;
  }
  historyMarkersGroup.clearLayers();
  historyStatsEl.style.display = 'none';
  
  // Restore live monitor badge
  liveBadge.textContent = 'Live Monitor';
  liveBadge.style.background = 'var(--green-dim)';
  liveBadge.style.color = 'var(--green)';

  // Reset Playback
  resetPlayback();
  playbackControls.style.display = 'none';

  // Pan back to latest coordinates of any device
  const keys = Object.keys(fleetMarkers);
  if (keys.length > 0) {
    const lastKey = keys[keys.length - 1];
    map.panTo(fleetMarkers[lastKey].getLatLng());
  }
}

function resetPlayback() {
  if (playbackInterval) {
    clearInterval(playbackInterval);
    playbackInterval = null;
  }
  playbackIndex = 0;
  playbackActive = false;
  if (playbackMarker) {
    map.removeLayer(playbackMarker);
    playbackMarker = null;
  }
  playHistoryBtn.innerHTML = '<i class="fa-solid fa-play"></i> Play Replay';
  playHistoryBtn.className = 'btn btn-outline';
  playHistoryBtn.style.borderColor = 'var(--blue)';
  playHistoryBtn.style.color = 'var(--blue)';
}

playHistoryBtn.addEventListener('click', () => {
  if (historicalDataList.length === 0) return;

  const src = historyDeviceSelect.value;
  const isDT = src.toUpperCase().startsWith('DT');

  if (playbackActive) {
    playbackActive = false;
    clearInterval(playbackInterval);
    playHistoryBtn.innerHTML = '<i class="fa-solid fa-play"></i> Play Replay';
  } else {
    playbackActive = true;
    playHistoryBtn.innerHTML = '<i class="fa-solid fa-pause"></i> Pause Replay';

    const replayIcon = L.divIcon({
      className: 'map-marker-replay',
      html: `<div class="marker-dot" style="background:#8b5cf6;box-shadow:0 0 14px #8b5cf6;border:3px solid #fff;"><i class="fa-solid fa-play"></i></div>`,
      iconSize: [36, 36],
      iconAnchor: [18, 18]
    });

    playbackInterval = setInterval(() => {
      if (playbackIndex >= historicalDataList.length) {
        resetPlayback();
        addLogSystem(`Replay for ${src} completed.`, 'info');
        return;
      }

      const item = historicalDataList[playbackIndex];
      
      if (!playbackMarker) {
        playbackMarker = L.marker([item.lat, item.lon], { icon: replayIcon }).addTo(map);
      } else {
        playbackMarker.setLatLng([item.lat, item.lon]);
      }

      const displayTime = item.ts.includes('T') ? item.ts.split('T')[1].substring(0, 8) : item.ts;

      playbackMarker.bindPopup(`
        <div style="font-family:'Outfit',sans-serif;color:#0f172a;font-size:12px;line-height:1.6;min-width:140px">
          <strong style="font-size:13px;color:#8b5cf6;display:block;margin-bottom:4px">Replay: ${src}</strong>
          <b>Speed:</b> ${item.spd.toFixed(1)} km/h<br>
          <b>Battery:</b> ${item.bat.toFixed(1)} V<br>
          <b>Time:</b> ${displayTime}<br>
          <span style="color:#64748b;font-size:10px;display:block;margin-top:6px">Point ${playbackIndex + 1} of ${historicalDataList.length}</span>
        </div>
      `).openPopup();

      map.panTo([item.lat, item.lon]);
      playbackIndex++;
    }, 400);
  }
});



// ===== UI Panel Toggles (Google Maps Style) =====
const menuToggleBtn = document.getElementById('menu-toggle-btn');
const sidebarContent = document.getElementById('sidebar-content');

// Toggle Left Sidebar Content
if (menuToggleBtn && sidebarContent) {
  menuToggleBtn.addEventListener('click', () => {
    sidebarContent.classList.toggle('active');
    setTimeout(() => map.invalidateSize(), 300);
  });
}

// Toggle Bottom Sheets
const toggleTableBtn = document.getElementById('toggle-table-btn');
const toggleStatsBtn = document.getElementById('toggle-stats-btn');
const toggleLogBtn = document.getElementById('toggle-log-btn');
const panelTelemetry = document.getElementById('panel-telemetry');
const panelStats = document.getElementById('panel-stats');
const panelLog = document.getElementById('panel-log');
const statsCardsContainer = document.getElementById('stats-cards-container');

function closeAllSheets() {
  if (panelTelemetry) panelTelemetry.classList.remove('active');
  if (panelStats) panelStats.classList.remove('active');
  if (panelLog) panelLog.classList.remove('active');
  if (toggleTableBtn) toggleTableBtn.classList.remove('active');
  if (toggleStatsBtn) toggleStatsBtn.classList.remove('active');
  if (toggleLogBtn) toggleLogBtn.classList.remove('active');
}

if (toggleTableBtn && panelTelemetry) {
  toggleTableBtn.addEventListener('click', () => {
    const isActive = panelTelemetry.classList.contains('active');
    closeAllSheets();
    if (!isActive) {
      panelTelemetry.classList.add('active');
      toggleTableBtn.classList.add('active');
    }
  });
}

if (toggleStatsBtn && panelStats) {
  toggleStatsBtn.addEventListener('click', () => {
    const isActive = panelStats.classList.contains('active');
    closeAllSheets();
    if (!isActive) {
      panelStats.classList.add('active');
      toggleStatsBtn.classList.add('active');
      loadDeviceStats();
    }
  });
}

if (toggleLogBtn && panelLog) {
  toggleLogBtn.addEventListener('click', () => {
    const isActive = panelLog.classList.contains('active');
    closeAllSheets();
    if (!isActive) {
      panelLog.classList.add('active');
      toggleLogBtn.classList.add('active');
    }
  });
}

// Close buttons inside sheets
document.querySelectorAll('.close-sheet-btn').forEach(btn => {
  btn.addEventListener('click', () => {
    closeAllSheets();
  });
});

// Load stats function
async function loadDeviceStats() {
  if (!statsCardsContainer) return;
  try {
    statsCardsContainer.innerHTML = '<div style="grid-column: 1/-1; text-align: center; padding: 20px; color: var(--text-secondary);"><i class="fa-solid fa-spinner fa-spin"></i> Loading statistics...</div>';
    
    const res = await fetch(`${API_BASE}/stats`);
    if (!res.ok) throw new Error('Failed to fetch device stats');
    const stats = await res.json();
    
    let html = '';
    stats.forEach(item => {
      const isDT = item.src.toUpperCase().startsWith('DT');
      const icon = isDT ? '<i class="fa-solid fa-truck"></i>' : '<i class="fa-solid fa-helmet-safety"></i>';
      const typeClass = isDT ? 'dt' : 'exca';
      const formattedCount = item.count.toLocaleString();
      
      let lastSeen = '—';
      if (item.last_ts) {
        if (item.last_ts.includes('T')) {
          const parts = item.last_ts.split('T');
          const datePart = parts[0].split('-').reverse().join('/');
          const timePart = parts[1].substring(0, 8);
          lastSeen = `${datePart} ${timePart}`;
        } else {
          lastSeen = item.last_ts;
        }
      }
      
      html += `
        <div class="device-stat-card ${typeClass}">
          <div class="card-icon">${icon}</div>
          <div class="card-title">${item.src}</div>
          <div class="card-count">${formattedCount}</div>
          <div class="card-label">Records</div>
          <div class="card-time" style="font-size: 0.65rem; color: var(--text-muted); margin-top: 4px;" title="Last update timestamp">
            <i class="fa-solid fa-clock"></i> ${lastSeen}
          </div>
        </div>
      `;
    });
    
    statsCardsContainer.innerHTML = html;
  } catch (err) {
    console.error('Failed to load device stats:', err);
    statsCardsContainer.innerHTML = `<div style="grid-column: 1/-1; text-align: center; padding: 20px; color: var(--red);"><i class="fa-solid fa-triangle-exclamation"></i> Error: ${err.message}</div>`;
  }
}

