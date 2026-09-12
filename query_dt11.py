import sqlite3

conn = sqlite3.connect('/opt/kutai-dashboard-backend/telemetry.db')
cur = conn.cursor()

# Get total count for DT011
cur.execute("SELECT COUNT(*) FROM telemetry WHERE src='DT011'")
count = cur.fetchone()[0]
print(f"Total rows for DT011: {count}")

# Get latest 10 rows for DT011
cur.execute("SELECT id, created_at FROM telemetry WHERE src='DT011' ORDER BY created_at DESC LIMIT 10")
rows = cur.fetchall()
print("\nLatest 10 rows for DT011:")
for r in rows:
    print(r)

# Check absolute min/max ts from payload
cur.execute("SELECT MIN(ts), MAX(ts) FROM telemetry WHERE src='DT011'")
print("\nDT011 ts min/max in DB:")
print(cur.fetchone())

conn.close()
