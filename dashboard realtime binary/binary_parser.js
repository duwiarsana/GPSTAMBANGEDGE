/**
 * Binary Parser for GPS Tambang Telemetry (66-Byte Struct Version 2)
 * Matches firmware TelemetryPacketBinary exactly.
 */

const TELEMETRY_PACKET_SIZE = 66;

// CRC16-CCITT (Poly: 0x1021, Init: 0xFFFF)
function calculateCRC16(buffer, length) {
  let crc = 0xFFFF;
  for (let i = 0; i < length; i++) {
    crc ^= (buffer[i] << 8);
    for (let j = 0; j < 8; j++) {
      if ((crc & 0x8000) !== 0) {
        crc = ((crc << 1) ^ 0x1021) & 0xFFFF;
      } else {
        crc = (crc << 1) & 0xFFFF;
      }
    }
  }
  return crc;
}

function parseBinaryPacket(buf) {
  if (buf.length !== TELEMETRY_PACKET_SIZE) return null;
  if (buf[0] !== 0xAA || buf[1] !== 0x55) return null;

  // CRC Check
  const receivedCrc = buf.readUInt16LE(64);
  const calculatedCrc = calculateCRC16(buf, 64);
  if (receivedCrc !== calculatedCrc) {
    return null;
  }

  const version = buf.readUInt8(2);

  // Device ID (bytes 3..10, 8 bytes null-terminated ASCII)
  let src = buf.subarray(3, 11).toString('ascii').replace(/\0/g, '').trim();
  if (!src) src = "UNKNOWN";

  // IMEI (bytes 11..18, uint64 Little Endian)
  let imei = "";
  try {
    const imeiBig = buf.readBigUInt64LE(11);
    imei = imeiBig > 0n ? imeiBig.toString() : "";
  } catch (e) {
    imei = "";
  }

  const seq = buf.readUInt32LE(19);
  const timestamp = buf.readUInt32LE(23);
  const lat_x1e7 = buf.readInt32LE(27);
  const lon_x1e7 = buf.readInt32LE(31);
  const speed_x10 = buf.readUInt16LE(35);
  const heading = buf.readUInt16LE(37);
  const altitude = buf.readInt16LE(39);
  const bat_mv = buf.readUInt16LE(41);
  const ignition = buf.readUInt8(43);
  const input_status = buf.readUInt8(44);
  const flags = buf.readUInt8(45);

  // Beacon MAC (bytes 46..51)
  const beaconBytes = buf.subarray(46, 52);
  let beaconMac = "";
  let hasBeacon = false;
  for (let i = 0; i < 6; i++) {
    if (beaconBytes[i] !== 0) {
      hasBeacon = true;
      break;
    }
  }
  if (hasBeacon) {
    beaconMac = Array.from(beaconBytes).map(b => b.toString(16).padStart(2, '0').toUpperCase()).join(':');
  }

  const beacon_rssi = buf.readInt8(52);
  const ibutton_id = buf.readUInt32LE(53);
  const ibutton_flags = buf.readUInt8(57);
  const gs_x = buf.readInt16LE(58);
  const gs_y = buf.readInt16LE(60);
  const gs_z = buf.readInt16LE(62);

  // Reject zero coordinate
  if (lat_x1e7 === 0 && lon_x1e7 === 0) return null;

  const lat = Math.round((lat_x1e7 / 1e7) * 1e7) / 1e7;
  const lon = Math.round((lon_x1e7 / 1e7) * 1e7) / 1e7;
  const spd = Math.round((speed_x10 / 10.0) * 10) / 10;
  const bat = Math.round((bat_mv / 1000.0) * 100) / 100;

  // Format ISO timestamp
  let dateObj = new Date(timestamp * 1000);
  if (isNaN(dateObj.getTime()) || timestamp < 1577836800) {
    dateObj = new Date();
  }
  const ts = dateObj.toISOString().replace('T', ' ').substring(0, 19);

  return {
    id: `${src}-${timestamp}-${seq}`,
    src: src,
    type: src.toUpperCase().startsWith('EXCA') ? 'EXCA' : 'DT',
    imei: imei,
    seq: seq,
    timestamp: timestamp,
    ts: ts,
    lat: lat,
    lon: lon,
    spd: spd,
    hdg: heading,
    alt: altitude,
    bat: bat,
    bat_mv: bat_mv,
    ign: ignition,
    pto: (input_status & 0x01) ? 1 : 0,
    gps_fix: (flags & 0x01) ? 1 : 0,
    relay: (flags & 0x02) ? 1 : 0,
    beacon_mac: beaconMac,
    beacon_rssi: beacon_rssi,
    ibutton: ibutton_id > 0 ? ibutton_id.toString(16).toUpperCase().padStart(8, '0') : "",
    ibutton_auth: (ibutton_flags & 0x02) ? true : false,
    gs: { x: gs_x, y: gs_y, z: gs_z },
    updated_at: Date.now()
  };
}

function parseBinaryPayload(payload) {
  if (!payload || payload.length === 0) return [];
  const records = [];
  const count = Math.floor(payload.length / TELEMETRY_PACKET_SIZE);

  for (let i = 0; i < count; i++) {
    const chunk = payload.subarray(i * TELEMETRY_PACKET_SIZE, (i + 1) * TELEMETRY_PACKET_SIZE);
    const rec = parseBinaryPacket(chunk);
    if (rec) {
      records.push(rec);
    }
  }
  return records;
}

module.exports = {
  TELEMETRY_PACKET_SIZE,
  parseBinaryPacket,
  parseBinaryPayload
};
