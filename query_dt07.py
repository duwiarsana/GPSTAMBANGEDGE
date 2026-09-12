import sqlite3

conn = sqlite3.connect('/opt/kutai-dashboard-backend/telemetry.db')
cur = conn.cursor()

# Get table info
cur.execute("PRAGMA table_info(telemetry)")
columns = cur.fetchall()
print("Columns in telemetry table:")
for col in columns:
    print(col)

# 1. Total row count for DT07
cur.execute("SELECT COUNT(*) FROM telemetry WHERE src='DT07'")
count = cur.fetchone()[0]
print(f"\nTotal rows for DT07: {count}")

# 2. Latest 5 rows for DT07 (without server_received_at)
cur.execute("SELECT id, created_at FROM telemetry WHERE src='DT07' ORDER BY created_at DESC LIMIT 5")
rows = cur.fetchall()
print("\nLatest 5 rows:")
for id_val, created_at in rows:
    print(f"ID: {id_val} | Created: {created_at}")

# 3. Check for duplicates in recent messages
cur.execute("SELECT COUNT(id), COUNT(DISTINCT id) FROM (SELECT id FROM telemetry WHERE src='DT07' ORDER BY created_at DESC LIMIT 100)")
dup_info = cur.fetchone()
print(f"\nDuplicates check in last 100 rows: Total rows = {dup_info[0]}, Distinct IDs = {dup_info[1]}")
conn.close()
