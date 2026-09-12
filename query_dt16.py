import sqlite3

conn = sqlite3.connect('/opt/kutai-dashboard-backend/telemetry.db')
cur = conn.cursor()

# Get total count for DT016
cur.execute("SELECT COUNT(*) FROM telemetry WHERE src='DT016'")
count = cur.fetchone()[0]
print(f"Total rows for DT016: {count}")

# Get latest 5 rows for DT016
cur.execute("SELECT id, created_at FROM telemetry WHERE src='DT016' ORDER BY created_at DESC LIMIT 5")
rows = cur.fetchall()
print("\nLatest 5 rows for DT016:")
for r in rows:
    print(r)

# Check duplicates in last 100 rows
cur.execute("SELECT COUNT(id), COUNT(DISTINCT id) FROM (SELECT id FROM telemetry WHERE src='DT016' ORDER BY created_at DESC LIMIT 100)")
dup_info = cur.fetchone()
print(f"\nDuplicates in last 100 rows: Total = {dup_info[0]}, Distinct = {dup_info[1]}")
conn.close()
