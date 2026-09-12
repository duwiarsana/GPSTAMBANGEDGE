import sqlite3

conn = sqlite3.connect('/opt/kutai-dashboard-backend/telemetry.db')
cur = conn.cursor()

# Get the latest message ID by natural order or by parsing the timestamp in the ID
# The ID format is EXCA03-864022083269463-20260628T142552Z-125852
# Let's query the latest 10 messages for EXCA03 ordered by created_at desc
cur.execute("SELECT id, created_at FROM telemetry WHERE src='EXCA03' ORDER BY created_at DESC LIMIT 10")
print("Latest 10 EXCA03 by created_at:")
for r in cur.fetchall():
    print(r)

# Let's also query if there are any IDs containing '20260714' or '20260715' for EXCA03
cur.execute("SELECT COUNT(*), MAX(id) FROM telemetry WHERE src='EXCA03' AND id LIKE '%2026071%'")
print("\nEXCA03 IDs containing '2026071':")
print(cur.fetchone())

# Let's check the absolute maximum timestamp parsed from payload (ts column) for EXCA03
cur.execute("SELECT COUNT(*), MIN(ts), MAX(ts) FROM telemetry WHERE src='EXCA03'")
print("\nEXCA03 'ts' column min/max:")
print(cur.fetchone())

conn.close()
