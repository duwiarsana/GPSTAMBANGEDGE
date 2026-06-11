// Configuration
const BROKER_URL = 'ws://72.62.126.85:9001';
const DATA_TOPIC = 'kutai/fleet/data';
const DEFAULT_DT_ID = 'DT01'; // Default target for ACKs from relayed EXCA data

// Stats Counter
let countDT = 0;
let countEXCA = 0;
let countACK = 0;

// Fleet positions and markers cache
const fleetMarkers = {};
const fleetData = {};

// Map Initialization
const map = L.map('map').setView([-0.95, 117.0], 12); // Default coordinates centered in Kutai area, East Kalimantan

// Add Dark Matter CartoDB tiles
L.tileLayer('https://{s}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}{r}.png', {
  attribution: '&copy; OpenStreetMap contributors &copy; CARTO',
  subdomains: 'abcd',
  maxZoom: 20
}).addTo(map);

// Custom Icons for DT and EXCA
const dtIcon = L.divIcon({
  className: 'custom-marker dt-marker',
  html: '<div class="marker-pin blue-pin"><i class="fa-solid fa-truck"></i></div>',
  iconSize: [36, 36],
  iconAnchor: [18, 36]
});

const excaIcon = L.divIcon({
  className: 'custom-marker exca-marker',
  html: '<div class="marker-pin orange-pin"><i class="fa-solid fa-helmet-safety"></i></div>',
  iconSize: [36, 36],
  iconAnchor: [18, 36]
});

// UI Elements
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

// CSS styles for custom markers to be added dynamically
const style = document.createElement('style');
style.innerHTML = `
  .marker-pin {
    width: 34px;
    height: 34px;
    border-radius: 50% 50% 50% 0;
    position: absolute;
    transform: rotate(-45deg);
    left: 50%;
    top: 50%;
    margin: -17px 0 0 -17px;
    display: flex;
    align-items: center;
    justify-content: center;
    border: 2px solid white;
    box-shadow: 0 0 10px rgba(0,0,0,0.5);
  }
  .marker-pin i {
    transform: rotate(45deg);
    color: white;
    font-size: 14px;
  }
  .blue-pin {
    background: #00f2fe;
    border-color: #00f2fe;
    box-shadow: 0 0 12px rgba(0, 242, 254, 0.6);
  }
  .orange-pin {
    background: #f97316;
    border-color: #f97316;
    box-shadow: 0 0 12px rgba(249, 115, 22, 0.6);
  }
`;
document.head.appendChild(style);

// Connect to MQTT Broker
console.log(`Connecting to MQTT Broker via WebSockets: ${BROKER_URL}...`);
const client = mqtt.connect(BROKER_URL, {
  keepalive: 60,
  reconnectPeriod: 2000,
});

// MQTT event handlers
client.on('connect', () => {
  console.log('Successfully connected to MQTT Broker!');
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
  console.warn('MQTT Connection closed.');
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
    addLogSystem(`Received non-JSON message on topic <code>${topic}</code>: ${rawPayload}`, 'warn');
  }
});

// Update connection status UI
function updateStatus(isConnected) {
  const dot = connStatusEl.querySelector('.pulse-dot');
  const text = connStatusEl.querySelector('.status-text');
  
  if (isConnected) {
    dot.className = 'pulse-dot green';
    text.textContent = 'Connected';
  } else {
    dot.className = 'pulse-dot red';
    text.textContent = 'Disconnected';
  }
}

// Process incoming payload
function handleIncomingData(data, rawJson) {
  const id = data.id || data.msg_id;
  const src = data.src || data.source || 'UNKNOWN';
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
  } else {
    countEXCA++;
    countExcaEl.textContent = countEXCA;
  }
  
  // Add to Live Log Stream UI
  addLogEntry(src, isDT ? 'dt' : 'exca', `[JSON] ${rawJson}`);
  
  // Update map coordinates
  if (!isNaN(latitude) && !isNaN(longitude)) {
    updateMapMarker(src, isDT, latitude, longitude, speed, timestamp);
  }

  // Update telemetry table
  updateTelemetryTable(src, timestamp, ignition, speed, battery, id);

  // Auto ACK mechanism
  if (autoAckToggle.checked && id) {
    sendAutoACK(id, src);
  }
}

// Send ACK to Broker
function sendAutoACK(msgId, src) {
  // Determine which DT ID topic we should send the ACK to
  // If the source of the message is a DT unit (e.g. DT01), send it to its topic.
  // If the source is an EXCA unit (e.g. EXCA01), it was relayed, so we broadcast the ACK 
  // to both the default DT01 topic and its own topic to make sure the relaying DT gets it.
  const targetTopics = [];
  if (src.toUpperCase().startsWith('DT')) {
    targetTopics.push(`kutai/fleet/ack/${src}`);
  } else {
    targetTopics.push(`kutai/fleet/ack/${DEFAULT_DT_ID}`);
    targetTopics.push(`kutai/fleet/ack/${src}`);
  }

  const ackPayload = JSON.stringify({
    id: msgId,
    status: 'ok'
  });

  targetTopics.forEach(topic => {
    client.publish(topic, ackPayload, { qos: 0 }, (err) => {
      if (!err) {
        countACK++;
        countAckEl.textContent = countACK;
        addLogEntry(src, 'ack', `📤 Sent ACK to <code>${topic}</code>: ${ackPayload}`);
      } else {
        addLogSystem(`Failed to send ACK to ${topic}: ${err.message}`, 'error');
      }
    });
  });
}

// Map marker updater
function updateMapMarker(src, isDT, lat, lon, speed, timestamp) {
  const key = src;
  
  if (fleetMarkers[key]) {
    // Smooth transition or direct move
    fleetMarkers[key].setLatLng([lat, lon]);
    fleetMarkers[key].getPopup().setContent(createPopupContent(src, lat, lon, speed, timestamp));
  } else {
    // Create new marker
    const marker = L.marker([lat, lon], {
      icon: isDT ? dtIcon : excaIcon
    }).addTo(map);
    
    marker.bindPopup(createPopupContent(src, lat, lon, speed, timestamp));
    fleetMarkers[key] = marker;
  }
  
  // Pan map to latest coordinate
  map.panTo([lat, lon]);
}

function createPopupContent(src, lat, lon, speed, timestamp) {
  return `
    <div style="font-family: 'Plus Jakarta Sans', sans-serif; color: #1e293b;">
      <h4 style="margin: 0 0 5px 0; font-family: 'Outfit'; font-weight: 700;">${src}</h4>
      <p style="margin: 0 0 3px 0; font-size: 11px;"><b>Lat:</b> ${lat.toFixed(6)}</p>
      <p style="margin: 0 0 3px 0; font-size: 11px;"><b>Lon:</b> ${lon.toFixed(6)}</p>
      <p style="margin: 0 0 3px 0; font-size: 11px;"><b>Speed:</b> ${speed} km/h</p>
      <p style="margin: 0; font-size: 10px; color: #64748b;">${timestamp}</p>
    </div>
  `;
}

// Telemetry Table updater
function updateTelemetryTable(src, timestamp, ignition, speed, battery, msgId) {
  fleetData[src] = {
    timestamp,
    ignition,
    speed,
    battery,
    msgId
  };

  // Redraw all rows sorted alphabetically by key (device ID)
  const sortedKeys = Object.keys(fleetData).sort();
  
  if (sortedKeys.length === 0) {
    telemetryRowsEl.innerHTML = `
      <tr class="empty-table-row">
        <td colspan="6">Belum ada data masuk.</td>
      </tr>
    `;
    return;
  }

  let html = '';
  sortedKeys.forEach(key => {
    const row = fleetData[key];
    const isDT = key.toUpperCase().startsWith('DT');
    const badgeClass = isDT ? 'badge-dt' : 'badge-exca';
    
    let ignHtml = '<span class="ign-state ign-off"><i class="fa-solid fa-circle-notch"></i> Off</span>';
    if (row.ignition === 1) {
      ignHtml = '<span class="ign-state ign-on"><i class="fa-solid fa-circle"></i> On</span>';
    } else if (row.ignition === 0) {
      ignHtml = '<span class="ign-state ign-off"><i class="fa-solid fa-circle-notch"></i> Cooldown</span>';
    }

    // Format time
    const timeStr = row.timestamp.includes('T') ? row.timestamp.split('T')[1].substring(0, 8) : row.timestamp;

    html += `
      <tr>
        <td><span class="badge ${badgeClass}">${key}</span></td>
        <td>${timeStr}</td>
        <td>${ignHtml}</td>
        <td><b>${row.speed.toFixed(1)}</b> km/h</td>
        <td>${row.battery.toFixed(1)}V</td>
        <td><small style="color: #64748b; font-family: monospace;">${row.msgId ? row.msgId.split('-').pop() : '-'}</small></td>
      </tr>
    `;
  });

  telemetryRowsEl.innerHTML = html;
}

// UI Logs utilities
function addLogEntry(src, type, message) {
  // Remove empty message if present
  const empty = logStreamEl.querySelector('.empty-log-msg');
  if (empty) {
    logStreamEl.removeChild(empty);
  }

  const entry = document.createElement('div');
  entry.className = `log-entry ${type}`;

  const timeStr = new Date().toLocaleTimeString();
  const label = type === 'ack' ? 'SYSTEM' : src;

  entry.innerHTML = `
    <div class="log-meta">
      <span class="log-tag"><b>${label}</b></span>
      <span class="log-time">${timeStr}</span>
    </div>
    <div class="log-body">${message}</div>
  `;

  logStreamEl.appendChild(entry);
  logStreamEl.scrollTop = logStreamEl.scrollHeight;

  // Limit logs to last 50 entries
  while (logStreamEl.children.length > 50) {
    logStreamEl.removeChild(logStreamEl.firstChild);
  }
}

function addLogSystem(message, level = 'info') {
  const empty = logStreamEl.querySelector('.empty-log-msg');
  if (empty) {
    logStreamEl.removeChild(empty);
  }

  const entry = document.createElement('div');
  entry.className = `log-entry sys-${level}`;
  entry.style.borderLeftColor = level === 'error' ? 'var(--accent-red)' : 'var(--text-secondary)';
  
  const timeStr = new Date().toLocaleTimeString();

  entry.innerHTML = `
    <div class="log-meta">
      <span class="log-tag"><b>${level.toUpperCase()}</b></span>
      <span class="log-time">${timeStr}</span>
    </div>
    <div class="log-body" style="color: ${level === 'error' ? 'var(--accent-red)' : 'var(--text-secondary)'}">${message}</div>
  `;

  logStreamEl.appendChild(entry);
  logStreamEl.scrollTop = logStreamEl.scrollHeight;
}

// Clear log stream button handler
clearLogBtn.addEventListener('click', () => {
  logStreamEl.innerHTML = '<div class="empty-log-msg">Menunggu data masuk dari topik <code class="code-topic">kutai/fleet/data</code>...</div>';
});

// Simulator functions
let simDtSeq = 1;
let simExcaSeq = 1;

simDtBtn.addEventListener('click', () => {
  if (!client.connected) {
    alert('Simulasi gagal: Web Dashboard tidak terhubung ke broker MQTT.');
    return;
  }

  // Randomize location slightly around Kutai area
  const lat = -0.95 + (Math.random() - 0.5) * 0.05;
  const lon = 117.0 + (Math.random() - 0.5) * 0.05;
  const speed = 10 + Math.random() * 40;
  const bat = 12.2 + Math.random() * 1.5;

  const dtPayload = {
    id: `DT01-861327085560006-${new Date().toISOString().replace(/[-:]/g, '').split('.')[0]}Z-${simDtSeq++}`,
    imei: "861327085560006",
    src: "DT01",
    type: "gps",
    ev: 1,
    ts: new Date().toISOString(),
    lat: lat,
    lon: lon,
    spd: speed,
    hdg: Math.floor(Math.random() * 360),
    alt: 45 + Math.floor(Math.random() * 10),
    bat: bat,
    odo: 10450 + simDtSeq * 2,
    ign: 1,
    in: 0,
    out: 0,
    hdop: 0.8,
    temp: 41.0 + Math.random() * 3
  };

  client.publish(DATA_TOPIC, JSON.stringify(dtPayload), { qos: 0 }, (err) => {
    if (!err) {
      console.log('Published mock DT01 data successfully.');
    } else {
      console.error('Publish mock DT01 error:', err);
    }
  });
});

simExcaBtn.addEventListener('click', () => {
  if (!client.connected) {
    alert('Simulasi gagal: Web Dashboard tidak terhubung ke broker MQTT.');
    return;
  }

  // Randomize location slightly around Kutai area
  const lat = -0.93 + (Math.random() - 0.5) * 0.03;
  const lon = 117.02 + (Math.random() - 0.5) * 0.03;
  const speed = Math.random() * 5; // Excavators are slow
  const bat = 24.1 + Math.random() * 1.8; // Excavator uses 24V system

  const excaPayload = {
    id: `EXCA01-861999085560111-${new Date().toISOString().replace(/[-:]/g, '').split('.')[0]}Z-${simExcaSeq++}`,
    imei: "861999085560111",
    src: "EXCA01",
    type: "gps",
    ev: 1,
    ts: new Date().toISOString(),
    lat: lat,
    lon: lon,
    spd: speed,
    hdg: Math.floor(Math.random() * 360),
    alt: 50 + Math.floor(Math.random() * 10),
    bat: bat,
    odo: 2310 + simExcaSeq,
    ign: 1,
    in: 0,
    out: 0,
    hdop: 0.9,
    temp: 45.0 + Math.random() * 4
  };

  client.publish(DATA_TOPIC, JSON.stringify(excaPayload), { qos: 0 }, (err) => {
    if (!err) {
      console.log('Published mock EXCA01 data successfully.');
    } else {
      console.error('Publish mock EXCA01 error:', err);
    }
  });
});
