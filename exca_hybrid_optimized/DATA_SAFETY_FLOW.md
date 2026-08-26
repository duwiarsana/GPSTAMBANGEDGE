# Data Safety Flow - EXCA Hybrid V2

## 🛡️ Zero Data Loss Guarantee

### Scenario: EXCA di Area Mining (No WiFi)

```
┌─────────────────────────────────────────────────────────────────┐
│ TIME: 06:00 - 08:00 (2 jam di area mining, no WiFi)             │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│ GPS Recording (every 5 seconds):                                │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐            │
│  │ Record  │  │ Record  │  │ Record  │  │ ...     │            │
│  │   #1    │  │   #2    │  │   #3    │  │         │            │
│  └────┬────┘  └────┬────┘  └────┬────┘  └─────────┘            │
│       │           │           │                                 │
│       ▼           ▼           ▼                                 │
│  ┌─────────────────────────────────────────┐                   │
│  │     microSD: /gps_log.jsonl             │                   │
│  │  Line 1: {"id":"#1", ...}               │                   │
│  │  Line 2: {"id":"#2", ...}               │                   │
│  │  Line 3: {"id":"#3", ...}               │                   │
│  │  ...                                    │                   │
│  │  Line 240: {"id":"#240", ...}           │                   │
│  └─────────────────────────────────────────┘                   │
│                                                                 │
│  ✅ Data SAVED to microSD                                       │
│  ✅ Offset: 0 (not advanced)                                    │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│ Upload Attempts (every 15 seconds):                             │
│                                                                 │
│  T+06:00:00 → tryInternetAndPublishChunk()                      │
│    → connectKnownInternet()                                     │
│    → Scan WiFi: "WIFI_GATEWAY_MINING_11" → NOT FOUND           │
│    → Scan WiFi: "HOTSPOT_DT_KEAMANAN" → NOT FOUND              │
│    → Result: FAIL (no WiFi)                                     │
│    → return (exit without uploading)                            │
│    → Offset: STILL 0 (not advanced)                             │
│    → Data: STILL ON SD (not deleted)                            │
│                                                                 │
│  T+06:00:15 → tryInternetAndPublishChunk()                      │
│    → connectKnownInternet() → FAIL (no WiFi)                    │
│    → Offset: STILL 0                                            │
│    → Data: STILL ON SD                                          │
│                                                                 │
│  T+06:00:30 → ... (retry)                                       │
│  T+06:00:45 → ... (retry)                                       │
│  ...                                                            │
│  T+07:59:45 → ... (retry)                                       │
│                                                                 │
│  ✅ Data SAFE on microSD (240 records)                          │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│ TIME: 08:00 - 08:10 (EXCA moves to WiFi coverage area)          │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│ Upload Sessions:                                                │
│                                                                 │
│  Session 1 (08:00:00):                                          │
│    → connectKnownInternet() → SUCCESS (WiFi: "WIFI_GATEWAY_11") │
│    → connectMQTT() → SUCCESS                                    │
│    → publishQueueFileChunk(max 50 records)                      │
│      - Publish #1 → ACK #1 ✓ → offset advance                   │
│      - Publish #2 → ACK #2 ✓ → offset advance                   │
│      - ...                                                      │
│      - Publish #50 → ACK #50 ✓ → offset advance                 │
│    → 50 records uploaded                                        │
│                                                                 │
│  Session 2 (08:00:15):                                          │
│    → publishQueueFileChunk(max 50 records)                      │
│      - Publish #51-100 → all ACK received ✓                     │
│    → 50 records uploaded                                        │
│                                                                 │
│  Session 3 (08:00:30):                                          │
│    → Publish #101-150 → all ACK received ✓                      │
│    → 50 records uploaded                                        │
│                                                                 │
│  Session 4 (08:00:45):                                          │
│    → Publish #151-200 → all ACK received ✓                      │
│    → 50 records uploaded                                        │
│                                                                 │
│  Session 5 (08:01:00):                                          │
│    → Publish #201-240 → all ACK received ✓                      │
│    → 50 records uploaded                                        │
│                                                                 │
│  ✅ All 240 records uploaded successfully                        │
│  ✅ Offset: 240 (all data acknowledged)                          │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│ Compaction (30 minutes after upload):                           │
│                                                                 │
│  → compactQueueFile()                                           │
│    → Read offset: 240                                           │
│    → Seek to position 240 (end of file)                         │
│    → No data remaining to keep                                  │
│    → Remove /gps_log.jsonl                                      │
│    → Create empty /gps_log.jsonl                                │
│    → Reset offset to 0                                          │
│                                                                 │
│  ✅ SD card space recovered (240 records cleaned)                │
│  ✅ Ready for new data                                          │
└─────────────────────────────────────────────────────────────────┘
```

---

## 📊 State Machine

```
┌─────────────────────────────────────────────────────────────────┐
│                        STATE MACHINE                            │
└─────────────────────────────────────────────────────────────────┘

[START]
   │
   ▼
[GPS Recording] ─────┐
   │                 │
   ▼                 │ (every 5s)
[Save to SD]         │
   │                 │
   ▼                 │
[Wait 15s] ──────────┘
   │
   ▼
[Try WiFi Connect]
   │
   ├─► FAIL (no WiFi) ────┐
   │   │                  │
   │   ▼                  │
   │ [Return]             │ (loop back)
   │   │                  │
   │   └──────────────────┘
   │
   ▼
[SUCCESS]
   │
   ▼
[Connect MQTT]
   │
   ├─► FAIL ──────────────┐
   │   │                  │
   │   ▼                  │
   │ [Disconnect WiFi]    │
   │   │                  │
   │   ▼                  │
   │ [Return]             │ (loop back)
   │   │                  │
   │   └──────────────────┘
   │
   ▼
[Publish Record #1]
   │
   ▼
[Wait ACK (2s)]
   │
   ├─► TIMEOUT ───────────┐
   │   │                  │
   │   ▼                  │
   │ [Retry #1]           │ (max 2x)
   │   │                  │
   │   ▼                  │
   │ [Still TIMEOUT]      │
   │   │                  │
   │   ▼                  │
   │ [FAIL]               │
   │   │                  │
   │   ▼                  │
   │ [Stop Chunk]         │
   │   │                  │
   │   ▼                  │
   │ [Offset NOT advanced]│
   │   │                  │
   │   ▼                  │
   │ [Data STILL on SD]   │
   │   │                  │
   │   ▼                  │
   │ [Return]             │ (loop back, retry later)
   │   │                  │
   │   └──────────────────┘
   │
   ▼
[ACK RECEIVED]
   │
   ▼
[Offset ADVANCED]
   │
   ▼
[Data DELETED from SD]
   │
   ▼
[Next Record #2] ─────────┐
   │                      │
   └──────────────────────┘
   │
   ▼
[Chunk Complete (50 records)]
   │
   ▼
[Keep-Alive or Disconnect]
   │
   ▼
[Return to Loop] ─────────┐
   │                      │
   └──────────────────────┘
```

---

## 🎯 Key Safety Guarantees

### 1. **Data Persistence**
- ✅ Data langsung ke microSD (immediate flush)
- ✅ Data TIDAK dihapus sampai ACK diterima
- ✅ Survive power loss, crash, WiFi dropout

### 2. **Offset Safety**
- ✅ Offset advance HANYA setelah ACK diterima
- ✅ Jika no WiFi → no ACK → offset tidak advance
- ✅ Jika ACK timeout → offset tidak advance

### 3. **Retry Forever**
- ✅ Upload retry setiap 15 detik
- ✅ Per-record retry max 2x (configurable)
- ✅ Continue dari offset terakhir (no data loss)

### 4. **Compaction**
- ✅ Hapus data setelah ter-upload & ter-ACK
- ✅ Compaction interval: 30 menit
- ✅ SD card space recovery otomatis

---

## 📈 Real-World Example

```
EXCA03 Journey (24 hours):

06:00 - 08:00: Area mining (no WiFi)
  → 240 records saved to SD
  → 0 records uploaded
  → Offset: 0

08:00 - 08:10: WiFi coverage
  → 240 records uploaded (5 sessions)
  → Offset: 240
  → Compaction cleanup

08:10 - 12:00: Area mining (no WiFi)
  → 528 records saved to SD (12/min × 230 min)
  → 0 records uploaded
  → Offset: 240

12:00 - 12:15: WiFi coverage
  → 528 records uploaded (11 sessions)
  → Offset: 768
  → Compaction cleanup

12:15 - 18:00: Area mining (no WiFi)
  → 1,584 records saved to SD
  → 0 records uploaded
  → Offset: 768

18:00 - 18:20: WiFi coverage
  → 1,584 records uploaded (32 sessions)
  → Offset: 2,352
  → Compaction cleanup

RESULT: ✅ All 2,352 records uploaded successfully
        ✅ Zero data loss
        ✅ SD card space stable
```

---

## ✅ Conclusion

**Tidak ada data yang hilang, bahkan jika:**
- EXCA di luar jangkauan WiFi berjam-jam
- WiFi sporadis (hanya muncul sebentar)
- Backend down untuk sementara
- Subscriber restart/crash

**Data akan:**
1. Tersimpan aman di microSD
2. Retry upload setiap 15 detik
3. Upload otomatis saat WiFi tersedia
4. Dihapus dari SD setelah ter-ACK
5. Cleanup otomatis via compaction

**Zero data loss guarantee!** 🛡️
