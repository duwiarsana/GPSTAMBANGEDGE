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

  // CartoDB Dark Matter Tiles
  L.tileLayer('https://{s}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}{r}.png', {
    attribution: '&copy; OpenStreetMap &copy; CARTO',
    subdomains: 'abcd',
    maxZoom: 19
  }).addTo(map);
}

// Create Custom Icon
function createVehicleIcon(src, heading = 0, isOnline = true) {
  const isExca = src.toUpperCase().startsWith('EXCA');
  const color = isExca ? '#f59e0b' : '#06b6d4';
  const symbol = isExca ? '🚜' : '🚛';
  const pulseClass = isOnline ? 'marker-pulse' : '';

  const html = `
    <div class="custom-marker ${pulseClass}" style="transform: rotate(${heading}deg);">
      <div style="background: ${color}; border: 2px solid #ffffff; width: 34px; height: 34px; border-radius: 50%; display: flex; align-items: center; justify-content: center; font-size: 16px; box-shadow: 0 0 12px ${color};">
        ${symbol}
      </div>
      <div style="position: absolute; bottom: -18px; left: 50%; transform: translateX(-50%); background: rgba(15,23,42,0.9); border: 1px solid #334155; padding: 1px 6px; border-radius: 4px; font-size: 10px; font-weight: 700; color: #ffffff; white-space: nowrap;">
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
      document.getElementById('active-devices').innerText = data.active_devices || 0;
    }
  } catch (err) {}
}

function updateHeaderStats() {
  document.getElementById('device-count-badge').innerText = `${devicesData.length} Unit`;
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
    const isOnline = d.status === 'green';
    const isSelected = selectedDevice && selectedDevice.src === d.src;
    const isExca = d.src.toUpperCase().startsWith('EXCA');
    const icon = isExca ? '🚜' : '🚛';

    return `
      <div class="fleet-card ${isSelected ? 'selected' : ''}" onclick="selectDevice('${d.src}')">
        <div class="fleet-card-header">
          <div class="fleet-title">
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
          <div>Packets: <strong>${d.count}</strong></div>
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

    const isOnline = d.status === 'green';
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

  const isExca = d.src.toUpperCase().startsWith('EXCA');
  document.getElementById('insp-icon').innerText = isExca ? '🚜' : '🚛';
  document.getElementById('insp-id').innerText = d.src;
  document.getElementById('insp-spd').innerHTML = `${d.spd} <small>km/h</small>`;

  const ignEl = document.getElementById('insp-ign');
  ignEl.innerText = d.ign ? 'ON' : 'OFF';
  ignEl.className = `card-value badge-ign ${d.ign ? 'on' : 'off'}`;

  document.getElementById('insp-bat').innerHTML = `${d.bat} <small>V</small>`;
  document.getElementById('insp-hdop').innerText = d.hdop || '-';
  document.getElementById('insp-odo').innerHTML = `${Number(d.odo || 0).toLocaleString()} <small>m</small>`;
  document.getElementById('insp-temp').innerHTML = `${d.temp || '-'} <small>°C</small>`;

  document.getElementById('insp-lat').innerText = d.lat;
  document.getElementById('insp-lon').innerText = d.lon;
  document.getElementById('insp-ts').innerText = d.ts;
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
