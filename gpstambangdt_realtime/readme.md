# 🚚 gpstambangdt_realtime (Dump Truck - Real-time Priority)

Firmware ini merupakan varian dari logger GPS Dump Truck (DT) yang dilengkapi dengan fitur **Prioritas Real-time (Dual-Publishing)**.

---

## 📌 Fitur Utama

- **Penyimpanan Lokal Mandiri**: Menulis koordinat GPS bersih ke SD card log file (`/dt_log.jsonl`).
- **Prioritas Real-time**: Jika koneksi WiFi dan MQTT terhubung, koordinat GPS terbaru langsung dipublikasikan seketika ke topic broker MQTT (`kutai/fleet/data`) tanpa perlu mengantre di belakang data lama.
- **Pemberes Antrean Otomatis**: Jika antrean backlog data lama sudah kosong ( caught up ), sistem secara otomatis memperbarui offset file (`/dt_offset.txt`) untuk mencegah pengiriman data ganda di kemudian hari.
- **Backlog Sync**: Upload data lama yang belum terkirim akibat putus sinyal diproses di latar belakang secara bertahap.

---

## ⚙️ Skema Kerja Prioritas

```
GPS Sensor
   ↓ (Parsing & Validation)
Clean JSON Coordinate
   ↓
Tulis ke SD Card (/dt_log.jsonl)
   ↓
WiFi/MQTT Terhubung?
   ├── YA  → Publikasikan langsung ke MQTT
   │         └─ Antrean Selesai? (Offset >= Old Size)
   │               ├── YA  → Majukan Offset ke Ukuran File Baru (No Duplicates)
   │               └── TIDAK → Tetap biarkan (Backlog Sync akan memproses nanti)
   └── TIDAK → Lewati (Hanya tersimpan di SD Card, menunggu koneksi kembali)
```

---

## 🔌 Konfigurasi
Konfigurasi WiFi, Broker MQTT, dan Pinout SD card sama seperti pada versi standard:
- **Default Topic**: `kutai/fleet/data`
- **Default ACK Topic**: `kutai/fleet/ack/DT014` (contoh)
