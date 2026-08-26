/**
 * EXCA Hybrid V2 - Optimized GPS Tracking Firmware
 *
 * Optimizations:
 * 1. Chunk Upload: 50 records/session (vs all-at-once)
 * 2. Faster Polling: 15 second interval (vs 60s)
 * 3. Faster ACK Timeout: 2 seconds (vs 5s)
 * 4. Compaction: Auto cleanup uploaded data every 30 minutes
 * 5. Keep-Alive: Maintain MQTT connection for consecutive chunks
 *
 * Target: EXCA01, EXCA02, EXCA03 (all EXCA Hybrid devices)
 * Expected: Backlog clearance 500+ records/hour, SD card auto recovery
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_task_wdt.h>

// ================= PIN =================
#define RXD2 16
#define TXD2 17
#define SD_CS 5

#define LED_REC 13 // 🟡 Unified Status LED
#define LED_LOG 13
#define LED_TRANSFER 13

// ================= WATCHDOG & MEMORY =================
#define WDT_TIMEOUT_SEC 30   // Hardware watchdog: 30 detik
#define HEAP_MIN_BYTES 20000 // Heap minimum: 20KB → restart

// ================= UART =================
#define GPS_BAUD 115200

// ================= WIFI AP =================
// NOTE: Update EXCA_ID per device (EXCA01, EXCA02, EXCA03)
const char *EXCA_ID = "EXCA01";
const char *AP_SSID = "EXCA01_DATA";
const char *AP_PASS = "12345678";
WiFiServer server(5000);

// ================= INTERNET WIFI & MQTT =================
struct WifiCredential {
  const char *ssid;
  const char *pass;
};

WifiCredential wifiList[] = {{"WIFI_GATEWAY_MINING_11", "46448951"},
                             {"HOTSPOT_DT_KEAMANAN", "46448951"}};
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

// Forward declarations
bool publishOneWithAck(const String &line, const String &msgId, int maxRetry);
bool publishQueueFileChunk(const char *logPath, const char *offsetPath,
                           int maxRecords);
void tryInternetAndPublishChunk();

// ================= FILE =================
const char *LOG_FILE = "/gps_log.jsonl";
const char *SNAP_FILE = "/snap.jsonl";
const char *OFFSET_FILE = "/offset.txt";
const char *SEQ_FILE = "/seq.txt";
const char *COMPACT_TEMP_FILE = "/tmp_compact.jsonl";

// ================= PARSER =================
#define BUF_SIZE 4096
char buf[BUF_SIZE];
int bufLen = 0;
int brace = 0;
bool collecting = false;
unsigned long startJson = 0;

// ================= STATE =================
File logFile;
bool busy = false;
uint32_t seq = 0;

unsigned long ledLogTimer = 0;
unsigned long lastInternetTry = 0;

// Polling interval: Cek hotspot tiap 10 detik di sela-sela waktu jeda GPS
const unsigned long INTERNET_INTERVAL = 10000;

// OPTIMIZATION: Chunk upload configuration
const int MAX_UPLOAD_CHUNK = 50; // Max 50 records per session

// OPTIMIZATION: Compaction configuration
const unsigned long COMPACT_INTERVAL = 1800000; // 30 minutes
unsigned long lastCompact = 0;

// OPTIMIZATION: Keep-alive connection
bool mqttConnected = false;
unsigned long lastMqttActivity = 0;
const unsigned long MQTT_KEEPALIVE_MS = 300000; // 5 minutes

// ================= IGNITION STATE =================
enum RecordState { REC_IDLE, REC_ACTIVE, REC_COOLDOWN };
RecordState recordState = REC_IDLE;
unsigned long ignOffTime = 0;
const unsigned long IGN_COOLDOWN_MS = 30000; // 30 detik cooldown
uint32_t statSkipped = 0;
uint32_t statLogged = 0;
uint32_t statMqttSent = 0;
uint32_t statChunksUploaded = 0;

// LED REC blink
unsigned long ledRecLastToggle = 0;
bool ledRecOn = false;

// ================= DEBUG =================
void logMsg(String s) {
  Serial.print("[");
  Serial.print(millis());
  Serial.print("] ");
  Serial.println(s);
}

// ================= FILE HELPERS =================
uint32_t readUint(const char *path, uint32_t def = 0) {
  File f = SD.open(path);
  if (!f)
    return def;
  String s = f.readString();
  f.close();
  s.trim();
  if (s.length() == 0)
    return def;
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

// ================= HARDWARE HEALTH & RECOVERY =================
int sdErrorCount = 0;
bool sdReady = false;

// ================= FAST CONNECT CACHE =================
int cachedChannel = 0;
uint8_t cachedBSSID[6] = {0};
bool hasCachedBSSID = false;
int cachedWifiIdx = -1;

// ================= INIT & AUTO-RECOVERY SD =================
bool initSD() {
  SD.end(); // Bersihkan bus SPI jika sebelumnya error
  delay(50);
  if (!SD.begin(SD_CS)) {
    logMsg("❌ SD Init FAIL (Periksa Micro SD)");
    sdReady = false;
    return false;
  }

  if (!SD.exists(OFFSET_FILE))
    writeUint(OFFSET_FILE, 0);
  if (!SD.exists(SEQ_FILE))
    writeUint(SEQ_FILE, 0);

  seq = readUint(SEQ_FILE);
  sdErrorCount = 0;
  sdReady = true;
  logMsg("✅ SD READY (Mounted)");
  return true;
}

void checkSDHealth() {
  if (sdErrorCount >= 3 || !sdReady) {
    logMsg("🔄 [Self-Healing] Mencoba re-mount Micro SD...");
    if (initSD()) {
      logMsg("✨ Micro SD berhasil dipulihkan (Hot-Plug Recovery OK)!");
    } else {
      sdErrorCount = 3; // Tetap kunci untuk retry di siklus berikutnya
    }
  }
}

// ================= RECORD FILTER (Ignition State Machine) =================
const char *recordStateStr(RecordState s) {
  switch (s) {
  case REC_IDLE:
    return "IDLE";
  case REC_ACTIVE:
    return "ACTIVE";
  case REC_COOLDOWN:
    return "COOLDOWN";
  default:
    return "?";
  }
}

bool shouldRecord(JsonDocument &doc) {
  int eventCode = doc["event_code"] | 0;
  int ignition = doc["ignition"] | -1;
  int inputStatus = doc["input_status"] | doc["din"] | -1;

  // Jika field ignition tidak ada, ekstrak dari bit 0 input_status / din (DIN 1 / Ignition)
  if (ignition == -1 && inputStatus != -1) {
    ignition = (inputStatus & 0x01) ? 1 : 0;
  }

  // Selalu catat event Ignition On/Off untuk audit trail
  if (eventCode == 2 || eventCode == 3) {
    if (eventCode == 2) {
      recordState = REC_ACTIVE;
      logMsg("🔑 IGN ON → ACTIVE");
    } else {
      if (recordState == REC_ACTIVE) {
        recordState = REC_COOLDOWN;
        ignOffTime = millis();
        logMsg("🔑 IGN OFF → COOLDOWN (" + String(IGN_COOLDOWN_MS / 1000) + "s)");
      }
    }
    return true; // selalu catat event ignition
  }

  // State machine 30 detik Cooldown
  switch (recordState) {
  case REC_IDLE:
    if (ignition == 1) {
      recordState = REC_ACTIVE;
      logMsg("⏺️ → ACTIVE (Ignition ON)");
      return true;
    }
    return false; // skip data saat idle

  case REC_ACTIVE:
    if (ignition == 0) {
      recordState = REC_COOLDOWN;
      ignOffTime = millis();
      logMsg("⏸️ → COOLDOWN (30s)");
    }
    return true; // catat selama aktif

  case REC_COOLDOWN:
    if (ignition == 1) {
      recordState = REC_ACTIVE;
      logMsg("⏺️ → ACTIVE (kembali ON)");
      return true;
    }
    // Cek apakah cooldown 30 detik sudah habis
    if (millis() - ignOffTime >= IGN_COOLDOWN_MS) {
      recordState = REC_IDLE;
      logMsg("⏹️ → IDLE (cooldown 30s selesai, Logger Berhenti)");
      return false;
    }
    return true; // masih dalam masa cooldown 30s, tetap catat

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

// ================= PROCESS JSON (Ported directly from working DT) =================
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
    filter["ibutton"]["status"] = true;
    filter["ibutton"]["auth"] = true;
    filter["ibeacon"][0]["mac"] = true;
    filter["ibeacon"][0]["rssi"] = true;
    filter["gsensor"]["x"] = true;
    filter["gsensor"]["y"] = true;
    filter["gsensor"]["z"] = true;
    filterInitialized = true;
  }

  if (deserializeJson(doc, json, DeserializationOption::Filter(filter))) {
    return false;
  }

  if (!doc["imei"] || !doc["timestamp"]) {
    return false;
  }

  // Filter berdasarkan ignition state
  if (!shouldRecord(doc)) {
    statSkipped++;
    return false;
  }

  StaticJsonDocument<1024> optDoc;
  String uid = makeUID(doc);
  optDoc["id"] = uid;
  optDoc["imei"] = doc["imei"];
  optDoc["src"] = EXCA_ID;
  optDoc["type"] = doc["event_info"];
  optDoc["ev"] = doc["event_code"];
  optDoc["ts"] = doc["timestamp"];
  optDoc["lat"] = doc["latitude"];
  optDoc["lon"] = doc["longitude"];
  optDoc["spd"] = doc["speed"];
  optDoc["hdg"] = doc["heading"];
  optDoc["alt"] = doc["altitude"];
  optDoc["bat"] = doc["external"];
  optDoc["odo"] = doc["odometer"];
  optDoc["ign"] = doc["ignition"];
  optDoc["in"] = doc["input_status"];
  optDoc["out"] = doc["output_status"];
  optDoc["hdop"] = doc["hdop"];
  optDoc["temp"] = doc["mcu_temp"];

  if (doc.containsKey("ibutton")) {
    JsonObject ib = optDoc.createNestedObject("ib");
    ib["id"] = doc["ibutton"]["id"];
    ib["st"] = doc["ibutton"]["status"];
    ib["au"] = doc["ibutton"]["auth"];
  }

  if (doc.containsKey("ibeacon")) {
    JsonArray be = optDoc.createNestedArray("be");
    JsonArray ibeacon = doc["ibeacon"].as<JsonArray>();
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

// ================= STORAGE APPEND =================
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
  sdErrorCount = 0; // Reset error jika sukses menulis
  return true;
}

// ================= GPS HANDLER (Persis Algoritma DT) =================
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

    if (c == '{')
      brace++;
    if (c == '}')
      brace--;

    if (brace == 0) {
      buf[bufLen] = '\0';

      String clean;
      if (processGPSJson(buf, clean)) {
        // Selalu catat ke SD Card untuk backup
        if (appendLine(LOG_FILE, clean)) {
          statLogged++;
          digitalWrite(LED_LOG, HIGH);
          ledLogTimer = millis();
          logMsg("📍 LOGGED #" + String(statLogged));
        }

        // ⚡ REAL-TIME DIRECT PUBLISH: Jika online dan tidak sedang dalam upload backlog
        if (!busy && WiFi.status() == WL_CONNECTED && mqtt.connected()) {
          StaticJsonDocument<256> idDoc;
          deserializeJson(idDoc, clean);
          String msgId = idDoc["id"] | "";

          if (msgId.length() > 0) {
            busy = true;
            if (publishOneWithAck(clean, msgId, 2)) {
              logMsg("⚡ Real-time direct MQTT publish success");
              // Majukan offset ke posisi akhir file terbaru agar tidak terkirim ganda
              File fCur = SD.open(LOG_FILE, FILE_READ);
              if (fCur) {
                uint32_t newSize = fCur.size();
                fCur.close();
                writeUint(OFFSET_FILE, newSize);
                logMsg("⚡ Offset updated: " + String(newSize));
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

// ================= SMART ASYNC WIFI & MQTT =================
bool connectKnownInternet() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  // ⚡ 1. FAST CONNECT ATTEMPT (Coba sambung langsung ke Channel & BSSID terakhir yang pernah sukses)
  if (cachedWifiIdx >= 0 && cachedChannel > 0) {
    logMsg("⚡ [Fast-Connect] Mencoba direct connect ke " + String(wifiList[cachedWifiIdx].ssid) + 
           " (Ch: " + String(cachedChannel) + ")...");
    
    if (hasCachedBSSID) {
      WiFi.begin(wifiList[cachedWifiIdx].ssid, wifiList[cachedWifiIdx].pass, cachedChannel, cachedBSSID);
    } else {
      WiFi.begin(wifiList[cachedWifiIdx].ssid, wifiList[cachedWifiIdx].pass, cachedChannel);
    }

    unsigned long tFast = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - tFast < 2000) { // Coba cepat max 2 detik
      esp_task_wdt_reset();
      handleGPS();
      delay(50);
    }

    if (WiFi.status() == WL_CONNECTED) {
      logMsg("⚡ [Fast-Connect] ✅ Terhubung cepat dalam " + String(millis() - tFast) + "ms! IP: " + WiFi.localIP().toString());
      return true;
    } else {
      logMsg("⚠️ Fast-connect miss, lanjut smart scan...");
      WiFi.disconnect(false, true);
    }
  }

  // 🔍 2. FULL ASYNC SCAN (Jika fast-connect miss / belum ada cache)
  logMsg("🔍 Smart scanning WiFi in background...");
  WiFi.scanNetworks(true); // true = Async mode

  unsigned long tScan = millis();
  while (WiFi.scanComplete() < 0) {
    esp_task_wdt_reset();
    handleGPS();
    delay(50);
    if (millis() - tScan > 4000) { // Max tunggu scan 4 detik
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

  // Temukan WiFi yang cocok dengan sinyal (RSSI) terkuat
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

  // Simpan Channel & BSSID untuk Fast Connect berikutnya!
  cachedWifiIdx = bestIdx;
  cachedChannel = WiFi.channel(bestScanIdx);
  uint8_t *bssidPtr = WiFi.BSSID(bestScanIdx);
  if (bssidPtr) {
    memcpy(cachedBSSID, bssidPtr, 6);
    hasCachedBSSID = true;
  }
  WiFi.scanDelete();

  logMsg("🌐 Connecting to " + String(wifiList[bestIdx].ssid) + " (RSSI: " + String(bestRSSI) + " dBm, Ch: " + String(cachedChannel) + ")...");
  if (hasCachedBSSID) {
    WiFi.begin(wifiList[bestIdx].ssid, wifiList[bestIdx].pass, cachedChannel, cachedBSSID);
  } else {
    WiFi.begin(wifiList[bestIdx].ssid, wifiList[bestIdx].pass);
  }

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    esp_task_wdt_reset();
    handleGPS();
    delay(100);
    if (millis() - t0 > 8000) { // Max 8s connect attempt
      logMsg("❌ Connect timeout");
      WiFi.disconnect(false, true);
      return false;
    }
  }

  logMsg("✅ Connected: " + WiFi.localIP().toString());
  return true;
}

bool connectMQTT() {
  if (mqtt.connected()) {
    logMsg("✅ MQTT already connected");
    return true;
  }

  String clientId = String(EXCA_ID) + "-" + String(millis());
  logMsg("🔌 MQTT connecting: " + clientId);

  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback([](char *topic, byte *payload, unsigned int length) {
    String msg;
    for (int i = 0; i < length; i++)
      msg += (char)payload[i];

    if (strstr(topic, "ack")) {
      DynamicJsonDocument doc(512);
      DeserializationError err = deserializeJson(doc, msg);
      if (!err) {
        String ackId = doc["id"] | "";
        if (ackId.length() > 0) {
          ackReceived = true;
          lastAckMsgId = ackId;
          logMsg("📥 ACK received: " + ackId);
        }
      } else {
        logMsg("⚠️ ACK deserialize error");
      }
    }
  });

  unsigned long t0 = millis();
  while (!mqtt.connected()) {
    if (mqtt.connect(clientId.c_str())) {
      ackTopic = String("kutai/fleet/ack/") + EXCA_ID;
      if (!mqtt.subscribe(ackTopic.c_str())) {
        logMsg("❌ MQTT sub fail");
        mqtt.disconnect();
        return false;
      }
      logMsg("✅ MQTT connected, sub: " + ackTopic);
      mqttConnected = true;
      lastMqttActivity = millis();
      return true;
    }

    delay(500);
    if (millis() - t0 > 15000) {
      logMsg("❌ MQTT timeout");
      return false;
    }
  }

  return false;
}

// OPTIMIZATION: Reduced ACK timeout (2s vs 5s)
bool publishOneWithAck(const String &line, const String &msgId,
                       int maxRetry = 2) {
  for (int attempt = 1; attempt <= maxRetry; attempt++) {
    ackReceived = false;
    lastAckMsgId = "";

    if (!mqtt.connected()) {
      if (!connectMQTT()) {
        delay(500 * attempt);
        continue;
      }
    }

    if (!mqtt.publish(MQTT_DATA_TOPIC, line.c_str())) {
      logMsg("❌ publish fail #" + String(attempt));
      delay(500 * attempt);
      continue;
    }

    logMsg("📤 published, wait ACK...");

    unsigned long t0 = millis();
    // OPTIMIZATION: Reduced timeout to 2 seconds
    while (millis() - t0 < 2000) {
      esp_task_wdt_reset();
      mqtt.loop();

      if (ackReceived && lastAckMsgId == msgId) {
        statMqttSent++;
        lastMqttActivity = millis(); // Update activity
        return true;
      }

      delay(5);
    }

    logMsg("🔁 ACK timeout #" + String(attempt));
  }

  return false;
}

// OPTIMIZATION: Chunk upload (50 records/session)
bool publishQueueFileChunk(const char *logPath, const char *offsetPath,
                           int maxRecords) {
  File f = SD.open(logPath, FILE_READ);
  if (!f) {
    logMsg(String("❌ open queue: ") + logPath);
    return false;
  }

  uint32_t offset = readUint(offsetPath);
  uint32_t fileSize = f.size();

  if (offset > fileSize) {
    logMsg("⚠️ offset(" + String(offset) + ") > fileSize(" +
           String(fileSize) + "), reset 0");
    offset = 0;
  }

  if (!f.seek(offset)) {
    logMsg("⚠️ seek fail, reset 0");
    f.seek(0);
    offset = 0;
  }

  if (offset >= fileSize) {
    f.close();
    logMsg("✅ No pending data");
    return true;
  }

  uint32_t currentPos = offset;
  int sentCount = 0;

  while (f.available() && sentCount < maxRecords) {
    esp_task_wdt_reset();

    String line = f.readStringUntil('\n');
    line.trim();
    currentPos = (uint32_t)f.position();

    if (line.length() == 0)
      continue;

    StaticJsonDocument<1024> doc;
    if (deserializeJson(doc, line)) {
      logMsg("⚠️ bad json, skip");
      writeUint(offsetPath, currentPos);
      continue;
    }

    String msgId = doc["id"] | doc["msg_id"] | "";
    if (msgId.length() == 0) {
      logMsg("⚠️ no id, skip");
      writeUint(offsetPath, currentPos);
      continue;
    }

    // OPTIMIZATION: 2 retries for chunk mode (faster)
    if (!publishOneWithAck(line, msgId, 2)) {
      logMsg("⚠️ publish fail, stop chunk at " + String(sentCount));
      f.close();
      return false;
    }

    writeUint(offsetPath, currentPos);
    sentCount++;
  }

  f.close();
  statChunksUploaded++;
  logMsg("✅ Chunk published: " + String(sentCount) +
         " records (total chunks: " + String(statChunksUploaded) + ")");
  return true;
}

// Upload all backlog data without disconnecting (keep connected as long as signal exists)
void tryInternetAndPublishChunk() {
  if (WiFi.status() != WL_CONNECTED) {
    if (!connectKnownInternet()) {
      return;
    }
  }

  if (!mqtt.connected()) {
    if (!connectMQTT()) {
      return;
    }
  }

  uint32_t off = readUint(OFFSET_FILE, 0);
  File fCheck = SD.open(LOG_FILE, FILE_READ);
  if (fCheck) {
    uint32_t fSize = fCheck.size();
    fCheck.close();
    if (off < fSize) {
      uint32_t remaining = fSize - off;
      logMsg("🚀 Uploading backlog data (sisa: " + String(remaining) + " bytes)...");
      // Upload semua sisa backlog data (per batch 50 data sampai tuntas atau terputus)
      while (publishQueueFileChunk(LOG_FILE, OFFSET_FILE, MAX_UPLOAD_CHUNK)) {
        esp_task_wdt_reset();
        handleGPS(); // Tetap tangani real-time GPS
        delay(5);
        uint32_t curOff = readUint(OFFSET_FILE, 0);
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

// ================= COMPACTION (Ported from DT) =================
bool compactQueueFile(const char *logPath, const char *offsetPath,
                      const char *tempPath) {
  uint32_t offset = readUint(offsetPath, 0);

  // Hanya compact jika offset cukup besar (hemat write cycle SD)
  if (offset < 4096) {
    logMsg("🧹 Skip compact: offset too small (" + String(offset) + ")");
    return true;
  }

  logMsg("🧹 Compacting " + String(logPath) + " offset=" + String(offset));

  File src = SD.open(logPath, FILE_READ);
  if (!src)
    return false;

  if (!src.seek(offset)) {
    src.close();
    return false;
  }

  // Jika sudah tidak ada sisa data, kosongkan file saja
  if (!src.available()) {
    src.close();
    SD.remove(logPath);
    File empty = SD.open(logPath, FILE_WRITE);
    if (empty)
      empty.close();
    writeUint(offsetPath, 0);
    logMsg("🧹 Cleared " + String(logPath));
    return true;
  }

  // Salin sisa data ke file temp
  SD.remove(tempPath);
  File dst = SD.open(tempPath, FILE_WRITE);
  if (!dst) {
    src.close();
    return false;
  }

  int lineCount = 0;
  while (src.available()) {
    esp_task_wdt_reset();
    String line = src.readStringUntil('\n');
    line.trim();
    if (line.length() == 0)
      continue;
    dst.println(line);
    lineCount++;
  }

  src.close();
  dst.close();

  // Replace file lama dengan temp
  SD.remove(logPath);
  if (!SD.rename(tempPath, logPath)) {
    logMsg("❌ rename fail: " + String(tempPath));
    return false;
  }

  writeUint(offsetPath, 0);
  logMsg("🧹 Compacted " + String(logPath) + ": " + String(lineCount) +
         " lines kept");
  return true;
}

// ================= LED RECORDING =================
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
void handleClient(WiFiClient client) {
  logMsg("🔌 DT client connected");
  digitalWrite(LED_TRANSFER, HIGH);

  File f = SD.open(LOG_FILE, FILE_READ);
  if (!f) {
    client.println("ERROR: Log file not found");
    client.stop();
    digitalWrite(LED_TRANSFER, LOW);
    return;
  }

  uint32_t offset = readUint(OFFSET_FILE);
  if (!f.seek(offset)) {
    f.seek(0);
    offset = 0;
  }

  client.println("READY");
  logMsg("📤 Sending from offset=" + String(offset));

  while (client.connected() && f.available()) {
    esp_task_wdt_reset();
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      client.println(line);
      offset = f.position();
      writeUint(OFFSET_FILE, offset);
    }
    delay(1);
  }

  f.close();
  client.stop();
  digitalWrite(LED_TRANSFER, LOW);
  logMsg("✅ DT transfer done");
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(1000);

  // 1. Inisialisasi WiFi Mode dulu agar MAC Address valid & stabil
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);

  logMsg("=== " + String(EXCA_ID) + " V2 STARTING ===");
  logMsg("MAC Address: " + WiFi.macAddress());

  esp_task_wdt_config_t wdt_config = {.timeout_ms = WDT_TIMEOUT_SEC * 1000,
                                      .idle_core_mask = 0,
                                      .trigger_panic = true};
  esp_task_wdt_reconfigure(&wdt_config);
  logMsg("🐕 Watchdog configured: " + String(WDT_TIMEOUT_SEC) + "s");

  pinMode(LED_LOG, OUTPUT);
  pinMode(LED_TRANSFER, OUTPUT);
  pinMode(LED_REC, OUTPUT);

  digitalWrite(LED_LOG, LOW);
  digitalWrite(LED_TRANSFER, LOW);
  digitalWrite(LED_REC, LOW);

  Serial2.setRxBufferSize(2048);
  Serial2.begin(GPS_BAUD);
  Serial2.setPins(RXD2, TXD2);

  mqtt.setBufferSize(2048); // Set MQTT buffer to 2KB to accommodate long telemetry payload

  initSD();

  server.begin();
  esp_task_wdt_add(NULL);

  logMsg("EXCA HYBRID V2 READY");
  logMsg("🚀 Optimizations:");
  logMsg("   • Chunk upload: " + String(MAX_UPLOAD_CHUNK) + " records/session");
  logMsg("   • Polling interval: " + String(INTERNET_INTERVAL / 1000) + "s");
  logMsg("   • ACK timeout: 2s");
  logMsg("   • Compaction: " + String(COMPACT_INTERVAL / 60000) +
         "min interval");
  logMsg("   • Keep-alive: " + String(MQTT_KEEPALIVE_MS / 1000) + "s");
}

// ================= LOOP =================
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
    logMsg("⏹️ → IDLE (cooldown 30s selesai, Logger Berhenti)");
  }

  // 3. Handle client local TCP (DT connecting)
  WiFiClient c = server.available();
  if (c) {
    handleClient(c);
  }

  // 4. Jaga koneksi MQTT tetap aktif
  if (WiFi.status() == WL_CONNECTED && mqtt.connected()) {
    mqtt.loop();
  }

  // 5. Periodic Check & Recovery Hardware SD Card (Self-Healing)
  static unsigned long lastSDCheck = 0;
  if (millis() - lastSDCheck > 5000) {
    lastSDCheck = millis();
    checkSDHealth();
  }

  // 6. Pengurasan Backlog & Koneksi Internet
  unsigned long now = millis();
  if (!busy && now - lastInternetTry >= INTERNET_INTERVAL) {
    lastInternetTry = now;
    busy = true;
    tryInternetAndPublishChunk();
    busy = false;
  }

  // 7. 🍃 POWER SAVING (Modem-Sleep saat Unit IDLE & Tidak ada backlog)
  if (recordState == REC_IDLE && WiFi.status() != WL_CONNECTED) {
    WiFi.setSleep(true); // Aktifkan modem-sleep untuk hemat aki & dingin
    delay(10);           // Jeda ringan CPU
  } else {
    WiFi.setSleep(false); // Maksimalkan throughput saat aktif/streaming
  }

  // OPTIMIZATION: Periodic compaction
  if (now - lastCompact >= COMPACT_INTERVAL) {
    lastCompact = now;
    compactQueueFile(LOG_FILE, OFFSET_FILE, COMPACT_TEMP_FILE);
  }

  // Heap Monitor
  if (ESP.getFreeHeap() < HEAP_MIN_BYTES) {
    logMsg("❌ Heap kritis: " + String(ESP.getFreeHeap()) +
           " bytes, RESTARTING...");
    delay(1000);
    ESP.restart();
  }

  delay(2);
}
