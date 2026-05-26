# Pemetaan Variabel Final (Optimasi GPS Log)

Dokumen ini menjelaskan pemetaan variabel dari format JSON asli yang dikirim oleh GPS tracker (NL02) ke format JSON yang telah dioptimasi dan disimpan ke Micro SD oleh firmware ESP32 (DT dan EXCA).

## Tabel Pemetaan Variabel

| Variabel Asli (JSON Tracker) | Variabel Baru (Shortened JSON) | Tipe Data | Keterangan |
| :--- | :--- | :--- | :--- |
| `msg_id` | `id` | String | ID pesan unik (Format: `DEVICE_ID-IMEI-TIMESTAMP-SEQ`) |
| `imei` | `imei` | String | IMEI perangkat tracker |
| `source` | `src` | String | Nama/ID perangkat asal (contoh: `EXCA01`, `DT01`) |
| `event_info` | `type` | String | Tipe/Alasan log dicatat (contoh: `Interval`, `Ignition On`, `Moving`) |
| `event_code` | `ev` | Integer | Kode event |
| `timestamp` | `ts` | String | Waktu log dicatat oleh GPS (ISO 8601 UTC) |
| `latitude` | `lat` | Double | Koordinat garis lintang (latitude) |
| `longitude` | `lon` | Double | Koordinat garis bujur (longitude) |
| `speed` | `spd` | Integer | Kecepatan dalam km/jam |
| `heading` | `hdg` | Integer | Arah hadap kendaraan (0-359 derajat) |
| `altitude` | `alt` | Integer | Ketinggian dari permukaan laut (meter) |
| `external` | `bat` | Integer | Tegangan aki/power supply eksternal (dalam mV, contoh: `25345` = 25.3V) |
| `odometer` | `odo` | Integer | Total jarak tempuh (meter) |
| `ignition` | `ign` | Integer | Status kunci kontak/mesin (`1` = ON, `0` = OFF) |
| `input_status` | `in` | String | Nilai status port input digital (hex string) |
| `output_status` | `out` | String | Nilai status port output digital (hex string) |
| `hdop` | `hdop` | Double | Horizontal Dilution of Precision (tingkat akurasi GPS) |
| `mcu_temp` | `temp` | Double | Suhu internal chip sensor / CPU (derajat Celcius) |
| `gsensor` | `gs` | Object | Data akselerasi 3-sumbu `{x, y, z}` |
| `ibutton` | `ib` | Object | Data RFID iButton `{id, st, au}` |
| `ibeacon` | `be` | Array | Daftar perangkat Bluetooth Beacon terdekat `{mac, rssi}` |

---

## Detail Struktur Nested Object

### 1. Sensor G-Sensor (`gs`)
Berisi data akselerasi pergerakan 3-sumbu kendaraan:
* `x` (Integer): Akselerasi sumbu X
* `y` (Integer): Akselerasi sumbu Y
* `z` (Integer): Akselerasi sumbu Z

### 2. RFID iButton (`ib`)
Berisi data identitas pengemudi melalui iButton/RFID:
* `id` (String): ID tag iButton/RFID
* `st` (String): Status login/logout
* `au` (Boolean): Status autentikasi (`true` / `false`)

### 3. Bluetooth iBeacon (`be`)
Berisi daftar beacon Bluetooth yang terdeteksi di sekitar alat:
* `mac` (String): MAC Address Bluetooth Beacon
* `rssi` (Integer): Kuat sinyal beacon (dBm)

---

## Contoh Format JSON Final (1 Baris Log)

```json
{
  "id": "EXCA01-861327085563067-20260409T123526Z-18812",
  "imei": "861327085563067",
  "src": "EXCA01",
  "type": "Interval",
  "ev": 51,
  "ts": "2026-04-09T12:35:26Z",
  "lat": -0.738881,
  "lon": 117.130152,
  "spd": 0,
  "hdg": 67,
  "alt": 0,
  "bat": 25345,
  "odo": 3815,
  "ign": 0,
  "in": "000000",
  "out": "00",
  "hdop": 0.6,
  "temp": 70.58,
  "gs": {
    "x": -734,
    "y": -263,
    "z": 583
  },
  "ib": {
    "id": "010A0D09",
    "st": "login",
    "au": true
  },
  "be": [
    {
      "mac": "C3:00:00:38:B4:52",
      "rssi": -67
    }
  ]
}
```
