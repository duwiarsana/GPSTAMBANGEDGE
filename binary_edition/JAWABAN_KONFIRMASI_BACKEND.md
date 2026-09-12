# Jawaban Konfirmasi untuk Tim Backend / Developer MQTT

Halo rekan-rekan developer, berikut jawaban dari saya per nomor untuk menjawab poin-poin pertanyaan konfirmasi terkait integrasi firmware ESP32 dan protokol binary telemetri armada GPS Tambang:

---

## A. Arti Bit yang Berbeda antara Dokumen dan Header

### 1. `input_status` bit 1 — apakah benar ACC?
* **1a. Status Bit 1:** Bit 1 **sudah tidak saya pakai sebagai penentu utama status kontak**, jadi silakan abaikan bit 1 pada `input_status`. Bitmask `input_status` yang aktif saat ini hanya **Bit 0** yaitu **PTO Bak Dump Truck** (`1` = Dump/Bak Naik, `0` = Normal/Turun).
* **1b. Penentu Status Mesin/Kontak:** **Gunakan field `ignition` pada offset 43 (`0` = OFF, `1` = ON)**. Field `ignition` ini adalah hasil konsolidasi logika firmware saya dari event code tracker (Event `2` = IGN ON, Event `3` = IGN OFF) dan fallback hardware.
* **1c. Bit 2–7:** Saat ini saya reserve bernilai `0`, belum dialokasikan untuk sensor lain.

---

### 2. `flags` bit 1 — "Data relay EXCA" atau "Backlog Relay"?
* **2a. Mana yang benar:** Yang benar adalah **"Data Relay EXCA"**.
  - `Bit 0` = GPS Fix Valid (`1` = Fix 3D Valid, `0` = No Fix).
  - `Bit 1` = Data Relay EXCA (`1` = Titipan data dari Excavator yang dibawa dan diunggah oleh Dump Truck lewat Wi-Fi P2P offline, `0` = Data asli milik unit Dump Truck itu sendiri).
* **2b. Field `src`:** Field `src` **SELALU berisi ID unit PEMILIK DATA asli** (misal: `"EXCA01"`). Meskipun paket tersebut diunggah ke internet oleh Dump Truck (`DT20`), isi field `src` tetap `"EXCA01"` karena seluruh struct dibuat dan ditandatangani oleh Excavator di tambang.
* **2c. Koordinat `lat`/`lon`:** **Posisi unit PEMILIK DATA asli** (posisi Excavator saat data tersebut dicatat di front tambang). Koordinat **TIDAK** mengikuti Dump Truck pengangkut data.

---

### 3. `flags` bit 0 = 0 (tanpa GPS fix) — apa isi `lat`/`lon`?
* **3a. Isi saat No Fix:** Pada firmware yang saya pasang, paket yang tidak memiliki koordinat (`lat == 0 && lon == 0`) **langsung di-drop oleh ESP32 dan tidak akan pernah saya simpan ke Micro SD maupun dikirim ke MQTT**. Jika suatu saat ada data dengan `Bit 0 = 0`, nilainya adalah posisi koordinat valid terakhir (last known position).
* **3b. Pengiriman:** **Ditahan/dibuang sampai GPS Fix didapat**. Paket dengan koordinat `0.0, 0.0` tidak saya kirimkan ke broker. Di sisi backend, jika ada titik tanpa GPS fix atau `lat/lon == 0`, silakan di-drop saja (jangan diplot ke map).

---

### 4. `ibutton_flags` bit 1 (Auth) = 0 — artinya apa?
* **4a. Arti Auth = 0:** Kartu RFID/iButton ditempel dan terbaca ID fisiknya, namun ID tersebut **tidak terdaftar dalam whitelist otorisasi driver** di memori tracker/ESP32 saya.
* **4b. Tindakan di Backend:** **Tetap catat kejadian tersebut ke database**. Tandai sebagai `ibutton_login = true` dengan status `unauthorized / unregistered driver` untuk keperluan audit operasional tambang.

---

### 5. `bat_mv` — tegangan mana?
* **5a. Sumber Tegangan:** Ini tegangan **Aki Kendaraan (Power Supply Eksternal 12V / 24V)** dalam satuan milli-Volt (mV). Contoh: `24500` = `24.50 V`.
* **5b. Saat Terputus:** Jika aki kendaraan dilepas/terputus dan tracker berjalan dengan baterai cadangan internalnya, nilainya akan turun ke tegangan baterai internal (~`3700` s/d `4100` mV = 3.7V - 4.1V), atau `0` jika power supply mati total.

---

## B. Mekanisme ACK, Retry, dan Kehilangan Paket

### 6. Satu paket rusak di tengah batch — saya harus ACK yang mana?
* **6a. ACK yang Dikirim:** **Tetap kirim ACK dengan `id` paket TERAKHIR dari payload batch tersebut**.
* **6b. Recovery Paket Rusak:** Ya, paket yang rusak di tengah batch akan terlewat (skip). Firmware saya menggunakan pointer file linear (offset) di Micro SD, jadi belum mendukung selective individual NACK per paket tunggal.
* **6c. Jika SEMUA paket dalam payload gagal CRC:** **DIAM SAJA (Jangan kirim ACK)**. Jika backend tidak membalas ACK dalam tempo 2–3 detik, ESP32 saya akan timeout dan otomatis mencoba mengirim ulang (retry) batch yang sama s/d 2 kali.

---

### 7. Apakah ESP32 memeriksa isi `id` pada ACK?
* **7a. Pencocokan ID:** **YA, FIRMWARE SAYA MENCOCOKKAN SECARA KETAT**.
  ```cpp
  if (ackReceived && lastAckMsgId == lastMsgId) { return true; }
  ```
  Jika string `id` pada payload ACK yang Anda kirim tidak sama persis dengan `lastMsgId` dari paket terakhir yang baru saja dikirim ESP32, ACK **SAYA ABAIKAN** dan pointer Micro SD tidak akan maju!
* **7b. Field `count`:** Field `count` diabaikan oleh ESP32 (hanya untuk log / audit backend Anda saja).
* **7c. Menambah field baru di JSON ACK:** **Sangat aman**. Parser JSON di firmware saya menggunakan `ArduinoJson` yang hanya mencari key `"id"` dan `"status": "ok"`. Field tambahan lain (seperti `accepted`, `rejected`, `timestamp`) akan otomatis diabaikan tanpa error.
* **7d. ACK dengan ID yang bukan paket terakhir:** **ACK akan saya abaikan 100%** dan offset Micro SD tidak akan maju.

---

### 8. Saat retry, apakah batch-nya persis sama?
* **8a. Struktur Batch Saat Retry:** **PERSIS SAMA**. Jika terjadi timeout ACK, loop retry di ESP32 saya akan mem-publish ulang buffer memori yang persis sama, dengan isi dan urutan paket yang identik.
* **8b. Paket Campuran:** Tidak ada potongan acak di tengah retry. Potongan hanya bisa bergeser jika terjadi restart total ESP32 di tengah-tengah transmisi. Disarankan backend Anda menangani idempotency dengan klausa database (`INSERT ... ON CONFLICT DO UPDATE` / `ON DUPLICATE KEY UPDATE`).

---

### 9. Perilaku `seq`
* **9a. Persistence `seq`:** **`seq` TETAP BERLANJUT**. Nilai `dtSeq` / `excaSeq` saya simpan secara persisten ke dalam flash NVS / file Micro SD (`/dt_seq.txt` & `/exca_seq.txt`). Saat reboot atau mati total, angka `seq` terakhir akan di-load kembali.
* **9b. Keunikan `seq`:** `seq` unik dan monotonik naik. Angka hanya akan reset ke 0 jika kartu Micro SD diformat ulang.
* **9c. Deteksi Paket Hilang:** **BISA digunakan sebagai indikator paket hilang**, dengan catatan: sequence dihitung per-device (`src`). Pastikan Anda memeriksa kontinuitas `seq` per masing-masing `src`.

---

### 10. Ukuran dan Pengaturan Publish
* **10a. Batas Maksimum Batch:** **Batas maksimum per payload saat ini adalah 16 Paket ($16 \times 66 = 1.056$ Bytes)** (`BULK_PUBLISH_RECORDS = 16`). Jika backlog menumpuk ribuan data di Micro SD, ESP32 saya akan mengirimkannya beruntun per chunk 16 paket (kirim 16 paket ➔ tunggu ACK ➔ kirim 16 paket berikutnya ➔ tunggu ACK, dst).
* **10b. Parameter MQTT:**
  - **QoS:** `0` (At most once di transport layer, keandalan pengiriman dijamin via Application-Level ACK).
  - **Retain Flag:** `false` (Semua telemetri saya kirim tanpa retain).
* **10c. Topik Ingestion:** Semua unit saya arahkan ke **1 topik yang sama: `kutai/fleet/binary`**. Tidak ada sub-topik terpisah untuk publish.

---

## C. Identitas Unit dan Waktu

### 11. Daftar Pasangan `src` ↔ IMEI ↔ Nomor Lambung
* **11a. Daftar Armada di Database Produksi:**
  - **Dump Truck (DT):** `DT01`, `DT02`, `DT04`, `DT05`, `DT06`, `DT07`, `DT08`, `DT09`, `DT011`, `DT012`, `DT014`, `DT016`, `DT20`.
  - **Excavator (EXCA):** `EXCA01`, `EXCA02`, `EXCA03`, `EXCA04`.
  - *IMEI unit uji coba aktif saat ini:* `861327085560279` / `861327085563067`.
* **11b. Format String `src` (Bebas / Fleksibel):**
  - **TIDAK HARUS berawalan "DT" atau "EXCA"**. Backend jangan mengunci atau mem-filter hanya prefix `DT` / `EXCA`.
  - Nama lambung bisa bebas sesuai penamaan tambang di lapangan, contohnya: **`D898`**, **`DT-10`**, **`EX300`**, **`HD785`**, **`LV01`**, dll.
  - **Batasan teknis di firmware saya:** Panjang teks adalah **maksimal 7 karakter ASCII** (+ 1 byte null terminator `\0` karena ukuran buffer `char src[8]` di struct binary).
* **11c. Pemindahan Alat:** Field `src` saya hardcode pada firmware masing-masing alat sesuai label lambung unit, sedangkan `imei` membaca nomor serial modul tracker GPS bawaan. Jika alat dipindah, konfigurasi ID di firmware akan saya sesuaikan dengan nomor lambung fisik yang baru.

---

### 12. Sumber Waktu dan Kondisi RTC
* **12a. Definisi `timestamp`:** `timestamp` adalah **WAKTU DATA DIREKAM (Generated Time)** di lapangan, **BUKAN waktu data dikirim ke internet**. Untuk data backlog yang tertimbun di Micro SD selama 5 jam, timestamp tetap mencerminkan waktu aktual 5 jam yang lalu saat armada beroperasi.
* **12b. Kondisi GPS Belum Sinkron:** Firmware saya memiliki guard ketat: jika timestamp dari satelit GPS belum valid (Epoch < 1577836800 / sebelum tahun 2020), **data ditolak dan tidak akan disimpan/dikirim**. Jadi paket yang sampai di backend dijamin sudah tersinkronisasi dengan waktu UTC satelit GPS.
* **12c. Waktu Masa Depan:** Jam diperoleh langsung dari sinyal konstelasi satelit GNSS (Atomic Clock), bukan dari RTC lokal modul, jadi tidak akan melenceng ke masa depan.

---

### 13. Beacon: Aturan Pemilihan "Terkuat"
* **13a. Jendela Scan:** Scan BLE dilakukan secara pasif oleh tracker eksternal (NL02) setiap interval paket (1–3 detik).
* **13b. Ambang Batas RSSI:** Firmware saya mengambil beacon dengan RSSI tertinggi dari array scan. Ambang batas noise floor sekitar `-90 dBm`. Jika tidak ada beacon terdeteksi, array `beacon_mac` diisi nol (`00:00:00:00:00:00`) dan RSSI bernilai `0`.
* **13c. Nilai Bergantian (Flipping):** Jika ada dua beacon dengan sinyal yang sangat mirip (misal -70 dBm vs -71 dBm), ada kemungkinan nilainya bergantian antar-detik karena fluktuasi radio. Di sisi backend disarankan membuat filter moving average / hysteresis sebelum memutuskan perpindahan posisi excavator.

---

### 14. Interval Pengiriman
* **14a. Interval Normal:**
  - **Mesin Hidup (IGN ON / Bergerak):** Setiap **5 detik sekali**. Firmware ESP32 saya bersifat pasif mengikuti GPS Tracker. Begitu tracker menembakkan 1 paket data via serial UART (setiap 5 detik), ESP32 saya langsung mengubahnya ke binary dan mem-publish secara real-time ke MQTT saat itu juga.
  - **Mesin Mati (IGN OFF):** Masuk masa *cooldown* selama 30 detik (tetap merekam per 5 detik), setelah itu jika tracker standby, pengiriman hanya terjadi saat ada event heartbeat dari tracker.
* **14b. Saat Sinyal Jelek / Hilang Sinyal:** Interval pencatatan ke Micro SD tetap berjalan normal tiap 5 detik. Transmisi ke internet ditahan (masuk backlog). Begitu ketemu sinyal Wi-Fi/Internet lagi, unit akan langsung menguras backlog dengan burst batch 16 paket secara beruntun dengan kecepatan tinggi.

---

## D. Perbaikan `binary_parser.py`

* **15a. Terkait `from pymupdf import _name`:** Itu artefak ketidaksengajaan copy-paste dari editor saya saat buka referensi PDF kemarin. **Sudah saya bersihkan**.
* **15b. Terkait `if __name__ == "__main__":`:** Sudah saya perbaiki penulisan dunder-nya dan blok test di dalamnya sudah **saya sesuaikan penuh untuk Versi 2 (66 Bytes FIX)**. File terbaru sudah bisa langsung dijalankan dengan `python3 binary_parser.py` dan menghasilkan status sukses parsing paket 66-byte.

---

## E. Catatan untuk Versi Protokol Berikutnya (V3 Roadmap)

Usulan sudah saya catat untuk kita terapkan di Versi berikutnya:
1. **Nomor 16 (HDOP & Satelit):** Setuju, nanti di alokasikan 2 byte (1 byte `hdop_x10`, 1 byte `sat_count`) pada struct V3.
2. **Nomor 17 (Timestamp 64-bit Y2038):** Nanti saya upgrade `timestamp` ke `uint64_t` agar aman untuk jangka panjang melewati 2038.
3. **Nomor 18 (Dual Beacon Slot):** Setuju, penambahan slot Beacon #2 (Secondary Beacon) akan sangat membantu deteksi antrean dump truck vs proses loading aktif di excavator.
