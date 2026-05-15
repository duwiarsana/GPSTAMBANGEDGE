# 🧪 Simulasi Transfer Data ESP32 ↔ ESP32 (SD Card)

Project ini dirancang untuk memverifikasi keamanan dan keandalan protokol transfer data antara dua ESP32 menggunakan media SD Card.

## 📁 Struktur Folder
- `Simulasi_Sender/`: Firmware untuk ESP32 pengirim (Role: EXCA/AP).
- `Simulasi_Receiver/`: Firmware untuk ESP32 penerima (Role: DT/STA).

## 🚀 Cara Setup

### 1. Persiapan Data (PC)
- Ambil file `gps_log.jsonl` dari folder `Example Data/` di repository ini.
- Copy file tersebut ke dalam **Root Directory** MicroSD Card milik **Sender**.

### 2. Flashing Firmware
- Upload folder `Simulasi_Sender` ke ESP32 pertama (Sender).
- Upload folder `Simulasi_Receiver` ke ESP32 kedua (Receiver).

### 3. Monitoring
- **Serial Monitor**: Buka dengan baudrate **115200**.
- **LED Indicator**:
  | LED | GPIO | State | Keterangan |
  |-----|------|-------|------------|
  | 🟡 | 13 | TRANS | Kedip cepat saat data sedang di-transfer/ditulis ke SD |

## 🔄 Skenario Simulasi

### Skenario A: Transfer Normal
1. Hidupkan Sender, pastikan Serial memunculkan "AP Created".
2. Hidupkan Receiver.
3. Receiver akan otomatis mencari WiFi "SIMULASI_SENDER" dan mulai men-download.
4. Perhatikan indikator `#` di Serial Monitor Receiver yang menandakan baris data diterima.

### Skenario B: Resume Transfer (Keamanan Data)
1. Saat transfer sedang berjalan, **matikan paksa** salah satu ESP32 (cabut power).
2. Hidupkan kembali.
3. Receiver akan otomatis melanjutkan transfer dari posisi baris terakhir yang berhasil disimpan (menggunakan `offset.txt`).
4. Verifikasi: Cek ukuran file `/received_log.jsonl` di SD Card Receiver, harus bertambah secara progresif tanpa ada data yang tumpang tindih.

## 🛡️ Apa yang di-verify?
- **Handshake Integrity**: Memastikan kedua device saling mengenali sebelum kirim data.
- **Offset Accuracy**: Memastikan `lastOffset` benar-benar menunjuk ke baris baru.
- **SD Card Stability**: Memastikan proses `flush()` per baris di Receiver aman terhadap power loss.
- **TCP Robustness**: Memastikan timeout dihandle jika koneksi WiFi terputus.

---
Developed by: **Duwi Arsana** 🚀
