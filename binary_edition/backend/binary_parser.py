"""
Binary Telemetry Parser (64-Byte Struct)
For GPS Tambang Edge - Binary Edition
"""

import struct
from datetime import datetime, timezone
from typing import Optional, Dict, Any

TELEMETRY_STRUCT_FMT_V2 = "<2sB8sQIIiiHHhHBBB6sbIBhhhH"
TELEMETRY_STRUCT_FMT_V1 = "<2sB8sIIiiHHhHI4Bh3h6sbBBHH"
TELEMETRY_PACKET_SIZE = 66

def calculate_crc16(data: bytes) -> int:
    """CRC16-CCITT (Poly: 0x1021, Init: 0xFFFF) matching ESP32 implementation."""
    crc = 0xFFFF
    for byte in data:
        crc ^= (byte << 8)
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc

def parse_telemetry_packet(raw_bytes: bytes) -> Optional[Dict[str, Any]]:
    """
    Unpacks 66-byte raw telemetry binary packet into a clean Python dictionary.
    Returns None if packet is invalid or CRC checksum fails.
    """
    if len(raw_bytes) != TELEMETRY_PACKET_SIZE:
        return None

    if raw_bytes[:2] != b'\xaa\x55':
        return None

    # Validate CRC16 over first 64 bytes
    received_crc = struct.unpack("<H", raw_bytes[64:66])[0]
    calculated_crc = calculate_crc16(raw_bytes[:64])
    if calculated_crc != received_crc:
        return None

    version = raw_bytes[2]
    imei_str = ""
    ibutton_hex = ""
    ibutton_id = 0
    ibutton_flags = 0
    output_status = 0
    hdop = 0.0
    temp = 0.0
    gs_x, gs_y, gs_z = 0, 0, 0
    event_code = 51

    if version == 2:
        (
            magic,
            version,
            src_raw,
            imei_num,
            seq,
            timestamp_sec,
            lat_x1e7,
            lon_x1e7,
            speed_x10,
            heading,
            altitude,
            bat_mv,
            ignition,
            input_status,
            flags,
            beacon_mac_raw,
            beacon_rssi,
            ibutton_id,
            ibutton_flags,
            gs_x,
            gs_y,
            gs_z,
            received_crc
        ) = struct.unpack(TELEMETRY_STRUCT_FMT_V2, raw_bytes)
        imei_str = str(imei_num) if imei_num > 0 else ""
        if ibutton_id > 0:
            ibutton_hex = f"{ibutton_id:08X}"
    else:
        (
            magic,
            version,
            src_raw,
            seq,
            timestamp_sec,
            lat_x1e7,
            lon_x1e7,
            speed_x10,
            heading,
            altitude,
            bat_mv,
            odo_m,
            ignition,
            input_status,
            output_status,
            hdop_x10,
            temp_x10,
            gs_x,
            gs_y,
            gs_z,
            beacon_mac_raw,
            beacon_rssi,
            event_code,
            flags,
            reserved,
            received_crc
        ) = struct.unpack(TELEMETRY_STRUCT_FMT_V1, raw_bytes)
        hdop = round(hdop_x10 / 10.0, 1)
        temp = round(temp_x10 / 10.0, 1)

    # Decode string source ID
    src = src_raw.decode('ascii', errors='ignore').rstrip('\x00').strip()
    
    # Format Beacon MAC Address
    if any(b != 0 for b in beacon_mac_raw):
        beacon_mac = ":".join(f"{b:02X}" for b in beacon_mac_raw)
    else:
        beacon_mac = ""

    # 🛡️ Data Integrity Guard: Tolak jika timestamp invalid, lat/lon 0.0, atau IMEI kosong
    if timestamp_sec < 1577836800 or (lat_x1e7 == 0 and lon_x1e7 == 0) or not imei_str or imei_str == "0":
        return None

    # Convert Timestamp to ISO8601 string
    try:
        dt = datetime.fromtimestamp(timestamp_sec, tz=timezone.utc)
        ts_iso = dt.strftime("%Y-%m-%d %H:%M:%S")
    except Exception:
        ts_iso = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S")

    unique_id = f"{src}-{timestamp_sec}-{seq}"

    return {
        "id": unique_id,
        "src": src,
        "imei": imei_str,
        "ibutton": ibutton_hex,
        "ibutton_auth": bool(ibutton_flags & 0x02),
        "ibutton_login": bool(ibutton_flags & 0x01),
        "seq": seq,
        "timestamp_sec": timestamp_sec,
        "ts": ts_iso,
        "lat": round(lat_x1e7 / 1e7, 7),
        "lon": round(lon_x1e7 / 1e7, 7),
        "spd": round(speed_x10 / 10.0, 1),
        "hdg": heading,
        "alt": altitude,
        "bat": round(bat_mv / 1000.0, 2),
        "bat_mv": bat_mv,
        "ign": ignition,
        "pto": 1 if (input_status & 0x01) else 0,
        "in": f"{input_status:02X}",
        "out": f"{output_status:02X}",
        "hdop": hdop,
        "temp": temp,
        "gs": {
            "x": gs_x,
            "y": gs_y,
            "z": gs_z
        },
        "be": [
            {
                "mac": beacon_mac,
                "rssi": beacon_rssi
            }
        ] if beacon_mac else [],
        "event_code": event_code,
        "flags": flags
    }

def parse_telemetry_batch(raw_bytes: bytes) -> list:
    """
    Unpacks a raw payload containing 1 or more 64-byte binary packets.
    Returns a list of valid parsed telemetry dictionaries.
    """
    if not raw_bytes:
        return []
    
    results = []
    num_packets = len(raw_bytes) // TELEMETRY_PACKET_SIZE
    for i in range(num_packets):
        chunk = raw_bytes[i * TELEMETRY_PACKET_SIZE : (i + 1) * TELEMETRY_PACKET_SIZE]
        parsed = parse_telemetry_packet(chunk)
        if parsed:
            results.append(parsed)
    return results

if __name__ == "__main__":
    # Test pack and unpack
    src_b = b"EXCA01\x00\x00"
    mac_b = bytes([0xC3, 0x00, 0x00, 0x38, 0xB4, 0x52])
    test_data = struct.pack(
        "<2sB8sIIiiHHhHI4Bh3h6sbBBHH",
        b'\xaa\x55', 1, src_b, 100, 1775738126,
        int(-0.7388810 * 1e7), int(117.1301520 * 1e7),
        250, 67, 45, 25400, 3815, 1, 0, 0, 8, 425,
        -734, -263, 583, mac_b, -67, 51, 1, 0, 0
    )
    # Calculate and put CRC
    crc = calculate_crc16(test_data[:62])
    final_packet = test_data[:62] + struct.pack("<H", crc)
    
    # Test batch unpack with 2 packets
    batch_data = final_packet * 2
    parsed_batch = parse_telemetry_batch(batch_data)
    print(f"Test Batch Unpack: Parsed {len(parsed_batch)} packets.")
    import pprint
    pprint.pprint(parsed_batch[0])

