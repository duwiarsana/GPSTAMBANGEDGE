import sqlite3
import json
from collections import defaultdict

conn = sqlite3.connect('/opt/kutai-dashboard-backend/telemetry.db')
cur = conn.cursor()

# Get all DT raw payloads
cur.execute("SELECT src, raw_payload FROM telemetry WHERE src LIKE 'DT%'")
rows = cur.fetchall()

# Map DT -> Beacon MAC -> Count
dt_counts = defaultdict(lambda: defaultdict(int))

for src, raw_payload in rows:
    try:
        data = json.loads(raw_payload)
        be_list = data.get('be', [])
        if isinstance(be_list, list):
            for be in be_list:
                mac = be.get('mac')
                if mac in ['C3:00:00:38:B4:50', 'C3:00:00:38:B4:52']:
                    dt_counts[src][mac] += 1
    except Exception as e:
        continue

print(f"{'Device':<10} | {'C3:00:00:38:B4:50':<20} | {'C3:00:00:38:B4:52':<20}")
print("-" * 58)
for dt in sorted(dt_counts.keys()):
    c50 = dt_counts[dt]['C3:00:00:38:B4:50']
    c52 = dt_counts[dt]['C3:00:00:38:B4:52']
    print(f"{dt:<10} | {c50:<20} | {c52:<20}")

conn.close()
