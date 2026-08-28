import json
import logging
import os
import sqlite3
import threading
import time
from datetime import datetime, timezone
from flask import Flask, jsonify, request, send_from_directory
from flask_cors import CORS
import paho.mqtt.client as mqtt

from binary_parser import parse_telemetry_packet

# Configuration
MQTT_HOST = os.environ.get("MQTT_HOST", "76.13.19.250")
MQTT_PORT = int(os.environ.get("MQTT_PORT", 1883))
MQTT_BINARY_TOPIC = "kutai/fleet/binary"
MQTT_JSON_TOPIC = "kutai/fleet/data"
DB_PATH = os.environ.get("DB_PATH", "telemetry_binary.db")
LOG_PATH = "backend_binary.log"
PORT = int(os.environ.get("PORT", 5001))
MAX_RECORDS_PER_DEVICE = 20000

# Setup Logging
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    handlers=[
        logging.FileHandler(LOG_PATH),
        logging.StreamHandler()
    ]
)
logger = logging.getLogger("BinaryBackend")

db_lock = threading.Lock()

def get_db_connection():
    conn = sqlite3.connect(DB_PATH, timeout=15)
    conn.row_factory = sqlite3.Row
    return conn

def init_db():
    with db_lock:
        conn = get_db_connection()
        cursor = conn.cursor()
        cursor.execute("""
            CREATE TABLE IF NOT EXISTS telemetry (
                id TEXT PRIMARY KEY,
                src TEXT NOT NULL,
                seq INTEGER,
                ts TEXT NOT NULL,
                timestamp_sec INTEGER,
                lat REAL,
                lon REAL,
                spd REAL,
                hdg INTEGER,
                alt INTEGER,
                bat REAL,
                odo INTEGER,
                ign INTEGER,
                hdop REAL,
                temp REAL,
                raw_json TEXT,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
            )
        """)
        cursor.execute("CREATE INDEX IF NOT EXISTS idx_telemetry_src ON telemetry(src)")
        cursor.execute("CREATE INDEX IF NOT EXISTS idx_telemetry_created_at ON telemetry(created_at)")
        cursor.execute("CREATE INDEX IF NOT EXISTS idx_telemetry_ts ON telemetry(ts)")
        conn.commit()
        conn.close()
        logger.info("⚡ Binary Database initialized successfully.")

def save_record(data: dict) -> bool:
    msg_id = data.get("id")
    src = data.get("src", "UNKNOWN")
    seq = data.get("seq", 0)
    ts = data.get("ts", datetime.now(timezone.utc).isoformat())
    timestamp_sec = data.get("timestamp_sec", int(time.time()))
    lat = data.get("lat", 0.0)
    lon = data.get("lon", 0.0)
    spd = data.get("spd", 0.0)
    hdg = data.get("hdg", 0)
    alt = data.get("alt", 0)
    bat = data.get("bat", 0.0)
    odo = data.get("odo", 0)
    ign = data.get("ign", 0)
    hdop = data.get("hdop", 0.0)
    temp = data.get("temp", 0.0)
    raw_json = json.dumps(data)

    with db_lock:
        try:
            conn = get_db_connection()
            cursor = conn.cursor()
            cursor.execute("""
                INSERT OR REPLACE INTO telemetry 
                (id, src, seq, ts, timestamp_sec, lat, lon, spd, hdg, alt, bat, odo, ign, hdop, temp, raw_json)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """, (msg_id, src, seq, ts, timestamp_sec, lat, lon, spd, hdg, alt, bat, odo, ign, hdop, temp, raw_json))
            conn.commit()
            conn.close()
            logger.info(f"⚡ [BINARY INGEST] Saved: {src} [{msg_id}] Lat:{lat} Lon:{lon} Spd:{spd}km/h")
            return True
        except Exception as e:
            logger.error(f"❌ DB insert error: {e}")
            return False

# MQTT Callbacks
def on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        logger.info(f"✅ Connected to MQTT Broker ({MQTT_HOST}:{MQTT_PORT})")
        client.subscribe(MQTT_BINARY_TOPIC)
        client.subscribe(MQTT_JSON_TOPIC)
        logger.info(f"📡 Subscribed to binary topic: '{MQTT_BINARY_TOPIC}' & json: '{MQTT_JSON_TOPIC}'")
    else:
        logger.error(f"❌ Connection failed with code: {rc}")

def on_message(client, userdata, msg):
    try:
        if msg.topic == MQTT_BINARY_TOPIC:
            # ⚡ 64-BYTE BINARY PACKET UNPACKING
            parsed = parse_telemetry_packet(msg.payload)
            if parsed:
                save_record(parsed)
                # Kirim Auto-ACK instan
                ack_topic = f"kutai/fleet/ack/{parsed['src']}"
                ack_payload = json.dumps({"id": parsed["id"], "status": "ok"})
                client.publish(ack_topic, ack_payload, qos=0)
                logger.info(f"📤 Sent Auto-ACK to {ack_topic} for {parsed['id']}")
            else:
                logger.warning(f"⚠️ Failed to parse binary packet (len: {len(msg.payload)} bytes)")
        
        elif msg.topic == MQTT_JSON_TOPIC:
            # Legacy JSON Fallback
            payload_str = msg.payload.decode('utf-8')
            data = json.loads(payload_str)
            save_record(data)
            msg_id = data.get("id")
            src = data.get("src")
            if msg_id and src:
                ack_topic = f"kutai/fleet/ack/{src}"
                ack_payload = json.dumps({"id": msg_id, "status": "ok"})
                client.publish(ack_topic, ack_payload, qos=0)
    except Exception as e:
        logger.error(f"❌ Error processing MQTT message: {e}")

# Flask REST API
dashboard_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "dashboard_binary"))
app = Flask(__name__, static_folder=dashboard_dir)
CORS(app)

@app.route("/")
def serve_index():
    return send_from_directory(dashboard_dir, "index.html")

@app.route("/<path:path>")
def serve_static(path):
    return send_from_directory(dashboard_dir, path)

@app.route("/api/devices", methods=["GET"])
def get_devices():
    try:
        conn = get_db_connection()
        cursor = conn.cursor()
        
        cursor.execute("""
            SELECT src, COUNT(*) FROM telemetry 
            WHERE created_at >= datetime('now', '-1 hour')
            GROUP BY src
        """)
        activity_counts = dict(cursor.fetchall())
        
        cursor.execute("""
            SELECT t.* FROM telemetry t
            INNER JOIN (
                SELECT src, MAX(ts) as max_ts
                FROM telemetry
                GROUP BY src
            ) latest ON t.src = latest.src AND t.ts = latest.max_ts
        """)
        
        columns = [col[0] for col in cursor.description]
        devices = []
        for row in cursor.fetchall():
            row_dict = dict(zip(columns, row))
            src = row_dict.get('src')
            count = activity_counts.get(src, 0)
            status = 'green' if count > 0 else 'red'
            
            raw_json_str = row_dict.get('raw_json')
            extra_data = {}
            if raw_json_str:
                try:
                    extra_data = json.loads(raw_json_str)
                except Exception:
                    pass
            
            devices.append({
                "src": src,
                "lat": row_dict.get('lat', 0.0),
                "lon": row_dict.get('lon', 0.0),
                "spd": row_dict.get('spd', 0.0),
                "hdg": row_dict.get('hdg', 0),
                "alt": row_dict.get('alt', 0),
                "bat": row_dict.get('bat', 0.0),
                "odo": row_dict.get('odo', 0),
                "ign": row_dict.get('ign', 0),
                "hdop": row_dict.get('hdop', 0.0),
                "temp": row_dict.get('temp', 0.0),
                "ts": row_dict.get('ts'),
                "count": count,
                "status": status,
                "raw_payload": extra_data if extra_data else row_dict
            })
            
        conn.close()
        return jsonify(devices)
    except Exception as e:
        logger.error(f"❌ Error fetching devices: {e}")
        return jsonify({"error": str(e)}), 500

@app.route("/api/telemetry/<device_id>", methods=["GET"])
def get_telemetry_history(device_id):
    try:
        limit = request.args.get('limit', default=100, type=int)
        conn = get_db_connection()
        cursor = conn.cursor()
        cursor.execute("""
            SELECT * FROM telemetry 
            WHERE src = ? 
            ORDER BY created_at DESC 
            LIMIT ?
        """, (device_id, limit))
        
        columns = [col[0] for col in cursor.description]
        rows = [dict(zip(columns, row)) for row in cursor.fetchall()]
        conn.close()
        return jsonify(rows)
    except Exception as e:
        logger.error(f"❌ Error fetching history: {e}")
        return jsonify({"error": str(e)}), 500

@app.route("/api/stats", methods=["GET"])
def get_stats():
    try:
        conn = get_db_connection()
        cursor = conn.cursor()
        
        cursor.execute("SELECT COUNT(*) FROM telemetry")
        total_packets = cursor.fetchone()[0]
        
        cursor.execute("SELECT COUNT(DISTINCT src) FROM telemetry")
        total_devices = cursor.fetchone()[0]
        
        cursor.execute("""
            SELECT COUNT(DISTINCT src) FROM telemetry 
            WHERE created_at >= datetime('now', '-1 hour')
        """)
        active_devices = cursor.fetchone()[0]
        
        conn.close()
        return jsonify({
            "total_packets": total_packets,
            "total_devices": total_devices,
            "active_devices": active_devices,
            "protocol": "64-Byte Binary Packets"
        })
    except Exception as e:
        logger.error(f"❌ Error fetching stats: {e}")
        return jsonify({"error": str(e)}), 500

def start_mqtt():
    global mqtt_client
    try:
        mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="kutai-binary-backend")
    except AttributeError:
        mqtt_client = mqtt.Client(client_id="kutai-binary-backend")

    mqtt_client.on_connect = on_connect
    mqtt_client.on_message = on_message

    while True:
        try:
            logger.info(f"Connecting to MQTT Broker at {MQTT_HOST}:{MQTT_PORT}...")
            mqtt_client.connect(MQTT_HOST, MQTT_PORT, 60)
            mqtt_client.loop_forever()
        except Exception as e:
            logger.error(f"❌ MQTT error, reconnecting in 5s: {e}")
            time.sleep(5)

if __name__ == "__main__":
    init_db()
    mqtt_thread = threading.Thread(target=start_mqtt, daemon=True)
    mqtt_thread.start()
    logger.info("⚡ Starting MQTT Binary Thread...")
    logger.info(f"🌐 Starting Binary Web Dashboard server on port {PORT}...")
    app.run(host="0.0.0.0", port=PORT, debug=False, use_reloader=False)
