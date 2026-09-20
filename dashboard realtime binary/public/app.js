/**
 * Realtime Fleet Tracker Frontend Logic
 * - Satellite Imagery Map with Leaflet
 * - WebSocket connection for live telemetry updates
 * - Smooth marker positioning and heading rotation
 * - In-memory Last Known Position tracking
 */

// State
const vehicles = new Map(); // src -> telemetry data
const markers = new Map();  // src -> L.marker
let map = null;
let activeBaseLayer = null;
let selectedSrc = null;

// Tile Layers (High Resolution Satellite Imagery)
const tileLayers = {
  satellite: L.tileLayer('https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}', {
    maxZoom: 19,
    attribution: 'Tiles &copy; Esri &mdash; Source: Esri, i-cubed, USDA, USGS, AEX, GeoEye, Getmapping, Aerogrid, IGN, IGP, UPR-EGP, and the GIS User Community'
  }),
  topo: L.tileLayer('https://{s}.tile.opentopomap.org/{z}/{x}/{y}.png', {
    maxZoom: 17,
    attribution: 'Map data: &copy; OpenStreetMap contributors, SRTM | Map style: &copy; OpenTopoMap (CC-BY-SA)'
  }),
  osm: L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
    maxZoom: 19,
    attribution: '&copy; OpenStreetMap contributors'
  })
};

// Initialize Leaflet Map (Default around East Kalimantan mining region -0.8, 117.1)
function initMap() {
  map = L.map('map-view', {
    zoomControl: false,
    layers: [tileLayers.satellite]
  }).setView([-0.73888, 117.13015], 13);

  activeBaseLayer = tileLayers.satellite;

  // Add zoom control at top-right
  L.control.zoom({ position: 'topright' }).addTo(map);

  // Basemap switch listeners
  document.querySelectorAll('input[name="basemap"]').forEach(radio => {
    radio.addEventListener('change', (e) => {
      const selected = e.target.value;
      if (tileLayers[selected]) {
        map.removeLayer(activeBaseLayer);
        tileLayers[selected].addTo(map);
        activeBaseLayer = tileLayers[selected];
      }
    });
  });

  // Recenter Button
  document.getElementById('btn-recenter').addEventListener('click', fitAllMarkers);
}

// Custom Marker HTML Generator
function createVehicleIcon(data) {
  const isExca = data.src.toUpperCase().startsWith('EXCA');
  const typeClass = isExca ? 'exca' : 'dt';
  const iconEmoji = isExca ? '🚜' : '🚚';
  const ptoClass = data.pto ? 'pto-active' : '';
  const heading = data.hdg || 0;

  const html = `
    <div class="custom-vehicle-marker" id="marker-${data.src}">
      <div class="marker-label">${data.src}</div>
      <div class="marker-pin ${typeClass} ${ptoClass}" style="transform: rotate(${heading}deg);">
        <span class="marker-icon">${iconEmoji}</span>
      </div>
    </div>
  `;

  return L.divIcon({
    className: 'leaflet-vehicle-container',
    html: html,
    iconSize: [38, 38],
    iconAnchor: [19, 19],
    popupAnchor: [0, -22]
  });
}

function getPopupContent(data) {
  const isExca = data.src.toUpperCase().startsWith('EXCA');
  return `
    <div style="font-family: 'Plus Jakarta Sans', sans-serif; font-size: 12px; color: #111; min-width: 180px;">
      <div style="font-weight: 800; font-size: 14px; margin-bottom: 6px; color: ${isExca ? '#d97706' : '#0284c7'};">
        ${isExca ? '🚜 Excavator' : '🚚 Dump Truck'} ${data.src}
      </div>
      <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 4px; margin-bottom: 6px; background: #f1f5f9; padding: 6px; border-radius: 4px;">
        <div><strong>Speed:</strong> ${data.spd ?? 0} km/h</div>
        <div><strong>Heading:</strong> ${data.hdg ?? 0}&deg;</div>
        <div><strong>Battery:</strong> ${data.bat ?? 0} V</div>
        <div><strong>Altitude:</strong> ${data.alt ?? 0} m</div>
        <div><strong>Ignition:</strong> ${data.ign ? '<span style="color:green;font-weight:bold;">ON</span>' : '<span style="color:gray;">OFF</span>'}</div>
        <div><strong>PTO/Bak:</strong> ${data.pto ? '<span style="color:red;font-weight:bold;">DUMP</span>' : 'DOWN'}</div>
      </div>
      <div style="font-size: 10px; color: #64748b;">
        <strong>Koordinat:</strong> ${data.lat}, ${data.lon}<br>
        <strong>Last Time:</strong> ${data.ts || '-'}<br>
        ${data.beacon_mac ? `<strong>Beacon:</strong> ${data.beacon_mac} (${data.beacon_rssi} dBm)<br>` : ''}
        ${data.ibutton ? `<strong>Driver ID:</strong> ${data.ibutton}` : ''}
      </div>
    </div>
  `;
}

// Update or Create Marker
function updateVehicleMarker(data) {
  if (!data.lat || !data.lon) return;

  const latLng = [data.lat, data.lon];

  if (markers.has(data.src)) {
    const marker = markers.get(data.src);
    marker.setLatLng(latLng);
    marker.setIcon(createVehicleIcon(data));
    marker.setPopupContent(getPopupContent(data));
  } else {
    const marker = L.marker(latLng, {
      icon: createVehicleIcon(data),
      title: data.src
    });
    marker.bindPopup(getPopupContent(data));
    marker.on('click', () => {
      selectVehicle(data.src);
    });
    marker.addTo(map);
    markers.set(data.src, marker);
  }
}

// Fit map bounds to show all active vehicles
function fitAllMarkers() {
  if (markers.size === 0) return;
  const group = L.featureGroup(Array.from(markers.values()));
  map.fitBounds(group.getBounds().pad(0.2));
}

// Focus on a specific vehicle
function selectVehicle(src) {
  selectedSrc = src;
  const data = vehicles.get(src);
  if (data && markers.has(src)) {
    map.setView([data.lat, data.lon], Math.max(map.getZoom(), 16), { animate: true });
    markers.get(src).openPopup();
  }

  // Highlight active in vehicle list
  document.querySelectorAll('.vehicle-card').forEach(c => c.classList.remove('active'));
  const card = document.getElementById(`card-${src}`);
  if (card) {
    card.classList.add('active');
    card.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
  }
}

// Render Sidebar List
function renderVehicleList() {
  const container = document.getElementById('vehicle-list-container');
  const filter = (document.getElementById('filter-input').value || '').toLowerCase().trim();

  const list = Array.from(vehicles.values())
    .filter(v => !filter || v.src.toLowerCase().includes(filter))
    .sort((a, b) => (b.updated_at || 0) - (a.updated_at || 0));

  if (list.length === 0) {
    container.innerHTML = `
      <div class="empty-state">
        <p>Tidak ada unit yang cocok dengan pencarian.</p>
      </div>
    `;
    return;
  }

  let dtCount = 0;
  let excaCount = 0;

  vehicles.forEach(v => {
    if (v.src.toUpperCase().startsWith('EXCA')) excaCount++;
    else dtCount++;
  });

  document.getElementById('count-dt').textContent = dtCount;
  document.getElementById('count-exca').textContent = excaCount;
  document.getElementById('total-units').textContent = `${vehicles.size} Units`;

  let html = '';
  for (const v of list) {
    const isExca = v.src.toUpperCase().startsWith('EXCA');
    const typeLabel = isExca ? 'EXCA' : 'DT';
    const tagClass = isExca ? 'exca' : 'dt';
    const isSelected = selectedSrc === v.src ? 'active' : '';

    html += `
      <div class="vehicle-card ${isSelected}" id="card-${v.src}" onclick="selectVehicle('${v.src}')">
        <div class="card-top">
          <div class="unit-badge">
            <span class="unit-tag ${tagClass}">${typeLabel}</span>
            <span>${v.src}</span>
          </div>
          ${v.pto ? '<span class="pto-badge">⚠️ DUMPING</span>' : ''}
        </div>

        <div class="card-metrics">
          <div class="metric-col">
            <div class="label">SPEED</div>
            <div class="val">${v.spd ?? 0} <span style="font-size:9px;">km/h</span></div>
          </div>
          <div class="metric-col">
            <div class="label">BATTERY</div>
            <div class="val">${v.bat ?? 0} <span style="font-size:9px;">V</span></div>
          </div>
          <div class="metric-col">
            <div class="label">ALT</div>
            <div class="val">${v.alt ?? 0} <span style="font-size:9px;">m</span></div>
          </div>
        </div>

        <div class="card-footer">
          <span>🕒 ${v.ts ? v.ts.split(' ')[1] || v.ts : '-'}</span>
          <span>${v.ign ? '🟢 IGN ON' : '⚪ IGN OFF'}</span>
        </div>
      </div>
    `;
  }

  container.innerHTML = html;
}

// WebSocket Connection Management
function connectWebSocket() {
  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  const wsUrl = `${protocol}//${window.location.host}`;
  const ws = new WebSocket(wsUrl);

  const indicator = document.getElementById('ws-indicator');
  const statusText = document.getElementById('ws-text');

  ws.onopen = () => {
    indicator.className = 'dot online';
    statusText.textContent = 'Live MQTT Stream';
  };

  ws.onmessage = (event) => {
    try {
      const msg = JSON.parse(event.data);
      if (msg.type === 'init' || msg.type === 'update') {
        const records = Array.isArray(msg.data) ? msg.data : [msg.data];
        let hasNewCoords = false;

        for (const item of records) {
          if (!item || !item.src) continue;
          vehicles.set(item.src, item);
          updateVehicleMarker(item);
          hasNewCoords = true;
        }

        renderVehicleList();

        // If it's initial load and we have markers, auto-fit map view
        if (msg.type === 'init' && hasNewCoords && markers.size > 0) {
          fitAllMarkers();
        }
      }
    } catch (e) {
      console.error('[WS Parse Error]', e);
    }
  };

  ws.onclose = () => {
    indicator.className = 'dot offline';
    statusText.textContent = 'Disconnected (Reconnecting...)';
    setTimeout(connectWebSocket, 3000);
  };

  ws.onerror = (err) => {
    console.error('[WS Error]', err);
    ws.close();
  };
}

// Filter Event
document.getElementById('filter-input').addEventListener('input', () => {
  renderVehicleList();
});

// App Startup
window.addEventListener('DOMContentLoaded', () => {
  initMap();
  connectWebSocket();
});
