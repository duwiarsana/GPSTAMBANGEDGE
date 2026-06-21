#!/usr/bin/env python3
import os
import json
import sqlite3
import logging
import threading
import time
from datetime import datetime
from flask import Flask, jsonify, request
from flask_cors import CORS
import paho.mqtt.client as mqtt

# Configure Logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] %(message)s',
    handlers=[logging.StreamHandler()]
)
logger = logging.getLogger("DashboardBackend")

DB_PATH = os.environ.get("DB_PATH", "/opt/kutai-dashboard-backend/telemetry.db")
MQTT_HOST = os.environ.get("MQTT_HOST", "127.0.0.1")
MQTT_PORT = int(os.environ.get("MQTT_PORT", 1883))
MQTT_TOPIC = os.environ.get("MQTT_TOPIC", "kutai/fleet/data")
PORT = int(os.environ.get("PORT", 5000))

# Initialize Database
def init_db():
    os.makedirs(os.path.dirname(DB_PATH), exist_ok=True)
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    cursor.execute("""
        CREATE TABLE IF NOT EXISTS telemetry (
            id TEXT PRIMARY KEY,
            src TEXT,
            ts TEXT,
            lat REAL,
            lon REAL,
            spd REAL,
            bat REAL,
            ign INTEGER,
            raw_payload TEXT,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        )
    """)
    cursor.execute("CREATE INDEX IF NOT EXISTS idx_telemetry_src_ts ON telemetry(src, ts)")
    cursor.execute("CREATE INDEX IF NOT EXISTS idx_telemetry_created_at ON telemetry(created_at)")
    conn.commit()
    conn.close()
    logger.info(f"Database initialized at: {DB_PATH}")

# Global lock for SQLite writes (just to be safe)
db_lock = threading.Lock()
last_cleanup_time = 0

def clean_old_records():
    global last_cleanup_time
    now = time.time()
    # Run cleanup at most once every hour
    if now - last_cleanup_time < 3600:
        return
    
    with db_lock:
        try:
            conn = sqlite3.connect(DB_PATH)
            cursor = conn.cursor()
            # Delete records older than 30 days
            cursor.execute("DELETE FROM telemetry WHERE created_at < datetime('now', '-30 days')")
            deleted = cursor.rowcount
            conn.commit()
            conn.close()
            last_cleanup_time = now
            if deleted > 0:
                logger.info(f"🧹 Cleaned up {deleted} telemetry records older than 30 days.")
        except Exception as e:
            logger.error(f"❌ Failed to run database cleanup: {e}")

def save_telemetry(data, raw_payload):
    # Data extraction
    msg_id = data.get("id") or data.get("msg_id")
    if not msg_id:
        return False
        
    src = data.get("src") or data.get("source") or "UNKNOWN"
    if src == "UNKNOWN" and msg_id:
        parts = msg_id.split('-')
        if len(parts) > 0 and parts[0].strip() != '':
            src = parts[0]
            
    ts = data.get("ts") or data.get("timestamp") or datetime.utcnow().isoformat()
    try:
        lat = float(data.get("lat") or data.get("latitude") or 0.0)
        lon = float(data.get("lon") or data.get("longitude") or 0.0)
        spd = float(data.get("spd") or data.get("speed") or 0.0)
        bat = float(data.get("bat") or data.get("external") or 0.0)
    except (ValueError, TypeError):
        lat, lon, spd, bat = 0.0, 0.0, 0.0, 0.0
        
    ign = data.get("ign")
    if ign is None:
        ign = data.get("ignition", -1)
    try:
        ign = int(ign)
    except (ValueError, TypeError):
        ign = -1

    with db_lock:
        try:
            conn = sqlite3.connect(DB_PATH)
            cursor = conn.cursor()
            cursor.execute("""
                INSERT OR REPLACE INTO telemetry (id, src, ts, lat, lon, spd, bat, ign, raw_payload)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
            """, (msg_id, src, ts, lat, lon, spd, bat, ign, raw_payload))
            conn.commit()
            conn.close()
            logger.info(f"💾 Telemetry saved: {src} [{msg_id}]")
            return True
        except Exception as e:
            logger.error(f"❌ Failed to save telemetry to DB: {e}")
            return False

# MQTT callbacks
def on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        logger.info(f"✅ Connected to MQTT Broker ({MQTT_HOST}:{MQTT_PORT}) successfully.")
        client.subscribe(MQTT_TOPIC)
        logger.info(f"Subscribed to topic: {MQTT_TOPIC}")
    else:
        logger.error(f"❌ Connection to MQTT Broker failed with code: {rc}")

def on_message(client, userdata, msg):
    try:
        payload_str = msg.payload.decode('utf-8')
        data = json.loads(payload_str)
        save_telemetry(data, payload_str)
        clean_old_records()
    except json.JSONDecodeError:
        logger.warning("⚠️ Non-JSON payload received.")
    except Exception as e:
        logger.error(f"❌ Error processing message: {e}")

# Flask Web Server
app = Flask(__name__)
CORS(app)

@app.route("/api/devices", methods=["GET"])
def get_devices():
    try:
        conn = sqlite3.connect(DB_PATH)
        cursor = conn.cursor()
        # Find latest row for each unique src
        cursor.execute("""
            SELECT t.* FROM telemetry t
            INNER JOIN (
                SELECT src, MAX(ts) as max_ts FROM telemetry GROUP BY src
            ) tm ON t.src = tm.src AND t.ts = tm.max_ts
        """)
        rows = cursor.fetchall()
        conn.close()
        
        devices = []
        for r in rows:
            devices.append({
                "id": r[0],
                "src": r[1],
                "ts": r[2],
                "lat": r[3],
                "lon": r[4],
                "spd": r[5],
                "bat": r[6],
                "ign": r[7],
                "raw_payload": json.loads(r[8]) if r[8] else None,
                "created_at": r[9]
            })
        return jsonify(devices)
    except Exception as e:
        logger.error(f"Error serving /api/devices: {e}")
        return jsonify({"error": str(e)}), 500

@app.route("/api/recent", methods=["GET"])
def get_recent():
    limit = request.args.get("limit", 100, type=int)
    try:
        conn = sqlite3.connect(DB_PATH)
        cursor = conn.cursor()
        cursor.execute("""
            SELECT id, src, ts, lat, lon, spd, bat, ign, raw_payload, created_at
            FROM telemetry
            ORDER BY ts DESC
            LIMIT ?
        """, (limit,))
        rows = cursor.fetchall()
        conn.close()
        
        recent = []
        for r in rows:
            recent.append({
                "id": r[0],
                "src": r[1],
                "ts": r[2],
                "lat": r[3],
                "lon": r[4],
                "spd": r[5],
                "bat": r[6],
                "ign": r[7],
                "raw_payload": json.loads(r[8]) if r[8] else None,
                "created_at": r[9]
            })
        return jsonify(recent)
    except Exception as e:
        logger.error(f"Error serving /api/recent: {e}")
        return jsonify({"error": str(e)}), 500

@app.route("/api/history", methods=["GET"])
def get_history():
    src = request.args.get("src")
    days = request.args.get("days", 30, type=int)
    if not src:
        return jsonify({"error": "Missing required parameter 'src'"}), 400
        
    try:
        conn = sqlite3.connect(DB_PATH)
        cursor = conn.cursor()
        cursor.execute("""
            SELECT id, src, ts, lat, lon, spd, bat, ign, created_at
            FROM telemetry
            WHERE src = ? AND created_at >= datetime('now', ?)
            ORDER BY ts ASC
        """, (src, f"-{days} days"))
        rows = cursor.fetchall()
        conn.close()
        
        history = []
        for r in rows:
            # Skip invalid coordinates
            if r[3] == 0.0 and r[4] == 0.0:
                continue
            history.append({
                "id": r[0],
                "src": r[1],
                "ts": r[2],
                "lat": r[3],
                "lon": r[4],
                "spd": r[5],
                "bat": r[6],
                "ign": r[7],
                "created_at": r[8]
            })
        return jsonify(history)
    except Exception as e:
        logger.error(f"Error serving /api/history: {e}")
        return jsonify({"error": str(e)}), 500

@app.route("/api/stats", methods=["GET"])
def get_device_stats():
    # Define expected devices matching database naming (e.g. DT01-DT08, DT010-DT016)
    expected_devices = []
    for i in range(1, 17):
        expected_devices.append(f"DT0{i}")
    for i in range(1, 4):
        expected_devices.append(f"EXCA{i:02d}")
        
    try:
        conn = sqlite3.connect(DB_PATH)
        cursor = conn.cursor()
        cursor.execute("SELECT src, COUNT(*), MAX(ts) FROM telemetry GROUP BY src")
        rows = cursor.fetchall()
        conn.close()
        
        counts = {r[0]: (r[1], r[2]) for r in rows}
        
        stats = []
        # First add expected devices
        for device in expected_devices:
            val = counts.pop(device, (0, None))
            stats.append({
                "src": device,
                "count": val[0],
                "last_ts": val[1]
            })
            
        # Then add any other unexpected devices that have records in the database
        for device, val in sorted(counts.items()):
            stats.append({
                "src": device,
                "count": val[0],
                "last_ts": val[1]
            })
            
        return jsonify(stats)
    except Exception as e:
        logger.error(f"Error serving /api/stats: {e}")
        return jsonify({"error": str(e)}), 500

@app.route("/api/clear", methods=["POST"])
def clear_device_data():
    try:
        req_data = request.get_json() or {}
        src = req_data.get("src")
        if not src:
            return jsonify({"error": "Missing required parameter 'src'"}), 400
            
        with db_lock:
            conn = sqlite3.connect(DB_PATH)
            cursor = conn.cursor()
            cursor.execute("DELETE FROM telemetry WHERE src = ?", (src,))
            deleted = cursor.rowcount
            conn.commit()
            conn.close()
            
        logger.info(f"🧹 Cleaned database telemetry for device: {src}. Deleted {deleted} rows.")
        return jsonify({"success": True, "deleted_rows": deleted})
    except Exception as e:
        logger.error(f"Error serving /api/clear: {e}")
        return jsonify({"error": str(e)}), 500

def run_mqtt():
    logger.info("Starting MQTT thread...")
    client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2)
    client.on_connect = on_connect
    client.on_message = on_message
    client.reconnect_delay_set(min_delay=1, max_delay=60)
    
    while True:
        try:
            client.connect(MQTT_HOST, MQTT_PORT, keepalive=60)
            client.loop_forever()
        except Exception as e:
            logger.error(f"MQTT Loop error: {e}. Retrying in 5 seconds...")
            time.sleep(5)

if __name__ == "__main__":
    init_db()
    
    # Start MQTT subscriber thread
    mqtt_thread = threading.Thread(target=run_mqtt, daemon=True)
    mqtt_thread.start()
    
    # Start Flask server
    logger.info(f"Starting Flask server on port {PORT}...")
    app.run(host="0.0.0.0", port=PORT)
