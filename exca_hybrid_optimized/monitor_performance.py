#!/usr/bin/env python3
"""
Monitor EXCA Hybrid V2 performance in database

Usage:
    python3 monitor_performance.py
    python3 monitor_performance.py EXCA01
    python3 monitor_performance.py --24h
"""

import subprocess
import sys
from datetime import datetime

def run_ssh_query(query):
    """Run SQL query on VPS"""
    script = f'''import sqlite3
conn = sqlite3.connect('/opt/kutai-dashboard-backend/telemetry.db')
cur = conn.cursor()
{query}
conn.close()'''
    
    # Escape for SSH
    escaped_script = script.replace("'", "'\"'\"'")
    cmd = f"ssh -i ~/.ssh/id_rsa root@72.62.126.85 'python3 -c \\'{escaped_script}\\''"
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    if result.returncode != 0:
        return f"Error: {result.stderr}"
    return result.stdout

def main():
    device = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith("--") else None
    hours_24 = "--24h" in sys.argv
    
    print("=" * 80)
    print("EXCA HYBRID V2 PERFORMANCE MONITOR")
    print("=" * 80)
    print(f"Timestamp: {datetime.now()}")
    print()
    
    if device:
        devices = [device]
    else:
        devices = ["EXCA01", "EXCA02", "EXCA03"]
    
    for dev in devices:
        print(f"\n{'=' * 80}")
        print(f"DEVICE: {dev}")
        print(f"{'=' * 80}")
        
        # Latest data
        query = f'''
cur.execute("SELECT COUNT(*), MAX(ts), MAX(created_at) FROM telemetry WHERE src='{dev}'")
row = cur.fetchone()
print(f"Total records: {{row[0]}}")
print(f"Latest ts: {{row[1]}}")
print(f"Latest received: {{row[2]}}")
'''
        print("\n📊 Latest Data:")
        print(run_ssh_query(query))
        
        # Backlog delay (last 24h)
        query = f'''
cur.execute("""
    SELECT 
        AVG(strftime('%s', created_at) - strftime('%s', ts)) / 60.0 as avg_delay_min,
        MAX(strftime('%s', created_at) - strftime('%s', ts)) / 60.0 as max_delay_min
    FROM telemetry 
    WHERE src='{dev}' AND ts >= datetime('now', '-1 day')
""")
row = cur.fetchone()
print(f"Average delay (24h): {{row[0]:.1f}} minutes")
print(f"Max delay (24h): {{row[1]:.1f}} minutes")
'''
        print("\n⏱️ Backlog Delay (24h):")
        print(run_ssh_query(query))
        
        # Upload pattern
        query = f'''
cur.execute("""
    SELECT strftime('%Y-%m-%d %H:%M', created_at) as minute, COUNT(*) as cnt
    FROM telemetry 
    WHERE src='{dev}' AND created_at >= datetime('now', '-1 hour')
    GROUP BY minute ORDER BY minute DESC LIMIT 10
""")
print("Upload pattern (last hour):")
for row in cur.fetchall():
    print(f"  {{row[0]}}: {{row[1]}} records")
'''
        print("\n📈 Upload Pattern (last hour):")
        print(run_ssh_query(query))
        
        # Real-time vs backlog classification
        query = f'''
cur.execute("""
    SELECT 
        SUM(CASE WHEN (strftime('%s', created_at) - strftime('%s', ts)) < 60 THEN 1 ELSE 0 END) as realtime,
        SUM(CASE WHEN (strftime('%s', created_at) - strftime('%s', ts)) BETWEEN 60 AND 600 THEN 1 ELSE 0 END) as light_backlog,
        SUM(CASE WHEN (strftime('%s', created_at) - strftime('%s', ts)) > 600 THEN 1 ELSE 0 END) as heavy_backlog
    FROM telemetry 
    WHERE src='{dev}' AND ts >= datetime('now', '-1 day')
""")
row = cur.fetchone()
total = row[0] + row[1] + row[2]
if total > 0:
    print(f"Real-time (<1min): {{row[0]}} ({{row[0]/total*100:.1f}}%)")
    print(f"Light backlog (1-10min): {{row[1]}} ({{row[1]/total*100:.1f}}%)")
    print(f"Heavy backlog (>10min): {{row[2]}} ({{row[2]/total*100:.1f}}%)")
'''
        print("\n🎯 Classification (24h):")
        print(run_ssh_query(query))
    
    print("\n" + "=" * 80)
    print("✅ Monitor complete")
    print("=" * 80)

if __name__ == "__main__":
    main()
