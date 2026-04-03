# 🏗️ GPS Tambang Edge — Fleet Tracking System

Sistem tracking GPS berbasis **ESP32** untuk armada tambang (Excavator & Dump Truck) menggunakan arsitektur **Delay-Tolerant Network (DTN)** dan **Store-and-Forward**.

Dirancang untuk lingkungan **tanpa internet** seperti area tambang, dengan mekanisme relay data dari EXCA → DT → Backend Server via MQTT.

---

## 🧠 Arsitektur Keseluruhan

```
┌─────────────────────────────────────────────────────────────┐
│                    AREA TAMBANG (Tanpa Internet)             │
│                                                             │
│   ┌──────────┐    WiFi AP     ┌──────────┐                  │
│   │  GPS     │  ──────────►   │  ESP32   │                  │
│   │  Tracker │  RS232/TTL     │  EXCA    │                  │
│   └──────────┘                │          │                  │
│                               │ SD Card  │                  │
│                               └────┬─────┘                  │
│                                    │ WiFi Transfer           │
│                                    ▼                        │
│   ┌──────────┐                ┌──────────┐                  │
│   │  GPS     │  ──────────►   │  ESP32   │                  │
│   │  Tracker │  RS232/TTL     │  DT      │                  │
│   └──────────┘                │          │                  │
│                               │ SD Card  │                  │
│                               └────┬─────┘                  │
│                                    │                        │
└────────────────────────────────────┼────────────────────────┘
                                     │ WiFi Internet
                                     ▼
                              ┌──────────────┐
                              │  MQTT Broker │
                              │  (HiveMQ)    │
                              └──────┬───────┘
                                     │
                                     ▼
                              ┌──────────────┐
                              │   Backend    │
                              │   Server     │
                              └──────────────┘
```

---

## 📦 Struktur Project

```
GPS TAMBANG EDGE/
│
├── gpstambangexca/              ← EXCA Produksi
│   ├── gpstambangexca.ino       ← GPS Logger + Transfer ke DT
│   └── readme.md
│
├── gpstambangdt/                ← DT Produksi
│   ├── gpstambangdt.ino         ← GPS Logger + Relay EXCA + MQTT
│   └── readme.md
│
├── gpstambangexca_monitor/      ← EXCA Testing/Monitor
│   ├── gpstambangexca_monitor.ino  ← GPS Logger + Web Dashboard
│   └── readme.md
│
├── gpstambangdt_monitor/        ← DT Testing/Monitor
│   ├── gpstambangdt_monitor.ino    ← GPS Logger + Web Dashboard
│   └── readme.md
│
└── readme.md                    ← File ini
```

---

## 🔧 Versi Firmware

### Versi Produksi

| Firmware | Fungsi | WiFi Mode |
|----------|--------|-----------|
| `gpstambangexca` | GPS logger + transfer data ke DT via TCP | AP (`EXCA01_DATA`) |
| `gpstambangdt` | GPS logger + relay data EXCA + kirim MQTT ke backend | STA (bergantian EXCA ↔ Internet) |

### Versi Monitor (Testing)

| Firmware | Fungsi | WiFi Mode |
|----------|--------|-----------|
| `gpstambangexca_monitor` | GPS logger + web dashboard di browser HP | AP (`EXCA01_MONITOR`) |
| `gpstambangdt_monitor` | GPS logger + web dashboard di browser HP | AP (`DT01_MONITOR`) |

> **Tips**: Gunakan versi monitor untuk **testing & validasi** di lapangan. Setelah yakin data masuk dengan benar, ganti ke versi produksi.

---

## 🔄 Alur Data (Produksi)

### 1. Logging GPS

```
GPS Tracker → RS232/TTL → ESP32 → Parse JSON → Tambah UID → SD Card
```

### 2. Transfer EXCA → DT

```
DT scan WiFi → Connect ke EXCA AP → Handshake TCP → Ambil data baru → Simpan ke SD
```

### 3. DT → Backend

```
DT scan WiFi Internet → Connect MQTT → Publish data + tunggu ACK → Update offset
```

---

## 📡 Format Data GPS

Data dari GPS Tracker (input):

```json
{
  "protocol": "Json-V001",
  "model": "NL02",
  "imei": "861327085560006",
  "timestamp": "2026-03-26T14:12:25Z",
  "latitude": -6.390116,
  "longitude": 106.994792
}
```

Data setelah diproses (tersimpan di SD):

```json
{
  "protocol": "Json-V001",
  "model": "NL02",
  "imei": "861327085560006",
  "timestamp": "2026-03-26T14:12:25Z",
  "latitude": -6.390116,
  "longitude": 106.994792,
  "msg_id": "EXCA01-861327085560006-20260326T141225Z-1023",
  "source": "EXCA01"
}
```

---

## 🧰 Hardware Requirement

| Komponen | Keterangan |
|----------|-----------|
| ESP32 | DevKit V1 atau WROOM-32 |
| SD Card Module | SPI, CS pin GPIO5 |
| MicroSD Card | Min. 2GB, Class 10 recommended |
| GPS Tracker | RS232 output, format JSON (contoh: NL02) |
| RS232 to TTL | MAX3232 converter module |
| Power Supply | **5V 2A stabil** (WAJIB) |

### Wiring

| ESP32 Pin | Koneksi |
|-----------|---------|
| GPIO 16 (RX2) | GPS TX (via MAX3232) |
| GPIO 17 (TX2) | GPS RX (via MAX3232) |
| GPIO 5 | SD Card CS |
| GPIO 18 | SD Card SCK |
| GPIO 19 | SD Card MISO |
| GPIO 23 | SD Card MOSI |
| GPIO 2 | LED Biru (GPS log indicator) |
| GPIO 4 | LED Merah (Transfer/EXCA indicator) — EXCA & DT produksi |
| GPIO 15 | LED (MQTT indicator) — DT produksi |

---

## ⚠️ Power Requirement (PENTING)

ESP32 sangat sensitif terhadap tegangan. Jika power tidak stabil, akan muncul:

```
Brownout detector was triggered
```

**Solusi:**

- Power supply minimal **5V 2A**
- Tambahkan kapasitor di dekat ESP32:
  - 100µF (elektrolit)
  - 10µF (elektrolit)
  - 0.1µF (keramik)
- Gunakan kabel pendek dan tebal
- Di lingkungan tambang, gunakan regulator DC-DC yang stabil

---

## 📊 Estimasi Kapasitas SD Card

| Interval GPS | Data/hari | 5 Hari | SD 2GB cukup untuk |
|:---:|:---:|:---:|:---:|
| 10 detik | 8.640 record (~2.5 MB) | ~12.5 MB | ~400 hari |
| 30 detik | 2.880 record (~840 KB) | ~4.1 MB | ~1.200 hari |
| 60 detik | 1.440 record (~420 KB) | ~2 MB | ~2.400 hari |

> SD Card **tidak akan penuh** dalam waktu operasional normal.

---

## 🛡️ Fitur Safety & Reliability

| Fitur | EXCA | DT | Keterangan |
|-------|:---:|:---:|-----------|
| Anti JSON corrupt | ✔ | ✔ | Brace counter + validasi field |
| Timeout parser | ✔ | ✔ | 4 detik max per JSON |
| Buffer overflow protection | ✔ | ✔ | Buffer 4096 byte dengan cek limit |
| Anti duplicate (UID) | ✔ | ✔ | Format: `DEVICE-IMEI-TIMESTAMP-SEQ` |
| Power loss safe | ✔ | ✔ | Flush setiap record |
| Resume transfer | ✔ | ✔ | Offset-based |
| Retry publish | — | ✔ | 3x retry per record ke MQTT |
| ACK backend | — | ✔ | Offset update hanya setelah ACK |
| Concurrency guard | ✔ | ✔ | Flag busy mencegah konflik |
| File compaction | — | ✔ | Bersihkan data lama dari SD |
| Heartbeat monitoring | — | ✔ | Status setiap 60 detik |
| LED indicator | ✔ | ✔ | Visual feedback di lapangan |

---

## 🚀 Quick Start

### 1. Persiapan

```
1. Install Arduino IDE
2. Install board ESP32 (Board Manager → esp32 by Espressif)
3. Install library:
   - ArduinoJson (by Benoit Blanchon)
   - PubSubClient (by Nick O'Leary) — hanya untuk DT produksi
4. Rakit hardware sesuai wiring diagram
```

### 2. Upload Firmware

```
1. Pilih Board: ESP32 Dev Module
2. Pilih Port: COM port yang sesuai
3. Upload firmware yang diinginkan
```

### 3. Testing dengan Versi Monitor

```
1. Upload versi monitor ke ESP32
2. Connect HP ke WiFi:
   - EXCA: SSID "EXCA01_MONITOR" / Pass "12345678"
   - DT:   SSID "DT01_MONITOR"   / Pass "12345678"
3. Buka browser → http://192.168.4.1
4. Pastikan data GPS masuk dan tersimpan di SD
```

### 4. Deploy Produksi

```
1. Upload versi produksi ke masing-masing ESP32
2. Konfigurasi DT:
   - Ganti DT_ID sesuai nomor unit
   - Isi WiFi internet di wifiList[]
   - Konfigurasi MQTT broker
3. Test transfer EXCA → DT
4. Test publish DT → MQTT
```

---

## 🙌 Credit

Developed by: **Duwi Arsana** 🚀

---

## 📝 Lisensi

Open source untuk keperluan internal perusahaan.
