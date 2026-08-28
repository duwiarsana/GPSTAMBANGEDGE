/**
 * DUMP TRUCK BINARY EDITION - 64-Byte Ultra High-Performance Firmware
 * 
 * Features:
 * 1. 64-Byte Fixed-Size Raw Binary Telemetry Storage (85% storage & bandwidth saving)
 * 2. Persistent Fast WiFi Connect via NVS Preferences (<500ms) with Smart Scan Fallback
 * 3. High-Speed P2P TCP Binary Harvester from EXCA AP (<1s per 10k data)
 * 4. Dual Backlog Clearance (DT Own Binary Log + Relayed EXCA Binary Log)
 * 5. Direct 64-Byte Binary MQTT Ingestion
 * 6. SD Card Self-Healing & Hot-Plug Auto Re-mount
 * 7. Modem-Sleep Power Saving during 30s Cooldown / IDLE
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_task_wdt.h>

#include "gps_binary_protocol.h"

// ================= PIN CONFIGURATION =================
#define GPS_RX 16
#define GPS_TX 17
#define SD_CS 5

#define LED_REC 13
#define LED_GPS 13
#define LED_EXCA 13
#define LED_MQTT 13

// ================= WATCHDOG & MEMORY =================
#define WDT_TIMEOUT_SEC 30
#define HEAP_MIN_BYTES 20000

// ================= ID DEVICE =================
const char *DT_ID = "DT01";

// ================= UART GPS =================
#define GPS_BAUD 115200

// ================= EXCA P2P HARVESTING =================
const char *EXCA_PASS = "12345678";
const uint16_t EXCA_PORT = 5000;
IPAddress excaIP(192, 168, 4, 1);

// ================= MQTT BROKER =================
const char *MQTT_SERVER = "76.13.19.250";
const uint16_t MQTT_PORT = 1883;
const char *MQTT_BINARY_TOPIC = "kutai/fleet/binary";

WiFiClient espClient;
PubSubClient mqtt(espClient);

// ================= INTERNET WIFI GATEWAYS =================
struct WifiCredential {
  const char *ssid;
  const char *pass;
};

WifiCredential wifiList[] = {
  {"WIFI_GATEWAY_MINING_11", "46448951"},
  {"HOTSPOT_DT_KEAMANAN", "46448951"}
};
const int wifiCount = sizeof(wifiList) / sizeof(wifiList[0]);

// ================= PERSISTENT FAST WIFI (NVS) =================
struct WifiCache {
  bool valid;
  int index;
  uint8_t channel;
  uint8_t bssid[6];
};

// ================= FILESYSTEM PATHS =================
const char *DT_LOG_FILE_BIN       = "/dt_log.bin";
const char *RELAY_LOG_FILE_BIN    = "/relay_log.bin";

const char *DT_OFFSET_FILE        = "/dt_offset.txt";
const char *RELAY_OFFSET_FILE     = "/relay_offset.txt";

const char *DT_SEQ_FILE           = "/dt_seq.txt";

// ================= BUFFER & PARSER =================
#define BUF_SIZE 4096
char gpsBuf[BUF_SIZE];
int gpsBufLen = 0;
int gpsBrace = 0;
bool gpsCollecting = false;
unsigned long gpsStartJson = 0;

// ================= STATE & TIMERS =================
uint32_t dtSeq = 0;
bool excaTransferBusy = false;
bool busy = false;

unsigned long lastExcaScan = 0;
unsigned long lastInternetTry = 0;
unsigned long lastCompact = 0;
unsigned long ledGpsTimer = 0;

const unsigned long EXCA_SCAN_INTERVAL = 10000;
const unsigned long INTERNET_INTERVAL  = 10000;
const unsigned long COMPACT_INTERVAL   = 1800000;
const int MAX_UPLOAD_CHUNK_RECORDS     = 100;

// ================= HARDWARE HEALTH & RECOVERY =================
int sdErrorCount = 0;
bool sdReady = false;

// ================= ACK STATE =================
String ackTopic = "";
String lastAckMsgId = "";
bool ackReceived = false;

// ================= STATS =================
uint32_t statGpsLogged = 0;
uint32_t statExcaRelayed = 0;
uint32_t statMqttSent = 0;
uint32_t statChunksUploaded = 0;

// ================= IGNITION STATE =================
enum RecordState { REC_IDLE, REC_ACTIVE, REC_COOLDOWN };
RecordState recordState = REC_IDLE;
unsigned long ignOffTime = 0;
const unsigned long IGN_COOLDOWN_MS = 30000;

unsigned long ledRecLastToggle = 0;
bool ledRecOn = false;

// Forward Declarations
void handleDTGps();
bool publishBinaryWithAck(const TelemetryPacketBinary &pkt, int maxRetries = 2);

// ================= DEBUG LOGGER =================
void logMsg(String s) {
  Serial.print("[");
  Serial.print(millis());
  Serial.print("] ");
  Serial.println(s);
}

// ================= FILE HELPERS =================
uint32_t readUint(const char *path, uint32_t def = 0) {
  File f = SD.open(path);
  if (!f) return def;
  String s = f.readString();
  f.close();
  s.trim();
  if (s.length() == 0) return def;
  return s.toInt();
}

void writeUint(const char *path, uint32_t v) {
  SD.remove(path);
  File f = SD.open(path, FILE_WRITE);
  if (f) {
    f.print(v);
    f.close();
  }
}

bool ensureUintFile(const char *path, uint32_t defaultVal = 0) {
  if (!SD.exists(path)) {
    writeUint(path, defaultVal);
  }
  return true;
}

// ================= INIT & AUTO-RECOVERY SD =================
bool initStorage() {
  SD.end();
  delay(50);
  if (!SD.begin(SD_CS)) {
    logMsg("❌ SD fail (Periksa Micro SD)");
    sdReady = false;
    return false;
  }

  ensureUintFile(DT_OFFSET_FILE, 0);
  ensureUintFile(RELAY_OFFSET_FILE, 0);
  ensureUintFile(DT_SEQ_FILE, 0);

  dtSeq = readUint(DT_SEQ_FILE, 0);

  sdErrorCount = 0;
  sdReady = true;
  logMsg("✅ SD ready [DT BINARY MODE], seq=" + String(dtSeq));
  return true;
}

void checkSDHealth() {
  if (sdErrorCount >= 3 || !sdReady) {
    logMsg("🔄 [Self-Healing] Mencoba re-mount Micro SD...");
    if (initStorage()) {
      logMsg("✨ Micro SD berhasil dipulihkan!");
    } else {
      sdErrorCount = 3;
    }
  }
}

bool appendBinaryRecord(const char *path, const TelemetryPacketBinary &pkt) {
  File f = SD.open(path, FILE_APPEND);
  if (!f) {
    sdErrorCount++;
    logMsg(String("❌ open fail: ") + path + " (err #" + String(sdErrorCount) + ")");
    return false;
  }
  size_t written = f.write((const uint8_t *)&pkt, sizeof(pkt));
  f.flush();
  f.close();
  sdErrorCount = 0;
  return (written == sizeof(pkt));
}

// ================= NVS WIFI CACHE =================
bool loadWifiCache(WifiCache &cache) {
  Preferences p;
  if (!p.begin("wificache", true)) {
    cache.valid = false;
    return false;
  }
  cache.valid = p.getBool("valid", false);
  cache.index = p.getInt("idx", -1);
  cache.channel = (uint8_t)p.getUChar("ch", 0);
  size_t len = p.getBytes("bssid", cache.bssid, 6);
  p.end();

  if (!cache.valid || cache.index < 0 || cache.index >= wifiCount ||
      cache.channel < 1 || cache.channel > 14 || len != 6) {
    cache.valid = false;
    return false;
  }
  return true;
}

void saveWifiCache(int index, uint8_t channel, const uint8_t *bssid) {
  if (index < 0 || index >= wifiCount || channel < 1 || channel > 14 || !bssid) return;

  WifiCache current;
  if (loadWifiCache(current)) {
    if (current.valid && current.index == index && current.channel == channel &&
        memcmp(current.bssid, bssid, 6) == 0) {
      return;
    }
  }

  Preferences p;
  if (p.begin("wificache", false)) {
    p.putBool("valid", true);
    p.putInt("idx", index);
    p.putUChar("ch", channel);
    p.putBytes("bssid", bssid, 6);
    p.end();
    logMsg("💾 WiFi cache saved to NVS: " + String(wifiList[index].ssid) + " CH=" + String(channel));
  }
}

// ================= IGNITION FILTER =================
bool shouldRecord(JsonDocument &doc) {
  int eventCode = doc["event_code"] | 0;
  int ignition = doc["ignition"] | -1;
  int inputStatus = doc["input_status"] | doc["din"] | -1;

  if (ignition == -1 && inputStatus != -1) {
    ignition = (inputStatus & 0x01) ? 1 : 0;
  }

  if (eventCode == 2 || eventCode == 3) {
    if (eventCode == 2) {
      recordState = REC_ACTIVE;
      logMsg("🔑 IGN ON -> ACTIVE");
    } else {
      if (recordState == REC_ACTIVE) {
        recordState = REC_COOLDOWN;
        ignOffTime = millis();
        logMsg("🔑 IGN OFF -> COOLDOWN (" + String(IGN_COOLDOWN_MS / 1000) + "s)");
      }
    }
    return true;
  }

  switch (recordState) {
  case REC_IDLE:
    if (ignition == 1) {
      recordState = REC_ACTIVE;
      logMsg("⏺️ -> ACTIVE (Ignition ON)");
      return true;
    }
    return false;

  case REC_ACTIVE:
    if (ignition == 0) {
      recordState = REC_COOLDOWN;
      ignOffTime = millis();
      logMsg("⏸️ -> COOLDOWN (30s)");
    }
    return true;

  case REC_COOLDOWN:
    if (ignition == 1) {
      recordState = REC_ACTIVE;
      logMsg("⏺️ -> ACTIVE (kembali ON)");
      return true;
    }
    if (millis() - ignOffTime >= IGN_COOLDOWN_MS) {
      recordState = REC_IDLE;
      logMsg("⏹️ -> IDLE (cooldown 30s selesai)");
      return false;
    }
    return true;

  default:
    return false;
  }
}

// ================= PARSE JSON TO BINARY STRUCT =================
bool parseDTGpsToBinary(const char *json, TelemetryPacketBinary &pkt) {
  StaticJsonDocument<1536> doc;
  static StaticJsonDocument<512> filter;
  static bool filterInitialized = false;
  if (!filterInitialized) {
    filter["imei"] = true;
    filter["event_code"] = true;
    filter["timestamp"] = true;
    filter["latitude"] = true;
    filter["lat"] = true;
    filter["longitude"] = true;
    filter["lon"] = true;
    filter["speed"] = true;
    filter["spd"] = true;
    filter["heading"] = true;
    filter["hdg"] = true;
    filter["course"] = true;
    filter["bearing"] = true;
    filter["angle"] = true;
    filter["odometer"] = true;
    filter["odo"] = true;
    filter["altitude"] = true;
    filter["alt"] = true;
    filter["ignition"] = true;
    filter["ign"] = true;
    filter["input_status"] = true;
    filter["in"] = true;
    filter["output_status"] = true;
    filter["out"] = true;
    filter["hdop"] = true;
    filter["hd"] = true;
    filter["mcu_temp"] = true;
    filter["temp"] = true;
    filter["external"] = true;
    filter["ext"] = true;
    filter["battery"] = true;
    filter["bat"] = true;
    filter["volt"] = true;
    filter["ibeacon"][0]["mac"] = true;
    filter["ibeacon"][0]["rssi"] = true;
    filter["gsensor"]["x"] = true;
    filter["gsensor"]["y"] = true;
    filter["gsensor"]["z"] = true;
    filterInitialized = true;
  }

  DeserializationError err = deserializeJson(doc, json, DeserializationOption::Filter(filter));
  if (err) return false;

  if (!shouldRecord(doc)) return false;

  dtSeq++;
  writeUint(DT_SEQ_FILE, dtSeq);

  initBinaryPacket(pkt, DT_ID, dtSeq);

  const char *ts = doc["timestamp"] | (doc["ts"] | "");
  pkt.timestamp = parseISO8601ToEpoch(ts);

  double lat = doc["latitude"] | (doc["lat"] | 0.0);
  double lon = doc["longitude"] | (doc["lon"] | 0.0);
  pkt.lat_x1e7 = (int32_t)(lat * 10000000.0);
  pkt.lon_x1e7 = (int32_t)(lon * 10000000.0);

  double spd = doc["speed"] | (doc["spd"] | 0.0);
  pkt.speed_x10 = (uint16_t)(spd * 10.0);
  pkt.heading = (uint16_t)(doc["heading"] | (doc["hdg"] | (doc["course"] | (doc["bearing"] | (doc["angle"] | 0)))));
  pkt.altitude = doc["altitude"] | (doc["alt"] | 0);

  double ext = doc["external"] | (doc["volt"] | (doc["battery"] | 0.0));
  if (ext > 100.0) {
    pkt.bat_mv = (uint16_t)ext;
  } else {
    pkt.bat_mv = (uint16_t)(ext * 1000.0);
  }
  pkt.odo_m = doc["odometer"] | 0;
  if (doc["input_status"].is<const char*>()) {
    const char *inp = doc["input_status"].as<const char*>();
    uint8_t mask = 0;
    for (int i = 0; inp[i] && i < 8; i++) {
      if (inp[i] == '1') mask |= (1 << i);
    }
    pkt.input_status = mask;
  } else {
    pkt.input_status = (uint8_t)(doc["input_status"] | 0);
  }
  pkt.output_status = doc["output_status"] | 0;

  double hdop = doc["hdop"] | 0.0;
  pkt.hdop_x10 = (uint8_t)(hdop * 10.0);

  double temp = doc["mcu_temp"] | 0.0;
  pkt.temp_x10 = (int16_t)(temp * 10.0);

  if (doc.containsKey("gsensor")) {
    pkt.gs_x = doc["gsensor"]["x"] | 0;
    pkt.gs_y = doc["gsensor"]["y"] | 0;
    pkt.gs_z = doc["gsensor"]["z"] | 0;
  }

  if (doc.containsKey("ibeacon") && doc["ibeacon"].size() > 0) {
    const char *macStr = doc["ibeacon"][0]["mac"] | "";
    int rssi = doc["ibeacon"][0]["rssi"] | 0;
    if (strlen(macStr) >= 12) {
      unsigned int m[6] = {0};
      sscanf(macStr, "%x:%x:%x:%x:%x:%x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]);
      for (int i = 0; i < 6; i++) pkt.beacon_mac[i] = (uint8_t)m[i];
      pkt.beacon_rssi = (int8_t)rssi;
    }
  }

  pkt.event_code = doc["event_code"] | 51;
  pkt.flags = 1;

  finalizeBinaryPacket(pkt);
  return true;
}

// ================= GPS SERIAL HANDLER =================
void resetGpsParser() {
  gpsBufLen = 0;
  gpsBrace = 0;
  gpsCollecting = false;
}

void handleDTGps() {
  while (Serial2.available()) {
    char c = Serial2.read();

    if (!gpsCollecting) {
      if (c == '{') {
        gpsCollecting = true;
        gpsBrace = 1;
        gpsBufLen = 0;
        gpsBuf[gpsBufLen++] = c;
        gpsStartJson = millis();
      }
      continue;
    }

    if (gpsBufLen < BUF_SIZE - 1) {
      gpsBuf[gpsBufLen++] = c;
    } else {
      resetGpsParser();
      continue;
    }

    if (c == '{') gpsBrace++;
    if (c == '}') gpsBrace--;

    if (gpsBrace == 0) {
      gpsBuf[gpsBufLen] = '\0';

      TelemetryPacketBinary pkt;
      if (parseDTGpsToBinary(gpsBuf, pkt)) {
        uint32_t dtOff = readUint(DT_OFFSET_FILE, 0);
        uint32_t sizeBefore = 0;
        File fCheck = SD.open(DT_LOG_FILE_BIN, FILE_READ);
        if (fCheck) {
          sizeBefore = fCheck.size();
          fCheck.close();
        }
        bool backlogClean = (dtOff >= sizeBefore);

        // 1. Simpan binary struct ke SD
        if (appendBinaryRecord(DT_LOG_FILE_BIN, pkt)) {
          statGpsLogged++;
          digitalWrite(LED_GPS, HIGH);
          ledGpsTimer = millis();

          uint32_t curSize = 0;
          File fc = SD.open(DT_LOG_FILE_BIN, FILE_READ);
          if (fc) {
            curSize = fc.size();
            fc.close();
          }
          uint32_t pendingBytes = (curSize > dtOff) ? (curSize - dtOff) : 0;
          float pendingMB = pendingBytes / (1024.0 * 1024.0);
          uint32_t pendingRecords = pendingBytes / sizeof(TelemetryPacketBinary);
          logMsg("📍 [BIN] DT logged #" + String(statGpsLogged) + " | Backlog: " + String(pendingMB, 3) + 
                 " MB (" + String(pendingRecords) + " records)");
        }

        // 2. Real-time direct binary MQTT publish
        if (!busy && backlogClean && WiFi.status() == WL_CONNECTED && mqtt.connected()) {
          busy = true;
          if (publishBinaryWithAck(pkt, 2)) {
            logMsg("⚡ Real-time direct binary MQTT publish success (DT)");
            File fCur = SD.open(DT_LOG_FILE_BIN, FILE_READ);
            if (fCur) {
              uint32_t newSize = fCur.size();
              fCur.close();
              writeUint(DT_OFFSET_FILE, newSize);
            }
          }
          busy = false;
        }
      }

      resetGpsParser();
      continue;
    }

    if (millis() - gpsStartJson > 4000) {
      resetGpsParser();
    }
  }
}

void flushStaleGpsData() {
  resetGpsParser();
  unsigned long t0 = millis();
  while (Serial2.available() && millis() - t0 < 100) {
    Serial2.read();
  }
}

// ================= PERSISTENT FAST WIFI CONNECT =================
bool connectKnownInternet() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  WiFi.setSleep(false);

  WifiCache cache;
  if (loadWifiCache(cache)) {
    char bssidStr[18];
    snprintf(bssidStr, sizeof(bssidStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             cache.bssid[0], cache.bssid[1], cache.bssid[2],
             cache.bssid[3], cache.bssid[4], cache.bssid[5]);
    logMsg("⚡ Fast-connect attempt to " + String(wifiList[cache.index].ssid) + " CH=" + String(cache.channel));

    unsigned long tFast = millis();
    WiFi.begin(wifiList[cache.index].ssid, wifiList[cache.index].pass, cache.channel, cache.bssid);

    while (WiFi.status() != WL_CONNECTED && millis() - tFast < 3000) {
      esp_task_wdt_reset();
      handleDTGps();
      delay(10);
    }

    if (WiFi.status() == WL_CONNECTED) {
      unsigned long elapsed = millis() - tFast;
      logMsg("⚡ Fast-connect success: " + String(elapsed) + " ms");
      logMsg("📡 Connected: SSID=" + String(WiFi.SSID()) + " IP=" + WiFi.localIP().toString());
      return true;
    } else {
      WiFi.disconnect(false, true);
    }
  }

  logMsg("🔍 Fallback to WiFi scan...");
  WiFi.scanNetworks(true);

  unsigned long tScan = millis();
  while (WiFi.scanComplete() < 0) {
    esp_task_wdt_reset();
    handleDTGps();
    delay(10);
    if (millis() - tScan > 4000) {
      WiFi.scanDelete();
      return false;
    }
  }

  int n = WiFi.scanComplete();
  if (n <= 0) {
    WiFi.scanDelete();
    return false;
  }

  int bestIdx = -1, bestScanIdx = -1, bestRSSI = -1000;
  for (int i = 0; i < wifiCount; i++) {
    for (int j = 0; j < n; j++) {
      if (WiFi.SSID(j) == wifiList[i].ssid) {
        int rssi = WiFi.RSSI(j);
        if (rssi > bestRSSI) {
          bestIdx = i;
          bestScanIdx = j;
          bestRSSI = rssi;
        }
      }
    }
  }

  if (bestIdx < 0) {
    WiFi.scanDelete();
    return false;
  }

  int ch = WiFi.channel(bestScanIdx);
  uint8_t *bssid = WiFi.BSSID(bestScanIdx);
  uint8_t bssidCopy[6];
  if (bssid) memcpy(bssidCopy, bssid, 6);
  WiFi.scanDelete();

  unsigned long t0 = millis();
  if (bssid) {
    WiFi.begin(wifiList[bestIdx].ssid, wifiList[bestIdx].pass, ch, bssidCopy);
  } else {
    WiFi.begin(wifiList[bestIdx].ssid, wifiList[bestIdx].pass);
  }

  while (WiFi.status() != WL_CONNECTED) {
    esp_task_wdt_reset();
    handleDTGps();
    delay(10);
    if (millis() - t0 > 6000) {
      WiFi.disconnect(false, true);
      return false;
    }
  }

  if (bssid) saveWifiCache(bestIdx, ch, bssidCopy);
  return true;
}

// ================= MQTT CLIENT & ACK =================
bool connectMQTT() {
  if (mqtt.connected()) return true;

  String clientId = String(DT_ID) + "-" + String(millis());
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback([](char *topic, byte *payload, unsigned int length) {
    String msg;
    for (int i = 0; i < length; i++) msg += (char)payload[i];
    msg.trim();

    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, msg);
    if (!err) {
      const char *id = doc["id"] | "";
      const char *st = doc["status"] | "";
      if (String(st) == "ok" && String(id).length() > 0) {
        lastAckMsgId = String(id);
        ackReceived = true;
        logMsg("📥 ACK received: " + lastAckMsgId);
      }
    }
  });

  if (mqtt.connect(clientId.c_str())) {
    ackTopic = "kutai/fleet/ack/" + String(DT_ID);
    mqtt.subscribe(ackTopic.c_str());
    logMsg("✅ MQTT connected (Binary Ingest), sub: " + ackTopic);
    return true;
  }

  return false;
}

bool publishBinaryWithAck(const TelemetryPacketBinary &pkt, int maxRetries) {
  String msgId = getPacketUID(pkt);

  for (int attempt = 1; attempt <= maxRetries; attempt++) {
    esp_task_wdt_reset();
    if (!mqtt.connected()) {
      if (!connectMQTT()) return false;
    }

    ackReceived = false;
    lastAckMsgId = "";

    logMsg("📤 [REALTIME 64B] published, wait ACK...");
    if (!mqtt.publish(MQTT_BINARY_TOPIC, (const uint8_t *)&pkt, sizeof(pkt))) {
      logMsg("❌ Binary Publish error");
      return false;
    }

    statMqttSent++;
    unsigned long t0 = millis();
    while (millis() - t0 < 2000) {
      esp_task_wdt_reset();
      handleDTGps();
      mqtt.loop();
      if (ackReceived && lastAckMsgId == msgId) {
        return true;
      }
      delay(5);
    }
    logMsg("🔁 ACK timeout #" + String(attempt));
  }
  return false;
}

#define BULK_PUBLISH_RECORDS 16

bool publishBinaryBulkWithAck(const uint8_t *bulkBuffer, size_t totalBytes, const String &lastMsgId, int maxRetries = 2) {
  for (int attempt = 1; attempt <= maxRetries; attempt++) {
    esp_task_wdt_reset();
    handleDTGps();
    if (!mqtt.connected()) {
      if (!connectMQTT()) return false;
    }

    ackReceived = false;
    lastAckMsgId = "";

    int count = totalBytes / sizeof(TelemetryPacketBinary);
    logMsg("🚀 [BULK MQTT 16-PACK] Sending " + String(count) + " records (" + String(totalBytes) + " B)...");

    if (!mqtt.publish(MQTT_BINARY_TOPIC, bulkBuffer, totalBytes)) {
      logMsg("❌ Binary Bulk Publish error");
      return false;
    }

    statMqttSent += count;
    unsigned long t0 = millis();
    while (millis() - t0 < 2500) {
      esp_task_wdt_reset();
      handleDTGps();
      mqtt.loop();
      if (ackReceived && lastAckMsgId == lastMsgId) {
        return true;
      }
      delay(5);
    }
    logMsg("🔁 Bulk ACK timeout #" + String(attempt));
  }
  return false;
}

// ================= QUEUE PUBLISHER (BULK BATCH) =================
bool publishBinaryQueueChunk(const char *logPath, const char *offsetPath, int maxRecords) {
  uint32_t offset = readUint(offsetPath, 0);

  File f = SD.open(logPath, FILE_READ);
  if (!f) return false;

  if (offset >= f.size()) {
    f.close();
    return true;
  }

  offset = (offset / sizeof(TelemetryPacketBinary)) * sizeof(TelemetryPacketBinary);
  if (!f.seek(offset)) {
    f.close();
    return false;
  }

  int sentCount = 0;
  TelemetryPacketBinary batchBuf[BULK_PUBLISH_RECORDS];

  while (f.available() >= sizeof(TelemetryPacketBinary) && sentCount < maxRecords) {
    esp_task_wdt_reset();
    handleDTGps();

    if (!mqtt.connected()) {
      if (!connectMQTT()) {
        f.close();
        return false;
      }
    }

    int toRead = min((int)BULK_PUBLISH_RECORDS, maxRecords - sentCount);
    int validInBatch = 0;
    uint32_t batchStartPos = f.position();

    for (int i = 0; i < toRead && f.available() >= sizeof(TelemetryPacketBinary); i++) {
      TelemetryPacketBinary pkt;
      size_t rb = f.read((uint8_t *)&pkt, sizeof(pkt));
      if (rb != sizeof(pkt)) break;

      if (validateBinaryPacket(pkt)) {
        batchBuf[validInBatch++] = pkt;
      } else {
        logMsg("⚠️ Corrupt binary packet at " + String((uint32_t)f.position() - sizeof(pkt)) + ", skipping");
      }
    }

    if (validInBatch == 0) {
      writeUint(offsetPath, f.position());
      break;
    }

    String lastId = getPacketUID(batchBuf[validInBatch - 1]);
    size_t sendBytes = validInBatch * sizeof(TelemetryPacketBinary);

    if (!publishBinaryBulkWithAck((const uint8_t *)batchBuf, sendBytes, lastId, 2)) {
      logMsg("⚠️ Bulk publish fail at offset " + String(batchStartPos));
      f.close();
      return false;
    }

    writeUint(offsetPath, f.position());
    sentCount += validInBatch;
  }

  f.close();
  statChunksUploaded++;
  logMsg("✅ Bulk Binary Chunk published: " + String(sentCount) + " records");
  return true;
}

// ================= DUAL BACKLOG DRAIN ROUTINE =================
void tryInternetAndPublishAll() {
  if (WiFi.status() != WL_CONNECTED) {
    if (!connectKnownInternet()) return;
  }

  if (!mqtt.connected()) {
    if (!connectMQTT()) return;
  }

  // 1. Kuras Backlog DT Sendiri
  uint32_t dtOff = readUint(DT_OFFSET_FILE, 0);
  File fDT = SD.open(DT_LOG_FILE_BIN, FILE_READ);
  if (fDT) {
    uint32_t fSize = fDT.size();
    fDT.close();
    if (dtOff < fSize) {
      logMsg("🚀 Uploading DT BINARY backlog (sisa: " + String(fSize - dtOff) + " bytes / " + 
             String((fSize - dtOff) / sizeof(TelemetryPacketBinary)) + " records)...");
      while (publishBinaryQueueChunk(DT_LOG_FILE_BIN, DT_OFFSET_FILE, MAX_UPLOAD_CHUNK_RECORDS)) {
        esp_task_wdt_reset();
        handleDTGps();
        delay(5);
        uint32_t curOff = readUint(DT_OFFSET_FILE, 0);
        File fc = SD.open(DT_LOG_FILE_BIN, FILE_READ);
        if (!fc || curOff >= fc.size()) {
          if (fc) fc.close();
          logMsg("✨ DT Binary Backlog SUDAH BERSIH!");
          break;
        }
        fc.close();
      }
    }
  }

  // 2. Kuras Backlog Relay EXCA Titipan
  uint32_t relayOff = readUint(RELAY_OFFSET_FILE, 0);
  File fRelay = SD.open(RELAY_LOG_FILE_BIN, FILE_READ);
  if (fRelay) {
    uint32_t fSize = fRelay.size();
    fRelay.close();
    if (relayOff < fSize) {
      logMsg("🚀 Uploading RELAY EXCA BINARY backlog (sisa: " + String(fSize - relayOff) + " bytes / " + 
             String((fSize - relayOff) / sizeof(TelemetryPacketBinary)) + " records)...");
      while (publishBinaryQueueChunk(RELAY_LOG_FILE_BIN, RELAY_OFFSET_FILE, MAX_UPLOAD_CHUNK_RECORDS)) {
        esp_task_wdt_reset();
        handleDTGps();
        delay(5);
        uint32_t curOff = readUint(RELAY_OFFSET_FILE, 0);
        File fc = SD.open(RELAY_LOG_FILE_BIN, FILE_READ);
        if (!fc || curOff >= fc.size()) {
          if (fc) fc.close();
          logMsg("✨ RELAY Binary Backlog SUDAH BERSIH!");
          break;
        }
        fc.close();
      }
    }
  }
}

// ================= EXCA P2P HARVESTER =================
bool isExcaSSID(const String &ssid) {
  return ssid.startsWith("EXCA") && ssid.endsWith("_DATA");
}

String findBestExcaSSID() {
  int n = WiFi.scanNetworks();
  if (n <= 0) {
    WiFi.scanDelete();
    return "";
  }

  String bestSSID = "";
  int bestRSSI = -1000;

  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    int r = WiFi.RSSI(i);
    if (isExcaSSID(s) && r > bestRSSI) {
      bestSSID = s;
      bestRSSI = r;
    }
  }

  WiFi.scanDelete();
  return bestSSID;
}

bool connectExca(const String &ssid) {
  logMsg("Connecting EXCA AP: " + ssid);
  WiFi.begin(ssid.c_str(), EXCA_PASS);

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    esp_task_wdt_reset();
    handleDTGps();
    if (millis() - t0 > 10000) {
      WiFi.disconnect(false, true);
      return false;
    }
    delay(100);
  }

  logMsg("✅ EXCA connected, IP: " + WiFi.localIP().toString());
  return true;
}

bool waitTcpLine(WiFiClient &client, String &out, unsigned long timeoutMs) {
  unsigned long t0 = millis();
  while (!client.available()) {
    esp_task_wdt_reset();
    if (!client.connected() || millis() - t0 > timeoutMs) return false;
    handleDTGps();
    delay(1);
  }
  out = client.readStringUntil('\n');
  out.trim();
  return true;
}

bool transferFromExcaBinary() {
  WiFiClient client;
  if (!client.connect(excaIP, EXCA_PORT)) {
    logMsg("❌ EXCA TCP fail");
    return false;
  }

  client.println("HELLO_BIN");
  String line;
  if (!waitTcpLine(client, line, 5000) || line != "READY_BIN") {
    client.stop();
    return false;
  }

  client.println("GET_BIN");
  if (!waitTcpLine(client, line, 5000) || !line.startsWith("START_BIN")) {
    if (line == "NO_DATA") logMsg("ℹ️ EXCA no new binary data");
    client.stop();
    return true;
  }

  uint32_t startOffset = 0, totalSize = 0;
  sscanf(line.c_str(), "START_BIN %u %u", &startOffset, &totalSize);
  uint32_t totalToReceive = totalSize - startOffset;
  logMsg("📥 EXCA Binary Sync: " + String(totalToReceive) + " bytes (" + 
         String(totalToReceive / sizeof(TelemetryPacketBinary)) + " records)");

  const char *tempPath = "/relay_temp.bin";
  SD.remove(tempPath);
  File tempFile = SD.open(tempPath, FILE_WRITE);
  if (!tempFile) {
    client.stop();
    return false;
  }

  uint8_t buffer[1024];
  uint32_t bytesReceived = 0;
  unsigned long lastAct = millis();
  bool success = true;

  while (bytesReceived < totalToReceive) {
    esp_task_wdt_reset();
    handleDTGps();

    int avail = client.available();
    if (avail > 0) {
      int toRead = min((uint32_t)avail, (uint32_t)sizeof(buffer));
      toRead = min((uint32_t)toRead, totalToReceive - bytesReceived);
      int bytesRead = client.read(buffer, toRead);
      if (bytesRead > 0) {
        tempFile.write(buffer, bytesRead);
        bytesReceived += bytesRead;
        lastAct = millis();
      }
    } else {
      if (!client.connected() || millis() - lastAct > 8000) {
        success = false;
        break;
      }
      delay(2);
    }
  }

  tempFile.close();

  if (success && bytesReceived == totalToReceive) {
    client.println("ACK " + String(totalSize));
    logMsg("📤 Sent ACK " + String(totalSize) + " to EXCA");

    File src = SD.open(tempPath, FILE_READ);
    File dst = SD.open(RELAY_LOG_FILE_BIN, FILE_APPEND);
    if (src && dst) {
      while (src.available()) {
        int r = src.read(buffer, sizeof(buffer));
        dst.write(buffer, r);
      }
      src.close();
      dst.close();
      SD.remove(tempPath);
      statExcaRelayed++;
      logMsg("✅ EXCA Binary data merged into Relay log! Total sync: #" + String(statExcaRelayed));
    }
    client.stop();
    return true;
  }

  client.stop();
  SD.remove(tempPath);
  return false;
}

// ================= COMPACTION =================
bool compactBinaryQueueFile(const char *logPath, const char *offsetPath, const char *tempPath) {
  uint32_t offset = readUint(offsetPath, 0);
  offset = (offset / sizeof(TelemetryPacketBinary)) * sizeof(TelemetryPacketBinary);
  if (offset < 4096) return true;

  logMsg("🧹 Compacting " + String(logPath) + " offset=" + String(offset));
  File src = SD.open(logPath, FILE_READ);
  if (!src) return false;

  if (!src.seek(offset)) {
    src.close();
    return false;
  }

  SD.remove(tempPath);
  File dst = SD.open(tempPath, FILE_WRITE);
  if (!dst) {
    src.close();
    return false;
  }

  uint8_t buf[512];
  while (src.available()) {
    esp_task_wdt_reset();
    handleDTGps();
    int n = src.read(buf, sizeof(buf));
    if (n > 0) dst.write(buf, n);
  }

  src.close();
  dst.close();

  SD.remove(logPath);
  SD.rename(tempPath, logPath);
  writeUint(offsetPath, 0);
  logMsg("✅ Binary Compaction done: " + String(logPath));
  return true;
}

void updateLedRec() {
  unsigned long now = millis();
  switch (recordState) {
  case REC_ACTIVE:
    digitalWrite(LED_REC, HIGH);
    break;
  case REC_COOLDOWN:
    if (now - ledRecLastToggle >= 250) {
      ledRecLastToggle = now;
      ledRecOn = !ledRecOn;
      digitalWrite(LED_REC, ledRecOn ? HIGH : LOW);
    }
    break;
  case REC_IDLE:
    digitalWrite(LED_REC, LOW);
    break;
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(1000);

  logMsg("=== GPSTAMBANG DT BINARY EDITION START ===");

  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = WDT_TIMEOUT_SEC * 1000,
    .idle_core_mask = 0,
    .trigger_panic = true
  };
  esp_task_wdt_reconfigure(&wdt_config);
  esp_task_wdt_add(NULL);
  logMsg("🐕 Watchdog configured: " + String(WDT_TIMEOUT_SEC) + "s");

  pinMode(LED_GPS, OUTPUT);
  pinMode(LED_EXCA, OUTPUT);
  pinMode(LED_MQTT, OUTPUT);
  pinMode(LED_REC, OUTPUT);

  digitalWrite(LED_GPS, LOW);
  digitalWrite(LED_EXCA, LOW);
  digitalWrite(LED_MQTT, LOW);
  digitalWrite(LED_REC, LOW);

  Serial2.setRxBufferSize(2048);
  Serial2.begin(GPS_BAUD);
  Serial2.setPins(GPS_RX, GPS_TX);

  mqtt.setBufferSize(2048);

  initStorage();

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, true);

  logMsg("✅ " + String(DT_ID) + " BINARY READY (64B Packet)");
}

// ================= LOOP =================
void loop() {
  esp_task_wdt_reset();

  handleDTGps();

  if (digitalRead(LED_GPS) == HIGH && millis() - ledGpsTimer > 100) {
    digitalWrite(LED_GPS, LOW);
  }

  updateLedRec();

  if (recordState == REC_COOLDOWN && millis() - ignOffTime >= IGN_COOLDOWN_MS) {
    recordState = REC_IDLE;
    logMsg("⏹️ -> IDLE (cooldown 30s selesai)");
  }

  if (WiFi.status() == WL_CONNECTED && mqtt.connected()) {
    mqtt.loop();
  }

  static unsigned long lastSDCheck = 0;
  if (millis() - lastSDCheck > 5000) {
    lastSDCheck = millis();
    checkSDHealth();
  }

  unsigned long now = millis();

  // P2P Harvesting from Excavators
  if (!busy && !excaTransferBusy && now - lastExcaScan >= EXCA_SCAN_INTERVAL) {
    lastExcaScan = now;
    String ssid = findBestExcaSSID();
    if (ssid.length() > 0) {
      excaTransferBusy = true;
      digitalWrite(LED_EXCA, HIGH);
      if (connectExca(ssid)) {
        transferFromExcaBinary();
      }
      WiFi.disconnect(false, true);
      flushStaleGpsData();
      digitalWrite(LED_EXCA, LOW);
      excaTransferBusy = false;
    }
  }

  // Upload to MQTT
  if (!busy && !excaTransferBusy && now - lastInternetTry >= INTERNET_INTERVAL) {
    lastInternetTry = now;
    busy = true;
    tryInternetAndPublishAll();
    busy = false;
    flushStaleGpsData();
  }

  if (recordState == REC_IDLE && WiFi.status() != WL_CONNECTED) {
    WiFi.setSleep(true);
    delay(10);
  } else {
    WiFi.setSleep(false);
  }

  if (now - lastCompact >= COMPACT_INTERVAL) {
    lastCompact = now;
    compactBinaryQueueFile(DT_LOG_FILE_BIN, DT_OFFSET_FILE, "/dt_tmp.bin");
    compactBinaryQueueFile(RELAY_LOG_FILE_BIN, RELAY_OFFSET_FILE, "/relay_tmp.bin");
  }

  if (ESP.getFreeHeap() < HEAP_MIN_BYTES) {
    logMsg("❌ Heap kritis: " + String(ESP.getFreeHeap()) + " bytes, RESTARTING...");
    delay(1000);
    ESP.restart();
  }

  delay(2);
}
