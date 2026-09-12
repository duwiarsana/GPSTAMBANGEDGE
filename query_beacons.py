import sqlite3
import json
from collections import defaultdict

conn = sqlite3.connect('/opt/kutai-dashboard-backend/telemetry.db')
cur = conn.cursor()

# Get all EXCA raw payloads
cur.execute("SELECT src, raw_payload FROM telemetry WHERE src LIKE 'EXCA%'")
rows = cur.fetchall()

exca_beacons = defaultdict(set)
all_beacons = set()

for src, raw_payload in rows:
    try:
        data = json.loads(raw_payload)
        be_list = data.get('be', [])
        if isinstance(be_list, list):
            for be in be_list:
                mac = be.get('mac')
                if mac:
                    exca_beacons[src].add(mac)
                    all_beacons.add(mac)
    except Exception as e:
        continue

print(f"Total Unique Beacon MAC Addresses across all EXCA: {len(all_beacons)}")
print("List of all unique Beacon MACs:", sorted(list(all_beacons)))
print("\nBeacons read by each EXCA:")
print("-" * 50)
for exca, macs in sorted(exca_beacons.items()):
    print(f"{exca:<10}: {', '.join(sorted(list(macs)))}")

conn.close()
