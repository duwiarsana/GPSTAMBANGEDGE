import sqlite3
import sys

conn = sqlite3.connect('/opt/kutai-dashboard-backend/telemetry.db')
cur = conn.cursor()
cur.execute("SELECT id FROM telemetry WHERE src='DT09' ORDER BY created_at DESC LIMIT 1")
res = cur.fetchone()
print(f"Latest DT09: {res}")

cur.execute("SELECT id FROM telemetry WHERE src='DT01' ORDER BY created_at DESC LIMIT 1")
res = cur.fetchone()
print(f"Latest DT01: {res}")
