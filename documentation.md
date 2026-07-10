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
  * **Status Layanan**: Systemd daemon `kutai-subscriber.service` (Aktif & Berjalan lancar, auto-restart).
  * **Kredensial API Backend**:
    * **URL Backend**: `http://34.101.245.159/v1/`
    * **Username**: `MQTT`
    * **Password**: `KutaiMqttSecureSub2026!`
  * **Status Integrasi & Alur ACK**: 
    * Sukses terhubung ke backend API (Login berhasil dan token JWT diperoleh). 
    * Layanan meneruskan telemetri dari broker MQTT lokal ke backend.
    * **Mekanisme ACK**: Sesuai dengan spesifikasi API, **Backend Server** yang bertanggung jawab penuh mempublikasikan ACK kembali ke MQTT broker (pada topik `kutai/fleet/ack/{src}`) setelah data sukses disimpan (`202 Accepted`). Layanan subscriber lokal dibersihkan dari logika ACK duplikat.

* **Dashboard Web Map VPS**:
  * **Lokasi VPS**: `/var/www/naturelink-dashboard/`
  * **URL Akses**: [http://72.62.126.85/](http://72.62.126.85/)
  * **Keamanan**: Dilindungi menggunakan Nginx Basic Authentication (`.htpasswd`).
  * **Kredensial Login Dashboard**:
    * **Username**: `admin`
    * **Password**: `kutai2026!`
  * **Pembaruan Konfigurasi**: Fitur **Auto-ACK** pada dashboard web default-nya diatur menjadi **OFF (Unchecked)** agar dashboard berfungsi murni sebagai alat pemantau (*monitoring*), tanpa mencampuri proses ACK resmi dari backend.

---

### 6. Pembaruan Logger Firmware (Terbaru)
Untuk mempermudah verifikasi identitas fisik perangkat di lapangan, firmware **EXCA** dan **DT** diperbarui untuk mencetak MAC Address hardware ESP32 ke port Serial pada saat pertama kali booting:
* **Log Booting**:
  ```text
  [1000] === DT01 STARTING ===
  [1001] MAC Address: AA:BB:CC:DD:EE:FF
  ```
* **Berkas yang Diperbarui**:
  * [gpstambangdt.ino](file:///Users/duwiarsana/.gemini/antigravity/scratch/GPSTAMBANGEDGE/gpstambangdt/gpstambangdt.ino) (Arduino IDE)
  * [main.cpp](file:///Users/duwiarsana/.gemini/antigravity/scratch/GPSTAMBANGEDGE/gpstambangdt_pio/src/main.cpp) (PlatformIO)
  * [gpstambangexca.ino](file:///Users/duwiarsana/.gemini/antigravity/scratch/GPSTAMBANGEDGE/gpstambangexca/gpstambangexca.ino) (Arduino IDE)

---

### 7. Perbaikan Bug SD Card Concurrency & Penyambungan Data Corrupt (Terbaru)
* **Penyebab Kerusakan Data (Non-JSON Warning)**:
  * Pada ESP32, library SD Card menggunakan buffer sektor bersama. Memanggil operasi tulis `FILE_APPEND` (`handleGPS()`) secara bersamaan di sela-sela loop pengiriman data yang sedang membaca file `FILE_READ` menyebabkan cache FATFS rusak. Hal ini mengakibatkan data terpotong secara acak di tengah kalimat JSON (disisipi newline `\n` atau kehilangan beberapa byte).
* **Pembersihan Firmware**:
  * Menghapus pemanggilan `handleGPS()` dan `handleDTGps()` dari dalam loop transmisi TCP (`handleClient`) dan loop tunggu ACK MQTT (`waitAck` / `publishOneWithAck`) pada berkas [gpstambangexca.ino](file:///Users/duwiarsana/.gemini/antigravity/scratch/GPSTAMBANGEDGE/gpstambangexca/gpstambangexca.ino), [gpstambangexca_hybrid.ino](file:///Users/duwiarsana/.gemini/antigravity/scratch/GPSTAMBANGEDGE/gpstambangexca_hybrid/gpstambangexca_hybrid.ino), dan [gpstambangdt.ino](file:///Users/duwiarsana/.gemini/antigravity/scratch/GPSTAMBANGEDGE/gpstambangdt/gpstambangdt.ino).
* **Penyelamatan Data di Sisi VPS & Dashboard (Robust Recovery)**:
  * **Pemisahan Buffer per Unit**: Memisahkan memory buffer penyimpanan data pecah berdasarkan Device ID (`src`) agar paket dari unit yang berbeda tidak saling menindih di MQTT topic `kutai/fleet/data`.
  * **Penyambungan Pintar (Robust Merge)**: Memperluas fungsi `tryMergeChunks` di `subscriber.py` dan `app.js` agar mampu menyambung data pecah baik yang bertumpuk (*overlap*), terbelah bersih (*clean split*), maupun yang kehilangan byte di tengah (memiliki *gap*, misal kata `"st":"logout"` terpotong menjadi `"s` dan `login"`) dengan menyisipkan kandidat jembatan karakter umum.
  * **Pembersihan Newline/Carriage Return Mentah**: Melakukan pembersihan otomatis terhadap karakter `\r` dan `\n` yang terselip di dalam string JSON sebelum di-parse.
  * **UI Safety Guard**: Membentengi fungsi rendering dashboard dari crash Javascript dengan menambahkan penanganan tipe data `null` / `NaN` pada field koordinat, kecepatan, dan baterai unit.

---

### 8. Penyaringan Data Telemetri Corrupt (Waktu `-605` & Koordinat Kosong)
* **Masalah**: GPS Tracker sesekali mempublikasikan paket kosong saat booting atau kehilangan sinyal satelit, menghasilkan timestamp `-605` dan koordinat kosong/`null`.
* **Solusi**:
  * **Sisi API Server (`server.py`)**: Query API `/api/devices` ditambahkan filter ketat `WHERE lat IS NOT NULL AND lat != 0 AND lon IS NOT NULL AND lon != 0 AND ts LIKE '%T%'` agar database hanya mengambil koordinat valid terakhir dari memori SQLite.
  * **Sisi Web UI (`app.js`)**: Ditambahkan *validation guard* pada fungsi `handleIncomingData` untuk langsung mengabaikan live message yang memiliki timestamp non-string atau koordinat `0.0`/`null`.

---

### 9. Peta Interaktif & Timeline Playback Replay
* **Masalah**: Pengguna ingin navigasi peta yang dinamis, bisa klik marker secara langsung untuk memuat jalur history, serta bisa memutar/menggeser timeline secara presisi.
* **Solusi**:
  * **Marker Click Integration**: Mengklik marker unit pada peta langsung memilih unit tersebut di dropdown history dan menembak request load track rute perjalanan.
  * **Interactive Timeline Slider (Seek Bar)**: Menyediakan slider linimasa di panel kiri untuk menggeser index perjalanan secara manual (dilengkapi `L.DomEvent.stopPropagation` agar drag slider tidak terinterupsi oleh event drag peta utama Leaflet).
  * **Viewport Focus Lock**: Menonaktifkan pemusatan kamera otomatis (`map.panTo`) saat simulasi replay berjalan agar user bebas melakukan zoom dan pan manual untuk meneliti pergerakan marker.
  * **Peta Dinamis**: Mengklik area kosong peta otomatis membersihkan layer polyline history dan mereset dropdown pemilihan unit.

---

### 10. Folder Project Varian Real-Time Priority
* **Masalah**: Pengiriman data antrean log SD Card bersifat FIFO. Ketika unit kembali mendapat sinyal internet, posisi map tidak ter-update ke lokasi saat ini secara langsung karena harus mengunggah ribuan baris log mati sinyal terlebih dahulu.
* **Solusi**: Dibuatkan folder project terpisah berkode `_realtime`:
  * **[gpstambangdt_realtime](file:///Users/duwiarsana/.gemini/antigravity/scratch/GPSTAMBANGEDGE/gpstambangdt_realtime/)**
  * **[gpstambangexca_realtime](file:///Users/duwiarsana/.gemini/antigravity/scratch/GPSTAMBANGEDGE/gpstambangexca_realtime/)**
  * **[gpstambangexca_hybrid_realtime](file:///Users/duwiarsana/.gemini/antigravity/scratch/GPSTAMBANGEDGE/gpstambangexca_hybrid_realtime/)**
  * **Logika Dual-Publishing**: Jika online, koordinat GPS terbaru dikirim seketika sebagai prioritas realtime. Di saat bersamaan, background loop mencicil pengiriman log SD Card secara bertahap. Jika antrean backlog MicroSD kosong, offset antrean langsung dimajukan secara otomatis agar tidak terjadi duplikasi kirim.

---

### 11. Penanganan Sinyal ACK Tersumbat di Server VPS
* **Masalah**: Sebelumnya, subscriber service hanya mengirimkan sinyal ACK ke unit fisik jika API backend mendeteksi data duplikat (`409 Conflict`). Jika data baru berhasil tersimpan (`200 OK` atau `202 Accepted`), subscriber lupa mengirimkan ACK. Akibatnya unit fisik terus-menerus mengulang pengiriman data yang sama dan tersumbat ketika dashboard browser ditutup (fitur Auto-ACK mati).
* **Solusi**: Memperbarui berkas [subscriber.py](file:///Users/duwiarsana/.gemini/antigravity/scratch/GPSTAMBANGEDGE/subscriber/subscriber.py) agar **selalu mengirimkan respons ACK (`status: ok`)** kembali ke unit fisik untuk semua status keberhasilan penyimpanan, baik itu data baru (`200`/`202`) maupun data duplikat (`409`). Layanan subscriber telah sukses di-deploy dan berjalan normal tanpa hambatan di VPS.

---

### 12. SQLite WAL Mode & Concurrency Write Optimization
* **Masalah**: Pengiriman backlog data antrean dalam skala besar secara beruntun dari 18 unit seringkali menyebabkan database SQLite di server backend terkunci (*database is locked*), mengganggu dashboard dalam membaca data telemetri.
* **Solusi**: 
  * Mengubah mode jurnal SQLite backend menjadi **Write-Ahead Logging (WAL)** di [server.py](file:///Users/duwiarsana/.gemini/antigravity/scratch/GPSTAMBANGEDGE/dashboard/server.py).
  * Menambahkan parameter `timeout=30.0` pada setiap inisialisasi koneksi database untuk memberi toleransi waktu tunggu yang aman selama antrean tulis yang padat.

---

### 13. Real-Time Stuck Device Alarm System
* **Masalah**: Operator tidak memiliki cara untuk mengetahui unit mana saja di lapangan yang sedang tersumbat antrean datanya (stuck dalam retry loop) akibat tidak menerima balasan ACK dari server.
* **Solusi**:
  * **Database tracking**: Membuat tabel `device_alerts` di database untuk menyimpan unit bermasalah, ID pesan yang tersumbat, dan jumlah pengulangan (`retry_count`).
  * **Auto-detect & Auto-resolve**: Subscriber mendeteksi pesan duplikat/gagal, mencatatnya sebagai alarm aktif, dan secara otomatis **menghapusnya seketika** setelah unit berhasil mengirimkan data baru (ID baru).
  * **Lonceng Peringatan (Web UI)**: Menambahkan tombol Lonceng Alarm berkedip merah di header dashboard dengan dropdown list unit stuck yang dinamis.
  * **Badge Peringatan di Tabel**: Menambahkan badge interaktif berkedip merah `⚠️ Stuck (X)` di samping nama unit pada tabel utama. Jika diklik, dialog pop-up akan menampilkan analisis penyebab lengkap dengan saran solusinya.

---

### 14. Konversi Waktu Lokal (Timezone conversion WITA)
* **Masalah**: Waktu deteksi alarm stuck di database direkam dalam standar UTC (GMT+0), sehingga memicu kebingungan pengguna karena selisih 8 jam dengan jam lokal laptop (WITA / GMT+8).
* **Solusi**: Menambahkan fungsi JavaScript `utcToLocalString` di [app.js](file:///Users/duwiarsana/.gemini/antigravity/scratch/GPSTAMBANGEDGE/dashboard/app.js) untuk secara dinamis mengonversi waktu UTC dari database ke zona waktu lokal pengguna sebelum ditampilkan di dashboard.

---

### 15. MQTT Retained ACK & Mirroring Bug Fix
* **Masalah**: 
  * Unit sering terputus (*keepalive timeout*) karena gangguan sinyal Wi-Fi satu arah (RX loss), sehingga sering melewatkan sinyal ACK reguler yang dikirim server saat proses reconnect.
  * Adanya bug mirroring di subscriber lama yang membanjiri semua topik DT dengan ACK milik EXCA02, sehingga variabel pencocokan ACK di memori ESP32 DT terus-menerus tertimpa dan membuat unit terblokir selamanya.
* **Solusi**:
  * Mengaktifkan bendera **`retain=True`** pada penerbitan ACK agar broker MQTT menyimpan pesan ACK terakhir di memori dan langsung menyerahkannya ke unit seketika setelah unit terhubung kembali.
  * Memperbaiki alur mirroring di subscriber agar **hanya mendistribusikan ACK yang relevan** (tidak ada lagi banjir ACK EXCA ke topik DT). Alur mirroring dibalik: ACK DT disalin ke EXCA agar EXCA dapat membantu me-relay ACK ke DT saat mode offline hybrid.

