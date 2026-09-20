# Realtime Mining Fleet Tracker (Binary Edition)

Dashboard visualisasi realtime posisi unit **Dump Truck (DT)** dan **Excavator (EXCA)** berbasis protokol Binary MQTT (66-Byte Struct V2) tanpa respon ACK (Mode Murni Observer).

---

## 🌟 Fitur Utama

1. **Observer Murni (Tanpa ACK)**:
   - Dashboard hanya bertindak sebagai pemantau (*listener*).
   - Tidak mengirim publish balasan ACK ke broker MQTT, sehingga tidak akan mengganggu alur transmisi backend utama maupun ESP32.
2. **Realtime Push via WebSocket**:
   - Setiap paket binary yang masuk dari ESP32 DT / EXCA langsung di-unpack dan disalurkan ke web browser via WebSocket (<50ms delay).
3. **In-Memory & Persistent Last Known Position**:
   - Jika armada sedang di luar jangkauan sinyal / tidak ada paket baru yang masuk, posisi terakhir unit **tetap bertahan di map**.
   - Posisi terakhir di-*cache* secara otomatis ke file `latest_fleet_state.json` sehingga jika dashboard di-restart, posisi unit tidak hilang.
4. **Satelit Imagery Map Berkualitas Tinggi**:
   - Menggunakan peta satelit resolusi tinggi ESRI World Imagery (sangat jelas untuk area konsesi tambang / pit / jalan hauling).
   - Tersedia opsi ganti layer peta: **Satellite**, **Topo**, dan **Street**.
5. **Indikator Visual Khusus**:
   - Ikon khusus: 🚚 Dump Truck (Biru Cyan) dan 🚜 Excavator (Kuning Amber).
   - Penanda arah hadap unit (*heading orientation*) rotasi 360°.
   - Animasi **PTO Dumping** berkedip merah saat bak dump truck sedang terangkat.
   - Popup info lengkap: Kecepatan, Tegangan Aki (Volt), Ketinggian (Altitude), Kontak Mesin (Ignition), MAC Beacon, Driver ID.

---

## 🚀 Cara Menjalankan

Masuk ke folder project:

```bash
cd "dashboard realtime binary"
```

Jalankan server:

```bash
npm start
```

Atau:

```bash
node server.js
```

Buka browser di:
👉 **[http://localhost:3000](http://localhost:3000)**

---

## ⚙️ Konfigurasi Environment (Opsional)

Jika ingin mengganti port atau broker MQTT, dapat menggunakan environment variables:

```bash
PORT=3000 \
MQTT_BROKER=mqtt://34.101.180.48:1883 \
MQTT_USER=kutai \
MQTT_PASS=79750d76450466d56b9f44926f38614a3846bdbf \
MQTT_BINARY_TOPIC=kutai/fleet/binary \
node server.js
```
