/**
 * GPSTAMBANG DT HYBRID OPTIMIZED - Dump Truck Firmware
 * 
 * Features:
 * 1. Persistent Fast WiFi Connect via NVS Preferences (<500ms) with Smart Scan Fallback
 * 2. Dual Backlog Clearance (DT own log + Merged EXCA relay log)
 * 3. Real-time Direct MQTT Publish when backlog is clean
 * 4. P2P Data Harvester from EXCA AP with Verified Chunk ACK Protocol
 * 5. SD Card Hot-Plug Self-Healing Recovery
 * 6. Ignition 30s Cooldown State Machine & Modem-Sleep
 * 7. Non-blocking UART GPS handling (<10ms polling)
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_task_wdt.h>

// ================= PIN CONFIGURATION =================
#define GPS_RX 16
#define GPS_TX 17
#define SD_CS 5

#define LED_REC 13 // Unified Status LED (GPIO 13)
#define LED_GPS 13
#define LED_EXCA 13
#define LED_MQTT 13

// ================= WATCHDOG & MEMORY =================
#define WDT_TIMEOUT_SEC 30   // Hardware watchdog: 30 detik
#define HEAP_MIN_BYTES 20000 // Heap minimum: 20KB -> restart

// ================= ID DEVICE =================
// Ganti ID ini sesuai nomor armada DT (DT01, DT02, DT03, dst.)
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
const char *MQTT_DATA_TOPIC = "kutai/fleet/data";

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
const char *DT_LOG_FILE       = "/dt_log.jsonl";
const char *RELAY_LOG_FILE    = "/relay_log.jsonl";

const char *DT_OFFSET_FILE    = "/dt_offset.txt";
const char *RELAY_OFFSET_FILE = "/relay_offset.txt";

const char *DT_SEQ_FILE       = "/dt_seq.txt";

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
unsigned long lastHeartbeat = 0;
unsigned long ledGpsTimer = 0;

const unsigned long EXCA_SCAN_INTERVAL = 10000; // Cek EXCA tiap 10 detik
const unsigned long INTERNET_INTERVAL  = 10000; // Cek Hotspot tiap 10 detik
const unsigned long COMPACT_INTERVAL   = 1800000; // Compaction tiap 30 menit
const unsigned long HEARTBEAT_INTERVAL = 60000; // Heartbeat tiap 1 menit
const int MAX_UPLOAD_CHUNK = 50;                // Batch 50 data per siklus

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
const unsigned long IGN_COOLDOWN_MS = 30000; // 30 Detik Cooldown

// LED REC Blink
unsigned long ledRecLastToggle = 0;
bool ledRecOn = false;

// Forward Declarations
void handleDTGps();
bool publishOneWithAck(const String &payload, const String &msgId, int maxRetries = 2);

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

  File f1 = SD.open(DT_LOG_FILE, FILE_APPEND);
  if (f1) f1.close();
  File f2 = SD.open(RELAY_LOG_FILE, FILE_APPEND);
  if (f2) f2.close();

  sdErrorCount = 0;
  sdReady = true;
  logMsg("✅ SD ready (Mounted), seq=" + String(dtSeq));
  return true;
}

void checkSDHealth() {
  if (sdErrorCount >= 3 || !sdReady) {
    logMsg("🔄 [Self-Healing] Mencoba re-mount Micro SD...");
    if (initStorage()) {
      logMsg("✨ Micro SD berhasil dipulihkan (Hot-Plug Recovery OK)!");
    } else {
      sdErrorCount = 3;
    }
  }
}

bool appendLine(const char *path, const String &line) {
  File f = SD.open(path, FILE_APPEND);
  if (!f) {
    sdErrorCount++;
    logMsg(String("❌ open fail: ") + path + " (err #" + String(sdErrorCount) + ")");
    return false;
  }
  f.println(line);
  f.flush();
  f.close();
  sdErrorCount = 0;
  return true;
}

// ================= NVS WIFI CACHE (PREFERENCES) =================
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
      return; // Tidak berubah
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

// ================= UID GENERATOR =================
String makeDTUID(JsonDocument &doc) {
  dtSeq++;
  writeUint(DT_SEQ_FILE, dtSeq);

  String imei = doc["imei"] | "0";
  String ts = doc["timestamp"] | "0";
  ts.replace("-", "");
  ts.replace(":", "");

  return String(DT_ID) + "-" + imei + "-" + ts + "-" + String(dtSeq);
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
      logMsg("⏹️ -> IDLE (cooldown 30s selesai, Logger Berhenti)");
      return false;
    }
    return true;

  default:
    return false;
  }
}

// ================= JSON PROCESSOR =================
bool processDTJson(const char *json, String &out) {
  StaticJsonDocument<1536> doc;
  static StaticJsonDocument<512> filter;
  static bool filterInit = false;
  if (!filterInit) {
    filter["imei"] = true;
    filter["event_code"] = true;
    filter["timestamp"] = true;
    filter["latitude"] = true;
    filter["longitude"] = true;
    filter["speed"] = true;
    filter["heading"] = true;
    filter["odometer"] = true;
    filter["altitude"] = true;
    filter["ignition"] = true;
    filter["input_status"] = true;
    filter["source"] = true;
    filter["event_info"] = true;
    filter["external"] = true;
    filter["battery"] = false;
    filter["output_status"] = true;
    filter["hdop"] = true;
    filter["mcu_temp"] = true;
    filter["ibutton"]["id"] = true;
    filter["ibeacon"][0]["mac"] = true;
    filter["ibeacon"][0]["rssi"] = true;
    filter["gsensor"]["x"] = true;
    filter["gsensor"]["y"] = true;
    filter["gsensor"]["z"] = true;
    filterInit = true;
  }

  DeserializationError err = deserializeJson(doc, json, DeserializationOption::Filter(filter));
  if (err) {
    logMsg("❌ JSON parse err: " + String(err.c_str()));
    return false;
  }

  if (!shouldRecord(doc)) return false;

  StaticJsonDocument<768> optDoc;
  optDoc["id"]  = makeDTUID(doc);
  optDoc["src"] = DT_ID;
  optDoc["ts"]  = doc["timestamp"] | "";
  optDoc["lat"] = doc["latitude"] | 0.0;
  optDoc["lon"] = doc["longitude"] | 0.0;
  optDoc["spd"] = doc["speed"] | 0;
  optDoc["hdg"] = doc["heading"] | 0;
  optDoc["odo"] = doc["odometer"] | 0;
  optDoc["alt"] = doc["altitude"] | 0;
  optDoc["ign"] = doc["ignition"] | 0;
  optDoc["in"]  = doc["input_status"] | 0;
  optDoc["out"] = doc["output_status"] | 0;
  optDoc["hd"]  = doc["hdop"] | 0.0;
  optDoc["ext"] = doc["external"] | 0.0;
  optDoc["tmp"] = doc["mcu_temp"] | 0.0;

  if (doc.containsKey("ibutton") && doc["ibutton"].containsKey("id")) {
    optDoc["ib"] = doc["ibutton"]["id"] | "";
  }

  if (doc.containsKey("ibeacon")) {
    JsonArray ibeacon = doc["ibeacon"].as<JsonArray>();
    JsonArray be = optDoc.createNestedArray("be");
    for (JsonObject beacon : ibeacon) {
      JsonObject b = be.createNestedObject();
      b["mac"] = beacon["mac"];
      b["rssi"] = beacon["rssi"];
    }
  }

  if (doc.containsKey("gsensor")) {
    JsonObject gs = optDoc.createNestedObject("gs");
    gs["x"] = doc["gsensor"]["x"];
    gs["y"] = doc["gsensor"]["y"];
    gs["z"] = doc["gsensor"]["z"];
  }

  serializeJson(optDoc, out);
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
      logMsg("⚠️ DT GPS overflow");
      resetGpsParser();
      continue;
    }

    if (c == '{') gpsBrace++;
    if (c == '}') gpsBrace--;

    if (gpsBrace == 0) {
      gpsBuf[gpsBufLen] = '\0';

      String clean;
      if (processDTJson(gpsBuf, clean)) {
        // Cek ukuran file sebelum append untuk memeriksa apakah ada backlog pending
        uint32_t dtOff = readUint(DT_OFFSET_FILE, 0);
        uint32_t sizeBefore = 0;
        File fCheck = SD.open(DT_LOG_FILE, FILE_READ);
        if (fCheck) {
          sizeBefore = fCheck.size();
          fCheck.close();
        }
        bool backlogClean = (dtOff >= sizeBefore);

        // 1. Simpan ke SD Card untuk backup
        if (appendLine(DT_LOG_FILE, clean)) {
          statGpsLogged++;
          digitalWrite(LED_GPS, HIGH);
          ledGpsTimer = millis();
          logMsg("📍 DT logged #" + String(statGpsLogged));
        }

        // 2. ⚡ REAL-TIME DIRECT PUBLISH: Hanya jika online dan backlog DT bersih
        if (!busy && backlogClean && WiFi.status() == WL_CONNECTED && mqtt.connected()) {
          StaticJsonDocument<256> idDoc;
          deserializeJson(idDoc, clean);
          String msgId = idDoc["id"] | "";

          if (msgId.length() > 0) {
            busy = true;
            if (publishOneWithAck(clean, msgId, 2)) {
              logMsg("⚡ Real-time direct MQTT publish success (DT)");
              File fCur = SD.open(DT_LOG_FILE, FILE_READ);
              if (fCur) {
                uint32_t newSize = fCur.size();
                fCur.close();
                writeUint(DT_OFFSET_FILE, newSize);
              }
            }
            busy = false;
          }
        }
      }

      resetGpsParser();
      continue;
    }

    if (millis() - gpsStartJson > 4000) {
      logMsg("⏱️ DT GPS timeout");
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

  // ⚡ 1. FAST CONNECT ATTEMPT DARI NVS CACHE
  WifiCache cache;
  if (loadWifiCache(cache)) {
    char bssidStr[18];
    snprintf(bssidStr, sizeof(bssidStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             cache.bssid[0], cache.bssid[1], cache.bssid[2],
             cache.bssid[3], cache.bssid[4], cache.bssid[5]);
    logMsg("⚡ WiFi cache loaded: " + String(wifiList[cache.index].ssid) + 
           " CH=" + String(cache.channel) + " BSSID=" + String(bssidStr));
    logMsg("⚡ Fast-connect attempt...");

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
      logMsg("📡 Connected: SSID=" + String(WiFi.SSID()) + 
             " BSSID=" + WiFi.BSSIDstr() + 
             " CH=" + String(WiFi.channel()) + 
             " RSSI=" + String(WiFi.RSSI()) + " dBm" + 
             " IP=" + WiFi.localIP().toString() + 
             " Time=" + String(elapsed) + " ms");
      return true;
    } else {
      logMsg("⚠️ Fast-connect failed after " + String(millis() - tFast) + " ms");
      WiFi.disconnect(false, true);
    }
  }

  // 🔍 2. FULL ASYNC SCAN FALLBACK
  logMsg("🔍 Falling back to WiFi scan...");
  WiFi.scanNetworks(true);

  unsigned long tScan = millis();
  while (WiFi.scanComplete() < 0) {
    esp_task_wdt_reset();
    handleDTGps();
    delay(10);
    if (millis() - tScan > 4000) {
      logMsg("⚠️ Scan timeout");
      WiFi.scanDelete();
      return false;
    }
  }

  int n = WiFi.scanComplete();
  if (n <= 0) {
    logMsg("❌ No WiFi networks found");
    WiFi.scanDelete();
    return false;
  }

  int bestIdx = -1;
  int bestScanIdx = -1;
  int bestRSSI = -1000;

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
    logMsg("❌ Known internet WiFi not in range");
    return false;
  }

  int ch = WiFi.channel(bestScanIdx);
  uint8_t *bssid = WiFi.BSSID(bestScanIdx);
  uint8_t bssidCopy[6];
  if (bssid) memcpy(bssidCopy, bssid, 6);
  WiFi.scanDelete();

  logMsg("🌐 Connecting to " + String(wifiList[bestIdx].ssid) + 
         " (RSSI: " + String(bestRSSI) + " dBm, CH: " + String(ch) + ")...");

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
      logMsg("❌ Connect timeout");
      WiFi.disconnect(false, true);
      return false;
    }
  }

  unsigned long totalTime = millis() - t0;
  logMsg("✅ Connected: SSID=" + String(WiFi.SSID()) + 
         " BSSID=" + WiFi.BSSIDstr() + 
         " CH=" + String(WiFi.channel()) + 
         " RSSI=" + String(WiFi.RSSI()) + " dBm" + 
         " IP=" + WiFi.localIP().toString() + 
         " Time=" + String(totalTime) + " ms");

  if (bssid) {
    saveWifiCache(bestIdx, ch, bssidCopy);
  }
  return true;
}

// ================= MQTT CLIENT & ACK =================
bool connectMQTT() {
  if (mqtt.connected()) return true;

  String clientId = String(DT_ID) + "-" + String(millis());
  logMsg("🔌 MQTT connecting: " + clientId);

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
    logMsg("✅ MQTT connected, sub: " + ackTopic);
    return true;
  }

  logMsg("❌ MQTT connect fail, state: " + String(mqtt.state()));
  return false;
}

bool publishOneWithAck(const String &payload, const String &msgId, int maxRetries) {
  for (int attempt = 1; attempt <= maxRetries; attempt++) {
    esp_task_wdt_reset();
    if (!mqtt.connected()) {
      if (!connectMQTT()) return false;
    }

    ackReceived = false;
    lastAckMsgId = "";

    logMsg("📤 published, wait ACK...");
    if (!mqtt.publish(MQTT_DATA_TOPIC, payload.c_str())) {
      logMsg("❌ Publish error");
      return false;
    }

    statMqttSent++;
    unsigned long t0 = millis();
    while (millis() - t0 < 2000) {
      esp_task_wdt_reset();
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

// ================= QUEUE PUBLISHER =================
bool publishQueueFileChunk(const char *logPath, const char *offsetPath, int maxRecords) {
  uint32_t offset = readUint(offsetPath, 0);

  File f = SD.open(logPath, FILE_READ);
  if (!f) {
    logMsg(String("❌ open fail: ") + logPath);
    return false;
  }

  if (offset >= f.size()) {
    f.close();
    return true;
  }

  if (!f.seek(offset)) {
    logMsg("❌ seek fail: " + String(offset));
    f.close();
    return false;
  }

  int sentCount = 0;
  while (f.available() && sentCount < maxRecords) {
    esp_task_wdt_reset();

    if (!mqtt.connected()) {
      if (!connectMQTT()) {
        f.close();
        return false;
      }
    }

    uint32_t currentLineOffset = f.position();
    String line = f.readStringUntil('\n');
    line.trim();

    if (line.length() == 0) continue;

    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, line);
    if (err) {
      logMsg("⚠️ Corrupt line, skip. offset=" + String(f.position()));
      writeUint(offsetPath, f.position());
      continue;
    }

    String msgId = doc["id"] | "";
    if (msgId.length() == 0) {
      writeUint(offsetPath, f.position());
      continue;
    }

    if (!publishOneWithAck(line, msgId, 2)) {
      logMsg("⚠️ publish fail, stop chunk at " + String(currentLineOffset));
      f.close();
      return false;
    }

    writeUint(offsetPath, f.position());
    sentCount++;
  }

  f.close();
  statChunksUploaded++;
  logMsg("✅ Chunk published: " + String(sentCount) + " records");
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
  File fDT = SD.open(DT_LOG_FILE, FILE_READ);
  if (fDT) {
    uint32_t fSize = fDT.size();
    fDT.close();
    if (dtOff < fSize) {
      logMsg("🚀 Uploading DT backlog (sisa: " + String(fSize - dtOff) + " bytes)...");
      while (publishQueueFileChunk(DT_LOG_FILE, DT_OFFSET_FILE, MAX_UPLOAD_CHUNK)) {
        esp_task_wdt_reset();
        handleDTGps();
        delay(5);
        uint32_t curOff = readUint(DT_OFFSET_FILE, 0);
        File fc = SD.open(DT_LOG_FILE, FILE_READ);
        if (!fc || curOff >= fc.size()) {
          if (fc) fc.close();
          logMsg("✨ DT Backlog SUDAH BERSIH!");
          break;
        }
        fc.close();
      }
    }
  }

  // 2. Kuras Backlog Relay EXCA Titipan
  uint32_t relayOff = readUint(RELAY_OFFSET_FILE, 0);
  File fRelay = SD.open(RELAY_LOG_FILE, FILE_READ);
  if (fRelay) {
    uint32_t fSize = fRelay.size();
    fRelay.close();
    if (relayOff < fSize) {
      logMsg("🚀 Uploading RELAY EXCA backlog (sisa: " + String(fSize - relayOff) + " bytes)...");
      while (publishQueueFileChunk(RELAY_LOG_FILE, RELAY_OFFSET_FILE, MAX_UPLOAD_CHUNK)) {
        esp_task_wdt_reset();
        handleDTGps();
        delay(5);
        uint32_t curOff = readUint(RELAY_OFFSET_FILE, 0);
        File fc = SD.open(RELAY_LOG_FILE, FILE_READ);
        if (!fc || curOff >= fc.size()) {
          if (fc) fc.close();
          logMsg("✨ RELAY Backlog SUDAH BERSIH!");
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
  if (bestSSID.length() > 0) {
    logMsg("📡 EXCA in range: " + bestSSID + " RSSI:" + String(bestRSSI) + " dBm");
  }
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
      logMsg("❌ EXCA connect timeout");
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

bool transferFromExca() {
  WiFiClient client;
  if (!client.connect(excaIP, EXCA_PORT)) {
    logMsg("❌ EXCA TCP fail");
    return false;
  }

  client.println("HELLO");
  String line;
  if (!waitTcpLine(client, line, 5000) || line != "READY") {
    logMsg("❌ EXCA HELLO error: " + line);
    client.stop();
    return false;
  }

  client.println("GET");
  if (!waitTcpLine(client, line, 5000) || !line.startsWith("START")) {
    if (line == "NO_DATA") logMsg("ℹ️ EXCA no new data");
    else logMsg("❌ EXCA GET error: " + line);
    client.stop();
    return true;
  }

  uint32_t startOffset = 0, totalSize = 0;
  sscanf(line.c_str(), "START %u %u", &startOffset, &totalSize);
  uint32_t totalToReceive = totalSize - startOffset;
  logMsg("📥 EXCA sync: " + String(totalToReceive) + " bytes");

  const char *tempPath = "/relay_temp.jsonl";
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
    File dst = SD.open(RELAY_LOG_FILE, FILE_APPEND);
    if (src && dst) {
      while (src.available()) {
        int r = src.read(buffer, sizeof(buffer));
        dst.write(buffer, r);
      }
      src.close();
      dst.close();
      SD.remove(tempPath);
      statExcaRelayed++;
      logMsg("✅ EXCA data merged to relay log! Total sync: #" + String(statExcaRelayed));
    }
    client.stop();
    return true;
  }

  client.stop();
  SD.remove(tempPath);
  logMsg("❌ EXCA transfer failed/incomplete");
  return false;
}

// ================= COMPACTION =================
bool compactQueueFile(const char *logPath, const char *offsetPath, const char *tempPath) {
  uint32_t offset = readUint(offsetPath, 0);
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
  logMsg("✅ Compaction done: " + String(logPath));
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

  logMsg("=== GPSTAMBANG DT HYBRID OPTIMIZED START ===");

  // Watchdog setup for ESP32 Core 3.x
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = WDT_TIMEOUT_SEC * 1000,
    .idle_core_mask = 0,
    .trigger_panic = true
  };
  esp_task_wdt_reconfigure(&wdt_config);
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

  logMsg("✅ " + String(DT_ID) + " READY | Fast-Connect Active");
}

// ================= LOOP =================
void loop() {
  esp_task_wdt_reset();

  // 1. Tangani Data GPS UART DT
  handleDTGps();

  if (digitalRead(LED_GPS) == HIGH && millis() - ledGpsTimer > 100) {
    digitalWrite(LED_GPS, LOW);
  }

  updateLedRec();

  // 2. Transisi State Cooldown
  if (recordState == REC_COOLDOWN && millis() - ignOffTime >= IGN_COOLDOWN_MS) {
    recordState = REC_IDLE;
    logMsg("⏹️ -> IDLE (cooldown 30s selesai, Logger Berhenti)");
  }

  // 3. Jaga koneksi MQTT tetap aktif
  if (WiFi.status() == WL_CONNECTED && mqtt.connected()) {
    mqtt.loop();
  }

  // 4. Periodic Check & Recovery Hardware SD Card (Self-Healing)
  static unsigned long lastSDCheck = 0;
  if (millis() - lastSDCheck > 5000) {
    lastSDCheck = millis();
    checkSDHealth();
  }

  unsigned long now = millis();

  // 5. Scan & Sedot Data dari Excavator (P2P Harvesting)
  if (!busy && !excaTransferBusy && now - lastExcaScan >= EXCA_SCAN_INTERVAL) {
    lastExcaScan = now;
    String ssid = findBestExcaSSID();
    if (ssid.length() > 0) {
      excaTransferBusy = true;
      digitalWrite(LED_EXCA, HIGH);
      if (connectExca(ssid)) {
        transferFromExca();
      }
      WiFi.disconnect(false, true);
      flushStaleGpsData();
      digitalWrite(LED_EXCA, LOW);
      excaTransferBusy = false;
    }
  }

  // 6. Internet WiFi Gateway & MQTT Publish (Dual Backlog Drain)
  if (!busy && !excaTransferBusy && now - lastInternetTry >= INTERNET_INTERVAL) {
    lastInternetTry = now;
    busy = true;
    tryInternetAndPublishAll();
    busy = false;
    flushStaleGpsData();
  }

  // 7. 🍃 POWER SAVING (Modem-Sleep saat Unit IDLE & Tidak ada koneksi)
  if (recordState == REC_IDLE && WiFi.status() != WL_CONNECTED) {
    WiFi.setSleep(true);
    delay(10);
  } else {
    WiFi.setSleep(false);
  }

  // 8. Periodic Compaction
  if (now - lastCompact >= COMPACT_INTERVAL) {
    lastCompact = now;
    compactQueueFile(DT_LOG_FILE, DT_OFFSET_FILE, "/dt_tmp.jsonl");
    compactQueueFile(RELAY_LOG_FILE, RELAY_OFFSET_FILE, "/relay_tmp.jsonl");
  }

  // 9. Heap Monitor
  if (ESP.getFreeHeap() < HEAP_MIN_BYTES) {
    logMsg("❌ Heap kritis: " + String(ESP.getFreeHeap()) + " bytes, RESTARTING...");
    delay(1000);
    ESP.restart();
  }

  delay(2);
}
