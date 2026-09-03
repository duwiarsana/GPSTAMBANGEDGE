# Laporan Analisis & Ringkasan Data Telemetry DT & EXCA

Dokumen ini berisi rangkuman analisis data telemetri untuk seluruh unit **Dump Truck (DT)** dan **Excavator (EXCA)** berbasis ESP32 pada sistem GPS Tambang Edge, berdasarkan data log lokal (`Example Data/`), skrip analisis database, serta dokumentasi integrasi backend.

---

## 1. Ringkasan Status Data Perangkat (DT & EXCA)

| Nama Unit | IMEI Hardware | Status Dataset | Record Total (Lokal) | Waktu Pertama Masuk | Waktu Terakhir Masuk (Latest) | Keterangan / Posisi Terakhir |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **EXCA03** | `864022083269463` | Live DB Server | - | Juni 2026 | **2026-07-15 (15 Juli 2026)** | 🏆 **Paling Update (Database Server)** |
| **DT01** | `864022083265024` / `861327085560006` | Live DB Server / Script | - | Juni 2026 | **2026-06-17T03:39:39Z** | Unit DT Utama (Direct & Relay Piggyback) |
| **EXCA01** | `861327085563067` | Dataset Log Lokal | **17,976 record** | `2026-04-09T12:35:26Z` | **2026-04-10T13:18:01Z** | 🏆 **Paling Update (Dataset Mentah Lokal)**<br>Lat: `-0.739818`, Lon: `117.131504` |
| **DT013** | `864022083271527` | Live DB / Whitelist | - | - | Juni 2026 | Unit fisik uji coba bypass log SD Card |
| **DT03** | Auto-assigned | Live DB Server | - | - | Juni 2026 | Terdaftar dalam skrip kueri `query_dt03.py` |
| **DT07** | Auto-assigned | Live DB Server | - | - | Juni 2026 | Terdaftar dalam skrip kueri `query_dt07.py` |
| **DT09** | Whitelist Server | Live DB Server | - | - | Juni 2026 | Terdaftar dalam skrip kueri `query_latest_dt01_dt09.py` |
| **DT011** | Whitelist Server | Live DB Server | - | - | Juni 2026 | Terdaftar dalam skrip kueri `query_dt11.py` |
| **DT016** | Whitelist Server | Live DB Server | - | - | Juni 2026 | Terdaftar dalam skrip kueri `query_dt16.py` |
| **EXCA02** | Whitelist Server | Live DB Server | - | - | Juni 2026 | Unit Excavator pendukung mode Relay |

---

## 2. Analisis Perangkat Paling Update

1. **Secara Historis Database Server:**
   * Unit **`EXCA03`** merupakan unit dengan catatan timestamp **paling baru / update** (tercatat hingga **15 Juli 2026** di `query_exca03_time.py`).
2. **Pada Fleet Dump Truck (DT):**
   * Unit **`DT01`** memiliki catatan aktivitas telemetri paling update di antara armada DT (tercatat hingga **17 Juni 2026** pukul `03:39:39 UTC`).
3. **Pada Berkas Log Lokal (`Example Data/*.jsonl`):**
   * Unit **`EXCA01`** memiliki data paling lengkap secara lokal dengan **17,976 baris data terstruktur**, tercatat dari tanggal **09 April 2026 12:35:26 UTC** hingga **10 April 2026 13:18:01 UTC**.

---

## 3. Detail Payload & Identifikasi Terakhir (EXCA01 Sampel Lokal)

* **ID Pesan Terakhir:** `EXCA01-861327085563067-20260410T131801Z-36787`
* **Satelit Aktif:** 28 - 29 Satelit (GPS Fix OK, HDOP 0.5 - 0.6)
* **Kondisi Mesin / Tegangan:**
  * Tegangan Eksternal: `25.34 V` (Sistem kelistrikan 24V stabil)
  * Baterai Internal: `4.2 V` (100% Charged)
  * Suhu MCU ESP32: `70.58 °C`
* **Deteksi Beacon iBeacon:** Berhasil mendeteksi Beacon MAC `C3:00:00:38:B4:52` dengan RSSI `-56 dBm` hingga `-67 dBm`.

---

## 4. Kesimpulan & Temuan Sistem Telemetri

1. **Mekanisme Relay (Piggybacking) Berfungsi:**
   * Data EXCA tidak hanya dikirim langsung saat berada di dekat tower Wi-Fi, namun juga dapat **dititipkan (relayed) melalui unit DT01** saat DT melintas di dekat Excavator.
2. **Identifikasi Perangkat Berdasarkan Waktu:**
   * Unit Excavator (`EXCA03` dan `EXCA01`) menunjukkan pengiriman data aktif dengan frekuensi tinggi saat beroperasi di titik muat (*loading point*).
   * Unit DT (`DT01`, `DT09`, `DT011`, `DT013`, `DT016`) aktif mengirimkan backlog data saat kembali memasuki area jangkauan Wi-Fi tower.
3. **Rekomendasi Operasional Sinyal Wi-Fi Tower:**
   * Karena unit EXCA dan DT mengandalkan koneksi 2.4 GHz ESP32 untuk mengirimkan akumulasi log antrean (backlog), kestabilan Wi-Fi tower M2 sangat menentukan seberapa cepat data backlog ini terkuras habis ke server.
