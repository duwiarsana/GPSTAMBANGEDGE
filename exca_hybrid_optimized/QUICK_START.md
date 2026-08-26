# Quick Start Guide - EXCA Hybrid V2

## 🚀 Deploy in 3 Steps

### Step 1: Generate Firmware for Target Device
```bash
cd exca_hybrid_optimized/
python3 generate_firmware.py EXCA01
# or EXCA02, EXCA03
```

### Step 2: Flash to Device
1. Connect ESP32 via USB
2. Open `exca_hybrid_exca01_v2.ino` in Arduino IDE
3. Select board: **ESP32 Dev Module**
4. Upload

### Step 3: Verify
```bash
# Check serial output - should see:
# EXCA HYBRID V2 READY
# 🚀 Optimizations:
#    • Chunk upload: 50 records/session
#    • Polling interval: 15s
#    • ACK timeout: 2s
#    • Compaction: 30min interval
#    • Keep-alive: 300s
```

## 📊 Monitor Performance

### Option 1: Run on Local Machine
```bash
cd exca_hybrid_optimized/
python3 monitor_performance.py EXCA01
```

### Option 2: Run on VPS (Direct)
```bash
ssh -i ~/.ssh/id_rsa root@72.62.126.85
cd /opt/kutai-dashboard-backend/
python3 /path/to/monitor_simple.py EXCA01
```

### Option 3: Manual SQL Query
```bash
ssh -i ~/.ssh/id_rsa root@72.62.126.85 "python3 << 'EOF'
import sqlite3
conn = sqlite3.connect('/opt/kutai-dashboard-backend/telemetry.db')
cur = conn.cursor()

# Check EXCA01 performance
cur.execute('''
    SELECT 
        AVG(strftime('%s', created_at) - strftime('%s', ts)) / 60.0 as avg_delay_min
    FROM telemetry 
    WHERE src='EXCA01' AND ts >= datetime('now', '-1 day')
''')
row = cur.fetchone()
print(f'EXCA01 average delay (24h): {row[0]:.1f} minutes')

# Compare before/after
cur.execute('''
    SELECT 
        SUM(CASE WHEN (strftime('%s', created_at) - strftime('%s', ts)) < 60 THEN 1 ELSE 0 END) as realtime,
        SUM(CASE WHEN (strftime('%s', created_at) - strftime('%s', ts)) > 600 THEN 1 ELSE 0 END) as heavy_backlog
    FROM telemetry 
    WHERE src='EXCA01' AND ts >= datetime('now', '-1 day')
''')
row = cur.fetchone()
total = row[0] + row[1]
print(f'Real-time: {row[0]} ({row[0]/total*100:.1f}%)')
print(f'Heavy backlog: {row[1]} ({row[1]/total*100:.1f}%)')

conn.close()
EOF"
```

## 🎯 Success Metrics (After 24h)

Check if:
- ✅ Average delay < 1 hour (from 16 hours for EXCA03)
- ✅ Real-time > 50% (from 7%)
- ✅ Heavy backlog < 20% (from 75% for EXCA03)

If not met, try:
- Reduce `MAX_UPLOAD_CHUNK` to 30
- Reduce `INTERNET_INTERVAL` to 10000 (10s)

## 🔄 Rollback

If issues occur:
```bash
# Flash old firmware
cd ../gpstambangexca_hybrid_realtime/
# Upload gpstambangexca_hybrid_realtime.ino
```

## 📞 Need Help?

Check serial logs for:
- WiFi connection issues
- MQTT connection issues
- Upload errors
- Compaction status
