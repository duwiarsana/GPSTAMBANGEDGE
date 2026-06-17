// ===== Configuration =====
const BROKER_URL = 'ws://72.62.126.85:9001';
const DATA_TOPIC = 'kutai/fleet/data';
const DEFAULT_DT_ID = 'DT01';

// ===== State =====
let countDT = 0;
let countEXCA = 0;
let countACK = 0;
const fleetMarkers = {};
const fleetData = {};

// ===== DOM Elements =====
const connStatusEl = document.getElementById('conn-status');
const countDtEl = document.getElementById('count-dt');
const countExcaEl = document.getElementById('count-exca');
const countAckEl = document.getElementById('count-ack');
const autoAckToggle = document.getElementById('auto-ack-toggle');
const logStreamEl = document.getElementById('log-stream');
const telemetryRowsEl = document.getElementById('telemetry-rows');
const simDtBtn = document.getElementById('sim-dt-btn');
const simExcaBtn = document.getElementById('sim-exca-btn');
const clearLogBtn = document.getElementById('clear-log-btn');
const clockEl = document.getElementById('live-clock');

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

// OpenStreetMap standard colorful tiles
L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
  attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors',
  maxZoom: 19
}).addTo(map);

// Custom Marker Icons
const dtIcon = L.divIcon({
  className: 'map-marker-dt',
  html: '<div class="marker-dot blue"><i class="fa-solid fa-truck"></i></div>',
  iconSize: [32, 32],
  iconAnchor: [16, 16]
});

const excaIcon = L.divIcon({
  className: 'map-marker-exca',
  html: '<div class="marker-dot amber"><i class="fa-solid fa-helmet-safety"></i></div>',
  iconSize: [32, 32],
  iconAnchor: [16, 16]
});

// Inject marker styles
const markerStyle = document.createElement('style');
markerStyle.textContent = `
  .marker-dot {
    width: 32px;
    height: 32px;
    border-radius: 50%;
    display: flex;
    align-items: center;
    justify-content: center;
    border: 2px solid;
    font-size: 12px;
    color: #fff;
    box-shadow: 0 0 10px rgba(0,0,0,0.5);
  }
  .marker-dot.blue {
    background: rgba(77, 171, 247, 0.9);
    border-color: #4dabf7;
    box-shadow: 0 0 12px rgba(77, 171, 247, 0.5);
  }
  .marker-dot.amber {
    background: rgba(255, 164, 79, 0.9);
    border-color: #ffa44f;
    box-shadow: 0 0 12px rgba(255, 164, 79, 0.5);
  }
`;
document.head.appendChild(markerStyle);

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
      addLogSystem(`Subscribed to topic: <code>${DATA_TOPIC}</code>`);
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

client.on('message', (topic, message) => {
  const rawPayload = message.toString();
  try {
    const data = JSON.parse(rawPayload);
    handleIncomingData(data, rawPayload);
  } catch (e) {
    addLogSystem(`Non-JSON message on <code>${topic}</code>: ${rawPayload}`, 'warn');
  }
});

// ===== Status Update =====
function updateStatus(isConnected) {
  const dot = connStatusEl.querySelector('.conn-dot');
  const text = connStatusEl.querySelector('.status-text');

  if (isConnected) {
    dot.className = 'conn-dot online';
    text.textContent = 'Connected';
  } else {
    dot.className = 'conn-dot offline';
    text.textContent = 'Disconnected';
  }
}

// ===== Data Handler =====
function handleIncomingData(data, rawJson) {
  const id = data.id || data.msg_id;
  let src = data.src || data.source || 'UNKNOWN';
  
  // Fallback: extract source from ID if source is missing/unknown (e.g. DT01-...)
  if (src === 'UNKNOWN' && id) {
    const parts = id.split('-');
    if (parts.length > 0 && parts[0].trim() !== '') {
      src = parts[0];
    }
  }

  const timestamp = data.ts || data.timestamp || new Date().toISOString();
  const latitude = parseFloat(data.lat || data.latitude);
  const longitude = parseFloat(data.lon || data.longitude);
  const speed = parseFloat(data.spd || data.speed || 0);
  const battery = parseFloat(data.bat || data.external || 0);
  const ignition = data.ign !== undefined ? data.ign : (data.ignition !== undefined ? data.ignition : -1);

  const isDT = src.toUpperCase().startsWith('DT');

  // Update counter
  if (isDT) {
    countDT++;
    countDtEl.textContent = countDT;
    animateStat('stat-dt');
  } else {
    countEXCA++;
    countExcaEl.textContent = countEXCA;
    animateStat('stat-exca');
  }

  // Log entry
  addLogEntry(src, isDT ? 'dt' : 'exca', rawJson);

  // Map marker
  if (!isNaN(latitude) && !isNaN(longitude)) {
    updateMapMarker(src, isDT, latitude, longitude, speed, timestamp);
  }

  // Telemetry table
  updateTelemetryTable(src, timestamp, ignition, speed, battery, id);

  // Auto-ACK
  if (autoAckToggle.checked && id) {
    sendAutoACK(id, src);
  }
}

// ===== Stat Animation =====
function animateStat(elementId) {
  const el = document.getElementById(elementId);
  if (!el) return;
  el.style.transition = 'none';
  el.style.borderColor = 'rgba(255,255,255,0.15)';
  requestAnimationFrame(() => {
    el.style.transition = 'border-color 0.8s ease';
    el.style.borderColor = '';
  });
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
    fleetMarkers[src] = marker;
  }
  map.panTo([lat, lon]);
}

function popupHTML(src, lat, lon, speed, ts) {
  return `
    <div style="font-family:Inter,sans-serif;color:#1e293b;font-size:12px;line-height:1.6">
      <strong style="font-size:13px">${src}</strong><br>
      <b>Lat:</b> ${lat.toFixed(6)}<br>
      <b>Lon:</b> ${lon.toFixed(6)}<br>
      <b>Speed:</b> ${speed.toFixed(1)} km/h<br>
      <span style="color:#64748b;font-size:10px">${ts}</span>
    </div>`;
}

// ===== Telemetry Table =====
function updateTelemetryTable(src, timestamp, ignition, speed, battery, msgId) {
  fleetData[src] = { timestamp, ignition, speed, battery, msgId };
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

    const time = r.timestamp.includes('T') ? r.timestamp.split('T')[1].substring(0, 8) : r.timestamp;
    const shortId = r.msgId ? r.msgId.split('-').pop() : '—';

    html += `<tr>
      <td><span class="badge ${badge}">${key}</span></td>
      <td style="font-family:var(--mono)">${time}</td>
      <td>${ignHtml}</td>
      <td><strong>${r.speed.toFixed(1)}</strong> <span style="color:var(--text-muted)">km/h</span></td>
      <td>${r.battery.toFixed(1)} <span style="color:var(--text-muted)">V</span></td>
      <td style="font-family:var(--mono);color:var(--text-muted);font-size:0.65rem">${shortId}</td>
    </tr>`;
  });

  telemetryRowsEl.innerHTML = html;
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

// ===== Simulator =====
let simDtSeq = 1;
let simExcaSeq = 1;

simDtBtn.addEventListener('click', () => {
  if (!client.connected) { alert('Dashboard belum terhubung ke broker MQTT.'); return; }
  const lat = -0.95 + (Math.random() - 0.5) * 0.05;
  const lon = 117.0 + (Math.random() - 0.5) * 0.05;
  const payload = {
    id: `DT01-861327085560006-${isoCompact()}-${simDtSeq++}`,
    imei: "861327085560006",
    src: "DT01", type: "gps", ev: 1,
    ts: new Date().toISOString(),
    lat, lon,
    spd: 10 + Math.random() * 40,
    hdg: Math.floor(Math.random() * 360),
    alt: 45 + Math.floor(Math.random() * 10),
    bat: 12.2 + Math.random() * 1.5,
    odo: 10450 + simDtSeq * 2,
    ign: 1, in: 0, out: 0,
    hdop: 0.8, temp: 41.0 + Math.random() * 3
  };
  client.publish(DATA_TOPIC, JSON.stringify(payload), { qos: 0 });
});

simExcaBtn.addEventListener('click', () => {
  if (!client.connected) { alert('Dashboard belum terhubung ke broker MQTT.'); return; }
  const lat = -0.93 + (Math.random() - 0.5) * 0.03;
  const lon = 117.02 + (Math.random() - 0.5) * 0.03;
  const payload = {
    id: `EXCA01-861999085560111-${isoCompact()}-${simExcaSeq++}`,
    imei: "861999085560111",
    src: "EXCA01", type: "gps", ev: 1,
    ts: new Date().toISOString(),
    lat, lon,
    spd: Math.random() * 5,
    hdg: Math.floor(Math.random() * 360),
    alt: 50 + Math.floor(Math.random() * 10),
    bat: 24.1 + Math.random() * 1.8,
    odo: 2310 + simExcaSeq,
    ign: 1, in: 0, out: 0,
    hdop: 0.9, temp: 45.0 + Math.random() * 4
  };
  client.publish(DATA_TOPIC, JSON.stringify(payload), { qos: 0 });
});

function isoCompact() {
  return new Date().toISOString().replace(/[-:]/g, '').split('.')[0] + 'Z';
}

// ===== Mobile Sidebar Toggle =====
const menuToggleBtn = document.getElementById('menu-toggle-btn');
const sidebarCloseBtn = document.getElementById('sidebar-close-btn');
const sidebarEl = document.getElementById('sidebar');
const backdropEl = document.getElementById('sidebar-backdrop');

if (menuToggleBtn && sidebarCloseBtn && sidebarEl && backdropEl) {
  function openSidebar() {
    sidebarEl.classList.add('active');
    backdropEl.classList.add('active');
  }

  function closeSidebar() {
    sidebarEl.classList.remove('active');
    backdropEl.classList.remove('active');
  }

  menuToggleBtn.addEventListener('click', openSidebar);
  sidebarCloseBtn.addEventListener('click', closeSidebar);
  backdropEl.addEventListener('click', closeSidebar);
}

