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

def forward_telemetry(payload_dict):
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
        if response.status_code in (200, 202):
            logger.info(f"✅ Telemetry ingest successful for device src: {payload_dict.get('src')} [ID: {payload_dict.get('id')}]")
            return True
        elif response.status_code in (401, 403):
            logger.warning("⚠️ Ingest returned unauthorized. Clearing token to force re-login on next message.")
            global access_token
            with token_lock:
                access_token = None # Clear token
            return False
        else:
            logger.error(f"❌ Ingest failed. Status: {response.status_code}. Msg: {response.text}")
            return False
    except Exception as e:
        logger.error(f"❌ Connection error during ingest: {e}")
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
        forward_telemetry(data)
    except Exception as e:
        logger.error(f"❌ Error in telemetry processing thread: {e}")

def on_message(client, userdata, msg):
    try:
        payload_str = msg.payload.decode('utf-8')
        
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
            data = json.loads(payload_str)
            # Submit to thread pool for concurrent processing
            executor.submit(handle_telemetry_message, client, data, payload_str)
                
    except json.JSONDecodeError:
        logger.error(f"❌ Failed to parse MQTT payload as JSON on topic {msg.topic}")
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
