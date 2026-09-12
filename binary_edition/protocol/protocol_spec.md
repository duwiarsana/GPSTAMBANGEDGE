# Spesifikasi Binary Telemetry Protocol (66 Bytes - Version 2)

## 1. Ikhtisar Arsitektur
Protokol ini dirancang untuk memaksimalkan efisiensi penyimpanan Micro SD, kecepatan transfer P2P Wi-Fi lokal, serta minimasi penggunaan kuota internet seluler/MQTT.

Setiap paket telemetry berukuran **tepat 66 Bytes** (Fixed-Size Struct Packed 1-Byte), sehingga:
* Memory footprint di ESP32 bersifat statis (0 heap allocation / 0 memory leak).
* Offset pencarian di SD Card beroperasi secara linear: `ByteOffset = RecordIndex * 66`.
* Sangat efisien untuk transmisi MQTT (bulk batch N * 66 bytes).

---

## 2. Struktur Memori Paket Versi 2 (66 Bytes Exact)

| Offset (Byte) | Nama Field | Tipe Data C++ | Ukuran | Deskripsi & Skala |
| :--- | :--- | :--- | :--- | :--- |
| `0..1` | `magic` | `uint8_t[2]` | 2 Bytes | Sync marker: `0xAA, 0x55` |
| `2` | `version` | `uint8_t` | 1 Byte | Protocol version: `2` |
| `3..10` | `src` | `char[8]` | 8 Bytes | Device ID ASCII (contoh: `"EXCA01\0"`, `"DT01\0\0\0\0"`) |
| `11..18` | `imei` | `uint64_t` | 8 Bytes | 15-digit IMEI Tracker (contoh: `861327085563067`) |
| `19..22` | `seq` | `uint32_t` | 4 Bytes | Sequence counter perangkat (monotonik naik) |
| `23..26` | `timestamp` | `uint32_t` | 4 Bytes | Unix Epoch UTC dalam detik |
| `27..30` | `lat_x1e7` | `int32_t` | 4 Bytes | Latitude $\times 10^7$ (contoh: `-7388810` = `-0.7388810`) |
| `31..34` | `lon_x1e7` | `int32_t` | 4 Bytes | Longitude $\times 10^7$ (contoh: `1171301520` = `117.1301520`) |
| `35..36` | `speed_x10` | `uint16_t` | 2 Bytes | Kecepatan $\times 10$ (contoh: `250` = `25.0` km/jam) |
| `37..38` | `heading` | `uint16_t` | 2 Bytes | Arah hadap kendaraan ($0 - 360^\circ$) |
| `39..40` | `altitude` | `int16_t` | 2 Bytes | Ketinggian dpl dalam meter ($-32768 .. +32767$) |
| `41..42` | `bat_mv` | `uint16_t` | 2 Bytes | Tegangan aki dalam mV (contoh: `24500` = `24.50` V) |
| `43` | `ignition` | `uint8_t` | 1 Byte | Status mesin (`1` = ON, `0` = OFF) |
| `44` | `input_status`| `uint8_t` | 1 Byte | Bit 0: PTO Bak Dump Truck (`1`=Dump/Naik, `0`=Turun) |
| `45` | `flags` | `uint8_t` | 1 Byte | Bit 0: GPS Fix Valid (`1`), Bit 1: Data Relay EXCA (`1`) |
| `46..51` | `beacon_mac`| `uint8_t[6]` | 6 Bytes | MAC address Bluetooth Beacon terdekat |
| `52` | `beacon_rssi`| `int8_t` | 1 Byte | Kuat sinyal Bluetooth Beacon dalam dBm |
| `53..56` | `ibutton_id` | `uint32_t` | 4 Bytes | RFID Driver ID (contoh: `0x010A0D09` -> `"010A0D09"`) |
| `57` | `ibutton_flags`| `uint8_t` | 1 Byte | Bit 0: Login/Logout (`1`=login), Bit 1: Auth (`1`=OK) |
| `58..59` | `gs_x` | `int16_t` | 2 Bytes | Akselerasi G-Sensor Sumbu X (milli-g) |
| `60..61` | `gs_y` | `int16_t` | 2 Bytes | Akselerasi G-Sensor Sumbu Y (milli-g) |
| `62..63` | `gs_z` | `int16_t` | 2 Bytes | Akselerasi G-Sensor Sumbu Z (milli-g) |
| `64..65` | `crc16` | `uint16_t` | 2 Bytes | Checksum CRC16-CCITT atas 64 byte pertama (`offset 0..63`) |

---

## 3. Format Topic MQTT
* **Binary Telemetry**: `kutai/fleet/binary`
* **ACK Topic**: `kutai/fleet/ack/<SRC>` (contoh: `kutai/fleet/ack/EXCA01`)
* **ACK Payload JSON**: `{"id": "<SRC>-<TIMESTAMP>-<SEQ>", "status": "ok"}`
