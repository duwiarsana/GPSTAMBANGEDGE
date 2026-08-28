# Spesifikasi Binary Telemetry Protocol (64 Bytes)

## 1. Ikhtisar Arsitektur
Protokol ini dirancang untuk memaksimalkan efisiensi penyimpanan Micro SD, kecepatan transfer P2P Wi-Fi lokal, serta minimasi penggunaan kuota internet seluler/MQTT.

Setiap paket telemetry berukuran **tepat 64 Bytes** (Fixed-Size Struct), sehingga:
* 1 sektor Micro SD (512 bytes) memuat **tepat 8 paket** tanpa fragmentasi.
* Offset pencarian di SD Card beroperasi secara linear: `ByteOffset = RecordIndex * 64`.
* Memory footprint di ESP32 bersifat statis (0 heap allocation / 0 memory leak).

---

## 2. Struktur Memori Paket (64 Bytes Exact)

| Offset (Byte) | Nama Field | Tipe Data C++ | Ukuran | Deskripsi & Skala |
| :--- | :--- | :--- | :--- | :--- |
| `0..1` | `magic` | `uint8_t[2]` | 2 Bytes | Sync marker: `0xAA, 0x55` |
| `2` | `version` | `uint8_t` | 1 Byte | Protocol version (saat ini: `1`) |
| `3..10` | `src` | `char[8]` | 8 Bytes | Device ID ASCII (contoh: `"EXCA01\0"`, `"DT01\0\0\0\0"`) |
| `11..14` | `seq` | `uint32_t` | 4 Bytes | Sequence counter perangkat |
| `15..18` | `timestamp` | `uint32_t` | 4 Bytes | Unix Epoch UTC dalam detik |
| `19..22` | `lat_x1e7` | `int32_t` | 4 Bytes | Latitude $\times 10^7$ (contoh: `-7388810` = `-0.7388810`) |
| `23..26` | `lon_x1e7` | `int32_t` | 4 Bytes | Longitude $\times 10^7$ (contoh: `1171301520` = `117.1301520`) |
| `27..28` | `speed_x10` | `uint16_t` | 2 Bytes | Kecepatan $\times 10$ (contoh: `455` = `45.5` km/jam) |
| `29..30` | `heading` | `uint16_t` | 2 Bytes | Arah hadap kendaraan ($0 - 360^\circ$) |
| `31..32` | `altitude` | `int16_t` | 2 Bytes | Ketinggian dpl dalam meter ($-32768 .. +32767$) |
| `33..34` | `bat_mv` | `uint16_t` | 2 Bytes | Tegangan aki dalam mV (contoh: `25400` = `25.40` V) |
| `35..38` | `odo_m` | `uint32_t` | 4 Bytes | Total odometer dalam meter |
| `39` | `ignition` | `uint8_t` | 1 Byte | Status mesin (`1` = ON, `0` = OFF) |
| `40` | `input_status`| `uint8_t` | 1 Byte | Bitmask status port input digital |
| `41` | `output_status`| `uint8_t` | 1 Byte | Bitmask status port output digital |
| `42` | `hdop_x10` | `uint8_t` | 1 Byte | Akurasi GPS HDOP $\times 10$ (contoh: `8` = `0.8`) |
| `43..44` | `temp_x10` | `int16_t` | 2 Bytes | Suhu CPU sensor $\times 10$ (contoh: `425` = `42.5` °C) |
| `45..46` | `gs_x` | `int16_t` | 2 Bytes | Akselerasi G-Sensor Sumbu X |
| `47..48` | `gs_y` | `int16_t` | 2 Bytes | Akselerasi G-Sensor Sumbu Y |
| `49..50` | `gs_z` | `int16_t` | 2 Bytes | Akselerasi G-Sensor Sumbu Z |
| `51..56` | `beacon_mac`| `uint8_t[6]` | 6 Bytes | MAC address Bluetooth Beacon terdekat |
| `57` | `beacon_rssi`| `int8_t` | 1 Byte | Kuat sinyal Bluetooth Beacon dalam dBm |
| `58` | `event_code` | `uint8_t` | 1 Byte | Kode event tracker (`2`=IGN ON, `3`=IGN OFF, `51`=Interval) |
| `59` | `flags` | `uint8_t` | 1 Byte | Flags sistem (Bit 0: GPS Fix, Bit 1: Relay Origin) |
| `60..61` | `reserved` | `uint16_t` | 2 Bytes | Reserved padding alignment |
| `62..63` | `crc16` | `uint16_t` | 2 Bytes | Checksum CRC16-CCITT atas byte 0 s/d 61 |

---

## 3. Format Topic MQTT
* **Binary Telemetry**: `kutai/fleet/binary`
* **ACK Topic**: `kutai/fleet/ack/<SRC>` (contoh: `kutai/fleet/ack/EXCA01`)
* **ACK Payload JSON**: `{"id": "<SRC>-<TIMESTAMP>-<SEQ>", "status": "ok"}`
