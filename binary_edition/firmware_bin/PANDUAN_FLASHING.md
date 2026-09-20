# Panduan Flashing Firmware Binary (DT & EXCA)

Firmware binary telah berhasil di-ekspor dan siap di-flash menggunakan tools seperti:
- **Espressif Flash Download Tools** (Windows)
- **esptool.py** / **ESP Web Flasher** (Browser Chrome via Web Serial)
- **NodeMCU PyFlasher** / GUI Flasher lainnya

---

## ⚡ CARA 1: Paling Mudah (HANYA 1 FILE BIN - MERGED)
Telah disediakan file **`merged.bin`** (sudah menggabungkan bootloader, partition table, boot app, dan firmware aplikasi menjadi satu file utuh).

### Untuk Dump Truck:
- **File**: `binary_edition/firmware_bin/dt/dt_binary.ino.merged.bin`
- **Offset Alamat**: **`0x0`** (atau `0x0000`)

### Untuk Excavator:
- **File**: `binary_edition/firmware_bin/exca/exca_binary.ino.merged.bin`
- **Offset Alamat**: **`0x0`** (atau `0x0000`)

> Cukup pilih satu file tersebut di software flasher di offset `0x0`, centang, lalu klik **START**.

---

## 🛠️ CARA 2: Multi-File (Standar Espressif Flash Download Tools)
Jika ingin memasukkan file partisi secara manual:

### Dump Truck (DT):
| File | Alamat / Offset |
| :--- | :--- |
| `dt_binary.ino.bootloader.bin` | **`0x1000`** |
| `dt_binary.ino.partitions.bin` | **`0x8000`** |
| `dt_binary.ino.bin` | **`0x10000`** |

### Excavator (EXCA):
| File | Alamat / Offset |
| :--- | :--- |
| `exca_binary.ino.bootloader.bin` | **`0x1000`** |
| `exca_binary.ino.partitions.bin` | **`0x8000`** |
| `exca_binary.ino.bin` | **`0x10000`** |

---

## ⚙️ Setting Rekomendasi di Flasher Tools
- **Chip Type**: ESP32
- **SPI Speed**: 40MHz atau 80MHz
- **SPI Mode**: DIO
- **Baud Rate**: 921600 (atau 115200 jika kabel kurang stabil)
- **DoNotChgBin**: Centang (Checked)
