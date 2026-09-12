import sqlite3
from collections import defaultdict

conn = sqlite3.connect('/opt/kutai-dashboard-backend/telemetry.db')
cur = conn.cursor()
cur.execute('SELECT DISTINCT src, id FROM telemetry')
rows = cur.fetchall()

imei_map = defaultdict(set)
for src, id_val in rows:
    parts = id_val.split('-')
    if len(parts) >= 2:
        imei_map[src].add(parts[1])

for src, imeis in sorted(imei_map.items()):
    print(f'{src}: {", ".join(imeis)}')
