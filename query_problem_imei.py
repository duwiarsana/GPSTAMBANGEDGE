import sqlite3
import sys

conn = sqlite3.connect('/opt/kutai-dashboard-backend/telemetry.db')
cur = conn.cursor()
cur.execute("SELECT DISTINCT src, id FROM telemetry WHERE id LIKE '%861327085560279%'")
rows = cur.fetchall()

for src, id_val in rows:
    print(f'{src}: {id_val}')
