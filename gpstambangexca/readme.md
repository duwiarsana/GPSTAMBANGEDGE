# 📦 EXCA GPS Logger & DT Relay System

Sistem logging GPS berbasis ESP32 dengan mekanisme **store-and-forward** untuk lingkungan tanpa internet (seperti tambang), mendukung transfer data ke Dump Truck (DT) via WiFi.

---

## 📌 Overview

Firmware ini digunakan pada perangkat **EXCA (Excavator)** yang bertugas:

- Membaca data GPS dari serial (RS232 → TTL)
- Parsing JSON multi-line dari GPS tracker
- **Smart Recording**: Otomatis memfilter data berdasarkan status mesin (Ignition)
- Menyimpan data ke SD Card (format JSONL)
- Menambahkan UID unik pada setiap data
- Menyediakan WiFi Access Point
- Mengirim data ke DT (Dump Truck) via TCP
- Menggunakan sistem snapshot + offset (anti duplikasi)
- **Anti-Blocking**: Tetap memproses GPS saat transfer WiFi sedang sibuk

---

## 🧠 Arsitektur Sistem

```
GPS Device
   ↓ (Serial JSON multi-line)
ESP32 (EXCA)
   ↓
SD Card (JSONL log)
   ↓
WiFi AP (EXCA)
   ↓
DT (Dump Truck)
   ↓
MQTT / Backend Server
```

---

## ⚙️ Fitur Utama

### 🔹 1. Parser JSON Multi-line (Real GPS Compatible)
- Support JSON bertingkat (nested)
- Support streaming per karakter
- Menggunakan brace counter untuk validasi JSON utuh

---

### 🔹 2. Logging ke SD Card
- Format: JSONL (1 line = 1 record)
- File: `/gps_log.jsonl`
- Flush setiap record (aman untuk power loss)

---

### 🔹 3. Smart Recording (Ignition-Based)
Sistem secara cerdas memfilter data yang disimpan ke SD Card untuk menghemat kapasitas:
- **Ignition ON (1)**: Langsung mulai mencatat data ke SD Card.
- **Ignition OFF (0)**: Tetap mencatat selama **3 menit (cooldown)** sebelum berhenti.
- **Event Penting**: Data "Ignition On" (event 2) dan "Ignition Off" (event 3) **SELALU dicatat** sebagai audit trail.
- **Tujuan**: Menghemat SD Card (60-70%) dan meringankan ukuran file transfer.

---

### 🔹 3. UID System (Anti Duplicate)

Format UID:

```
EXCA01-IMEI-TIMESTAMP-SEQ
```

Contoh:

```
EXCA01-861327085560006-20260326T141225Z-1023
```

---

### 🔹 4. Snapshot System (DT Friendly)

Saat DT connect:
- EXCA membuat snapshot dari log
- Hanya data baru (berdasarkan offset)

File:
```
/snap.jsonl
```

---

### 🔹 5. Offset System (Anti Double Send)

File:
```
/offset.txt
```

Fungsi:
- Menyimpan posisi terakhir yang sudah dikirim ke DT
- Update hanya setelah transfer sukses

---

### 🔹 6. Transfer Protocol EXCA ↔ DT

Handshake:

```
DT → HELLO
EXCA → READY
DT → GET
EXCA → kirim data
DT → NEXT (per line)
EXCA → END
DT → OK
```

---

### 🔹 7. WiFi Access Point

```
SSID: EXCA01_DATA
PASS: 12345678
IP:   192.168.4.1
PORT: 5000
```

---

### 🔹 8. LED Status Indicator

| LED | GPIO | State | Pattern | Keterangan |
|-----|------|-------|---------|------------|
| 🔵 | 2 | LOG | Blink (100ms) | Berkedip setiap ada data GPS yang masuk & di-log |
| 🔴 | 4 | TRANS | ON | Menyala saat ada transfer data ke Dump Truck |
| 🟡 | 13 | REC | **OFF** | IDLE: Mesin mati, data di-skip |
| 🟡 | 13 | REC | **Slow Blink** | ACTIVE: Mesin hidup, data dicatat |
| 🟡 | 13 | REC | **Fast Blink** | COOLDOWN: Mesin baru mati, mencatat selama 3 menit |

---

## 🧰 Hardware Requirement

- ESP32
- SD Card Module
- GPS Tracker (RS232 output)
- RS232 to TTL converter (MAX3232)
- Power supply stabil (WAJIB)

---

## ⚠️ Power Requirement (PENTING)

ESP32 sangat sensitif terhadap tegangan.

Gunakan:
- Power supply minimal **5V 2A**
- Kapasitor:
  - 100µF
  - 10µF
  - 0.1µF

Jika tidak stabil:
```
Brownout detector was triggered
```

---

## 📡 Format Data GPS

Contoh data dari device:

```json
{
  "protocol":"Json-V001",
  "model":"NL02",
  "imei":"861327085560006",
  "timestamp":"2026-03-26T14:12:25Z",
  "latitude":-6.390116,
  "longitude":106.994792
}
```

---

## 🔄 Flow Data

### Saat Logging
```
GPS → EXCA → SD
```

### Saat DT Connect
```
EXCA → Snapshot → Transfer → Update Offset
```

---

## 🛡️ Safety & Reliability

| Fitur | Status |
|-------|:---:|
| Anti JSON corrupt (brace counter) | ✔ |
| Timeout parser (4 detik) | ✔ |
| Buffer overflow protection (4096 byte) | ✔ |
| Smart Recording (Ignition-based) | ✔ |
| Anti-Blocking Logger (Parallel GPS) | ✔ |
| UID unik (Anti duplicate) | ✔ |
| Snapshot isolation (Transfer safe) | ✔ |
| Power loss safe (Flush per record) | ✔ |
| Resume transfer (Offset-based) | ✔ |
| Non-blocking LED status | ✔ |

---

## 📂 Struktur File di SD

```
/gps_log.jsonl
/snap.jsonl
/offset.txt
/seq.txt
```

---

## 🧠 Konsep Utama

```
Store-and-Forward System
```

---

## 🙌 Credit

Developed by: **Duwi Arsana**
