#!/usr/bin/env python3
"""
Simple monitor for EXCA Hybrid V2 - run on VPS directly
"""

import sqlite3
from datetime import datetime, timedelta

def monitor_device(device_id):
    conn = sqlite3.connect('/opt/kutai-dashboard-backend/telemetry.db')
    cur = conn.cursor()
    
    print(f"\n{'=' * 80}")
    print(f"DEVICE: {device_id}")
    print(f"{'=' * 80}")
    
    # Latest data
    cur.execute(f"SELECT COUNT(*), MAX(ts), MAX(created_at) FROM telemetry WHERE src='{device_id}'")
    row = cur.fetchone()
    print(f"\n📊 Latest Data:")
    print(f"  Total records: {row[0]}")
    print(f"  Latest ts: {row[1]}")
    print(f"  Latest received: {row[2]}")
    
    # Backlog delay (last 24h)
    cur.execute(f"""
        SELECT 
            AVG(strftime('%s', created_at) - strftime('%s', ts)) / 60.0 as avg_delay_min,
            MAX(strftime('%s', created_at) - strftime('%s', ts)) / 60.0 as max_delay_min
        FROM telemetry 
        WHERE src='{device_id}' AND ts >= datetime('now', '-1 day')
    """)
    row = cur.fetchone()
    print(f"\n⏱️ Backlog Delay (24h):")
    print(f"  Average delay: {row[0]:.1f} minutes")
    print(f"  Max delay: {row[1]:.1f} minutes")
    
    # Upload pattern (last hour)
    cur.execute(f"""
        SELECT strftime('%Y-%m-%d %H:%M', created_at) as minute, COUNT(*) as cnt
        FROM telemetry 
        WHERE src='{device_id}' AND created_at >= datetime('now', '-1 hour')
        GROUP BY minute ORDER BY minute DESC LIMIT 10
    """)
    print(f"\n📈 Upload Pattern (last hour):")
    for row in cur.fetchall():
        print(f"  {row[0]}: {row[1]} records")
    
    # Real-time vs backlog classification
    cur.execute(f"""
        SELECT 
            SUM(CASE WHEN (strftime('%s', created_at) - strftime('%s', ts)) < 60 THEN 1 ELSE 0 END) as realtime,
            SUM(CASE WHEN (strftime('%s', created_at) - strftime('%s', ts)) BETWEEN 60 AND 600 THEN 1 ELSE 0 END) as light_backlog,
            SUM(CASE WHEN (strftime('%s', created_at) - strftime('%s', ts)) > 600 THEN 1 ELSE 0 END) as heavy_backlog
        FROM telemetry 
        WHERE src='{device_id}' AND ts >= datetime('now', '-1 day')
    """)
    row = cur.fetchone()
    total = row[0] + row[1] + row[2]
    print(f"\n🎯 Classification (24h):")
    if total > 0:
        print(f"  Real-time (<1min): {row[0]} ({row[0]/total*100:.1f}%)")
        print(f"  Light backlog (1-10min): {row[1]} ({row[1]/total*100:.1f}%)")
        print(f"  Heavy backlog (>10min): {row[2]} ({row[2]/total*100:.1f}%)")
    else:
        print(f"  No data in last 24h")
    
    conn.close()

def main():
    import sys
    device = sys.argv[1] if len(sys.argv) > 1 else None
    
    print("=" * 80)
    print("EXCA HYBRID V2 PERFORMANCE MONITOR")
    print("=" * 80)
    print(f"Timestamp: {datetime.now()}")
    
    if device:
        devices = [device]
    else:
        devices = ["EXCA01", "EXCA02", "EXCA03"]
    
    for dev in devices:
        monitor_device(dev)
    
    print("\n" + "=" * 80)
    print("✅ Monitor complete")
    print("=" * 80)

if __name__ == "__main__":
    main()
