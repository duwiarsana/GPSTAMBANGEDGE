#!/usr/bin/env bash

# Exit immediately if a command exits with a non-zero status
set -e

VPS_IP="72.62.126.85"
REMOTE_DIR="/opt/kutai-subscriber"

echo "=== 1. Packaging & Uploading subscriber code to VPS ==="
# Create temporary folder on local machine
TMP_DIR=$(mktemp -d)
cp subscriber/requirements.txt "$TMP_DIR/"
cp subscriber/subscriber.py "$TMP_DIR/"
cp subscriber/.env.example "$TMP_DIR/"
if [ -f subscriber/.env ]; then
  cp subscriber/.env "$TMP_DIR/"
fi

# Make sure remote directory exists
ssh root@$VPS_IP "mkdir -p $REMOTE_DIR"

# Rsync/scp code to VPS
scp "$TMP_DIR"/requirements.txt "$TMP_DIR"/subscriber.py "$TMP_DIR"/.env.example root@$VPS_IP:$REMOTE_DIR/
if [ -f "$TMP_DIR"/.env ]; then
  scp "$TMP_DIR"/.env root@$VPS_IP:$REMOTE_DIR/
fi
rm -rf "$TMP_DIR"

echo "=== 2. Creating python virtual environment and installing dependencies on VPS ==="
ssh root@$VPS_IP bash << 'EOF'
  cd /opt/kutai-subscriber
  
  # Set up Virtual Environment
  python3 -m venv venv
  
  # Install dependencies
  ./venv/bin/pip install --upgrade pip
  ./venv/bin/pip install -r requirements.txt
  
  # Setup configuration file (.env) if not exists
  if [ ! -f .env ]; then
    if [ -f .env.example ]; then
      cp .env.example .env
      echo "⚠️ .env file created from example. Please edit it."
    fi
  else
    echo "✅ Remote .env already exists or has been uploaded."
  fi
EOF

echo "=== 3. Creating and configuring Systemd Service ==="
SERVICE_CONTENT="[Unit]
Description=Kutai Fleet MQTT to Backend Subscriber Service
After=network.target mosquitto.service
Requires=mosquitto.service

[Service]
Type=simple
User=root
WorkingDirectory=$REMOTE_DIR
ExecStart=$REMOTE_DIR/venv/bin/python $REMOTE_DIR/subscriber.py
Restart=always
RestartSec=5
StandardOutput=journal
StandardError=journal
SyslogIdentifier=kutai-subscriber

[Install]
WantedBy=multi-user.target"

# Write systemd file to VPS
ssh root@$VPS_IP "echo '$SERVICE_CONTENT' > /etc/systemd/system/kutai-subscriber.service"

# Reload systemd, enable service
ssh root@$VPS_IP bash << 'EOF'
  systemctl daemon-reload
  systemctl enable kutai-subscriber.service
  echo "✅ Systemd service 'kutai-subscriber' registered and enabled!"
  echo "👉 To start the service, run: systemctl start kutai-subscriber"
  echo "👉 To view logs, run: journalctl -u kutai-subscriber -f"
EOF

echo "=== Setup Completed Successfully! ==="
