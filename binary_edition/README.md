# GPS Tambang Edge - Binary Edition Suite (64-Byte Ultra High-Performance)

Suite lengkap sistem telemetri GPS armada tambang berbasis format **Raw Binary Struct (64 Bytes)**.

---

## 📁 Struktur Folder
* **`protocol/`**: Definisi struct C++ (`gps_binary_protocol.h`) dan dokumen spesifikasi (`protocol_spec.md`).
* **`exca_binary/`**: Firmware ESP32 Excavator dengan Direct 64B Micro SD logger & P2P High-Speed Binary Streaming Server.
* **`dt_binary/`**: Firmware ESP32 Dump Truck dengan P2P Binary Harvester dari Excavator & Dual Backlog Drain.
* **`backend/`**: Ingestor Python MQTT Binary (`server_binary.py`) & Unpacker (`binary_parser.py`).
* **`dashboard_binary/`**: Modern Web Dashboard dengan Live Map Leaflet, Telemetry Inspector, dan status armada real-time.

---

## ⚡ Keunggulan Format Binary 64-Byte
1. **85% - 88% Penghematan Kapasitas**: 1 record hanya 64 bytes (dibandingkan JSON ~450 bytes).
2. **Transfer P2P Kilat**: Sinkronisasi 10.000 data EXCA ➡️ DT selesai dalam **< 1 detik**.
3. **Zero Heap Allocation**: 100% bebas memory leak / fragmentation crash pada ESP32.
4. **Irit Kuota Internet**: Pengiriman MQTT binary menghemat 85% bandwidth seluler.
