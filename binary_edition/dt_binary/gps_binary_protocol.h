#ifndef GPS_BINARY_PROTOCOL_H
#define GPS_BINARY_PROTOCOL_H

#include <Arduino.h>

#define GPS_BINARY_MAGIC_0 0xAA
#define GPS_BINARY_MAGIC_1 0x55
#define GPS_BINARY_VERSION 2
#define TELEMETRY_PACKET_SIZE 64

#pragma pack(push, 1)
struct TelemetryPacketBinary {
  uint8_t  magic[2];       // 0xAA, 0x55 (Sync Marker) [2B]
  uint8_t  version;        // Protocol version: 2 [1B]
  char     src[6];         // Device ID, null-padded e.g. "EXCA01" [6B]
  uint64_t imei;           // 15-digit IMEI number e.g. 861327085563067 [8B]
  uint32_t seq;            // Sequential counter [4B]
  uint32_t timestamp;      // Unix Epoch UTC seconds [4B]
  int32_t  lat_x1e7;       // Latitude * 10,000,000 [4B]
  int32_t  lon_x1e7;       // Longitude * 10,000,000 [4B]
  uint16_t speed_x10;      // Speed km/h * 10 [2B]
  uint16_t heading;        // Heading in degrees (0-360) [2B]
  int16_t  altitude;       // Altitude in meters (-32768..32767) [2B]
  uint16_t bat_mv;         // Battery / External in mV (e.g. 24500 = 24.5V) [2B]
  uint8_t  ignition;       // 1 = ON, 0 = OFF [1B]
  uint8_t  input_status;   // Digital Inputs bitmask (Bit0: PTO Bak, Bit1: ACC) [1B]
  uint8_t  flags;          // Status flags (bit0: GPS Fix, bit1: Backlog Relay) [1B]
  uint8_t  beacon_mac[6];  // Strongest Bluetooth Beacon MAC [6B]
  int8_t   beacon_rssi;    // Strongest Beacon RSSI in dBm (-128..127) [1B]
  uint32_t ibutton_id;     // iButton Hex ID as uint32 (e.g. 0x010A0D09) [4B]
  uint8_t  ibutton_flags;  // iButton status (bit 0: 1=login/0=logout, bit 1: 1=auth/0=unauth) [1B]
  int16_t  gs_x;           // G-Sensor X axis in milli-g [2B]
  int16_t  gs_y;           // G-Sensor Y axis in milli-g [2B]
  int16_t  gs_z;           // G-Sensor Z axis in milli-g [2B]
  uint16_t crc16;          // CRC16-CCITT checksum over first 62 bytes [2B]
};
#pragma pack(pop)

// Verifikasi compile-time bahwa ukuran struct tepat 64 bytes
static_assert(sizeof(TelemetryPacketBinary) == TELEMETRY_PACKET_SIZE, "TelemetryPacketBinary must be exactly 64 bytes");

// Helper CRC16-CCITT (Poly: 0x1021, Init: 0xFFFF)
inline uint16_t calculateCRC16(const uint8_t *data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < length; i++) {
    crc ^= ((uint16_t)data[i] << 8);
    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 0x8000) {
        crc = (crc << 1) ^ 0x1021;
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

inline void initBinaryPacket(TelemetryPacketBinary &pkt, const char *srcId, uint32_t seq) {
  memset(&pkt, 0, sizeof(pkt));
  pkt.magic[0] = GPS_BINARY_MAGIC_0;
  pkt.magic[1] = GPS_BINARY_MAGIC_1;
  pkt.version = GPS_BINARY_VERSION;
  strncpy(pkt.src, srcId, sizeof(pkt.src) - 1);
  pkt.src[sizeof(pkt.src) - 1] = '\0';
  pkt.seq = seq;
}

inline void finalizeBinaryPacket(TelemetryPacketBinary &pkt) {
  pkt.magic[0] = GPS_BINARY_MAGIC_0;
  pkt.magic[1] = GPS_BINARY_MAGIC_1;
  pkt.version = GPS_BINARY_VERSION;
  pkt.crc16 = calculateCRC16((const uint8_t *)&pkt, offsetof(TelemetryPacketBinary, crc16));
}

inline bool validateBinaryPacket(const TelemetryPacketBinary &pkt) {
  if (pkt.magic[0] != GPS_BINARY_MAGIC_0 || pkt.magic[1] != GPS_BINARY_MAGIC_1) return false;
  if (pkt.version != GPS_BINARY_VERSION) return false;
  uint16_t computed = calculateCRC16((const uint8_t *)&pkt, offsetof(TelemetryPacketBinary, crc16));
  return (computed == pkt.crc16);
}

// Helper konversi string ISO8601 UTC "2026-06-25T07:39:35Z" atau "20260625T073935Z" ke Unix Epoch seconds
inline uint32_t parseISO8601ToEpoch(const char *isoStr) {
  if (!isoStr || strlen(isoStr) < 10) return 0;
  int yr = 1970, mo = 1, dy = 1, hr = 0, mn = 0, sc = 0;
  
  if (strchr(isoStr, '-')) {
    sscanf(isoStr, "%d-%d-%dT%d:%d:%d", &yr, &mo, &dy, &hr, &mn, &sc);
  } else {
    sscanf(isoStr, "%4d%2d%2dT%2d%2d%2d", &yr, &mo, &dy, &hr, &mn, &sc);
  }

  if (yr < 1970) return 0;
  
  int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if ((yr % 4 == 0 && yr % 100 != 0) || (yr % 400 == 0)) {
    daysInMonth[1] = 29;
  }

  long totalDays = 0;
  for (int y = 1970; y < yr; y++) {
    totalDays += ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) ? 366 : 365;
  }
  for (int m = 1; m < mo && m <= 12; m++) {
    totalDays += daysInMonth[m - 1];
  }
  totalDays += (dy - 1);

  return (uint32_t)(totalDays * 86400UL + hr * 3600UL + mn * 60UL + sc);
}

// Helper format unique UID string dari binary packet untuk ACK
inline String getPacketUID(const TelemetryPacketBinary &pkt) {
  char cleanSrc[sizeof(pkt.src) + 1];
  memset(cleanSrc, 0, sizeof(cleanSrc));
  memcpy(cleanSrc, pkt.src, sizeof(pkt.src));
  char buf[64];
  snprintf(buf, sizeof(buf), "%s-%u-%u", cleanSrc, pkt.timestamp, pkt.seq);
  return String(buf);
}

#endif // GPS_BINARY_PROTOCOL_H
