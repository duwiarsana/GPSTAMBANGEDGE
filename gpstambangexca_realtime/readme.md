# 🚜 gpstambangexca_realtime (Excavator AP Mode - Real-time Priority Architecture)

Firmware ini digunakan pada unit Excavator (EXCA) standar yang beroperasi di area blank spot tanpa jaringan internet/GSM langsung.

---

## 📌 Deskripsi Model Kerja
Berbeda dengan unit DT atau EXCA Hybrid, unit EXCA standard ini **tidak memiliki client WiFi/MQTT mandiri**. Unit ini bertindak sebagai **WiFi Access Point lokal**.

1. **Local Recording**: Unit EXCA memproses GPS lokal, memfilter status mesin (Ignition), dan menyimpannya ke SD Card (`/gps_log.jsonl`).
2. **Relay Transfer**: Dump Truck (DT) yang melintas akan tersambung ke WiFi AP milik EXCA ini dan mendownload log yang belum terkirim via koneksi TCP.
3. **DT Priority Relay**: DT-lah yang kemudian bertanggung jawab mempublikasikan data EXCA tersebut secara real-time / backlog ke server saat DT kembali mendapatkan koneksi internet.

*Catatan: Modifikasi real-time priority publish pada versi ini diletakkan dalam bentuk dokumentasi arsitektur karena peran utama pengiriman internet diwakilkan oleh unit DT.*
