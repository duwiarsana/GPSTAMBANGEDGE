# 📡 EXCA Monitor — GPS Logger + Web Dashboard

Versi **testing/monitor** dari firmware EXCA. GPS Logger dengan **web dashboard** yang bisa diakses dari browser HP untuk memantau dan memvalidasi data GPS secara real-time.

> **Catatan**: Versi ini **tidak memiliki** fungsi transfer data ke DT. Gunakan untuk testing dan validasi sebelum deploy versi produksi (`gpstambangexca`).

---

## 📌 Overview

Firmware ini digunakan untuk:

- Membaca data GPS dari serial (RS232 → TTL)
- Parsing JSON multi-line dari GPS tracker
- Menyimpan data ke SD Card (format JSONL)
- Menambahkan UID unik pada setiap data
- **Menyediakan web dashboard** yang bisa dibuka dari browser HP

---

## 🧠 Arsitektur

```
GPS Tracker
    ↓ (Serial JSON)
ESP32 (EXCA Monitor)
    ↓
SD Card (/gps_log.jsonl)
    ↓
WiFi AP (EXCA01_MONITOR)
    ↓
HP/Laptop → Browser → http://192.168.4.1
```

---

## 📱 Cara Menggunakan

### Step 1: Upload Firmware

```
1. Buka Arduino IDE
2. Pilih Board: ESP32 Dev Module
3. Upload file gpstambangexca_monitor.ino
```

### Step 2: Connect HP ke WiFi

```
SSID:     EXCA01_MONITOR
Password: 12345678
```

### Step 3: Buka Browser

```
http://192.168.4.1
```

Dashboard akan muncul dengan data GPS real-time.

---

## 🖥️ Fitur Web Dashboard

### 1. System Status
- **Uptime** — berapa lama device sudah menyala
- **Total Records** — jumlah data GPS yang tersimpan di SD
- **Free Memory** — sisa RAM ESP32 (monitoring kesehatan)
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

### 3. Riwayat Log
Tabel 30 data terbaru dengan kolom:
- Waktu
- Latitude
- Longitude
- Source

### 4. Auto Refresh
- Data di-update otomatis setiap **3 detik**
- Progress bar animasi menunjukkan countdown refresh
- Indicator **LIVE** berkedip menandakan dashboard aktif

---

## 🌐 API Endpoints

Dashboard menggunakan REST API, bisa juga diakses langsung:

| Endpoint | Method | Response | Keterangan |
|----------|--------|----------|-----------|
| `/` | GET | HTML | Halaman dashboard |
| `/api/status` | GET | JSON | Uptime, records, heap, SD status |
| `/api/latest` | GET | JSON | Data GPS terakhir |
| `/api/logs?n=30` | GET | JSON Array | N data terbaru (max 30) |

### Contoh Response `/api/status`

```json
{
  "uptime": 325000,
  "totalRecords": 145,
  "freeHeap": 245760,
  "sdReady": true,
  "seq": 145,
  "deviceId": "EXCA01"
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
  "msg_id": "EXCA01-861327085560006-20260326T141225Z-145",
  "source": "EXCA01"
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
Format: `EXCA01-IMEI-TIMESTAMP-SEQ`

Sequence disimpan persisten di `/seq.txt`.

### SD Card Logging
- Format: JSONL (1 baris = 1 record)
- Flush setiap record (aman untuk power loss)
- Auto-count record saat boot

### History Ring Buffer
- 30 record terbaru disimpan di RAM
- Update otomatis setiap data baru masuk
- Digunakan oleh web API (tanpa baca ulang SD)

### LED Indicator
| LED | GPIO | Fungsi |
|-----|------|--------|
| 🔵 | 2 | Kedip saat GPS data masuk + SD logging (auto-off 100ms) |

---

## 📂 Struktur File di SD Card

```
📂 SD Card
├── gps_log.jsonl    ← Log semua data GPS
└── seq.txt          ← Sequence counter UID
```

---

## 🔧 Konfigurasi

### WiFi AP

```cpp
const char* AP_SSID = "EXCA01_MONITOR";  // Ganti sesuai kebutuhan
const char* AP_PASS = "12345678";
```

### Pin

```cpp
#define RXD2    16    // GPS RX
#define TXD2    17    // GPS TX
#define SD_CS    5    // SD Card CS
#define LED_LOG  2    // LED indicator
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

| Fitur | EXCA Produksi | EXCA Monitor |
|-------|:---:|:---:|
| GPS Logging | ✔ | ✔ |
| SD Card Storage | ✔ | ✔ |
| UID System | ✔ | ✔ |
| Transfer ke DT (TCP) | ✔ | ✘ |
| Snapshot System | ✔ | ✘ |
| Offset System | ✔ | ✘ |
| Web Dashboard | ✘ | ✔ |
| REST API | ✘ | ✔ |
| WiFi AP SSID | `EXCA01_DATA` | `EXCA01_MONITOR` |

---

## 🙌 Credit

Developed by: **Duwi Arsana** 🚀
