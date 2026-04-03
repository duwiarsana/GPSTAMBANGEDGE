# 🚛 DT (Dump Truck) — IoT Relay System

Firmware ESP32 untuk unit **Dump Truck (DT)** yang berfungsi sebagai **relay data** antara EXCA (Excavator) dan backend server menggunakan MQTT.

---

## 📌 Overview

DT menjalankan 3 tugas utama secara bergantian:

1. **Logging GPS sendiri** → menyimpan posisi DT ke SD Card
2. **Mengambil data dari EXCA** → scan WiFi, connect, transfer via TCP
3. **Mengirim data ke backend** → connect WiFi internet, publish MQTT + tunggu ACK

---

## 🧠 Arsitektur Sistem

```
GPS Tracker DT
     ↓ (Serial JSON)
  ESP32 DT
     ↓
  SD Card
  ├── /dt_log.jsonl      ← GPS DT sendiri
  └── /relay_log.jsonl   ← Data dari EXCA

  ESP32 DT ──WiFi──► EXCA AP (ambil data)
  ESP32 DT ──WiFi──► Internet (kirim MQTT)
                           ↓
                      MQTT Broker
                           ↓
                      Backend Server
                           ↓
                        ACK ↑
```

---

## ⚙️ Fitur Utama

### 🔹 1. Dual Logger (Data Terpisah)

| File | Isi | Sumber |
|------|-----|--------|
| `/dt_log.jsonl` | GPS DT sendiri | `handleDTGps()` |
| `/relay_log.jsonl` | Data dari EXCA | `transferFromExca()` |

Data GPS DT ditambahkan field:
- `msg_id` — UID unik
- `source` — ID device (misal: `DT01`)
- `record_type` — `"dt"` untuk membedakan dengan data relay

Data EXCA disimpan **apa adanya** (sudah punya `msg_id` dan `source` dari EXCA).

---

### 🔹 2. UID System (Anti Duplicate)

Format:
```
DT01-IMEI-TIMESTAMP-SEQ
```

Contoh:
```
DT01-861327085560006-20260326T141225Z-55
```

Sequence disimpan persisten di `/dt_seq.txt`, tetap aman meski restart.

---

### 🔹 3. EXCA Auto Discovery

DT secara otomatis scan WiFi setiap **10 detik** mencari SSID yang cocok:

```
Pattern: EXCA*_DATA
```

Contoh SSID yang terdeteksi: `EXCA01_DATA`, `EXCA02_DATA`, dst.

Jika ditemukan beberapa EXCA, DT akan connect ke yang **RSSI terbaik** (sinyal terkuat).

---

### 🔹 4. Transfer Protocol EXCA ↔ DT

Handshake berbasis text-command via TCP:

```
DT    → HELLO
EXCA  → READY        (atau BUSY jika sedang sibuk)
DT    → GET
EXCA  → {json data}  (per baris)
DT    → NEXT         (ACK per baris)
EXCA  → END          (selesai)
DT    → OK           (konfirmasi)
```

Perlindungan:
- DT menangani response `BUSY` → retry nanti
- DT menangani response `NO_DATA` → tidak ada data baru
- Timeout per operasi (5 detik handshake, 8 detik data)
- Validasi JSON sebelum simpan ke SD

---

### 🔹 5. Store & Forward System

Data disimpan dulu di SD Card:
- **Aman walau tidak ada internet** — data tetap di SD
- **Kirim saat internet tersedia** — otomatis publish ke MQTT
- **SD Card tidak akan penuh** — 2GB cukup untuk 400+ hari

---

### 🔹 6. MQTT Communication

| Parameter | Nilai |
|-----------|-------|
| Server | `broker.hivemq.com` (ganti untuk produksi) |
| Port | `1883` |
| Publish Topic | `kutai/fleet/data` |
| ACK Topic | `kutai/fleet/ack/{DT_ID}` |
| Buffer Size | `1024` byte |
| Keep Alive | `30` detik |

---

### 🔹 7. Retry Engine + ACK Backend

Flow publish per record:

```
Publish ke MQTT
    ↓
Tunggu ACK (max 5 detik)
    ├── ACK diterima   → update offset → lanjut record berikutnya
    └── Timeout        → retry (max 3x dengan exponential backoff)
                            └── Gagal semua → stop, coba lagi nanti
```

Backend harus:
1. Simpan data berdasarkan `msg_id` (deduplikasi)
2. Subscribe topic `kutai/fleet/data`
3. Publish ACK ke topic `kutai/fleet/ack/{DT_ID}`

Contoh ACK dari backend:

```json
{
  "msg_id": "EXCA01-861327085560006-20260326T141225Z-1023",
  "status": "ok"
}
```

---

### 🔹 8. Offset System (Anti Double Send)

| File | Fungsi |
|------|--------|
| `/dt_offset.txt` | Posisi terakhir data DT yang sudah terkirim ke MQTT |
| `/relay_offset.txt` | Posisi terakhir data EXCA yang sudah terkirim ke MQTT |

Mekanisme:
- Offset update **hanya setelah ACK diterima** dari backend
- Jika offset > file size → auto reset ke 0
- Jika seek gagal → fallback ke 0

Prioritas publish:
1. **Relay data (EXCA) dulu** — karena lebih sulit didapat
2. **Data DT sendiri** — setelah relay selesai

---

### 🔹 9. File Compaction

Setiap **2 menit**, DT membersihkan data lama dari SD:

- Hanya data yang **sudah terkirim** (offset sudah lewat) yang dihapus
- Threshold: compact hanya jika offset > **4096 byte** (hemat write cycle SD)
- Jika semua data sudah terkirim → file dikosongkan

```
Sebelum compact:
  [data lama - sudah kirim] ← dihapus
  [data baru - belum kirim] ← dipertahankan

Sesudah compact:
  [data baru - belum kirim] ← offset reset ke 0
```

---

### 🔹 10. LED Status Indicator

| LED | GPIO | Fungsi |
|-----|------|--------|
| 🔵 | 2 | Kedip saat GPS data masuk + SD logging (auto-off 100ms) |
| 🟢 | 4 | Menyala saat transfer data dari EXCA |
| 🔴 | 15 | Menyala saat publish data ke MQTT |

---

### 🔹 11. Heartbeat Monitoring

Setiap **60 detik**, DT mencetak status ke Serial Monitor:

```
[60000] 💓 DT01 | GPS:42 | EXCA:120 | MQTT:162 | heap:245760
```

| Field | Keterangan |
|-------|-----------|
| GPS | Jumlah record GPS DT yang sudah di-log |
| EXCA | Jumlah record dari EXCA yang sudah di-relay |
| MQTT | Jumlah record yang sudah terkirim ke MQTT |
| heap | Free heap memory (byte) |

---

### 🔹 12. WiFi Internet — Multi SSID + RSSI Best

DT mendukung **multiple WiFi internet**:

```cpp
WifiCredential wifiList[] = {
  {"WiFi_Kantor", "password123"},
  {"Hotspot_HP", "hotspot456"},
  {"WiFi_Basecamp", "basecamp789"}
};
```

DT akan scan semua WiFi dan pilih yang **RSSI terbaik** (sinyal terkuat).

---

## 🔄 Loop Utama

```
┌──────────────────────────────────────────┐
│  1. handleDTGps()        ← selalu jalan  │
│                                          │
│  2. Scan EXCA (tiap 10 detik)            │
│     → Scan WiFi EXCA                     │
│     → Connect + Transfer                 │
│     → Disconnect + Flush GPS buffer      │
│                                          │
│  3. Internet (tiap 30 detik)             │
│     → Scan WiFi Internet                 │
│     → Connect MQTT                       │
│     → Publish relay + DT data            │
│     → Disconnect + Flush GPS buffer      │
│                                          │
│  4. Compaction (tiap 2 menit)            │
│  5. Heartbeat (tiap 1 menit)            │
└──────────────────────────────────────────┘
```

**Tidak ada konflik WiFi** — semua operasi berjalan bergantian, dilindungi flag `excaTransferBusy` dan interval timer. Setelah setiap operasi WiFi, GPS serial buffer di-flush untuk membuang data basi.

---

## 📂 Struktur File di SD Card

```
📂 SD Card
├── dt_log.jsonl         ← Log GPS DT sendiri
├── relay_log.jsonl      ← Data relay dari EXCA
├── dt_offset.txt        ← Offset publish DT
├── relay_offset.txt     ← Offset publish relay
├── dt_seq.txt           ← Sequence counter UID
├── dt_tmp.jsonl         ← Temp file compaction (auto-hapus)
└── relay_tmp.jsonl      ← Temp file compaction (auto-hapus)
```

---

## 🔧 Konfigurasi

### Ganti ID Device

```cpp
const char* DT_ID = "DT01";   // Ganti: DT02, DT03, dst
```

### WiFi Internet

```cpp
WifiCredential wifiList[] = {
  {"SSID_1", "PASSWORD_1"},
  {"SSID_2", "PASSWORD_2"}
};
```

### MQTT Broker

```cpp
const char* MQTT_SERVER = "broker.hivemq.com";  // Ganti untuk produksi
const uint16_t MQTT_PORT = 1883;
const char* MQTT_DATA_TOPIC = "kutai/fleet/data";
```

### EXCA Connection

```cpp
const char* EXCA_PASS = "12345678";
const uint16_t EXCA_PORT = 5000;
IPAddress excaIP(192, 168, 4, 1);
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
| LED × 3 | GPIO 2 (GPS), GPIO 4 (EXCA), GPIO 15 (MQTT) |
| Power Supply | **5V 2A stabil** |

---

## 🛡️ Safety & Reliability

| Fitur | Status |
|-------|:---:|
| Anti JSON corrupt (brace counter) | ✔ |
| Timeout parser (4 detik) | ✔ |
| Buffer overflow protection (4096 byte) | ✔ |
| Anti duplicate (UID unik) | ✔ |
| Resume transfer (offset-based) | ✔ |
| Power loss safe (flush setiap record) | ✔ |
| Retry publish (3x + exponential backoff) | ✔ |
| ACK backend (offset update hanya saat sukses) | ✔ |
| Relay JSON validation | ✔ |
| Offset vs file size validation | ✔ |
| GPS buffer flush setelah WiFi | ✔ |
| Concurrency guard (busy flag) | ✔ |
| File compaction (threshold 4KB) | ✔ |
| Heartbeat monitoring (60 detik) | ✔ |
| LED status indicator (3 LED) | ✔ |

---

## 🚀 Multi DT Support

Cukup ubah `DT_ID`:

```
DT01 → DT02 → DT03 → ...
```

Setiap DT:
- Punya ACK topic sendiri (`kutai/fleet/ack/DT01`, `kutai/fleet/ack/DT02`, ...)
- Punya MQTT client ID unik
- Bisa jalan bersamaan tanpa konflik
- Bisa ambil data dari EXCA mana saja

---

## 🙌 Credit

Developed by: **Duwi Arsana** 🚀
