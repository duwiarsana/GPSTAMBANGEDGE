# 🖥️ Kutai Fleet Edge — Live Web Dashboard

Aplikasi web lokal (single-page application) berbasis HTML5, Vanilla CSS, dan JavaScript untuk mensimulasikan monitoring data telemetri **Dump Truck (DT)** dan **Excavator (EXCA)** dari jarak jauh.

Dashboard ini terhubung secara langsung via **MQTT over WebSockets** ke broker Mosquitto di VPS Anda.

---

## 🚀 Cara Menjalankan

Karena dashboard ini murni berjalan di sisi client (browser), Anda **tidak perlu menginstal server lokal (Node.js/NPM/Python)**. Cukup ikuti langkah mudah berikut:

1. Buka folder `dashboard`.
2. Klik ganda file **[index.html](file:///Users/duwiarsana/.gemini/antigravity/scratch/GPSTAMBANGEDGE/dashboard/index.html)** untuk membukanya langsung di Google Chrome, Edge, Firefox, atau Safari.
3. Dashboard akan langsung terhubung secara otomatis ke VPS Anda (`72.62.126.85:9001`).

---

## ⚙️ Fitur Utama

### 📡 1. Live MQTT WebSocket Connection
* Otomatis melakukan *handshake* dan subscribe ke topik utama: `kutai/fleet/data`.
* Dilengkapi indikator status koneksi (*glowing pulse dot* hijau/merah) secara real-time.

### 🤖 2. Auto-ACK System (Simulasi Backend)
* Ketika data GPS dikirim oleh DT masuk ke topik `kutai/fleet/data`, dashboard akan otomatis membalas dengan mem-publish JSON ACK ke topik:
  * `kutai/fleet/ack/DT01` (dan topik unit pengirim lainnya).
* Fitur Auto-ACK ini dapat diaktifkan/dinonaktifkan secara instan melalui tombol switch toggle di panel kontrol.

### 🗺️ 3. Premium Interactive Map (Dark Theme)
* Menggunakan peta berbasis **Leaflet.js** dengan tile-layer gelap (*CartoDB Dark Matter*).
* Menampilkan marker dinamis untuk melacak lokasi terkini dari setiap unit DT (biru) dan EXCA (oranye).
* Klik pada marker untuk memunculkan detail koordinat, kecepatan, dan timestamp update terakhir.

### 📊 4. Real-time Telemetry & Logs
* **Telemetry Table**: Menampilkan ringkasan status aktif semua armada (ID Device, Kecepatan, Voltase Baterai, Status Ignition/Mesin, dan Sequence Terakhir).
* **Live Data Stream Log**: Konsol bergulir (*scrolling log*) interaktif yang mencetak lalu lintas data masuk (JSON data mentah) dan data keluar (ACK) secara real-time dengan kode warna yang dinamis.

### 🧪 5. Test Data Simulator
Jika perangkat keras ESP32 Anda belum menyala, Anda dapat menguji seluruh sistem Web & Broker secara instan menggunakan tombol simulator bawaan:
* **Sim DT01 Data**: Mengirimkan paket JSON data GPS acak yang menyimulasikan Dump Truck DT01 di area pertambangan Kutai Timur.
* **Sim EXCA01 Data**: Mengirimkan paket JSON data GPS acak yang menyimulasikan Excavator EXCA01 di area pertambangan Kutai Timur.

---

## 📂 Struktur File

```
📂 dashboard/
├── index.html        # Struktur UI Dashboard Utama
├── style.css         # Styling Premium (Glassmorphism & Dark Mode)
├── app.js            # Logika MQTT WebSockets, Map Leaflet, & Simulator
└── readme.md         # Petunjuk penggunaan (File ini)
```
