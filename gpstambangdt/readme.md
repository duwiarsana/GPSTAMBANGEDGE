# 🚛 DT (Dump Truck) IoT Relay System

Sistem DT berfungsi sebagai **relay data** antara EXCA (Excavator) dan backend server menggunakan MQTT.

DT akan:
- Logging GPS sendiri
- Mengambil data dari EXCA via WiFi
- Menyimpan data ke SD Card
- Mengirim data ke server saat ada internet
- Menggunakan retry engine + ACK backend (anti data hilang & duplicate)

---

## 🧠 Arsitektur Sistem

```
GPS DT → SD (dt_log.jsonl)
EXCA → WiFi → SD (relay_log.jsonl)

DT → Internet → MQTT → Backend
                 ↑
                ACK
```

---

## ⚙️ Fitur Utama

### 🔹 1. Dual Logger
- DT log sendiri → `/dt_log.jsonl`
- Data dari EXCA → `/relay_log.jsonl`

---

### 🔹 2. UID System (Anti Duplicate)

Format:
```
DT01-IMEI-TIMESTAMP-SEQ
```

Contoh:
```
DT01-861327085560006-20260326T141225Z-55
```

---

### 🔹 3. EXCA Auto Discovery

DT akan scan WiFi:
```
EXCA*_DATA
```

→ connect ke EXCA terdekat (RSSI terbaik)

---

### 🔹 4. Transfer Protocol

```
HELLO → READY → GET → DATA → NEXT → END → OK
```

---

### 🔹 5. Store & Forward System

Data disimpan dulu di SD:
- aman walau tidak ada internet
- kirim saat internet tersedia

---

### 🔹 6. Retry Engine + ACK Backend

Flow:

```
publish → tunggu ACK
   → sukses → update offset
   → gagal → retry
```

---

### 🔹 7. Offset System

File:
```
/dt_offset.txt
/relay_offset.txt
```

Fungsi:
- mencegah double kirim
- bisa resume

---

### 🔹 8. MQTT Communication

Publish:
```
kutai/fleet/data
```

ACK:
```
kutai/fleet/ack/DT01
```

---

## 📂 Struktur File

```
/dt_log.jsonl
/relay_log.jsonl
/dt_offset.txt
/relay_offset.txt
/dt_seq.txt
```

---

## 🛡️ Reliability Features

| Feature | Status |
|--------|-------|
| Retry publish | ✔ |
| ACK backend | ✔ |
| Anti duplicate | ✔ |
| Resume transfer | ✔ |
| Offline safe | ✔ |

---

## 🔧 Konfigurasi

Ganti ID DT:

```cpp
const char* DT_ID = "DT01";
```

---

## 🌐 Internet Setup

Edit:

```cpp
WifiCredential wifiList[] = {
  {"SSID", "PASSWORD"}
};
```

---

## 📡 EXCA Setup

Default:
```
SSID: EXCA01_DATA
PASS: 12345678
IP:   192.168.4.1
PORT: 5000
```

---

## 📤 Backend Requirement

Backend harus:
- simpan berdasarkan `msg_id`
- kirim ACK

Contoh ACK:

```json
{
  "msg_id":"EXCA01-861327085560006-20260326T141225Z-1023",
  "status":"ok"
}
```

---

## 🧠 Konsep Utama

```
Delay-Tolerant Network (DTN)
```

---

## 🚀 Multi DT Support

Cukup ubah:

```
DT01 → DT02 → DT03
```

Semua DT:
- tidak bentrok
- punya ACK sendiri
- bisa jalan bersamaan

---

## 🔥 Status Sistem

```
EXCA → DT → MQTT → Backend
```

✔ Production ready  
✔ Scalable  
✔ Reliable  

---

## 🙌 Credit

Developed by: Duwi Arsana 🚀
