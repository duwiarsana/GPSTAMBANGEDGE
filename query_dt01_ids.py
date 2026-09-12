import sqlite3

conn = sqlite3.connect('/opt/kutai-dashboard-backend/telemetry.db')
cur = conn.cursor()
cur.execute("SELECT id, created_at FROM telemetry WHERE src='DT01' ORDER BY created_at DESC LIMIT 20")
rows = cur.fetchall()
print("Latest 20 messages in DB for DT01:")
for r in rows:
    print(r)
conn.close()
