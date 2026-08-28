/**
 * EXCA Hybrid V2 - Optimized GPS Tracking Firmware
 * 
 * Features:
 * 1. Persistent Fast WiFi Connect via NVS Preferences (<500ms) with Smart Scan Fallback
 * 2. Simultaneous AP (EXCA01_DATA) + STA (Gateway Internet)
 * 3. Safe Sequential Data Integrity & Real-time Direct MQTT Publish
 * 4. Independent Offset Tracking for MQTT Upload and DT Relay
 * 5. Reliable TCP Stream & Chunk Transfer with DT Harvest ACK
 * 6. SD Card Self-Healing / Hot-Plug Auto Re-mount
 * 7. Modem-Sleep Power Saving during 30s Cooldown / IDLE
 * 8. Non-blocking UART GPS handling (<10ms polling)
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_task_wdt.h>

// ================= PIN DEFINITIONS =================
#define RXD2 16
#define TXD2 17
#define SD_CS 5

#define LED_REC 13 // Unified Status LED
#define LED_LOG 13
#define LED_TRANSFER 13

// ================= WATCHDOG & MEMORY =================
#define WDT_TIMEOUT_SEC 30   // Hardware watchdog: 30 detik
#define HEAP_MIN_BYTES 20000 // Heap minimum: 20KB -> restart

// ================= UART CONFIGURATION =================
#define GPS_BAUD 115200

// ================= AP (ACCESS POINT) CONFIGURATION =================
// Ganti ID armada per unit (EXCA01, EXCA02, EXCA03, dst.)
const char *EXCA_ID = "EXCA01";
const char *AP_SSID = "EXCA01_DATA";
const char *AP_PASS = "12345678";
WiFiServer server(5000);

// ================= INTERNET WIFI & MQTT =================
struct WifiCredential {
  const char *ssid;
  const char *pass;
};

WifiCredential wifiList[] = {
  {"WIFI_GATEWAY_MINING_11", "46448951"},
  {"HOTSPOT_DT_KEAMANAN", "46448951"}
};
const int wifiCount = sizeof(wifiList) / sizeof(wifiList[0]);

const char *MQTT_SERVER = "76.13.19.250";
const uint16_t MQTT_PORT = 1883;
const char *MQTT_DATA_TOPIC = "kutai/fleet/data";

WiFiClient espClient;
PubSubClient mqtt(espClient);

// MQTT ACK State
String ackTopic = "";
volatile bool ackReceived = false;
String lastAckMsgId = "";

// ================= PERSISTENT FAST WIFI (NVS) =================
struct WifiCache {
  bool valid;
  int index;
  uint8_t channel;
  uint8_t bssid[6];
};

// ================= FILE PATHS =================
const char *LOG_FILE           = "/gps_log.jsonl";
const char *MQTT_OFFSET_FILE   = "/mqtt_offset.txt";
const char *DT_OFFSET_FILE     = "/dt_offset.txt";
const char *SEQ_FILE           = "/seq.txt";
const char *COMPACT_TEMP_FILE  = "/tmp_compact.jsonl";

// ================= UART BUFFER & PARSER =================
#define BUF_SIZE 4096
char buf[BUF_SIZE];
int bufLen = 0;
int brace = 0;
bool collecting = false;
unsigned long startJson = 0;

// ================= STATE VARIABLES =================
bool busy = false;
uint32_t seq = 0;

unsigned long ledLogTimer = 0;
unsigned long lastInternetTry = 0;
const unsigned long INTERNET_INTERVAL = 10000; // Polling tiap 10 detik

const int MAX_UPLOAD_CHUNK = 50;
const unsigned long COMPACT_INTERVAL = 1800000; // 30 menit
unsigned long lastCompact = 0;

// ================= HARDWARE HEALTH & RECOVERY =================
int sdErrorCount = 0;
bool sdReady = false;

// ================= IGNITION STATE MACHINE =================
enum RecordState { REC_IDLE, REC_ACTIVE, REC_COOLDOWN };
RecordState recordState = REC_IDLE;
unsigned long ignOffTime = 0;
const unsigned long IGN_COOLDOWN_MS = 30000; // 30 detik cooldown
uint32_t statSkipped = 0;
uint32_t statLogged = 0;
uint32_t statMqttSent = 0;
uint32_t statChunksUploaded = 0;

// LED Blink state
unsigned long ledRecLastToggle = 0;
bool ledRecOn = false;

// Forward Declarations
void handleGPS();
bool publishOneWithAck(const String &line, const String &msgId, int maxRetry = 2);

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

// ================= STORAGE INIT & RECOVERY =================
bool initSD() {
  SD.end();
  delay(50);
  if (!SD.begin(SD_CS)) {
    logMsg("❌ SD Init FAIL (Periksa Micro SD)");
    sdReady = false;
    return false;
  }

  // Migrasi aman dari offset legacy /offset.txt jika ada
  if (SD.exists("/offset.txt") && !SD.exists(MQTT_OFFSET_FILE)) {
    uint32_t legacyOff = readUint("/offset.txt", 0);
    writeUint(MQTT_OFFSET_FILE, legacyOff);
    writeUint(DT_OFFSET_FILE, legacyOff);
  }

  if (!SD.exists(MQTT_OFFSET_FILE)) writeUint(MQTT_OFFSET_FILE, 0);
  if (!SD.exists(DT_OFFSET_FILE)) writeUint(DT_OFFSET_FILE, 0);
  if (!SD.exists(SEQ_FILE)) writeUint(SEQ_FILE, 0);

  seq = readUint(SEQ_FILE);
  sdErrorCount = 0;
  sdReady = true;
  logMsg("✅ SD READY (Mounted), seq=" + String(seq));
  return true;
}

void checkSDHealth() {
  if (sdErrorCount >= 3 || !sdReady) {
    logMsg("🔄 [Self-Healing] Mencoba re-mount Micro SD...");
    if (initSD()) {
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
      return; // Tidak ada perubahan, jangan tulis NVS
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
      logMsg("⏹️ -> IDLE (cooldown 30s selesai, Logger Berhenti)");
      return false;
    }
    return true;

  default:
    return false;
  }
}

// ================= UID EXCA =================
String makeUID(JsonDocument &doc) {
  seq++;
  writeUint(SEQ_FILE, seq);

  String imei = doc["imei"] | "0";
  String ts = doc["timestamp"] | "0";
  ts.replace("-", "");
  ts.replace(":", "");

  return String(EXCA_ID) + "-" + imei + "-" + ts + "-" + String(seq);
}

// ================= PROCESS JSON =================
bool processGPSJson(const char *json, String &out) {
  StaticJsonDocument<1536> doc;
  static StaticJsonDocument<512> filter;
  static bool filterInitialized = false;
  if (!filterInitialized) {
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
    filterInitialized = true;
  }

  DeserializationError err = deserializeJson(doc, json, DeserializationOption::Filter(filter));
  if (err) {
    logMsg("❌ JSON parse err: " + String(err.c_str()));
    return false;
  }

  if (!shouldRecord(doc)) return false;

  StaticJsonDocument<768> optDoc;
  optDoc["id"]  = makeUID(doc);
  optDoc["src"] = EXCA_ID;
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

// ================= GPS UART HANDLER =================
void resetParser() {
  bufLen = 0;
  brace = 0;
  collecting = false;
}

void handleGPS() {
  while (Serial2.available()) {
    char c = Serial2.read();

    if (!collecting) {
      if (c == '{') {
        collecting = true;
        brace = 1;
        bufLen = 0;
        buf[bufLen++] = c;
        startJson = millis();
      }
      continue;
    }

    if (bufLen < BUF_SIZE - 1) {
      buf[bufLen++] = c;
    } else {
      logMsg("⚠️ GPS overflow");
      resetParser();
      continue;
    }

    if (c == '{') brace++;
    if (c == '}') brace--;

    if (brace == 0) {
      buf[bufLen] = '\0';

      String clean;
      if (processGPSJson(buf, clean)) {
        // Cek ukuran file sebelum append untuk memeriksa apakah ada backlog pending
        uint32_t offMqtt = readUint(MQTT_OFFSET_FILE, 0);
        uint32_t sizeBefore = 0;
        File fCheck = SD.open(LOG_FILE, FILE_READ);
        if (fCheck) {
          sizeBefore = fCheck.size();
          fCheck.close();
        }
        bool backlogClean = (offMqtt >= sizeBefore);

        // 1. Selalu simpan ke SD Card untuk jaminan data integrity
        if (appendLine(LOG_FILE, clean)) {
          statLogged++;
          digitalWrite(LED_LOG, HIGH);
          ledLogTimer = millis();
          
          uint32_t curSize = 0;
          File fc = SD.open(LOG_FILE, FILE_READ);
          if (fc) {
            curSize = fc.size();
            fc.close();
          }
          uint32_t pendingBytes = (curSize > offMqtt) ? (curSize - offMqtt) : 0;
          float pendingMB = pendingBytes / (1024.0 * 1024.0);
          logMsg("📍 LOGGED #" + String(statLogged) + " | Backlog: " + String(pendingMB, 3) + " MB (" + String(pendingBytes) + " B)");
        }

        // 2. ⚡ REAL-TIME DIRECT MQTT: Hanya jika online dan backlog sudah 100% bersih
        if (!busy && backlogClean && WiFi.status() == WL_CONNECTED && mqtt.connected()) {
          StaticJsonDocument<256> idDoc;
          deserializeJson(idDoc, clean);
          String msgId = idDoc["id"] | "";

          if (msgId.length() > 0) {
            busy = true;
            if (publishOneWithAck(clean, msgId, 2)) {
              logMsg("⚡ Real-time direct MQTT publish success");
              File fCur = SD.open(LOG_FILE, FILE_READ);
              if (fCur) {
                uint32_t newSize = fCur.size();
                fCur.close();
                writeUint(MQTT_OFFSET_FILE, newSize);
              }
            }
            busy = false;
          }
        }
      }

      resetParser();
      continue;
    }

    if (millis() - startJson > 4000) {
      resetParser();
    }
  }
}

// ================= PERSISTENT FAST WIFI CONNECT =================
bool connectKnownInternet() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  WiFi.setSleep(false); // Pastikan radio WiFi bangun penuh

  // ⚡ 1. COBA PERSISTENT FAST CONNECT DARI NVS CACHE
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
      handleGPS();
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

  // 🔍 2. FALLBACK KE ASYNCHRONOUS WIFI SCAN
  logMsg("🔍 Falling back to WiFi scan...");
  WiFi.scanNetworks(true);

  unsigned long tScan = millis();
  while (WiFi.scanComplete() < 0) {
    esp_task_wdt_reset();
    handleGPS();
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
    handleGPS();
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

  String clientId = String(EXCA_ID) + "-" + String(millis());
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
    ackTopic = "kutai/fleet/ack/" + String(EXCA_ID);
    mqtt.subscribe(ackTopic.c_str());
    logMsg("✅ MQTT connected, sub: " + ackTopic);
    return true;
  }

  logMsg("❌ MQTT connect fail, state: " + String(mqtt.state()));
  return false;
}

bool publishOneWithAck(const String &line, const String &msgId, int maxRetry) {
  for (int attempt = 1; attempt <= maxRetry; attempt++) {
    esp_task_wdt_reset();
    if (!mqtt.connected()) {
      if (!connectMQTT()) return false;
    }

    ackReceived = false;
    lastAckMsgId = "";

    logMsg("📤 published, wait ACK...");
    if (!mqtt.publish(MQTT_DATA_TOPIC, line.c_str())) {
      logMsg("❌ publish fail");
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

// ================= BACKLOG MQTT UPLOADER =================
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

    uint32_t currentPos = f.position();
    String line = f.readStringUntil('\n');
    line.trim();

    if (line.length() == 0) continue;

    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, line);
    if (err) {
      logMsg("⚠️ bad json, skip");
      writeUint(offsetPath, f.position());
      continue;
    }

    String msgId = doc["id"] | "";
    if (msgId.length() == 0) {
      writeUint(offsetPath, f.position());
      continue;
    }

    if (!publishOneWithAck(line, msgId, 2)) {
      logMsg("⚠️ publish fail, stop chunk at " + String(currentPos));
      f.close();
      return false;
    }

    writeUint(offsetPath, f.position());
    sentCount++;
  }

  f.close();
  statChunksUploaded++;
  logMsg("✅ Chunk published: " + String(sentCount) +
         " records (total chunks: " + String(statChunksUploaded) + ")");
  return true;
}

void tryInternetAndPublishChunk() {
  if (WiFi.status() != WL_CONNECTED) {
    if (!connectKnownInternet()) return;
  }

  if (!mqtt.connected()) {
    if (!connectMQTT()) return;
  }

  uint32_t off = readUint(MQTT_OFFSET_FILE, 0);
  File fCheck = SD.open(LOG_FILE, FILE_READ);
  if (fCheck) {
    uint32_t fSize = fCheck.size();
    fCheck.close();
    if (off < fSize) {
      uint32_t remaining = fSize - off;
      logMsg("🚀 Uploading backlog data (sisa: " + String(remaining) + " bytes)...");
      while (publishQueueFileChunk(LOG_FILE, MQTT_OFFSET_FILE, MAX_UPLOAD_CHUNK)) {
        esp_task_wdt_reset();
        handleGPS();
        delay(5);
        uint32_t curOff = readUint(MQTT_OFFSET_FILE, 0);
        File fc = SD.open(LOG_FILE, FILE_READ);
        if (!fc || curOff >= fc.size()) {
          if (fc) fc.close();
          logMsg("✨ Backlog SUDAH BERSIH! (Semua data tersinkron)");
          break;
        }
        uint32_t rem = fc.size() - curOff;
        fc.close();
        logMsg("📊 Backlog sisa: " + String(rem) + " bytes");
      }
    } else {
      logMsg("✨ Backlog SUDAH BERSIH! (Tidak ada data tertunda)");
    }
  }
}

// ================= COMPACTION =================
bool compactQueueFile(const char *logPath, const char *mqttOffsetPath, const char *dtOffsetPath, const char *tempPath) {
  uint32_t offMqtt = readUint(mqttOffsetPath, 0);
  uint32_t offDt   = readUint(dtOffsetPath, 0);
  uint32_t offset  = (offMqtt > 0 && offDt > 0) ? min(offMqtt, offDt) : max(offMqtt, offDt);

  if (offset < 4096) {
    return true;
  }

  logMsg("🧹 Compacting " + String(logPath) + " up to offset=" + String(offset));

  File src = SD.open(logPath, FILE_READ);
  if (!src) return false;

  if (!src.seek(offset)) {
    src.close();
    return false;
  }

  if (!src.available()) {
    src.close();
    SD.remove(logPath);
    File empty = SD.open(logPath, FILE_WRITE);
    if (empty) empty.close();
    writeUint(mqttOffsetPath, offMqtt >= offset ? offMqtt - offset : 0);
    writeUint(dtOffsetPath, offDt >= offset ? offDt - offset : 0);
    logMsg("🧹 Cleared " + String(logPath));
    return true;
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
    handleGPS();
    int n = src.read(buf, sizeof(buf));
    if (n > 0) dst.write(buf, n);
  }

  src.close();
  dst.close();

  SD.remove(logPath);
  if (!SD.rename(tempPath, logPath)) {
    logMsg("❌ rename fail: " + String(tempPath));
    return false;
  }

  writeUint(mqttOffsetPath, offMqtt >= offset ? offMqtt - offset : 0);
  writeUint(dtOffsetPath, offDt >= offset ? offDt - offset : 0);
  logMsg("🧹 Compacted " + String(logPath) + " new mqtt_off=" + String(offMqtt >= offset ? offMqtt - offset : 0) + 
         " dt_off=" + String(offDt >= offset ? offDt - offset : 0));
  return true;
}

// ================= LED STATE =================
void updateLedRec() {
  unsigned long now = millis();
  unsigned long interval = 0;

  switch (recordState) {
  case REC_IDLE:
    if (ledRecOn) {
      digitalWrite(LED_REC, LOW);
      ledRecOn = false;
    }
    return;
  case REC_ACTIVE:
    interval = 1000;
    break;
  case REC_COOLDOWN:
    interval = 200;
    break;
  }

  if (now - ledRecLastToggle >= interval) {
    ledRecLastToggle = now;
    ledRecOn = !ledRecOn;
    digitalWrite(LED_REC, ledRecOn ? HIGH : LOW);
  }
}

// ================= CLIENT LOCAL TCP (DT RELAY) =================
bool waitTcpMsg(WiFiClient &c, String expect, unsigned long timeoutMs = 3000) {
  unsigned long t0 = millis();
  while (!c.available()) {
    esp_task_wdt_reset();
    handleGPS();
    if (!c.connected() || millis() - t0 > timeoutMs) return false;
    delay(2);
  }
  String s = c.readStringUntil('\n');
  s.trim();
  return (s == expect);
}

void handleClient(WiFiClient client) {
  if (busy) {
    client.println("BUSY");
    client.stop();
    return;
  }

  busy = true;
  digitalWrite(LED_TRANSFER, HIGH);
  logMsg("🔌 DT client connected");

  if (!waitTcpMsg(client, "HELLO", 3000)) {
    client.stop();
    digitalWrite(LED_TRANSFER, LOW);
    busy = false;
    return;
  }

  client.println("READY");

  if (!waitTcpMsg(client, "GET", 3000)) {
    client.stop();
    digitalWrite(LED_TRANSFER, LOW);
    busy = false;
    return;
  }

  File f = SD.open(LOG_FILE, FILE_READ);
  if (!f) {
    client.println("NO_DATA");
    client.stop();
    digitalWrite(LED_TRANSFER, LOW);
    busy = false;
    return;
  }

  uint32_t dtOffset = readUint(DT_OFFSET_FILE, 0);
  uint32_t totalSize = f.size();

  if (dtOffset >= totalSize) {
    client.println("NO_DATA");
    f.close();
    client.stop();
    digitalWrite(LED_TRANSFER, LOW);
    busy = false;
    return;
  }

  client.printf("START %u %u\n", dtOffset, totalSize);

  if (!f.seek(dtOffset)) {
    f.close();
    client.stop();
    digitalWrite(LED_TRANSFER, LOW);
    busy = false;
    return;
  }

  uint8_t buffer[1024];
  uint32_t totalToSend = totalSize - dtOffset;
  uint32_t bytesSent = 0;
  bool success = true;

  while (f.available() && bytesSent < totalToSend && client.connected()) {
    esp_task_wdt_reset();
    handleGPS();

    int toRead = min((uint32_t)sizeof(buffer), totalToSend - bytesSent);
    int bytesRead = f.read(buffer, toRead);
    if (bytesRead > 0) {
      int written = client.write(buffer, bytesRead);
      if (written != bytesRead) {
        success = false;
        break;
      }
      bytesSent += bytesRead;
    }
    delay(1);
  }

  f.close();

  // Menunggu ACK konfirmasi penerimaan dari DT
  if (success && bytesSent == totalToSend) {
    unsigned long tAck = millis();
    bool dtAckOk = false;
    while (millis() - tAck < 5000 && client.connected()) {
      esp_task_wdt_reset();
      handleGPS();
      if (client.available()) {
        String resp = client.readStringUntil('\n');
        resp.trim();
        if (resp.startsWith("ACK") || resp == "OK") {
          dtAckOk = true;
          break;
        }
      }
      delay(5);
    }

    if (dtAckOk) {
      writeUint(DT_OFFSET_FILE, totalSize);
      logMsg("✅ DT Transfer confirmed by ACK, dt_offset=" + String(totalSize));
    } else {
      logMsg("⚠️ DT Transfer ended without ACK, dt_offset preserved");
    }
  } else {
    logMsg("❌ DT Transfer incomplete");
  }

  client.stop();
  digitalWrite(LED_TRANSFER, LOW);
  busy = false;
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(1000);

  // 1. SoftAP + STA mode
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);

  logMsg("=== " + String(EXCA_ID) + " HYBRID V2 STARTING ===");
  logMsg("MAC: " + WiFi.macAddress() + " | AP: " + String(AP_SSID));

  // 2. Watchdog configuration
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = WDT_TIMEOUT_SEC * 1000,
    .idle_core_mask = 0,
    .trigger_panic = true
  };
  esp_task_wdt_reconfigure(&wdt_config);
  esp_task_wdt_add(NULL); // Daftarkan task loop utama ke Watchdog
  logMsg("🐕 Watchdog configured: " + String(WDT_TIMEOUT_SEC) + "s");

  pinMode(LED_LOG, OUTPUT);
  pinMode(LED_TRANSFER, OUTPUT);
  pinMode(LED_REC, OUTPUT);

  digitalWrite(LED_LOG, LOW);
  digitalWrite(LED_TRANSFER, LOW);
  digitalWrite(LED_REC, LOW);

  // 3. Serial GPS
  Serial2.setRxBufferSize(2048);
  Serial2.begin(GPS_BAUD);
  Serial2.setPins(RXD2, TXD2);

  mqtt.setBufferSize(2048);

  // 4. SD Card Init
  initSD();

  // 5. Start TCP Server for DT
  server.begin();

  logMsg("✅ " + String(EXCA_ID) + " READY | Fast-Connect Active");
}

// ================= MAIN LOOP =================
void loop() {
  esp_task_wdt_reset();

  // 1. Tangani Data GPS UART
  handleGPS();

  if (digitalRead(LED_LOG) == HIGH && millis() - ledLogTimer > 100) {
    digitalWrite(LED_LOG, LOW);
  }

  updateLedRec();

  // 2. Transisi State Cooldown
  if (recordState == REC_COOLDOWN && millis() - ignOffTime >= IGN_COOLDOWN_MS) {
    recordState = REC_IDLE;
    logMsg("⏹️ -> IDLE (cooldown 30s selesai, Logger Berhenti)");
  }

  // 3. Tangani client DT lokal yang masuk ke SoftAP
  WiFiClient c = server.available();
  if (c) {
    handleClient(c);
  }

  // 4. Jaga koneksi MQTT tetap aktif
  if (WiFi.status() == WL_CONNECTED && mqtt.connected()) {
    mqtt.loop();
  }

  // 5. Health Check & Self-Healing SD Card
  static unsigned long lastSDCheck = 0;
  if (millis() - lastSDCheck > 5000) {
    lastSDCheck = millis();
    checkSDHealth();
  }

  // 6. Polling WiFi Internet & Kuras Backlog MQTT
  unsigned long now = millis();
  if (!busy && now - lastInternetTry >= INTERNET_INTERVAL) {
    lastInternetTry = now;
    busy = true;
    tryInternetAndPublishChunk();
    busy = false;
  }

  // 7. 🍃 Power Saving Modem-Sleep saat IDLE
  if (recordState == REC_IDLE && WiFi.status() != WL_CONNECTED) {
    WiFi.setSleep(true);
    delay(10);
  } else {
    WiFi.setSleep(false);
  }

  // 8. Compaction berkala
  if (now - lastCompact >= COMPACT_INTERVAL) {
    lastCompact = now;
    compactQueueFile(LOG_FILE, MQTT_OFFSET_FILE, DT_OFFSET_FILE, COMPACT_TEMP_FILE);
  }

  // 9. Heap Monitor
  if (ESP.getFreeHeap() < HEAP_MIN_BYTES) {
    logMsg("❌ Heap kritis: " + String(ESP.getFreeHeap()) + " bytes, RESTARTING...");
    delay(1000);
    ESP.restart();
  }

  delay(2);
}
