# 🚛 DT Monitor — GPS Logger + Web Dashboard

Versi **testing/monitor** dari firmware DT (Dump Truck). GPS Logger dengan **web dashboard** yang bisa diakses dari browser HP untuk memantau dan memvalidasi data GPS secara real-time.

> **Catatan**: Versi ini **tidak memiliki** fungsi relay EXCA atau MQTT. Gunakan untuk testing dan validasi sebelum deploy versi produksi (`gpstambangdt`).

---

## 📌 Overview

Firmware ini digunakan untuk:

- Membaca data GPS dari serial (RS232 → TTL)
- Parsing JSON multi-line dari GPS tracker
- Menyimpan data ke SD Card (format JSONL)
- Menambahkan UID unik + field `record_type: "dt"` pada setiap data
- **Menyediakan web dashboard** yang bisa dibuka dari browser HP

---

## 🧠 Arsitektur

```
GPS Tracker
    ↓ (Serial JSON)
ESP32 (DT Monitor)
    ↓
SD Card (/dt_log.jsonl)
    ↓
WiFi AP (DT01_MONITOR)
    ↓
HP/Laptop → Browser → http://192.168.4.1
```

---

## 📱 Cara Menggunakan

### Step 1: Upload Firmware

```
1. Buka Arduino IDE
2. Pilih Board: ESP32 Dev Module
3. Upload file gpstambangdt_monitor.ino
```

### Step 2: Connect HP ke WiFi

```
SSID:     DT01_MONITOR
Password: 12345678
```

### Step 3: Buka Browser

```
http://192.168.4.1
```

Dashboard DT (tema orange) akan muncul dengan data GPS real-time.

---

## 🖥️ Fitur Web Dashboard

### 1. System Status
- **Uptime** — berapa lama device sudah menyala
- **Total Records** — jumlah data GPS yang tersimpan di SD
- **Free Memory** — sisa RAM ESP32
- **SD Card** — status SD Card (OK / FAIL)

### 2. Data GPS Terbaru
Menampilkan field terakhir yang diterima:
- 📱 IMEI
- 🕐 Timestamp
- 🌍 Latitude & Longitude
- 📡 Protocol
- 📟 Model
- 🆔 Msg ID
- 🏷️ Source

### 3. 💾 Download Data Logger
Tombol download langsung dari HP:
- **📄 JSONL** — Download file log mentah (format JSON Lines)
- **📊 CSV** — Download dalam format CSV (bisa dibuka di Excel)
- Info jumlah record & ukuran file ditampilkan otomatis

### 4. Riwayat Log
Tabel 30 data terbaru dengan kolom:
- Waktu
- Latitude
- Longitude
- Source

### 5. Auto Refresh
- Data di-update otomatis setiap **3 detik**
- Progress bar animasi (tema orange) menunjukkan countdown refresh
- Indicator **LIVE** berkedip menandakan dashboard aktif

---

## 🌐 API Endpoints

| Endpoint | Method | Response | Keterangan |
|----------|--------|----------|-----------|
| `/` | GET | HTML | Halaman dashboard |
| `/api/status` | GET | JSON | Uptime, records, heap, SD status, fileSize |
| `/api/latest` | GET | JSON | Data GPS terakhir |
| `/api/logs?n=30` | GET | JSON Array | N data terbaru (max 30) |
| `/api/download` | GET | File (JSONL) | Download seluruh log GPS (format JSONL) |
| `/api/download/csv` | GET | File (CSV) | Download seluruh log GPS (format CSV) |

### Contoh Response `/api/status`

```json
{
  "uptime": 325000,
  "totalRecords": 87,
  "freeHeap": 245760,
  "sdReady": true,
  "seq": 87,
  "deviceId": "DT01"
}
```

### Contoh Response `/api/latest`

```json
{
  "protocol": "Json-V001",
  "model": "NL02",
  "imei": "861327085560006",
  "timestamp": "2026-03-26T14:12:25Z",
  "latitude": -6.390116,
  "longitude": 106.994792,
  "msg_id": "DT01-861327085560006-20260326T141225Z-87",
  "source": "DT01",
  "record_type": "dt"
}
```

---

## ⚙️ Fitur Teknis

### GPS Parser
- Streaming per karakter dari Serial2
- Brace counter untuk validasi JSON utuh
- Buffer 4096 byte dengan overflow protection
- Timeout 4 detik per JSON

### UID System
Format: `DT01-IMEI-TIMESTAMP-SEQ`

Sequence disimpan persisten di `/dt_seq.txt`.

### Record Type
Setiap data GPS ditambahkan field `"record_type": "dt"` untuk membedakan dengan data relay dari EXCA saat digunakan di versi produksi.

### SD Card Logging
- Format: JSONL (1 baris = 1 record)
- Flush setiap record (aman untuk power loss)
- Auto-count record saat boot

### History Ring Buffer
- 30 record terbaru disimpan di RAM
- Update otomatis setiap data baru masuk
- Digunakan oleh web API (tanpa baca SD berulang)

### LED Indicator
| LED | GPIO | Fungsi |
|-----|------|--------|
| 🔵 | 2 | Kedip saat GPS data masuk + SD logging (auto-off 100ms) |

---

## 📂 Struktur File di SD Card

```
📂 SD Card
├── dt_log.jsonl     ← Log semua data GPS DT
└── dt_seq.txt       ← Sequence counter UID
```

---

## 🔧 Konfigurasi

### Device ID

```cpp
const char* DT_ID = "DT01";    // Ganti: DT02, DT03, dst
```

### WiFi AP

```cpp
const char* AP_SSID = "DT01_MONITOR";  // Ganti sesuai DT_ID
const char* AP_PASS = "12345678";
```

### Pin

```cpp
#define GPS_RX   16    // GPS RX
#define GPS_TX   17    // GPS TX
#define SD_CS     5    // SD Card CS
#define LED_LOG   2    // LED indicator
```

---

## 🧰 Hardware Requirement

| Komponen | Keterangan |
|----------|-----------|
| ESP32 | DevKit V1 / WROOM-32 |
| SD Card Module | SPI, CS pin GPIO5 |
| MicroSD Card | Min. 2GB, Class 10 |
| GPS Tracker | RS232 output, JSON format |
| RS232 to TTL | MAX3232 module |
| Power Supply | **5V 2A stabil** |

---

## 🛡️ Safety & Reliability

| Fitur | Status |
|-------|:---:|
| Anti JSON corrupt | ✔ |
| Timeout parser (4 detik) | ✔ |
| Buffer overflow protection | ✔ |
| Anti duplicate (UID) | ✔ |
| Power loss safe (flush) | ✔ |
| SD status monitoring | ✔ |
| Memory monitoring | ✔ |

---

## 🆚 Perbedaan dengan Versi Produksi

| Fitur | DT Produksi | DT Monitor |
|-------|:---:|:---:|
| GPS Logging | ✔ | ✔ |
| SD Card Storage | ✔ | ✔ |
| UID System | ✔ | ✔ |
| Relay data EXCA | ✔ | ✘ |
| MQTT Publish | ✔ | ✘ |
| ACK Backend | ✔ | ✘ |
| Retry Engine | ✔ | ✘ |
| File Compaction | ✔ | ✘ |
| Heartbeat Stats | ✔ | ✘ |
| Multi WiFi Internet | ✔ | ✘ |
| Web Dashboard | ✘ | ✔ |
| Download Logger (JSONL/CSV) | ✘ | ✔ |
| REST API | ✘ | ✔ |
| WiFi Mode | STA (bergantian) | AP (tetap) |
| WiFi SSID | — | `DT01_MONITOR` |

---

## 🎨 Tema Dashboard

Dashboard DT menggunakan **tema orange** untuk membedakan dengan EXCA Monitor (tema cyan/biru):

| Device | Tema |
|--------|------|
| EXCA Monitor | 🔵 Cyan / Hijau |
| DT Monitor | 🟠 Orange |

---

## 🙌 Credit

Developed by: **Duwi Arsana** 🚀
