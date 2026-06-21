#!/usr/bin/env bash
set -e

VPS_IP="72.62.126.85"
REMOTE_DIR="/opt/kutai-dashboard-backend"
WEB_DIR="/var/www/naturelink-dashboard"

echo "=== 1. Deploying Backend Code to VPS ==="
ssh root@$VPS_IP "mkdir -p $REMOTE_DIR"
scp dashboard/server.py dashboard/requirements.txt root@$VPS_IP:$REMOTE_DIR/

echo "=== 2. Creating python virtual environment and installing backend dependencies ==="
ssh root@$VPS_IP bash << 'EOF'
  cd /opt/kutai-dashboard-backend
  python3 -m venv venv
  ./venv/bin/pip install --upgrade pip
  ./venv/bin/pip install -r requirements.txt
EOF

echo "=== 3. Creating and configuring Systemd Service ==="
SERVICE_CONTENT="[Unit]
Description=Kutai Fleet Dashboard Backend Service
After=network.target mosquitto.service
Requires=mosquitto.service

[Service]
Type=simple
User=root
WorkingDirectory=$REMOTE_DIR
ExecStart=$REMOTE_DIR/venv/bin/python $REMOTE_DIR/server.py
Restart=always
RestartSec=5
StandardOutput=journal
StandardError=journal
SyslogIdentifier=kutai-dashboard-backend

[Install]
WantedBy=multi-user.target"

ssh root@$VPS_IP "echo '$SERVICE_CONTENT' > /etc/systemd/system/kutai-dashboard-backend.service"
ssh root@$VPS_IP bash << 'EOF'
  systemctl daemon-reload
  systemctl enable kutai-dashboard-backend.service
  systemctl restart kutai-dashboard-backend.service
  echo "✅ Systemd service 'kutai-dashboard-backend' started!"
EOF

echo "=== 4. Updating Nginx Configuration ==="
NGINX_CONTENT="server {
    listen 80;
    server_name _;
    
    root $WEB_DIR;
    index index.html;
    
    location / {
        auth_basic \"Kutai Fleet Dashboard - Restrict Access\";
        auth_basic_user_file /etc/nginx/.htpasswd;
        try_files \$uri \$uri/ =404;
    }

    location /api/ {
        auth_basic \"Kutai Fleet Dashboard - Restrict Access\";
        auth_basic_user_file /etc/nginx/.htpasswd;
        proxy_pass http://127.0.0.1:5000/api/;
        proxy_set_header Host \$host;
        proxy_set_header X-Real-IP \$remote_addr;
        proxy_set_header X-Forwarded-For \$proxy_add_x_forwarded_for;
    }
    
    # Cache static files
    location ~* \.(css|js|jpg|jpeg|png|gif|ico|svg)$ {
        auth_basic \"Kutai Fleet Dashboard - Restrict Access\";
        auth_basic_user_file /etc/nginx/.htpasswd;
        expires 1d;
        add_header Cache-Control \"public, immutable\";
    }
}"

ssh root@$VPS_IP "echo '$NGINX_CONTENT' > /etc/nginx/sites-available/naturelink-dashboard"
ssh root@$VPS_IP "nginx -t && systemctl reload nginx"
echo "✅ Nginx reloaded and API proxy configured!"

echo "=== 5. Deploying Frontend Code to VPS ==="
ssh root@$VPS_IP "mkdir -p $WEB_DIR"
scp dashboard/index.html dashboard/style.css dashboard/app.js root@$VPS_IP:$WEB_DIR/
echo "✅ Frontend deployed to $WEB_DIR"

echo "=== Deployment Completed Successfully! ==="
