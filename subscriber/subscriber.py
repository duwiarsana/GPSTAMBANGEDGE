#!/usr/bin/env python3
import os
import json
import logging
import time
import requests
import threading
from concurrent.futures import ThreadPoolExecutor
from dotenv import load_dotenv
import paho.mqtt.client as mqtt
import sqlite3

# Configure Logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] %(message)s',
    handlers=[
        logging.StreamHandler()
    ]
)
logger = logging.getLogger("MQTT_Subscriber")

# Load configuration
load_dotenv()

MQTT_HOST = os.getenv("MQTT_HOST", "127.0.0.1")
try:
    MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
except ValueError:
    MQTT_PORT = 1883
MQTT_TOPIC = os.getenv("MQTT_TOPIC", "kutai/fleet/data")

raw_backend_url = os.getenv("BACKEND_URL", "").rstrip("/")
if "/v1" in raw_backend_url:
    base_v1 = raw_backend_url
    BACKEND_URL = raw_backend_url.split("/v1")[0]
else:
    BACKEND_URL = raw_backend_url
    base_v1 = f"{BACKEND_URL}/v1" if BACKEND_URL else ""

BACKEND_USERNAME = os.getenv("BACKEND_USERNAME", "")
BACKEND_PASSWORD = os.getenv("BACKEND_PASSWORD", "")

# App State
access_token = None
token_expires_at = 0

# Thread pool, thread locks, and active DTs tracking
executor = ThreadPoolExecutor(max_workers=10)
token_lock = threading.Lock()
active_dts = set(f"DT{i:02d}" for i in range(1, 21))

DB_PATH = os.getenv("DB_PATH", "/opt/kutai-dashboard-backend/telemetry.db")
db_write_lock = threading.Lock()

def log_device_alert(src, msg_id, status_code, response_msg):
    with db_write_lock:
        try:
            conn = sqlite3.connect(DB_PATH, timeout=30.0)
            conn.execute("PRAGMA journal_mode=WAL")
            cursor = conn.cursor()
            
            # Check if alert exists
            cursor.execute("SELECT retry_count, msg_id FROM device_alerts WHERE src = ?", (src,))
            row = cursor.fetchone()
            
            if row:
                curr_count, curr_msg_id = row
                new_count = curr_count + 1 if curr_msg_id == msg_id else 1
            else:
                new_count = 1
                
            cursor.execute("""
                INSERT INTO device_alerts (src, msg_id, retry_count, alert_type, last_seen)
                VALUES (?, ?, ?, ?, CURRENT_TIMESTAMP)
                ON CONFLICT(src) DO UPDATE SET
                    retry_count = excluded.retry_count,
                    msg_id = excluded.msg_id,
                    alert_type = excluded.alert_type,
                    last_seen = CURRENT_TIMESTAMP
            """, (src, msg_id, new_count, f"HTTP {status_code}: {response_msg}"))
            conn.commit()
            conn.close()
            logger.info(f"🔔 Alert logged for {src} (Status: {status_code}, Retry: {new_count})")
        except Exception as e:
            logger.error(f"❌ Failed to log device alert to DB: {e}")

def clear_device_alert(src):
    with db_write_lock:
        try:
            conn = sqlite3.connect(DB_PATH, timeout=30.0)
            conn.execute("PRAGMA journal_mode=WAL")
            cursor = conn.cursor()
            cursor.execute("DELETE FROM device_alerts WHERE src = ?", (src,))
            conn.commit()
            conn.close()
            logger.info(f"🔕 Alert cleared for {src}")
        except Exception as e:
            logger.error(f"❌ Failed to clear device alert in DB: {e}")

def login_to_backend():
    global access_token, token_expires_at
    if not base_v1 or not BACKEND_USERNAME or not BACKEND_PASSWORD:
        logger.error("Configuration error: BACKEND_URL, BACKEND_USERNAME, or BACKEND_PASSWORD is empty.")
        return False

    login_endpoint = f"{base_v1}/auth/login"
    payload = {
        "username": BACKEND_USERNAME,
        "password": BACKEND_PASSWORD
    }

    logger.info(f"Attempting to login to backend: {login_endpoint}")
    try:
        response = requests.post(login_endpoint, json=payload, timeout=10)
        if response.status_code == 200:
            data = response.json()
            access_token = data.get("accessToken")
            expires_in = data.get("expiresIn", 86400)
            token_expires_at = time.time() + expires_in - 300 # Buffer 5 minutes
            logger.info("✅ Login successful, token acquired.")
            return True
        else:
            logger.error(f"❌ Login failed with status: {response.status_code}. Response: {response.text}")
            return False
    except Exception as e:
        logger.error(f"❌ Connection error during login: {e}")
        return False

def get_auth_token():
    global access_token, token_expires_at
    with token_lock:
        if not access_token or time.time() >= token_expires_at:
            logger.info("Token expired or missing. Fetching new token...")
            success = login_to_backend()
            if not success:
                return None
        return access_token

def send_mqtt_ack(client, src, msg_id):
    if not msg_id or not src:
        return
    ack_payload = json.dumps({"id": msg_id, "status": "ok"})
    # Send the ACK directly to the device's ACK topic
    target_topic = f"kutai/fleet/ack/{src}"
    client.publish(target_topic, ack_payload, qos=0)
    
    # Also mirror to active DTs if it's an EXCA device
    if str(src).upper().startswith("EXCA"):
        for dt in list(active_dts):
            dt_topic = f"kutai/fleet/ack/{dt}"
            client.publish(dt_topic, ack_payload, qos=0)

def forward_telemetry(client, payload_dict):
    token = get_auth_token()
    if not token:
        logger.error("Cannot forward telemetry: Authorization token is unavailable.")
        return False

    ingest_endpoint = f"{base_v1}/ingest/telemetry?source=MQTT"
    headers = {
        "Authorization": f"Bearer {token}",
        "Content-Type": "application/json"
    }

    try:
        response = requests.post(ingest_endpoint, json=payload_dict, headers=headers, timeout=10)
        msg_id = payload_dict.get('id') or payload_dict.get('msg_id')
        src = payload_dict.get('src') or payload_dict.get('source')

        if response.status_code in (200, 202, 409):
            if response.status_code == 409:
                logger.warning(f"⚠️ Telemetry duplicate (409 Conflict) for device src: {src} [ID: {msg_id}]. Sending ACK to unblock client.")
                log_device_alert(src, msg_id, 409, "Duplicate / Conflict (Already Ingested)")
            else:
                logger.info(f"✅ Telemetry ingest successful for device src: {src} [ID: {msg_id}]")
                clear_device_alert(src)
            
            send_mqtt_ack(client, src, msg_id)
            return True
        elif response.status_code in (401, 403):
            logger.warning("⚠️ Ingest returned unauthorized. Clearing token to force re-login on next message.")
            global access_token
            with token_lock:
                access_token = None # Clear token
            return False
        else:
            logger.error(f"❌ Ingest failed. Status: {response.status_code}. Msg: {response.text}")
            # If the backend actually replied with 400 or 500, it means it's a validation/processing failure.
            # We must send an ACK to unblock the device, otherwise it will be locked in an infinite retry loop.
            logger.warning(f"⚠️ Sending ACK to unblock device {src} from stuck invalid packet [ID: {msg_id}].")
            log_device_alert(src, msg_id, response.status_code, f"Backend Reject: {response.text[:100]}")
            send_mqtt_ack(client, src, msg_id)
            return False
    except Exception as e:
        logger.error(f"❌ Connection error during ingest: {e}")
        # Note: If it's a network/connection error, the device will retry, but it's not a server rejection. 
        # We can still track it as a network timeout issue alert.
        log_device_alert(src, payload_dict.get('id', 'unknown'), 599, f"Network Error: {str(e)[:100]}")
        return False

# MQTT Event Callbacks
def on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        logger.info(f"✅ Connected to MQTT Broker ({MQTT_HOST}:{MQTT_PORT}) successfully.")
        client.subscribe(MQTT_TOPIC)
        client.subscribe("kutai/fleet/ack/+")
        logger.info(f"Subscribed to topics: {MQTT_TOPIC} and kutai/fleet/ack/+")
    else:
        logger.error(f"❌ Connection to MQTT Broker failed with code: {rc}")

def handle_telemetry_message(client, data, payload_str):
    try:
        # Dynamically record active DTs
        src = data.get("src") or data.get("source")
        if src and str(src).upper().startswith("DT"):
            active_dts.add(str(src))
        
        # Forward telemetry to backend
        forward_telemetry(client, data)
    except Exception as e:
        logger.error(f"❌ Error in telemetry processing thread: {e}")

# Buffer for fragmented/corrupted JSON payloads
partial_buffers = {}

def try_merge_chunks(s1, s2):
    """
    Tries to merge two chunks where a suffix of s1 overlaps with a prefix of s2,
    or joins them using candidate bridges to fix gaps.
    """
    # 1. Try overlap matching
    max_len = min(len(s1), len(s2))
    for k in range(max_len, 0, -1):
        prefix = s2[:k]
        if s1.endswith(prefix):
            merged = s1 + s2[k:]
            try:
                json.loads(merged)
                return merged
            except ValueError:
                pass

    # 2. Try candidate bridges (direct concatenation and common gap fillers)
    candidates = ["", "t\":\"", "\":\"", "st\":\"", "\":", ",", "\":{", "\":[\"", "t\":"]
    for bridge in candidates:
        merged = s1 + bridge + s2
        try:
            json.loads(merged)
            return merged
        except ValueError:
            pass

    return None

def on_message(client, userdata, msg):
    try:
        payload_str = msg.payload.decode('utf-8').replace('\r', '').replace('\n', '').strip()
        
        if msg.topic.startswith("kutai/fleet/ack/"):
            # Handle ACK Mirroring
            topic_parts = msg.topic.split('/')
            src = topic_parts[-1]
            
            if src.upper().startswith("EXCA"):
                data = json.loads(payload_str)
                status = data.get("status")
                msg_id = data.get("id") or data.get("msg_id")
                
                if status == "ok" and msg_id:
                    logger.info(f"🔄 Mirroring EXCA ACK for {msg_id} (from {src}) to active DTs")
                    for dt in list(active_dts):
                        dt_topic = f"kutai/fleet/ack/{dt}"
                        client.publish(dt_topic, msg.payload, qos=0)
        else:
            # Handle Telemetry Message
            logger.info(f"📥 Received MQTT telemetry message on {msg.topic}")
            
            # Try parsing directly
            try:
                data = json.loads(payload_str)
                # Parse success: remove any existing partial buffer for this device if present
                src = data.get("src") or data.get("source")
                if src:
                    partial_buffers.pop(src, None)
                executor.submit(handle_telemetry_message, client, data, payload_str)
            except json.JSONDecodeError:
                # Failed to parse. Try to reconstruct if it is a fragmented message
                recovered = False
                if payload_str.startswith("{"):
                    import re
                    match = re.search(r'"src"\s*:\s*"([^"]+)"', payload_str)
                    src_key = match.group(1) if match else "unknown"
                    partial_buffers[src_key] = payload_str
                    logger.warning(f"⚠️ Received partial JSON start for {src_key}. Buffered.")
                else:
                    # Attempt to merge with any pending first-halves from all buffered devices
                    for src_key, pending in list(partial_buffers.items()):
                        merged = try_merge_chunks(pending, payload_str)
                        if merged:
                            try:
                                data = json.loads(merged)
                                partial_buffers.pop(src_key, None)
                                logger.info(f"❇️ Successfully recovered and merged fragmented JSON for {src_key}!")
                                executor.submit(handle_telemetry_message, client, data, merged)
                                recovered = True
                                break
                            except json.JSONDecodeError:
                                pass
                
                if not recovered:
                    logger.error(f"❌ Failed to parse MQTT payload as JSON on topic {msg.topic}: {payload_str}")
                
    except Exception as e:
        logger.error(f"❌ Error in on_message: {e}")

def main():
    logger.info("Starting Kutai Fleet MQTT to Backend Subscriber Service...")
    
    # Try initial login to verify credentials configuration
    login_to_backend()

    # Setup MQTT Client (Supports Paho MQTT v2 API compatibility)
    client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2)
    client.on_connect = on_connect
    client.on_message = on_message

    # Auto reconnection
    client.reconnect_delay_set(min_delay=1, max_delay=60)

    try:
        client.connect(MQTT_HOST, MQTT_PORT, keepalive=60)
    except Exception as e:
        logger.error(f"❌ Initial MQTT connect failed: {e}. Will auto-retry in loop.")

    # Start loop
    try:
        client.loop_forever()
    except KeyboardInterrupt:
        logger.info("Service shutting down cleanly.")
        client.disconnect()

if __name__ == "__main__":
    main()
