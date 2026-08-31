/**
 * GPS Tambang - Binary Edition Dashboard Frontend
 */

const API_BASE = window.location.origin.includes('5001') ? '' : 'http://76.13.19.250:5001';
let map;
let markers = {};
let devicesData = [];
let selectedDevice = null;

let baseLayers = {};
let currentLayerName = 'satellite';

// Initialize Map
function initMap() {
  // Center around mining site (e.g., Kutai / Samarinda coordinates: -0.738, 117.130)
  map = L.map('map', {
    zoomControl: false
  }).setView([-0.7388, 117.1301], 14);

  // 1. Citra Satelit Esri (Tambang)
  baseLayers.satellite = L.tileLayer('https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}', {
    attribution: 'Tiles &copy; Esri &mdash; World Imagery',
    maxZoom: 19
  });

  // 2. OpenStreetMap (Standar Jalan & Kontur)
  baseLayers.osm = L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', {
    attribution: '&copy; OpenStreetMap contributors',
    maxZoom: 19
  });

  // 3. OpenTopoMap (Kontur Elevasi & Ketinggian Lereng)
  baseLayers.topo = L.tileLayer('https://a.tile.opentopomap.org/{z}/{x}/{y}.png', {
    attribution: '&copy; OpenTopoMap &copy; OpenStreetMap',
    maxZoom: 17
  });

  // Restore preferred map layer from localStorage
  let savedLayer = localStorage.getItem('gps_tambang_map_layer') || 'satellite';
  if (savedLayer === 'dark' || !baseLayers[savedLayer]) savedLayer = 'satellite';
  switchMapLayer(savedLayer);
}

// Switch Active Map Layer
function switchMapLayer(layerName) {
  if (!baseLayers[layerName]) layerName = 'satellite';

  // Remove existing base layers
  Object.keys(baseLayers).forEach(name => {
    if (map && map.hasLayer(baseLayers[name])) {
      map.removeLayer(baseLayers[name]);
    }
  });

  // Add selected base layer to map
  if (map && baseLayers[layerName]) {
    baseLayers[layerName].addTo(map);
  }

  currentLayerName = layerName;
  localStorage.setItem('gps_tambang_map_layer', layerName);

  // Update UI active states
  document.querySelectorAll('.layer-btn').forEach(btn => {
    if (btn.getAttribute('data-layer') === layerName) {
      btn.classList.add('active');
    } else {
      btn.classList.remove('active');
    }
  });
}

// Zoom & Fit Bounds to all fleet vehicles
function fitFleetBounds() {
  const activeMarkers = Object.values(markers);
  if (activeMarkers.length === 0) return;

  if (activeMarkers.length === 1) {
    const latlng = activeMarkers[0].getLatLng();
    map.setView(latlng, 16, { animate: true });
    return;
  }

  const group = L.featureGroup(activeMarkers);
  map.fitBounds(group.getBounds(), { padding: [70, 70], maxZoom: 17 });
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
  const baseColor = isExca ? '#d97706' : '#0284c7';
  const borderColor = isOnline ? '#16a34a' : '#dc2626';
  const shadowGlow = isOnline ? '0 0 12px rgba(22,163,74,0.7)' : '0 0 6px rgba(220,38,38,0.4)';
  const symbol = isExca ? '🚜' : '🚛';
  const pulseClass = isOnline ? 'marker-pulse' : '';

  const html = `
    <div class="custom-marker ${pulseClass}" style="transform: rotate(${heading}deg);">
      <div style="background: ${baseColor}; border: 3px solid ${borderColor}; width: 34px; height: 34px; border-radius: 50%; display: flex; align-items: center; justify-content: center; font-size: 16px; box-shadow: ${shadowGlow}; color: #ffffff;">
        ${symbol}
      </div>
      <div style="position: absolute; bottom: -18px; left: 50%; transform: translateX(-50%); background: #ffffff; border: 1px solid ${borderColor}; padding: 1px 6px; border-radius: 4px; font-size: 10px; font-weight: 800; color: #0f172a; white-space: nowrap; display: flex; align-items: center; gap: 4px; box-shadow: 0 2px 4px rgba(0,0,0,0.15);">
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
      const totalPkts = Number(data.total_packets || 0).toLocaleString();
      const totalDevs = data.total_devices || 0;
      const onlineCount = devicesData.filter(d => isDeviceOnline(d)).length;

      document.getElementById('total-packets').innerText = totalPkts;
      document.getElementById('total-devices').innerText = totalDevs;
      document.getElementById('active-devices').innerText = onlineCount;

      // Mobile Stats Card
      const mTotPkts = document.getElementById('m-stat-total-packets');
      if (mTotPkts) mTotPkts.innerText = totalPkts;
      const mTotDevs = document.getElementById('m-stat-total-devices');
      if (mTotDevs) mTotDevs.innerText = totalDevs;
      const mActDevs = document.getElementById('m-stat-active-devices');
      if (mActDevs) mActDevs.innerText = onlineCount;
    }
  } catch (err) {}
}

function updateHeaderStats() {
  const countStr = `${devicesData.length} Unit`;
  const badgeEl = document.getElementById('device-count-badge');
  if (badgeEl) badgeEl.innerText = countStr;
  const btnBadge = document.getElementById('btn-sidebar-badge');
  if (btnBadge) btnBadge.innerText = devicesData.length;
  const mapBadge = document.getElementById('map-drawer-badge');
  if (mapBadge) mapBadge.innerText = devicesData.length;
  const navBadge = document.getElementById('nav-fleet-count');
  if (navBadge) navBadge.innerText = devicesData.length;

  const mTotDevs = document.getElementById('m-stat-total-devices');
  if (mTotDevs) mTotDevs.innerText = devicesData.length;
}

// Dedicated Mobile Tab View Switcher (Peta | Armada | Ringkasan)
let currentMobileView = 'map';

function switchMobileView(viewName) {
  currentMobileView = viewName;
  const mainLayout = document.querySelector('.main-layout');
  if (mainLayout) {
    mainLayout.classList.remove('view-map', 'view-fleet', 'view-stats');
    mainLayout.classList.add(`view-${viewName}`);
  }

  // If switched to map, invalidate map size so Leaflet tiles render immediately
  if (viewName === 'map' && map) {
    setTimeout(() => {
      map.invalidateSize();
    }, 100);
  }

  // Update bottom nav buttons active class
  document.querySelectorAll('.nav-tab-btn').forEach(btn => {
    if (btn.getAttribute('data-view') === viewName) {
      btn.classList.add('active');
    } else {
      btn.classList.remove('active');
    }
  });

  // If switched to stats, refresh stats
  if (viewName === 'stats') {
    fetchStats();
  }
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

// Calculate Inclinometer Tilt, Roll, Pitch from G-Sensor (mg)
function calculateTilt(gx, gy, gz) {
  const x = Number(gx || 0);
  const y = Number(gy || 0);
  const z = Number(gz || 0);

  if (x === 0 && y === 0 && z === 0) {
    return {
      tilt: 0,
      roll: 0,
      pitch: 0,
      status: 'normal',
      statusText: '0.0° NORMAL',
      rollText: '0.0° (Datar)',
      pitchText: '0.0° (Rata)',
      color: '#10b981',
      percent: 3
    };
  }

  // Pitch (incline forward/backward in degrees)
  const pitchRad = Math.atan2(y, Math.sqrt(x * x + z * z));
  const pitchDeg = (pitchRad * 180) / Math.PI;

  // Roll (tilt lateral left/right in degrees)
  const rollRad = Math.atan2(x, Math.sqrt(y * y + z * z));
  const rollDeg = (rollRad * 180) / Math.PI;

  // Total absolute tilt angle from flat horizontal plane
  const tiltDeg = Math.sqrt(pitchDeg * pitchDeg + rollDeg * rollDeg);

  let status = 'normal';
  let color = '#10b981';
  if (tiltDeg > 18) {
    status = 'danger';
    color = '#ef4444';
  } else if (tiltDeg >= 10) {
    status = 'warning';
    color = '#d97706';
  }

  const rollDirection = rollDeg > 0.5 ? 'Miring Kanan' : (rollDeg < -0.5 ? 'Miring Kiri' : 'Datar');
  const pitchDirection = pitchDeg > 0.5 ? 'Nanjak' : (pitchDeg < -0.5 ? 'Turun' : 'Rata');
  const percent = Math.min(100, Math.max(4, (tiltDeg / 25) * 100));

  return {
    tilt: parseFloat(tiltDeg.toFixed(1)),
    roll: parseFloat(Math.abs(rollDeg).toFixed(1)),
    pitch: parseFloat(Math.abs(pitchDeg).toFixed(1)),
    status: status,
    statusText: `${tiltDeg.toFixed(1)}° ${status.toUpperCase()}`,
    rollText: `${Math.abs(rollDeg).toFixed(1)}° (${rollDirection})`,
    pitchText: `${Math.abs(pitchDeg).toFixed(1)}° (${pitchDirection})`,
    color: color,
    percent: percent
  };
}

// Render Sidebar List
let currentFleetFilter = 'all';

function renderFleetList() {
  const container = document.getElementById('fleet-list') || document.getElementById('fleet-list-container');
  if (!container) return;
  const searchEl = document.getElementById('search-input');
  const search = searchEl ? searchEl.value.toLowerCase() : '';

  // Calculate counts for all 4 filter categories
  const countAll = devicesData.length;
  const countOnline = devicesData.filter(d => isDeviceOnline(d)).length;
  const countExca = devicesData.filter(d => d.src.toUpperCase().startsWith('EXCA')).length;
  const countDt = devicesData.filter(d => d.src.toUpperCase().startsWith('DT')).length;

  const elAll = document.getElementById('filter-count-all');
  if (elAll) elAll.innerText = countAll;
  const elOnline = document.getElementById('filter-count-online');
  if (elOnline) elOnline.innerText = countOnline;
  const elExca = document.getElementById('filter-count-exca');
  if (elExca) elExca.innerText = countExca;
  const elDt = document.getElementById('filter-count-dt');
  if (elDt) elDt.innerText = countDt;

  // Filter list by search query
  let filtered = devicesData.filter(d => d.src.toLowerCase().includes(search));

  // Filter list by selected tab
  if (currentFleetFilter === 'online') {
    filtered = filtered.filter(d => isDeviceOnline(d));
  } else if (currentFleetFilter === 'exca') {
    filtered = filtered.filter(d => d.src.toUpperCase().startsWith('EXCA'));
  } else if (currentFleetFilter === 'dt') {
    filtered = filtered.filter(d => d.src.toUpperCase().startsWith('DT'));
  }

  if (filtered.length === 0) {
    container.innerHTML = '<div class="empty-state">Tidak ada unit sesuai filter.</div>';
    return;
  }

  container.innerHTML = filtered.map(d => {
    const isOnline = isDeviceOnline(d);
    const isSelected = selectedDevice && selectedDevice.src === d.src;
    const isExca = d.src.toUpperCase().startsWith('EXCA');
    const icon = isExca ? '🚜' : '🚛';
    
    // Parse PTO (Bit0 of input_status / in)
    let isPtoOn = false;
    const ptoVal = d.pto ?? d.raw_payload?.pto;
    if (ptoVal !== undefined && ptoVal !== null) {
      isPtoOn = (ptoVal === 1 || ptoVal === '1' || ptoVal === true);
    } else {
      const inVal = d.in ?? d.raw_payload?.in;
      if (typeof inVal === 'string') {
        isPtoOn = inVal.length > 2 ? inVal[0] === '1' : ((parseInt(inVal, 16) & 0x01) !== 0);
      } else if (typeof inVal === 'number') {
        isPtoOn = ((inVal & 0x01) !== 0);
      }
    }

    const rxTimeRel = formatRelativeTime(d.created_at || d.ts);

    // G-Sensor & Tilt
    const gs = d.raw_payload?.gs || {};
    const gx = gs.x ?? d.gs_x ?? 0;
    const gy = gs.y ?? d.gs_y ?? 0;
    const gz = gs.z ?? d.gs_z ?? 0;
    const tilt = calculateTilt(gx, gy, gz);

    const imeiBadge = d.imei ? `<span style="font-size: 0.72rem; color: #64748b; font-family: monospace;">IMEI: ${d.imei}</span>` : '';
    const stBadge = d.ibutton ? (d.ibutton_status || (d.raw_payload?.ibutton_login ? 'LOGIN' : 'LOGOUT')).toUpperCase() : '';
    const ibuttonBadge = d.ibutton ? `<span style="font-size: 0.72rem; color: #10b981; font-weight: 700; font-family: monospace;">🔑 ID: ${d.ibutton} (${stBadge})</span>` : '';

    return `
      <div class="fleet-card ${isSelected ? 'selected' : ''}" onclick="selectDevice('${d.src}')">
        <div class="fleet-card-header">
          <div class="fleet-title">
            <span class="status-dot-sm ${isOnline ? 'online' : 'offline'}"></span>
            <span>${icon}</span>
            <div style="display: flex; flex-direction: column; gap: 1px;">
              <span>${d.src}</span>
              ${imeiBadge}
              ${ibuttonBadge}
            </div>
          </div>
          <div style="display: flex; flex-direction: column; align-items: flex-end; gap: 3px;">
            <span class="fleet-badge ${isOnline ? 'online' : 'offline'}">
              ${isOnline ? 'ONLINE' : 'OFFLINE'}
            </span>
            <span class="fleet-tilt-badge ${tilt.status}" title="Kemiringan Unit K3">
              📐 ${tilt.tilt}°
            </span>
          </div>
        </div>
        <div class="fleet-info-grid">
          <div>Spd: <strong>${d.spd} km/h</strong></div>
          <div>Bat: <strong>${d.bat} V</strong></div>
          <div>Ign: <strong style="color: ${d.ign ? '#10b981' : '#ef4444'}">${d.ign ? 'ON' : 'OFF'}</strong></div>
          <div>PTO: <strong style="color: ${isPtoOn ? '#f59e0b' : '#64748b'}">${isPtoOn ? 'ON' : 'OFF'}</strong></div>
        </div>
        <div class="fleet-db-count">
          <span>Database Record:</span>
          <strong>${Number(d.total_records || d.count || 0).toLocaleString()}</strong>
        </div>
        <div class="fleet-time-row">
          <div>GPS: <strong>${d.ts}</strong></div>
          <div>Server: <strong>${d.created_at || d.ts}</strong> <small>(${rxTimeRel})</small></div>
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

// ================= TRAJECTORY / BREADCRUMB TRAIL =================
let trajectoryLayerGroup = null;
let isTrajectoryVisible = true;
let currentTrajectoryData = [];

function clearTrajectory() {
  if (trajectoryLayerGroup) {
    map.removeLayer(trajectoryLayerGroup);
    trajectoryLayerGroup = null;
  }
  currentTrajectoryData = [];
  const trailBadge = document.getElementById('insp-trail-count');
  if (trailBadge) {
    trailBadge.innerText = '🛣️ Jejak Dinonaktifkan';
  }
}

async function loadAndDrawTrajectory(src) {
  if (!src) return;

  if (trajectoryLayerGroup) {
    map.removeLayer(trajectoryLayerGroup);
    trajectoryLayerGroup = null;
  }

  const trailBadge = document.getElementById('insp-trail-count');

  if (!isTrajectoryVisible) {
    if (trailBadge) trailBadge.innerText = '🛣️ Jejak Dimatikan';
    return;
  }

  if (trailBadge) {
    trailBadge.innerText = '⏳ Mengambil jejak lintasan...';
  }

  try {
    const res = await fetch(`${API_BASE}/api/telemetry/${encodeURIComponent(src)}?limit=500`);
    if (!res.ok) throw new Error('Gagal memuat histori');
    const rawData = await res.json();

    // Filter valid coordinates & sort chronologically (oldest to newest)
    const points = rawData
      .filter(p => p.lat && p.lon && (p.lat !== 0 || p.lon !== 0))
      .sort((a, b) => (a.timestamp_sec || 0) - (b.timestamp_sec || 0));

    currentTrajectoryData = points;

    if (points.length === 0) {
      if (trailBadge) trailBadge.innerText = '🛣️ Belum ada histori jejak';
      return;
    }

    const latlngs = points.map(p => [p.lat, p.lon]);
    const group = L.featureGroup();

    // 1. Aura Glow Polyline
    const auraLine = L.polyline(latlngs, {
      color: '#0284c7',
      weight: 8,
      opacity: 0.3,
      lineCap: 'round',
      lineJoin: 'round'
    });
    group.addLayer(auraLine);

    // 2. Core Polyline
    const isExca = src.toUpperCase().startsWith('EXCA');
    const trailColor = isExca ? '#f59e0b' : '#38bdf8';
    const mainLine = L.polyline(latlngs, {
      color: trailColor,
      weight: 3.5,
      opacity: 0.95,
      dashArray: '8, 6',
      lineCap: 'round',
      lineJoin: 'round'
    });
    group.addLayer(mainLine);

    // 3. Start Point Flag (Titik Awal Lintasan)
    if (points.length > 1) {
      const startP = points[0];
      const startIcon = L.divIcon({
        className: 'custom-start-marker',
        html: `
          <div style="background:#10b981; width:20px; height:20px; border-radius:50%; border:2px solid #ffffff; box-shadow:0 0 8px rgba(16,185,129,0.9); display:flex; align-items:center; justify-content:center; font-size:10px; color:#fff;">
            🏁
          </div>
        `,
        iconSize: [20, 20],
        iconAnchor: [10, 10]
      });
      const startMarker = L.marker([startP.lat, startP.lon], { icon: startIcon })
        .bindPopup(`<strong>🏁 Titik Awal Rute</strong><br>Waktu: ${startP.ts || '—'}<br>Kecepatan: ${startP.spd} km/h`);
      group.addLayer(startMarker);
    }

    // 4. Sample Breadcrumb Dots along the track
    const step = Math.max(1, Math.floor(points.length / 25));
    for (let i = 0; i < points.length - 1; i += step) {
      const pt = points[i];
      const isStopped = pt.spd === 0;
      const dotIcon = L.divIcon({
        className: 'custom-breadcrumb-marker',
        html: `<div style="background:${isStopped ? '#ef4444' : trailColor}; width:7px; height:7px; border-radius:50%; border:1.5px solid #ffffff; box-shadow:0 0 3px rgba(0,0,0,0.6);"></div>`,
        iconSize: [7, 7],
        iconAnchor: [3.5, 3.5]
      });
      const dot = L.marker([pt.lat, pt.lon], { icon: dotIcon })
        .bindTooltip(`⏱️ ${pt.ts}<br>⚡ ${pt.spd} km/h ${pt.ign ? '(IGN ON)' : '(IGN OFF)'}`, {
          direction: 'top',
          offset: [0, -4]
        });
      group.addLayer(dot);
    }

    trajectoryLayerGroup = group;
    group.addTo(map);

    if (trailBadge) {
      const tStart = points[0].ts ? points[0].ts.slice(11, 16) : '';
      const tEnd = points[points.length - 1].ts ? points[points.length - 1].ts.slice(11, 16) : '';
      trailBadge.innerText = `🛣️ ${points.length.toLocaleString()} Titik Rute (${tStart} - ${tEnd})`;
    }

    // Smoothly fit map view to show full path
    if (latlngs.length > 1) {
      map.fitBounds(group.getBounds(), { padding: [60, 60], maxZoom: 16 });
    }
  } catch (err) {
    console.error('Error loading trajectory:', err);
    if (trailBadge) {
      trailBadge.innerText = '⚠️ Gagal memuat jejak';
    }
  }
}

// Select Device & Open Inspector
function selectDevice(src) {
  const dev = devicesData.find(d => d.src === src);
  if (!dev) return;

  selectedDevice = dev;
  renderFleetList();
  updateInspector(dev);

  // Switch to map view immediately on mobile
  if (window.innerWidth <= 768) {
    switchMobileView('map');
  }

  // Load and render trajectory polyline path
  loadAndDrawTrajectory(src);

  // Pan to marker
  if (markers[src]) {
    const latlng = markers[src].getLatLng();
    map.panTo(latlng, { animate: true });
    markers[src].openPopup();
  }
}

function updateInspector(d) {
  const inspector = document.getElementById('telemetry-inspector');
  if (inspector) inspector.style.display = 'block';

  const isOnline = isDeviceOnline(d);
  const isExca = d.src.toUpperCase().startsWith('EXCA');
  const iconEl = document.getElementById('insp-icon');
  if (iconEl) iconEl.innerText = isExca ? '🚜' : '🚛';
  const idEl = document.getElementById('insp-id') || document.getElementById('insp-unit-id');
  if (idEl) idEl.innerText = d.src;
  
  const statusBadge = document.getElementById('insp-status-badge') || document.getElementById('insp-status');
  if (statusBadge) {
    statusBadge.innerText = isOnline ? 'ONLINE' : 'OFFLINE';
    statusBadge.className = `fleet-badge ${isOnline ? 'online' : 'offline'}`;
  }

  const spdEl = document.getElementById('insp-spd');
  if (spdEl) spdEl.innerHTML = `${d.spd} <small>km/h</small>`;

  const ignEl = document.getElementById('insp-ign');
  if (ignEl) {
    ignEl.innerText = d.ign ? 'ON' : 'OFF';
    ignEl.className = `card-value badge-ign ${d.ign ? 'on' : 'off'}`;
  }

  const batEl = document.getElementById('insp-bat');
  if (batEl) batEl.innerHTML = `${d.bat} <small>V</small>`;

  // Parse PTO (Bit0 of input_status / in)
  let isPtoOn = false;
  const ptoVal = d.pto ?? d.raw_payload?.pto;
  if (ptoVal !== undefined && ptoVal !== null) {
    isPtoOn = (ptoVal === 1 || ptoVal === '1' || ptoVal === true);
  } else {
    const inVal = d.in ?? d.raw_payload?.in;
    if (typeof inVal === 'string') {
      isPtoOn = inVal.length > 2 ? inVal[0] === '1' : ((parseInt(inVal, 16) & 0x01) !== 0);
    } else if (typeof inVal === 'number') {
      isPtoOn = ((inVal & 0x01) !== 0);
    }
  }
  const ptoEl = document.getElementById('insp-pto');
  if (ptoEl) {
    ptoEl.innerText = isPtoOn ? 'ON' : 'OFF';
    ptoEl.className = `card-value badge-ign ${isPtoOn ? 'on' : 'off'}`;
  }

  const odoEl = document.getElementById('insp-odo');
  if (odoEl) {
    odoEl.innerHTML = `${Number(d.odo || 0).toLocaleString()} <small>m</small>`;
  }
  
  const hdg = d.hdg ?? 0;
  const directions = ['N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW'];
  const dir = directions[Math.round(hdg / 45) % 8];
  const hdgEl = document.getElementById('insp-hdg');
  if (hdgEl) hdgEl.innerHTML = `${hdg}° <small>(${dir})</small>`;

  // Total data di database
  const totalDbEl = document.getElementById('insp-total-db');
  if (totalDbEl) {
    totalDbEl.innerHTML = `${Number(d.total_records || d.count || 0).toLocaleString()} <small>record</small>`;
  }

  // IMEI Tracker
  const imeiEl = document.getElementById('insp-imei');
  if (imeiEl) {
    imeiEl.innerText = d.imei || '—';
  }

  // iButton Driver ID
  const ibuttonEl = document.getElementById('insp-ibutton');
  if (ibuttonEl) {
    if (d.ibutton) {
      const st = (d.ibutton_status || (d.raw_payload?.ibutton_login ? 'LOGIN' : 'LOGOUT')).toUpperCase();
      const isLogin = st === 'LOGIN';
      const stBg = isLogin ? 'rgba(16,185,129,0.15)' : 'rgba(245,158,11,0.15)';
      const stBorder = isLogin ? '#10b981' : '#f59e0b';
      const stColor = isLogin ? '#10b981' : '#f59e0b';
      ibuttonEl.innerHTML = `${d.ibutton} <span style="font-size: 0.68rem; background: ${stBg}; border: 1px solid ${stBorder}; color: ${stColor}; padding: 1px 5px; border-radius: 4px; vertical-align: middle; margin-left: 4px; font-weight: 800;">${st}</span>`;
      ibuttonEl.style.color = '#10b981';
      ibuttonEl.style.fontWeight = '700';
    } else {
      ibuttonEl.innerText = '—';
      ibuttonEl.style.color = '#94a3b8';
      ibuttonEl.style.fontWeight = '400';
    }
  }

  const latEl = document.getElementById('insp-lat');
  if (latEl) latEl.innerText = d.lat;
  const lonEl = document.getElementById('insp-lon');
  if (lonEl) lonEl.innerText = d.lon;

  // G-Sensor (X, Y, Z) & Inclinometer Tilt Gauge
  const gs = d.raw_payload?.gs || {};
  const gx = gs.x ?? d.gs_x ?? 0;
  const gy = gs.y ?? d.gs_y ?? 0;
  const gz = gs.z ?? d.gs_z ?? 0;

  const tiltInfo = calculateTilt(gx, gy, gz);

  const tiltBadge = document.getElementById('insp-tilt-badge');
  if (tiltBadge) {
    tiltBadge.innerText = tiltInfo.statusText;
    tiltBadge.className = `tilt-status-badge ${tiltInfo.status}`;
  }

  const tiltTotalEl = document.getElementById('insp-tilt-total');
  if (tiltTotalEl) {
    tiltTotalEl.innerHTML = `${tiltInfo.tilt}<small>°</small>`;
    tiltTotalEl.style.color = tiltInfo.color;
  }

  const tiltBarEl = document.getElementById('insp-tilt-bar');
  if (tiltBarEl) {
    tiltBarEl.style.width = `${tiltInfo.percent}%`;
    tiltBarEl.style.background = tiltInfo.color;
  }

  const tiltRollEl = document.getElementById('insp-tilt-roll');
  if (tiltRollEl) tiltRollEl.innerText = tiltInfo.rollText;

  const tiltPitchEl = document.getElementById('insp-tilt-pitch');
  if (tiltPitchEl) tiltPitchEl.innerText = tiltInfo.pitchText;

  const gsRawEl = document.getElementById('insp-gs-raw');
  if (gsRawEl) gsRawEl.innerText = `X:${gx}, Y:${gy}, Z:${gz} mg`;

  const gsEl = document.getElementById('insp-gs');
  if (gsEl) gsEl.innerText = `${gx}, ${gy}, ${gz}`;

  // Timestamp GPS
  const tsEl = document.getElementById('insp-ts');
  if (tsEl) tsEl.innerText = d.ts || '—';

  // Waktu Server Terima Data
  const rxEl = document.getElementById('insp-rx-time');
  const rxRelEl = document.getElementById('insp-rx-rel');
  if (rxEl) {
    const rxTime = d.created_at || d.ts;
    rxEl.innerText = rxTime || '—';
    if (rxRelEl) {
      const rel = formatRelativeTime(rxTime);
      rxRelEl.innerText = rel ? `(${rel})` : '';
      rxRelEl.className = `time-rel-badge ${isOnline ? 'online' : 'offline'}`;
    }
  }
}

// Event Listeners
const closeInspBtn = document.getElementById('close-inspector-btn');
if (closeInspBtn) {
  closeInspBtn.addEventListener('click', () => {
    const inspector = document.getElementById('telemetry-inspector');
    if (inspector) inspector.style.display = 'none';
    selectedDevice = null;
    clearTrajectory();
    renderFleetList();
  });
}

const trailToggleBtn = document.getElementById('btn-trail-toggle');
if (trailToggleBtn) {
  trailToggleBtn.addEventListener('click', () => {
    isTrajectoryVisible = !isTrajectoryVisible;
    if (isTrajectoryVisible) {
      trailToggleBtn.classList.add('active');
      if (selectedDevice) {
        loadAndDrawTrajectory(selectedDevice.src);
      }
    } else {
      trailToggleBtn.classList.remove('active');
      clearTrajectory();
    }
  });
}

const searchInput = document.getElementById('search-input');
if (searchInput) {
  searchInput.addEventListener('input', () => {
    renderFleetList();
  });
}

// Quick Filter Tabs Listener
document.querySelectorAll('.filter-tab').forEach(tab => {
  tab.addEventListener('click', () => {
    currentFleetFilter = tab.getAttribute('data-filter') || 'all';
    document.querySelectorAll('.filter-tab').forEach(t => t.classList.remove('active'));
    tab.classList.add('active');
    renderFleetList();
  });
});

// Map Layer Switcher Listeners
document.querySelectorAll('.layer-btn').forEach(btn => {
  btn.addEventListener('click', () => {
    const layer = btn.getAttribute('data-layer');
    if (layer) switchMapLayer(layer);
  });
});

// Fit Fleet Bounds Listener
const fitFleetBtn = document.getElementById('btn-fit-fleet');
if (fitFleetBtn) {
  fitFleetBtn.addEventListener('click', () => {
    fitFleetBounds();
  });
}

// Mobile & Tablet Sidebar Drawer Listeners
const toggleSidebarBtn = document.getElementById('btn-toggle-sidebar');
if (toggleSidebarBtn) {
  toggleSidebarBtn.addEventListener('click', () => {
    toggleSidebarDrawer();
  });
}

// Mobile Bottom Navigation Tabs Listener
document.querySelectorAll('.nav-tab-btn').forEach(btn => {
  btn.addEventListener('click', () => {
    const view = btn.getAttribute('data-view');
    if (view) switchMobileView(view);
  });
});

// Mobile Stats Action Buttons (inside #view-stats)
const mBtnOpenExport = document.getElementById('m-btn-open-export');
if (mBtnOpenExport) {
  mBtnOpenExport.addEventListener('click', () => {
    if (typeof openModal === 'function') openModal();
  });
}

const mBtnOpenDelete = document.getElementById('m-btn-open-delete');
if (mBtnOpenDelete) {
  mBtnOpenDelete.addEventListener('click', () => {
    if (typeof openDeleteModal === 'function') openDeleteModal();
  });
}

function triggerDownload(url) {
  const link = document.createElement('a');
  link.href = url;
  link.setAttribute('download', '');
  document.body.appendChild(link);
  link.click();
  document.body.removeChild(link);
}

// Quick Download from Inspector
const inspDlBtn = document.getElementById('insp-download-unit-btn');
if (inspDlBtn) {
  inspDlBtn.addEventListener('click', () => {
    if (!selectedDevice) return;
    const url = `${API_BASE}/api/export/csv?src=${encodeURIComponent(selectedDevice.src)}&limit=100000`;
    triggerDownload(url);
  });
}

// Export Modal Logic
const exportModal = document.getElementById('export-modal');
const openExportBtn = document.getElementById('open-export-modal-btn') || document.getElementById('export-db-btn');
const closeExportBtn = document.getElementById('close-export-modal-btn') || document.getElementById('close-modal-btn');
const cancelExportBtn = document.getElementById('cancel-export-btn');
const startDownloadBtn = document.getElementById('start-download-btn');
const unitSelect = document.getElementById('export-unit-select');

function closeModal() {
  if (exportModal) exportModal.style.display = 'none';
}

function populateExportUnits() {
  if (!unitSelect) return;
  unitSelect.innerHTML = '<option value="ALL">🌐 Semua Unit (Seluruh Database)</option>';
  devicesData.forEach(d => {
    const opt = document.createElement('option');
    opt.value = d.src;
    opt.innerText = `🚜 ${d.src} (${Number(d.total_records || d.count || 0).toLocaleString()} data)`;
    unitSelect.appendChild(opt);
  });
  if (selectedDevice) {
    unitSelect.value = selectedDevice.src;
  }
}

if (openExportBtn) {
  openExportBtn.addEventListener('click', () => {
    populateExportUnits();
    if (exportModal) exportModal.style.display = 'flex';
    const isDb = document.querySelector('input[name="export-format"]:checked')?.value === 'db';
    const limitGroup = document.getElementById('limit-group');
    const dateGroup = document.getElementById('date-range-group');
    if (limitGroup) limitGroup.style.display = isDb ? 'none' : 'flex';
    if (dateGroup) dateGroup.style.display = isDb ? 'none' : 'flex';
  });
}

if (closeExportBtn) {
  closeExportBtn.addEventListener('click', closeModal);
}

// Support any .close-btn inside modal
document.querySelectorAll('#export-modal .close-btn').forEach(btn => {
  btn.addEventListener('click', closeModal);
});

if (cancelExportBtn) {
  cancelExportBtn.addEventListener('click', closeModal);
}

if (exportModal) {
  exportModal.addEventListener('click', (e) => {
    if (e.target === exportModal) closeModal();
  });
}

window.addEventListener('keydown', (e) => {
  if (e.key === 'Escape' && exportModal && exportModal.style.display === 'flex') {
    closeModal();
  }
});

const startInput = document.getElementById('export-start-date');
const endInput = document.getElementById('export-end-date');

// Format Card Selection Styling
document.querySelectorAll('.format-card').forEach(card => {
  card.addEventListener('click', () => {
    document.querySelectorAll('.format-card').forEach(c => c.classList.remove('selected'));
    card.classList.add('selected');
    const radio = card.querySelector('input[type="radio"]');
    if (radio) radio.checked = true;

    // Toggle limit & date range visibility for .db format
    const limitGroup = document.getElementById('limit-group');
    const dateGroup = document.getElementById('date-range-group');
    const isDb = radio.value === 'db';
    if (limitGroup) limitGroup.style.display = isDb ? 'none' : 'flex';
    if (dateGroup) dateGroup.style.display = isDb ? 'none' : 'flex';
  });
});

if (startDownloadBtn) {
  startDownloadBtn.addEventListener('click', () => {
    const selectedFormat = document.querySelector('input[name="export-format"]:checked')?.value || 'db';
    const selectedUnit = unitSelect ? unitSelect.value : 'ALL';
    const limit = document.getElementById('export-limit')?.value || '50000';
    const startVal = startInput ? startInput.value : '';
    const endVal = endInput ? endInput.value : '';

    let downloadUrl = '';
    if (selectedFormat === 'db') {
      downloadUrl = `${API_BASE}/api/export/db`;
    } else {
      const params = new URLSearchParams();
      params.append('src', selectedUnit);
      params.append('limit', limit);
      if (startVal) params.append('start', startVal);
      if (endVal) params.append('end', endVal);

      if (selectedFormat === 'csv') {
        downloadUrl = `${API_BASE}/api/export/csv?${params.toString()}`;
      } else if (selectedFormat === 'json') {
        downloadUrl = `${API_BASE}/api/export/json?${params.toString()}`;
      }
    }

    if (downloadUrl) {
      triggerDownload(downloadUrl);
      if (exportModal) exportModal.style.display = 'none';
    }
  });
}

// ================= DELETE DATABASE MODAL LOGIC =================
const deleteModal = document.getElementById('delete-modal');
const openDeleteBtn = document.getElementById('open-delete-modal-btn');
const inspDeleteBtn = document.getElementById('insp-delete-unit-btn');
const closeDeleteBtn = document.getElementById('close-delete-modal-btn');
const cancelDeleteBtn = document.getElementById('cancel-delete-btn');
const confirmDeleteBtn = document.getElementById('confirm-delete-btn');
const deleteUnitSelect = document.getElementById('delete-unit-select');
const deletePasswordInput = document.getElementById('delete-password-input');
const deleteStatusMsg = document.getElementById('delete-status-msg');

function closeDeleteModal() {
  if (deleteModal) deleteModal.style.display = 'none';
  if (deletePasswordInput) deletePasswordInput.value = '';
  if (deleteStatusMsg) deleteStatusMsg.style.display = 'none';
}

function populateDeleteUnits() {
  if (!deleteUnitSelect) return;
  deleteUnitSelect.innerHTML = '<option value="ALL">🌐 Semua Unit (Seluruh Database)</option>';
  devicesData.forEach(d => {
    const opt = document.createElement('option');
    opt.value = d.src;
    opt.innerText = `🚜 ${d.src} (${Number(d.total_records || d.count || 0).toLocaleString()} data)`;
    deleteUnitSelect.appendChild(opt);
  });
}

function openDeleteDialog(preselectedUnit = 'ALL') {
  populateDeleteUnits();
  if (deleteUnitSelect) deleteUnitSelect.value = preselectedUnit;
  if (deletePasswordInput) deletePasswordInput.value = '';
  if (deleteStatusMsg) deleteStatusMsg.style.display = 'none';
  if (deleteModal) deleteModal.style.display = 'flex';
  setTimeout(() => {
    if (deletePasswordInput) deletePasswordInput.focus();
  }, 100);
}

if (openDeleteBtn) {
  openDeleteBtn.addEventListener('click', () => {
    openDeleteDialog(selectedDevice ? selectedDevice.src : 'ALL');
  });
}

if (inspDeleteBtn) {
  inspDeleteBtn.addEventListener('click', () => {
    if (selectedDevice) {
      openDeleteDialog(selectedDevice.src);
    }
  });
}

if (closeDeleteBtn) {
  closeDeleteBtn.addEventListener('click', closeDeleteModal);
}

if (cancelDeleteBtn) {
  cancelDeleteBtn.addEventListener('click', closeDeleteModal);
}

if (deleteModal) {
  deleteModal.addEventListener('click', (e) => {
    if (e.target === deleteModal) closeDeleteModal();
  });
}

window.addEventListener('keydown', (e) => {
  if (e.key === 'Escape' && deleteModal && deleteModal.style.display === 'flex') {
    closeDeleteModal();
  }
});

if (deletePasswordInput) {
  deletePasswordInput.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') {
      e.preventDefault();
      if (confirmDeleteBtn) confirmDeleteBtn.click();
    }
  });
}

if (confirmDeleteBtn) {
  confirmDeleteBtn.addEventListener('click', async () => {
    const targetUnit = deleteUnitSelect ? deleteUnitSelect.value : 'ALL';
    const password = deletePasswordInput ? deletePasswordInput.value.trim() : '';

    if (!password) {
      if (deleteStatusMsg) {
        deleteStatusMsg.className = 'error-banner';
        deleteStatusMsg.innerText = '⚠️ Silakan masukkan password admin.';
        deleteStatusMsg.style.display = 'block';
      }
      if (deletePasswordInput) deletePasswordInput.focus();
      return;
    }

    // Prepare UI for request
    confirmDeleteBtn.disabled = true;
    confirmDeleteBtn.innerText = '⏳ Menghapus...';
    if (deleteStatusMsg) {
      deleteStatusMsg.style.display = 'none';
    }

    try {
      const resp = await fetch(`${API_BASE}/api/admin/delete_db`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ src: targetUnit, password: password })
      });

      const res = await resp.json();

      if (resp.ok && res.success) {
        if (deleteStatusMsg) {
          deleteStatusMsg.className = 'success-banner';
          deleteStatusMsg.innerText = `✅ ${res.message}`;
          deleteStatusMsg.style.display = 'block';
        }

        // Live refresh app
        await fetchDevices();
        await fetchStats();

        // Close inspector if current device was deleted
        if (targetUnit === 'ALL' || (selectedDevice && selectedDevice.src === targetUnit)) {
          const insp = document.getElementById('telemetry-inspector');
          if (insp) insp.style.display = 'none';
          selectedDevice = null;
          renderFleetList();
        }

        setTimeout(() => {
          closeDeleteModal();
          confirmDeleteBtn.disabled = false;
          confirmDeleteBtn.innerText = '🗑️ Hapus Data Sekarang';
        }, 1200);
      } else {
        if (deleteStatusMsg) {
          deleteStatusMsg.className = 'error-banner';
          deleteStatusMsg.innerText = `❌ ${res.error || 'Password admin salah!'}`;
          deleteStatusMsg.style.display = 'block';
        }
        confirmDeleteBtn.disabled = false;
        confirmDeleteBtn.innerText = '🗑️ Hapus Data Sekarang';
        if (deletePasswordInput) {
          deletePasswordInput.value = '';
          deletePasswordInput.focus();
        }
      }
    } catch (err) {
      if (deleteStatusMsg) {
        deleteStatusMsg.className = 'error-banner';
        deleteStatusMsg.innerText = `❌ Gagal menghubungi server: ${err.message}`;
        deleteStatusMsg.style.display = 'block';
      }
      confirmDeleteBtn.disabled = false;
      confirmDeleteBtn.innerText = '🗑️ Hapus Data Sekarang';
    }
  });
}

// App Startup
window.addEventListener('DOMContentLoaded', () => {
  initMap();
  switchMobileView('map');
  fetchDevices();
  fetchStats();

  setInterval(fetchDevices, 2500);
  setInterval(fetchStats, 5000);
});
