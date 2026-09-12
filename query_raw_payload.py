import sqlite3

conn = sqlite3.connect('/opt/kutai-dashboard-backend/telemetry.db')
cur = conn.cursor()
cur.execute("SELECT raw_payload FROM telemetry WHERE id='DT01-864022083265024-20260617T033939Z-2'")
res = cur.fetchone()
if res:
    print(res[0])
else:
    print("Not found in DB")
conn.close()
