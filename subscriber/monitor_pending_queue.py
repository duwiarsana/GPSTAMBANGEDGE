#!/usr/bin/env python3
"""
Monitor pending_ingests queue on VPS
Run this to check if subscriber is keeping up
"""

import sqlite3
from datetime import datetime

def main():
    DB_PATH = '/opt/kutai-dashboard-backend/telemetry.db'
    
    conn = sqlite3.connect(DB_PATH, timeout=30.0)
    cur = conn.cursor()
    
    print("=" * 80)
    print("PENDING_INGESTS QUEUE MONITOR")
    print("=" * 80)
    print(f"Timestamp: {datetime.now()}")
    
    # Total pending
    cur.execute("SELECT COUNT(*) FROM pending_ingests")
    total = cur.fetchone()[0]
    print(f"\n📊 Total pending: {total}")
    
    if total > 1000:
        print("⚠️ WARNING: Queue is large! Subscriber may be behind.")
    elif total > 100:
        print("⚡ Queue growing, monitor closely.")
    else:
        print("✅ Queue healthy.")
    
    # Oldest pending
    cur.execute("SELECT id, created_at FROM pending_ingests ORDER BY created_at ASC LIMIT 1")
    row = cur.fetchone()
    if row:
        msg_id, created_at = row
        age = (datetime.now() - datetime.fromisoformat(created_at.replace('Z', '+00:00'))).total_seconds()
        print(f"\n🕐 Oldest pending: {msg_id}")
        print(f"   Age: {age:.0f} seconds ({age/60:.1f} minutes)")
        
        if age > 3600:  # > 1 hour
            print("⚠️ WARNING: Old pending item! Backend may be down.")
        elif age > 300:  # > 5 minutes
            print("⚡ Queue processing slow.")
        else:
            print("✅ Processing fresh.")
    
    # Per device
    cur.execute("""
        SELECT 
            json_extract(payload, '$.src') as src,
            COUNT(*) as count
        FROM pending_ingests
        GROUP BY src
        ORDER BY count DESC
    """)
    print("\n📱 Pending per device:")
    for row in cur.fetchall():
        print(f"  {row[0]}: {row[1]} pending")
    
    conn.close()
    print("\n" + "=" * 80)

if __name__ == "__main__":
    main()
