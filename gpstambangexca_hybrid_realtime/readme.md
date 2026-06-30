# 🚜 gpstambangexca_hybrid_realtime (Excavator Hybrid - Real-time Priority)

Firmware ini digunakan pada unit Excavator (EXCA) tipe Hybrid yang memiliki kemampuan koneksi internet mandiri (WiFi Client/GSM) sekaligus bertindak sebagai Access Point lokal untuk unit di sekitarnya.

---

## 📌 Fitur Utama

- **Penyimpanan Lokal Mandiri**: Menulis koordinat GPS bersih ke SD card log file (`/gps_log.jsonl`).
- **Prioritas Real-time**: Jika koneksi WiFi dan MQTT terhubung langsung (client mode), koordinat GPS terbaru langsung dipublikasikan seketika ke topic broker MQTT (`kutai/fleet/data`) tanpa perlu mengantre di belakang data lama.
- **Pemberes Antrean Otomatis**: Jika antrean backlog data lama sudah kosong ( caught up ), sistem secara otomatis memperbarui offset file (`/offset.txt`) untuk mencegah pengiriman data ganda di kemudian hari.
- **Backlog Sync**: Upload data lama yang belum terkirim akibat putus sinyal diproses di latar belakang secara bertahap.

---

## ⚙️ Skema Kerja Prioritas

```
GPS Sensor
   ↓ (Parsing & Validation)
Clean JSON Coordinate
   ↓
Tulis ke SD Card (/gps_log.jsonl)
   ↓
WiFi/MQTT Terhubung?
   ├── YA  → Publikasikan langsung ke MQTT
   │         └─ Antrean Selesai? (Offset >= Old Size)
   │               ├── YA  → Majukan Offset ke Ukuran File Baru (No Duplicates)
   │               └── TIDAK → Tetap biarkan (Backlog Sync akan memproses nanti)
   └── TIDAK → Lewati (Hanya tersimpan di SD Card, menunggu koneksi kembali)
```
