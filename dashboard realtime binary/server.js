/**
 * Ultra-lightweight Realtime Fleet Tracker Server
 * - Connects to MQTT (binary & json) as a pure observer (NO ACK reply)
 * - Keeps latest state of every DT and EXCA in memory & stores to local cache
 * - Broadcasts updates instantly to browser clients via WebSocket
 * - Serves frontend web UI with Satellite Map
 */

const http = require('http');
const fs = require('fs');
const path = require('path');
const mqtt = require('mqtt');
const { WebSocketServer } = require('ws');
const { parseBinaryPayload } = require('./binary_parser');

// Configuration (can be overridden with env variables)
const HTTP_PORT = parseInt(process.env.PORT || '3000', 10);
const MQTT_BROKER = process.env.MQTT_BROKER || 'mqtt://34.101.180.48:1883';
const MQTT_USER = process.env.MQTT_USER || 'kutai';
const MQTT_PASS = process.env.MQTT_PASS || '79750d76450466d56b9f44926f38614a3846bdbf';

const TOPIC_BINARY = process.env.MQTT_BINARY_TOPIC || 'kutai/fleet/binary';
const TOPIC_JSON = process.env.MQTT_JSON_TOPIC || 'kutai/fleet/data';

const CACHE_FILE = path.join(__dirname, 'latest_fleet_state.json');

// In-Memory Fleet State: Map(src -> latestTelemetryData)
const fleetState = new Map();

// Load cached state on startup so markers remain if server restarts
try {
  if (fs.existsSync(CACHE_FILE)) {
    const raw = fs.readFileSync(CACHE_FILE, 'utf8');
    const saved = JSON.parse(raw);
    for (const [k, v] of Object.entries(saved)) {
      fleetState.set(k, v);
    }
    console.log(`[Cache] Loaded ${fleetState.size} cached vehicle states from ${CACHE_FILE}`);
  }
} catch (e) {
  console.warn('[Cache] Could not load state cache:', e.message);
}

// Periodic auto-save cache every 30 seconds
setInterval(() => {
  try {
    const obj = {};
    for (const [k, v] of fleetState.entries()) {
      obj[k] = v;
    }
    fs.writeFileSync(CACHE_FILE, JSON.stringify(obj, null, 2), 'utf8');
  } catch (e) {
    // ignore
  }
}, 30000);

// ================= HTTP SERVER (Static files) =================
const MIME_TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'application/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.png': 'image/png',
  '.svg': 'image/svg+xml',
  '.ico': 'image/x-icon'
};

const server = http.createServer((req, res) => {
  let reqUrl = req.url.split('?')[0];

  // API endpoint for direct fetch
  if (reqUrl === '/api/fleet') {
    res.writeHead(200, {
      'Content-Type': 'application/json',
      'Access-Control-Allow-Origin': '*'
    });
    const fleetList = Array.from(fleetState.values());
    res.end(JSON.stringify(fleetList));
    return;
  }

  // Static files in public/
  let filePath = path.join(__dirname, 'public', reqUrl === '/' ? 'index.html' : reqUrl);
  const ext = path.extname(filePath).toLowerCase();

  fs.readFile(filePath, (err, content) => {
    if (err) {
      if (err.code === 'ENOENT') {
        res.writeHead(404, { 'Content-Type': 'text/plain' });
        res.end('404 Not Found');
      } else {
        res.writeHead(500, { 'Content-Type': 'text/plain' });
        res.end('500 Internal Server Error');
      }
    } else {
      res.writeHead(200, { 'Content-Type': MIME_TYPES[ext] || 'application/octet-stream' });
      res.end(content);
    }
  });
});

// ================= WEBSOCKET SERVER =================
const wss = new WebSocketServer({ server });

function broadcast(data) {
  const msg = JSON.stringify(data);
  for (const client of wss.clients) {
    if (client.readyState === 1 /* OPEN */) {
      client.send(msg);
    }
  }
}

wss.on('connection', (ws) => {
  console.log(`[WS] Client connected. Active clients: ${wss.clients.size}`);

  // Send initial full fleet state to the newly connected browser
  const initList = Array.from(fleetState.values());
  ws.send(JSON.stringify({ type: 'init', data: initList }));

  ws.on('close', () => {
    console.log(`[WS] Client disconnected. Active clients: ${wss.clients.size}`);
  });
});

// ================= INGESTION & FLEET UPDATE =================
function handleTelemetryUpdate(records) {
  if (!records || records.length === 0) return;

  const updatedItems = [];
  for (const rec of records) {
    if (!rec.src || !rec.lat || !rec.lon) continue;
    fleetState.set(rec.src, rec);
    updatedItems.push(rec);
  }

  if (updatedItems.length > 0) {
    // Broadcast updates to all connected browser dashboards
    broadcast({
      type: 'update',
      data: updatedItems
    });
  }
}

// ================= MQTT OBSERVER CLIENT =================
console.log(`[MQTT] Connecting to ${MQTT_BROKER}...`);

const mqttClient = mqtt.connect(MQTT_BROKER, {
  username: MQTT_USER,
  password: MQTT_PASS,
  clientId: 'dashboard_observer_' + Math.random().toString(16).substring(2, 8),
  clean: true,
  reconnectPeriod: 3000
});

mqttClient.on('connect', () => {
  console.log(`[MQTT] Connected successfully to ${MQTT_BROKER}`);
  mqttClient.subscribe([TOPIC_BINARY, TOPIC_JSON], (err) => {
    if (err) {
      console.error('[MQTT] Subscribe error:', err);
    } else {
      console.log(`[MQTT] Subscribed to [${TOPIC_BINARY}] and [${TOPIC_JSON}] (Observer Mode - No ACK sent)`);
    }
  });
});

mqttClient.on('error', (err) => {
  console.error('[MQTT] Connection error:', err.message);
});

mqttClient.on('message', (topic, payload) => {
  try {
    if (topic === TOPIC_BINARY) {
      const records = parseBinaryPayload(payload);
      if (records.length > 0) {
        handleTelemetryUpdate(records);
        const last = records[records.length - 1];
        console.log(`[MQTT Binary] Ingested ${records.length} packet(s). Latest: ${last.src} (${last.lat}, ${last.lon}) Spd:${last.spd}km/h Bat:${last.bat}V`);
      }
    } else if (topic === TOPIC_JSON) {
      // Legacy JSON parsing fallback
      const text = payload.toString('utf8');
      const data = JSON.parse(text);
      const src = data.src || data.source || 'UNKNOWN';
      const lat = parseFloat(data.lat || data.latitude || 0);
      const lon = parseFloat(data.lon || data.longitude || 0);

      if (lat !== 0 && lon !== 0) {
        const item = {
          id: data.id || `${src}-${Date.now()}`,
          src: src,
          type: src.toUpperCase().startsWith('EXCA') ? 'EXCA' : 'DT',
          lat: lat,
          lon: lon,
          spd: parseFloat(data.spd || data.speed || 0),
          hdg: parseInt(data.hdg || data.heading || 0, 10),
          bat: parseFloat(data.bat || data.external || 0),
          ign: parseInt(data.ign !== undefined ? data.ign : 0, 10),
          ts: data.ts || new Date().toISOString().replace('T', ' ').substring(0, 19),
          updated_at: Date.now()
        };
        handleTelemetryUpdate([item]);
        console.log(`[MQTT JSON] Ingested: ${item.src} (${item.lat}, ${item.lon}) Spd:${item.spd}km/h`);
      }
    }
  } catch (err) {
    console.error('[MQTT] Error processing packet:', err.message);
  }
});

// Start Server
server.listen(HTTP_PORT, () => {
  console.log(`\n======================================================`);
  console.log(`🚀 Fleet Realtime Dashboard is running on:`);
  console.log(`   👉 http://localhost:${HTTP_PORT}`);
  console.log(`======================================================\n`);
});
