import sqlite3
from datetime import datetime

conn = sqlite3.connect('/opt/kutai-dashboard-backend/telemetry.db')
cur = conn.cursor()

# Get list of unique devices
cur.execute("SELECT DISTINCT src FROM telemetry ORDER BY src")
devices = [r[0] for r in cur.fetchall()]

print(f"{'Device':<10} | {'Total Rows':<10} | {'Latest Message ID':<50} | {'Server Received (UTC)':<20}")
print("-" * 105)

for dev in devices:
    # Count rows
    cur.execute("SELECT COUNT(*) FROM telemetry WHERE src=?", (dev,))
    count = cur.fetchone()[0]
    
    # Get latest message
    cur.execute("SELECT id, created_at FROM telemetry WHERE src=? ORDER BY created_at DESC LIMIT 1", (dev,))
    latest = cur.fetchone()
    
    if latest:
        msg_id, created_at = latest
        print(f"{dev:<10} | {count:<10} | {msg_id:<50} | {created_at:<20}")
    else:
        print(f"{dev:<10} | {count:<10} | {'N/A':<50} | {'N/A':<20}")

conn.close()
