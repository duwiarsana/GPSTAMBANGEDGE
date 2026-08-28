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

from binary_parser import parse_telemetry_packet, parse_telemetry_batch

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
    conn = sqlite3.connect(DB_PATH, timeout=20)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA journal_mode=WAL;")
    conn.execute("PRAGMA synchronous=NORMAL;")
    return conn

def init_db():
    with db_lock:
        conn = get_db_connection()
        cursor = conn.cursor()
        cursor.execute("PRAGMA journal_mode=WAL;")
        cursor.execute("PRAGMA synchronous=NORMAL;")
        cursor.execute("PRAGMA cache_size = 10000;")
        cursor.execute("""
            CREATE TABLE IF NOT EXISTS telemetry (
                id TEXT PRIMARY KEY,
                src TEXT NOT NULL,
                imei TEXT,
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
        # Auto-migration for existing DB
        try:
            cursor.execute("ALTER TABLE telemetry ADD COLUMN imei TEXT")
        except Exception:
            pass

        cursor.execute("CREATE INDEX IF NOT EXISTS idx_telemetry_src ON telemetry(src)")
        cursor.execute("CREATE INDEX IF NOT EXISTS idx_telemetry_imei ON telemetry(imei)")
        cursor.execute("CREATE INDEX IF NOT EXISTS idx_telemetry_created_at ON telemetry(created_at)")
        cursor.execute("CREATE INDEX IF NOT EXISTS idx_telemetry_ts ON telemetry(ts)")
        cursor.execute("CREATE INDEX IF NOT EXISTS idx_telemetry_ts_sec ON telemetry(timestamp_sec)")
        conn.commit()
        conn.close()
        logger.info("⚡ Binary Database initialized with WAL Mode & IMEI Support.")

def save_records_bulk(records: list) -> bool:
    if not records:
        return False
    rows = []
    for data in records:
        msg_id = data.get("id")
        src = data.get("src", "UNKNOWN")
        imei = data.get("imei", "")
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
        rows.append((msg_id, src, imei, seq, ts, timestamp_sec, lat, lon, spd, hdg, alt, bat, odo, ign, hdop, temp, raw_json))

    with db_lock:
        try:
            conn = get_db_connection()
            cursor = conn.cursor()
            cursor.executemany("""
                INSERT OR REPLACE INTO telemetry 
                (id, src, imei, seq, ts, timestamp_sec, lat, lon, spd, hdg, alt, bat, odo, ign, hdop, temp, raw_json)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """, rows)
            conn.commit()
            conn.close()
            last_rec = records[-1]
            if len(records) > 1:
                logger.info(f"⚡ [BULK INGEST] Saved {len(rows)} records. Last: {last_rec['src']} (IMEI:{last_rec.get('imei')}) [{last_rec['id']}] Lat:{last_rec.get('lat')} Spd:{last_rec.get('spd')}")
            else:
                logger.info(f"⚡ [REALTIME INGEST] Saved: {last_rec['src']} (IMEI:{last_rec.get('imei')}) [{last_rec['id']}] Lat:{last_rec.get('lat')} Spd:{last_rec.get('spd')}")
            return True
        except Exception as e:
            logger.error(f"❌ DB insert error: {e}")
            return False

def save_record(data: dict) -> bool:
    return save_records_bulk([data])

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
            # ⚡ 64-BYTE SINGLE OR MULTI-PACKET BULK UNPACKING
            records = parse_telemetry_batch(msg.payload)
            if records:
                save_records_bulk(records)
                # Kirim Auto-ACK instan untuk record terakhir di batch
                last_rec = records[-1]
                ack_topic = f"kutai/fleet/ack/{last_rec['src']}"
                ack_payload = json.dumps({
                    "id": last_rec["id"],
                    "count": len(records),
                    "status": "ok"
                })
                client.publish(ack_topic, ack_payload, qos=0)
                logger.info(f"📤 Sent Auto-ACK to {ack_topic} for {len(records)} record(s) (last: {last_rec['id']})")
            else:
                logger.warning(f"⚠️ Failed to parse binary payload (len: {len(msg.payload)} bytes)")
        
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
base_dir = os.path.dirname(os.path.abspath(__file__))
dashboard_dir = os.path.join(base_dir, "dashboard_binary")
if not os.path.exists(dashboard_dir):
    dashboard_dir = os.path.abspath(os.path.join(base_dir, "..", "dashboard_binary"))

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
            WHERE created_at >= datetime('now', '-30 seconds')
            GROUP BY src
        """)
        activity_counts = dict(cursor.fetchall())

        cursor.execute("SELECT src, COUNT(*) FROM telemetry GROUP BY src")
        total_counts = dict(cursor.fetchall())
        
        cursor.execute("""
            SELECT t.* FROM telemetry t
            INNER JOIN (
                SELECT src, MAX(id) as max_id
                FROM telemetry
                GROUP BY src
            ) latest ON t.src = latest.src AND t.id = latest.max_id
        """)
        
        columns = [col[0] for col in cursor.description]
        devices = []
        for row in cursor.fetchall():
            row_dict = dict(zip(columns, row))
            src = row_dict.get('src')
            count = activity_counts.get(src, 0)
            total_records = total_counts.get(src, 0)
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
                "imei": row_dict.get('imei') or "",
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
                "created_at": row_dict.get('created_at'),
                "count": count,
                "total_records": total_records,
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
            SELECT id, src, imei, seq, ts, timestamp_sec, lat, lon, spd, hdg, alt, bat, odo, ign, hdop, temp, raw_json, created_at
            FROM telemetry
            WHERE src = ?
            ORDER BY timestamp_sec DESC
            LIMIT ?
        """, (device_id, limit))
        
        columns = [col[0] for col in cursor.description]
        history = []
        for row in cursor.fetchall():
            history.append(dict(zip(columns, row)))
            
        conn.close()
        return jsonify(history)
    except Exception as e:
        logger.error(f"❌ Error fetching telemetry history: {e}")
        return jsonify({"error": str(e)}), 500

from flask import Flask, jsonify, request, send_from_directory, send_file, Response
import io
import csv

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
            WHERE created_at >= datetime('now', '-30 seconds')
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

# ================= EXPORT & DOWNLOAD ENDPOINTS =================
@app.route("/api/export/csv", methods=["GET"])
def export_csv():
    try:
        src = request.args.get("src", default=None)
        start_date = request.args.get("start", default=None)
        end_date = request.args.get("end", default=None)
        limit = request.args.get("limit", default=200000, type=int)

        conn = get_db_connection()
        cursor = conn.cursor()

        conditions = []
        params = []

        if src and src.upper() != "ALL":
            conditions.append("src = ?")
            params.append(src)

        if start_date:
            clean_start = start_date.replace("T", " ")
            if len(clean_start) == 10:
                clean_start += " 00:00:00"
            conditions.append("ts >= ?")
            params.append(clean_start)

        if end_date:
            clean_end = end_date.replace("T", " ")
            if len(clean_end) == 10:
                clean_end += " 23:59:59"
            conditions.append("ts <= ?")
            params.append(clean_end)

        where_clause = f"WHERE {' AND '.join(conditions)}" if conditions else ""
        
        query = f"""
            SELECT id, src, imei, seq, ts, timestamp_sec, lat, lon, spd, hdg, alt, bat, odo, ign, hdop, temp, created_at, raw_json
            FROM telemetry
            {where_clause}
            ORDER BY timestamp_sec ASC
            LIMIT ?
        """
        params.append(limit)
        cursor.execute(query, tuple(params))

        unit_tag = src if (src and src.upper() != "ALL") else "all_units"
        date_tag = ""
        if start_date or end_date:
            s_tag = start_date[:10].replace("-", "") if start_date else "start"
            e_tag = end_date[:10].replace("-", "") if end_date else "end"
            date_tag = f"_{s_tag}_to_{e_tag}"
        filename = f"telemetry_{unit_tag}{date_tag}_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"

        rows = cursor.fetchall()
        conn.close()

        output = io.StringIO()
        writer = csv.writer(output)
        writer.writerow([
            "Msg ID", "Source ID", "IMEI", "Sequence", "GPS Timestamp (UTC)", "Epoch Sec", 
            "Latitude", "Longitude", "Speed (km/h)", "Heading (deg)", "Altitude (m)", 
            "Battery (V)", "Odometer (m)", "Ignition", "PTO (Dump Bed)", "HDOP", 
            "MCU Temp (C)", "Server Received At"
        ])

        for r in rows:
            raw_json_str = r[17]
            pto = 0
            if raw_json_str:
                try:
                    p = json.loads(raw_json_str)
                    pto = p.get("pto", 0)
                except Exception:
                    pass
            
            writer.writerow([
                r[0], r[1], r[2] or "", r[3], r[4], r[5],
                r[6], r[7], r[8], r[9], r[10],
                r[11], r[12], "ON" if r[13] == 1 else "OFF", "ON" if pto == 1 else "OFF",
                r[14], r[15], r[16]
            ])

        output.seek(0)
        return Response(
            output.getvalue(),
            mimetype="text/csv",
            headers={"Content-Disposition": f"attachment; filename={filename}"}
        )
    except Exception as e:
        logger.error(f"❌ Error exporting CSV: {e}")
        return jsonify({"error": str(e)}), 500

@app.route("/api/export/json", methods=["GET"])
def export_json():
    try:
        src = request.args.get("src", default=None)
        start_date = request.args.get("start", default=None)
        end_date = request.args.get("end", default=None)
        limit = request.args.get("limit", default=200000, type=int)

        conn = get_db_connection()
        cursor = conn.cursor()

        conditions = []
        params = []

        if src and src.upper() != "ALL":
            conditions.append("src = ?")
            params.append(src)

        if start_date:
            clean_start = start_date.replace("T", " ")
            if len(clean_start) == 10:
                clean_start += " 00:00:00"
            conditions.append("ts >= ?")
            params.append(clean_start)

        if end_date:
            clean_end = end_date.replace("T", " ")
            if len(clean_end) == 10:
                clean_end += " 23:59:59"
            conditions.append("ts <= ?")
            params.append(clean_end)

        where_clause = f"WHERE {' AND '.join(conditions)}" if conditions else ""

        query = f"""
            SELECT id, src, imei, seq, ts, timestamp_sec, lat, lon, spd, hdg, alt, bat, odo, ign, hdop, temp, created_at, raw_json
            FROM telemetry
            {where_clause}
            ORDER BY timestamp_sec ASC
            LIMIT ?
        """
        params.append(limit)
        cursor.execute(query, tuple(params))
        columns = [col[0] for col in cursor.description]
        records = [dict(zip(columns, row)) for row in cursor.fetchall()]
        conn.close()

        unit_tag = src if (src and src.upper() != "ALL") else "all_units"
        date_tag = ""
        if start_date or end_date:
            s_tag = start_date[:10].replace("-", "") if start_date else "start"
            e_tag = end_date[:10].replace("-", "") if end_date else "end"
            date_tag = f"_{s_tag}_to_{e_tag}"
        filename = f"telemetry_{unit_tag}{date_tag}_{datetime.now().strftime('%Y%m%d_%H%M%S')}.json"

        columns = [col[0] for col in cursor.description]
        rows = []
        for r in cursor.fetchall():
            row_dict = dict(zip(columns, r))
            if row_dict.get('raw_json'):
                try:
                    row_dict['details'] = json.loads(row_dict['raw_json'])
                except Exception:
                    pass
            rows.append(row_dict)
        conn.close()

        json_data = json.dumps(rows, indent=2)
        return Response(
            json_data,
            mimetype="application/json",
            headers={"Content-Disposition": f"attachment; filename={filename}"}
        )
    except Exception as e:
        logger.error(f"❌ Error exporting JSON: {e}")
        return jsonify({"error": str(e)}), 500

@app.route("/api/export/db", methods=["GET"])
def export_db():
    try:
        abs_db_path = os.path.abspath(DB_PATH)
        if not os.path.exists(abs_db_path):
            return jsonify({"error": "Database file not found"}), 404
        
        filename = f"telemetry_binary_{datetime.now().strftime('%Y%m%d_%H%M%S')}.db"
        return send_file(
            abs_db_path,
            as_attachment=True,
            download_name=filename,
            mimetype="application/x-sqlite3"
        )
    except Exception as e:
        logger.error(f"❌ Error exporting DB file: {e}")
        return jsonify({"error": str(e)}), 500

@app.route("/api/admin/reset_db", methods=["POST", "GET"])
def reset_database():
    """Wipes all telemetry rows and vacuums the database."""
    try:
        with db_lock:
            conn = get_db_connection()
            cursor = conn.cursor()
            cursor.execute("DELETE FROM telemetry;")
            conn.commit()
            cursor.execute("VACUUM;")
            conn.commit()
            conn.close()
        logger.info("🧹 Database reset by user request.")
        return jsonify({"status": "ok", "message": "Database successfully wiped (0 records)."}), 200
    except Exception as e:
        logger.error(f"❌ Error resetting DB: {e}")
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
