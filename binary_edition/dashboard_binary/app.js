/**
 * GPS Tambang - Binary Edition Dashboard Frontend
 */

const API_BASE = window.location.origin.includes('5001') ? '' : 'http://76.13.19.250:5001';
let map;
let markers = {};
let devicesData = [];
let selectedDevice = null;

// Initialize Map
function initMap() {
  // Center around mining site (e.g., Kutai / Samarinda coordinates: -0.738, 117.130)
  map = L.map('map', {
    zoomControl: false
  }).setView([-0.7388, 117.1301], 14);

  L.control.zoom({ position: 'topright' }).addTo(map);

  const satellite = L.tileLayer('https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}', {
    attribution: 'Tiles &copy; Esri &mdash; World Imagery',
    maxZoom: 19
  });

  const osm = L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', {
    attribution: '&copy; OpenStreetMap contributors',
    maxZoom: 19
  });

  const voyager = L.tileLayer('https://a.basemaps.cartocdn.com/rastertiles/voyager/{z}/{x}/{y}.png', {
    attribution: '&copy; CARTO &copy; OpenStreetMap',
    maxZoom: 19
  });

  satellite.addTo(map);

  const baseMaps = {
    "🛰️ Citra Satelit (Tambang)": satellite,
    "🗺️ OpenStreetMap": osm,
    "🏙️ Carto Voyager": voyager
  };

  L.control.layers(baseMaps, null, { position: 'topright' }).addTo(map);
}

// Check if device received data within 30 seconds
function isDeviceOnline(d) {
  if (!d) return false;
  const timeStr = d.created_at || d.ts;
  if (!timeStr) return false;
  try {
    const cleanStr = timeStr.includes('T') ? timeStr : timeStr.replace(' ', 'T') + 'Z';
    const rxTime = new Date(cleanStr).getTime();
    const now = Date.now();
    const diffSec = (now - rxTime) / 1000;
    return diffSec >= 0 && diffSec <= 30;
  } catch (e) {
    return false;
  }
}

// Create Custom Icon
function createVehicleIcon(src, heading = 0, isOnline = true) {
  const isExca = src.toUpperCase().startsWith('EXCA');
  const baseColor = isExca ? '#f59e0b' : '#06b6d4';
  const borderColor = isOnline ? '#10b981' : '#ef4444';
  const shadowGlow = isOnline ? '0 0 14px rgba(16,185,129,0.8)' : '0 0 8px rgba(239,68,68,0.5)';
  const symbol = isExca ? '🚜' : '🚛';
  const pulseClass = isOnline ? 'marker-pulse' : '';

  const html = `
    <div class="custom-marker ${pulseClass}" style="transform: rotate(${heading}deg);">
      <div style="background: ${baseColor}; border: 3px solid ${borderColor}; width: 34px; height: 34px; border-radius: 50%; display: flex; align-items: center; justify-content: center; font-size: 16px; box-shadow: ${shadowGlow};">
        ${symbol}
      </div>
      <div style="position: absolute; bottom: -18px; left: 50%; transform: translateX(-50%); background: rgba(15,23,42,0.95); border: 1px solid ${borderColor}; padding: 1px 6px; border-radius: 4px; font-size: 10px; font-weight: 700; color: #ffffff; white-space: nowrap; display: flex; align-items: center; gap: 4px;">
        <span style="display:inline-block; width:6px; height:6px; border-radius:50%; background:${borderColor};"></span>
        ${src}
      </div>
    </div>
  `;

  return L.divIcon({
    html: html,
    className: 'leaflet-vehicle-marker',
    iconSize: [34, 34],
    iconAnchor: [17, 17]
  });
}

// Fetch Devices Data
async function fetchDevices() {
  try {
    const res = await fetch(`${API_BASE}/api/devices`);
    if (!res.ok) throw new Error('Network response not ok');
    devicesData = await res.json();
    renderFleetList();
    renderMarkers();
    updateHeaderStats();
    document.getElementById('conn-status').classList.remove('offline');
    document.getElementById('conn-status').classList.add('online');
    document.querySelector('.status-text').innerText = 'CONNECTED';
  } catch (err) {
    console.warn('API fetch error:', err);
    document.getElementById('conn-status').classList.remove('online');
    document.getElementById('conn-status').classList.add('offline');
    document.querySelector('.status-text').innerText = 'DISCONNECTED';
  }
}

// Fetch General Stats
async function fetchStats() {
  try {
    const res = await fetch(`${API_BASE}/api/stats`);
    if (res.ok) {
      const data = await res.json();
      document.getElementById('total-packets').innerText = Number(data.total_packets || 0).toLocaleString();
      document.getElementById('total-devices').innerText = data.total_devices || 0;
      
      // Calculate active devices <= 30s directly from devicesData
      const onlineCount = devicesData.filter(d => isDeviceOnline(d)).length;
      document.getElementById('active-devices').innerText = onlineCount;
    }
  } catch (err) {}
}

function updateHeaderStats() {
  document.getElementById('device-count-badge').innerText = `${devicesData.length} Unit`;
}

function formatRelativeTime(dateStr) {
  if (!dateStr) return '—';
  try {
    const cleanStr = dateStr.includes('T') ? dateStr : dateStr.replace(' ', 'T') + 'Z';
    const d = new Date(cleanStr);
    const now = new Date();
    const diffSec = Math.floor((now.getTime() - d.getTime()) / 1000);
    if (isNaN(diffSec)) return dateStr;
    if (diffSec < 5) return 'Baru saja';
    if (diffSec < 60) return `${diffSec} dtk lalu`;
    if (diffSec < 3600) return `${Math.floor(diffSec / 60)} mnt lalu`;
    if (diffSec < 86400) return `${Math.floor(diffSec / 3600)} jam lalu`;
    return `${Math.floor(diffSec / 86400)} hari lalu`;
  } catch (e) {
    return dateStr;
  }
}

// Render Sidebar List
function renderFleetList() {
  const container = document.getElementById('fleet-list');
  const search = document.getElementById('search-input').value.toLowerCase();

  const filtered = devicesData.filter(d => d.src.toLowerCase().includes(search));

  if (filtered.length === 0) {
    container.innerHTML = '<div class="empty-state">Tidak ada unit ditemukan.</div>';
    return;
  }

  container.innerHTML = filtered.map(d => {
    const isOnline = isDeviceOnline(d);
    const isSelected = selectedDevice && selectedDevice.src === d.src;
    const isExca = d.src.toUpperCase().startsWith('EXCA');
    const icon = isExca ? '🚜' : '🚛';
    
    // Parse PTO
    let isPtoOn = false;
    if (d.pto !== undefined) {
      isPtoOn = d.pto === 1;
    } else if (typeof d.in === 'string') {
      isPtoOn = d.in.length > 2 ? d.in[0] === '1' : ((parseInt(d.in, 16) & 0x01) !== 0);
    }

    const rxTimeRel = formatRelativeTime(d.created_at || d.ts);

    return `
      <div class="fleet-card ${isSelected ? 'selected' : ''}" onclick="selectDevice('${d.src}')">
        <div class="fleet-card-header">
          <div class="fleet-title">
            <span class="status-dot-sm ${isOnline ? 'online' : 'offline'}"></span>
            <span>${icon}</span>
            <span>${d.src}</span>
          </div>
          <span class="fleet-badge ${isOnline ? 'online' : 'offline'}">
            ${isOnline ? 'ONLINE' : 'OFFLINE'}
          </span>
        </div>
        <div class="fleet-info-grid">
          <div>Spd: <strong>${d.spd} km/h</strong></div>
          <div>Bat: <strong>${d.bat} V</strong></div>
          <div>Ign: <strong style="color: ${d.ign ? '#10b981' : '#ef4444'}">${d.ign ? 'ON' : 'OFF'}</strong></div>
          <div>PTO: <strong style="color: ${isPtoOn ? '#f59e0b' : '#64748b'}">${isPtoOn ? 'ON' : 'OFF'}</strong></div>
        </div>
        <div class="fleet-db-count">
          <span>📦 Data di Database:</span>
          <strong>${Number(d.total_records || d.count || 0).toLocaleString()} record</strong>
        </div>
        <div class="fleet-time-row">
          <div>📡 Online: <strong style="color: ${isOnline ? '#10b981' : '#ef4444'}">${rxTimeRel}</strong></div>
          <div>⏱️ GPS: <strong>${d.ts || '—'}</strong></div>
        </div>
      </div>
    `;
  }).join('');
}

// Render Map Markers
function renderMarkers() {
  const currentSrcs = new Set();

  devicesData.forEach(d => {
    currentSrcs.add(d.src);
    if (!d.lat || !d.lon || (d.lat === 0 && d.lon === 0)) return;

    const isOnline = isDeviceOnline(d);
    const icon = createVehicleIcon(d.src, d.hdg || 0, isOnline);

    if (markers[d.src]) {
      markers[d.src].setLatLng([d.lat, d.lon]);
      markers[d.src].setIcon(icon);
    } else {
      const marker = L.marker([d.lat, d.lon], { icon: icon }).addTo(map);
      marker.on('click', () => selectDevice(d.src));
      markers[d.src] = marker;
    }
  });

  // Remove stale markers
  Object.keys(markers).forEach(src => {
    if (!currentSrcs.has(src)) {
      map.removeLayer(markers[src]);
      delete markers[src];
    }
  });

  // Update inspector if device is selected
  if (selectedDevice) {
    const updated = devicesData.find(d => d.src === selectedDevice.src);
    if (updated) updateInspector(updated);
  }
}

// Select Device & Open Inspector
function selectDevice(src) {
  const device = devicesData.find(d => d.src === src);
  if (!device) return;

  selectedDevice = device;
  renderFleetList();
  updateInspector(device);

  if (device.lat && device.lon && (device.lat !== 0 || device.lon !== 0)) {
    map.flyTo([device.lat, device.lon], 16, { duration: 1.2 });
  }
}

function updateInspector(d) {
  const inspector = document.getElementById('telemetry-inspector');
  inspector.style.display = 'block';

  const isOnline = isDeviceOnline(d);
  const isExca = d.src.toUpperCase().startsWith('EXCA');
  document.getElementById('insp-icon').innerText = isExca ? '🚜' : '🚛';
  document.getElementById('insp-id').innerText = d.src;
  
  const statusBadge = document.getElementById('insp-status-badge');
  if (statusBadge) {
    statusBadge.innerText = isOnline ? 'ONLINE' : 'OFFLINE';
    statusBadge.className = `fleet-badge ${isOnline ? 'online' : 'offline'}`;
  }

  document.getElementById('insp-spd').innerHTML = `${d.spd} <small>km/h</small>`;

  const ignEl = document.getElementById('insp-ign');
  ignEl.innerText = d.ign ? 'ON' : 'OFF';
  ignEl.className = `card-value badge-ign ${d.ign ? 'on' : 'off'}`;

  document.getElementById('insp-bat').innerHTML = `${d.bat} <small>V</small>`;

  // Parse PTO
  let isPtoOn = false;
  if (d.pto !== undefined) {
    isPtoOn = d.pto === 1;
  } else if (typeof d.in === 'string') {
    isPtoOn = d.in.length > 2 ? d.in[0] === '1' : ((parseInt(d.in, 16) & 0x01) !== 0);
  }
  const ptoEl = document.getElementById('insp-pto');
  if (ptoEl) {
    ptoEl.innerText = isPtoOn ? 'ON' : 'OFF';
    ptoEl.className = `card-value badge-ign ${isPtoOn ? 'on' : 'off'}`;
  }

  document.getElementById('insp-odo').innerHTML = `${Number(d.odo || 0).toLocaleString()} <small>m</small>`;
  
  const hdg = d.hdg ?? 0;
  const directions = ['N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW'];
  const dir = directions[Math.round(hdg / 45) % 8];
  document.getElementById('insp-hdg').innerHTML = `${hdg}° <small>(${dir})</small>`;

  // Total data di database
  const totalDbEl = document.getElementById('insp-total-db');
  if (totalDbEl) {
    totalDbEl.innerHTML = `${Number(d.total_records || d.count || 0).toLocaleString()} <small>record</small>`;
  }

  // Suhu ESP32
  const tempEl = document.getElementById('insp-temp');
  if (tempEl) {
    tempEl.innerHTML = `${d.temp ?? 0} <small>°C</small>`;
  }

  document.getElementById('insp-lat').innerText = d.lat;
  document.getElementById('insp-lon').innerText = d.lon;

  // Timestamp GPS
  document.getElementById('insp-ts').innerText = d.ts || '—';

  // Waktu Server Terima Data (Online Status)
  const rxEl = document.getElementById('insp-rx-time');
  if (rxEl) {
    const rxTime = d.created_at || d.ts;
    const rel = formatRelativeTime(rxTime);
    rxEl.innerText = rxTime ? `${rxTime} (${rel})` : '—';
  }
}

// Event Listeners
document.getElementById('close-inspector-btn').addEventListener('click', () => {
  document.getElementById('telemetry-inspector').style.display = 'none';
  selectedDevice = null;
  renderFleetList();
});

document.getElementById('search-input').addEventListener('input', () => {
  renderFleetList();
});

// App Startup
window.addEventListener('DOMContentLoaded', () => {
  initMap();
  fetchDevices();
  fetchStats();

  setInterval(fetchDevices, 2500);
  setInterval(fetchStats, 5000);
});
