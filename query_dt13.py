import sqlite3

conn = sqlite3.connect('/opt/kutai-dashboard-backend/telemetry.db')
cur = conn.cursor()

# Get total count for DT013
cur.execute("SELECT COUNT(*) FROM telemetry WHERE src='DT013'")
count = cur.fetchone()[0]
print(f"Total rows for DT013: {count}")

# Get latest 10 rows for DT013
cur.execute("SELECT id, created_at FROM telemetry WHERE src='DT013' ORDER BY created_at DESC LIMIT 10")
rows = cur.fetchall()
print("\nLatest 10 rows for DT013:")
for r in rows:
    print(r)

conn.close()
