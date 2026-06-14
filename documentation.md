# 📝 Dokumentasi Integrasi & Pengujian Fleet Tracking System

Dokumen ini merangkum langkah-langkah, konfigurasi, dan pengujian yang telah kita selesaikan pada sistem tracking armada pertambangan Kutai Energy.

---

## 🛠️ Ringkasan Pekerjaan

### 1. Sinkronisasi & Pembaruan Alamat IP Server MQTT
* **Pembaruan Kode Firmware**: Menyesuaikan IP server MQTT menjadi **`72.62.126.85`** di berkas utama:
  * [gpstambangdt.ino](file:///Users/duwiarsana/.gemini/antigravity/scratch/GPSTAMBANGEDGE/gpstambangdt/gpstambangdt.ino)
  * [main.cpp](file:///Users/duwiarsana/.gemini/antigravity/scratch/GPSTAMBANGEDGE/gpstambangdt_pio/src/main.cpp) (PlatformIO)
* **Pembaruan Dokumentasi**: Menyesuaikan referensi IP broker lama (`broker.hivemq.com`) menjadi IP baru di berkas:
  * [readme.md (gpstambangdt)](file:///Users/duwiarsana/.gemini/antigravity/scratch/GPSTAMBANGEDGE/gpstambangdt/readme.md)

---

### 2. Konfigurasi Ulang Broker Mosquitto di VPS
Untuk mendukung koneksi dari perangkat ESP32 di lapangan tanpa membutuhkan autentikasi password (*anonymous access*), konfigurasi Mosquitto di VPS `72.62.126.85` disesuaikan sebagai berikut:

* **File Konfigurasi**: `/etc/mosquitto/conf.d/remote-access.conf`
* **Isi Konfigurasi Baru**:
  ```ini
  listener 1883 0.0.0.0
  allow_anonymous true
  max_connections -1
  max_queued_messages 1000
  max_inflight_messages 20
  max_keepalive 300
  ```
* **Status Layanan**: Service `mosquitto` telah di-restart dan berstatus `active (running)`.

---

### 3. Pembuatan Web Dashboard Lokal
Dibuat sebuah folder project web lokal di laptop pada path:
* **Directory**: [dashboard/](file:///Users/duwiarsana/.gemini/antigravity/scratch/GPSTAMBANGEDGE/dashboard/)

Dashboard ini murni berbasis client-side HTML5/CSS/JS (tanpa dependensi local backend) dengan spesifikasi:
* **[index.html](file:///Users/duwiarsana/.gemini/antigravity/scratch/GPSTAMBANGEDGE/dashboard/index.html)**: Layout responsive bertema gelap dengan panel status, statistik, peta, dan data stream.
* **[style.css](file:///Users/duwiarsana/.gemini/antigravity/scratch/GPSTAMBANGEDGE/dashboard/style.css)**: Desain premium bernuansa pertambangan (*charcoal & neon indicators*), *glassmorphism*, dan transisi micro-animation.
* **[app.js](file:///Users/duwiarsana/.gemini/antigravity/scratch/GPSTAMBANGEDGE/dashboard/app.js)**: 
  * Melakukan koneksi WebSocket MQTT ke `ws://72.62.126.85:9001`
  * Subscribe ke topik telemetri `kutai/fleet/data`
  * Sistem **Auto-ACK** interaktif untuk membalas pesan log ke topik `kutai/fleet/ack/DT01` (dan topik unit pengirim) saat data masuk.
  * Peta interaktif menggunakan **Leaflet.js** untuk memetakan titik koordinat secara langsung.
  * Tombol simulator payload untuk pengujian lokal instan.

---

## 🧪 Skenario Pengujian & Hasil

Pengujian end-to-end dilakukan menggunakan script simulator Python untuk mengirimkan 2 paket data telemetri:
1. **Data GPS DT01**: Menguji data yang berasal langsung dari Dump Truck.
2. **Data GPS EXCA01 (Piggybacked)**: Menguji data Excavator yang dibawa/direlay oleh DT01 menuju broker.

### Hasil Log Simulator:
```text
Simulator: Connected to MQTT Broker!

[Step 1] Publishing DT01's own GPS data...
[Step 2] Publishing piggybacked EXCA01 GPS data (relayed by DT01)...

Simulator received ACK on `kutai/fleet/ack/DT01`: {"id":"DT01-861327085560006-20260611T231100Z-99","status":"ok"}
Simulator received ACK on `kutai/fleet/ack/DT01`: {"id":"EXCA01-861999085560111-20260611T230800Z-501","status":"ok"}

--- SIMULATION RESULTS ---
Total ACKs received by simulator: 2
 - ACK verified for Message ID: DT01-861327085560006-20260611T231100Z-99 -> Status: ok
 - ACK verified for Message ID: EXCA01-861999085560111-20260611T230800Z-501 -> Status: ok
```

### Kesimpulan Pengujian:
* **Broker MQTT** di VPS berjalan dengan sangat baik dan menerima koneksi eksternal.
* **Web Dashboard** sukses melakukan *subscribe* dan menerima pesan dari broker, serta menampilkan posisi DT01 & EXCA01 pada peta.
* **Mekanisme Auto-ACK** berfungsi 100%, di mana simulator menerima konfirmasi `"status": "ok"` untuk kedua ID pesan di topik `kutai/fleet/ack/DT01`.

---

### 4. Perbaikan Klasifikasi Device & Redesain Dashboard (Terbaru)
* **Redesain Dashboard**: Tampilan antarmuka dashboard ditingkatkan sepenuhnya menjadi bertema gelap (dark mode) yang bersih, minimalis, dan profesional. Menggunakan font Inter, layout sidebar, peta Leaflet.js yang terintegrasi rapi, tabel telemetri, log stream berwarna, serta indikator status koneksi yang dinamis.
* **Perbaikan Bug Klasifikasi Perangkat**: Mengatasi masalah di mana data Dump Truck (DT) terhitung sebagai "Relayed EXCA" pada dashboard akibat field `"src"` bernilai `null`.
  * **Sisi Firmware**: Mengubah `optDoc["src"] = doc["source"]` menjadi `optDoc["src"] = DT_ID` (di DT) dan `optDoc["src"] = EXCA_ID` (di EXCA) karena modul GPS asli tidak menyertakan field `"source"`.
  * **Sisi Dashboard**: Menambahkan proteksi *fallback* di `app.js` yang akan mengekstrak ID perangkat dari awalan string ID pesan (misal `DT01-IMEI-...` akan dipotong menjadi `DT01`) jika field `"src"` kosong atau `"UNKNOWN"`.

---

### 5. Deployment Subscriber & Proteksi Dashboard (Terbaru)
* **MQTT Ingest Subscriber**:
  * **Lokasi VPS**: `/opt/kutai-subscriber/`
  * **Status Layanan**: Systemd daemon `kutai-subscriber.service` (aktif berjalan, auto-restart).
  * **Kredensial API Subscriber (Diberikan ke Backend)**:
    * **Username**: `kutai_mqtt_subscriber`
    * **Password**: `KutaiMqttSecureSub2026!`
  * **Konfigurasi API**: File `/opt/kutai-subscriber/.env` berisi port MQTT broker lokal dan target URL API backend (sedang menunggu input URL).

* **Dashboard Web Map VPS**:
  * **Lokasi VPS**: `/var/www/naturelink-dashboard/`
  * **URL Akses**: [http://72.62.126.85/](http://72.62.126.85/)
  * **Keamanan**: Dilindungi menggunakan Nginx Basic Authentication (`.htpasswd`).
  * **Kredensial Login Dashboard**:
    * **Username**: `admin`
    * **Password**: `kutai2026!`

