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

#define LED_REC 13 // 🟡 Unified Status LED (hanya ada GPIO 13)
#define LED_LOG 13
#define LED_TRANSFER 13

// ================= WATCHDOG & MEMORY =================
#define WDT_TIMEOUT_SEC 30   // Hardware watchdog: 30 detik
#define HEAP_MIN_BYTES 20000 // Heap minimum: 20KB → restart

// ================= UART =================
#define GPS_BAUD 115200

// ================= WIFI AP =================
const char *EXCA_ID = "EXCA02";
const char *AP_SSID = "EXCA02_DATA";
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

const char *MQTT_SERVER = "72.62.126.85";
const uint16_t MQTT_PORT = 1883;
const char *MQTT_DATA_TOPIC = "kutai/fleet/data";

WiFiClient espClient;
PubSubClient mqtt(espClient);

// MQTT ACK State
String ackTopic = "";
volatile bool ackReceived = false;
String lastAckMsgId = "";

// ================= FILE =================
const char *LOG_FILE = "/gps_log.jsonl";
const char *SNAP_FILE = "/snap.jsonl";
const char *OFFSET_FILE = "/offset.txt";
const char *SEQ_FILE = "/seq.txt";

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
const unsigned long INTERNET_INTERVAL = 60000; // Coba internet tiap 60 detik

// ================= IGNITION STATE =================
enum RecordState { REC_IDLE, REC_ACTIVE, REC_COOLDOWN };
RecordState recordState = REC_IDLE;
unsigned long ignOffTime = 0;
const unsigned long IGN_COOLDOWN_MS = 180000; // 3 menit
uint32_t statSkipped = 0;
uint32_t statLogged = 0;
uint32_t statMqttSent = 0;

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

// ================= FILE =================
uint32_t readUint(const char *path) {
  File f = SD.open(path);
  if (!f)
    return 0;
  String s = f.readString();
  f.close();
  return s.toInt();
}

void writeUint(const char *path, uint32_t v) {
  SD.remove(path);
  File f = SD.open(path, FILE_WRITE);
  f.print(v);
  f.close();
}

// ================= UID =================
String makeUID(JsonDocument &doc) {
  seq++;
  writeUint(SEQ_FILE, seq);

  String imei = doc["imei"] | "0";
  String ts = doc["timestamp"] | "0";

  ts.replace("-", "");
  ts.replace(":", "");

  return String(EXCA_ID) + "-" + imei + "-" + ts + "-" + String(seq);
}

// ================= INIT =================
void initSD() {
  if (!SD.begin(SD_CS)) {
    logMsg("SD FAIL");
    return;
  }

  if (!SD.exists(OFFSET_FILE))
    writeUint(OFFSET_FILE, 0);
  if (!SD.exists(SEQ_FILE))
    writeUint(SEQ_FILE, 0);

  seq = readUint(SEQ_FILE);

  logFile = SD.open(LOG_FILE, FILE_APPEND);

  logMsg("SD READY");
}

// ================= LOG =================
void appendLog(String line) {
  if (!logFile) {
    logFile = SD.open(LOG_FILE, FILE_APPEND);
    if (!logFile) {
      logMsg("LOG FAIL");
      return;
    }
  }

  logFile.println(line);
  logFile.flush();

  digitalWrite(LED_LOG, HIGH);
  ledLogTimer = millis();

  statLogged++;
  logMsg("📍 LOGGED #" + String(statLogged));
}

// ================= IGNITION FILTER =================
const char *recStateStr(RecordState s) {
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

  // Selalu catat event Ignition On/Off untuk audit trail
  if (eventCode == 2 || eventCode == 3) {
    if (eventCode == 2) {
      recordState = REC_ACTIVE;
      logMsg("🔑 IGN ON → ACTIVE");
    } else {
      if (recordState == REC_ACTIVE) {
        recordState = REC_COOLDOWN;
        ignOffTime = millis();
        logMsg("🔑 IGN OFF → COOLDOWN (" + String(IGN_COOLDOWN_MS / 1000) +
               "s)");
      }
    }
    return true; // selalu catat event ignition
  }

  // State machine berdasarkan field ignition
  switch (recordState) {
  case REC_IDLE:
    if (ignition == 1) {
      recordState = REC_ACTIVE;
      logMsg("⏺️ → ACTIVE");
      return true;
    }
    return false; // skip data saat idle

  case REC_ACTIVE:
    if (ignition == 0) {
      recordState = REC_COOLDOWN;
      ignOffTime = millis();
      logMsg("⏸️ → COOLDOWN");
    }
    return true; // catat termasuk data pertama ignition=0

  case REC_COOLDOWN:
    if (ignition == 1) {
      recordState = REC_ACTIVE;
      logMsg("⏺️ → ACTIVE (dari cooldown)");
      return true;
    }
    // Cek apakah cooldown sudah habis
    if (millis() - ignOffTime >= IGN_COOLDOWN_MS) {
      recordState = REC_IDLE;
      logMsg("⏹️ → IDLE (cooldown selesai)");
      return false;
    }
    return true; // masih dalam cooldown, tetap catat

  default:
    return false;
  }
}

// ================= JSON =================
bool processJSON(const char *json, String &out) {
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
    logMsg("JSON ERROR");
    return false;
  }

  if (!doc["imei"] || !doc["timestamp"]) {
    logMsg("INVALID FIELD");
    return false;
  }

  // Filter berdasarkan ignition state
  if (!shouldRecord(doc)) {
    statSkipped++;
    return false;
  }

  StaticJsonDocument<1024> optDoc;
  optDoc["id"] = makeUID(doc);
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

  out = "";
  serializeJson(optDoc, out);
  return true;
}

// ================= PARSER =================
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
      logMsg("OVERFLOW");
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
      if (processJSON(buf, clean)) {
        appendLog(clean);
      }
      resetParser();
      continue;
    }

    if (millis() - startJson > 4000) {
      logMsg("TIMEOUT");
      resetParser();
    }
  }
}

// ================= SEND TO DT (LOCAL AP/TCP) =================
bool waitAck(WiFiClient &c, String expect) {
  unsigned long t = millis();
  while (!c.available()) {
    esp_task_wdt_reset();
    if (!c.connected() || millis() - t > 3000)
      return false;

    delay(1);
  }

  String s = c.readStringUntil('\n');
  s.trim();
  return s == expect;
}

void handleClient(WiFiClient c) {
  if (busy) {
    c.println("BUSY");
    c.stop();
    return;
  }

  busy = true;
  digitalWrite(LED_TRANSFER, HIGH);

  if (!waitAck(c, "HELLO")) {
    c.stop();
    digitalWrite(LED_TRANSFER, LOW);
    busy = false;
    return;
  }

  c.println("READY");

  if (!waitAck(c, "GET")) {
    c.stop();
    digitalWrite(LED_TRANSFER, LOW);
    busy = false;
    return;
  }

  File f = SD.open(LOG_FILE, FILE_READ);
  if (!f) {
    c.println("NO_DATA");
    c.stop();
    digitalWrite(LED_TRANSFER, LOW);
    busy = false;
    return;
  }

  uint32_t startOffset = readUint(OFFSET_FILE);
  uint32_t totalSize = f.size();

  if (startOffset >= totalSize) {
    c.println("NO_DATA");
    f.close();
    c.stop();
    digitalWrite(LED_TRANSFER, LOW);
    busy = false;
    return;
  }

  c.printf("START %u %u\n", startOffset, totalSize);

  if (!f.seek(startOffset)) {
    c.println("ERROR_SEEK");
    f.close();
    c.stop();
    digitalWrite(LED_TRANSFER, LOW);
    busy = false;
    return;
  }

  uint8_t buffer[1024];
  uint32_t bytesSent = 0;
  uint32_t totalToSend = totalSize - startOffset;
  bool success = true;

  while (f.available() && bytesSent < totalToSend && c.connected()) {
    esp_task_wdt_reset();
    int toRead = min((uint32_t)sizeof(buffer), totalToSend - bytesSent);
    int bytesRead = f.read(buffer, toRead);
    if (bytesRead > 0) {
      int written = c.write(buffer, bytesRead);
      if (written != bytesRead) {
        logMsg("❌ Send fail midway");
        success = false;
        break;
      }
      bytesSent += bytesRead;
    }
    delay(1);
  }

  f.close();

  if (success && bytesSent == totalToSend) {
    c.println("END");
    if (waitAck(c, "OK")) {
      writeUint(OFFSET_FILE, totalSize);
      logMsg("✅ TRANSFER OK, offset updated to: " + String(totalSize));
    } else {
      logMsg("⚠️ No OK ack from receiver");
    }
  } else {
    logMsg("❌ Transfer incomplete");
  }

  c.stop();
  digitalWrite(LED_TRANSFER, LOW);
  busy = false;
}

// ================= SEND DIRECT TO MQTT (STA) =================
bool connectKnownInternet() {
  WiFi.mode(WIFI_AP_STA);
  delay(100);

  int n = WiFi.scanNetworks();
  if (n <= 0) {
    WiFi.scanDelete();
    return false;
  }

  int bestIdx = -1;
  int bestRSSI = -1000;

  for (int i = 0; i < wifiCount; i++) {
    for (int j = 0; j < n; j++) {
      if (WiFi.SSID(j) == wifiList[i].ssid) {
        int rssi = WiFi.RSSI(j);
        if (rssi > bestRSSI) {
          bestIdx = i;
          bestRSSI = rssi;
        }
      }
    }
  }

  WiFi.scanDelete();

  if (bestIdx < 0)
    return false;

  logMsg("🌐 Connecting to internet WiFi: " + String(wifiList[bestIdx].ssid) +
         " RSSI:" + String(bestRSSI));
  WiFi.begin(wifiList[bestIdx].ssid, wifiList[bestIdx].pass);

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    esp_task_wdt_reset();
    if (millis() - t0 > 15000) {
      logMsg("❌ Internet connection timeout");
      WiFi.disconnect(false, true);
      return false;
    }
    delay(300);
  }

  logMsg("✅ Internet connected, IP:" + WiFi.localIP().toString());
  return true;
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String topicStr = String(topic);
  if (topicStr != ackTopic)
    return;

  String msg;
  msg.reserve(length + 1);
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, msg)) {
    logMsg("⚠️ ACK json error");
    return;
  }

  String msgId = doc["id"] | doc["msg_id"] | "";
  String status = doc["status"] | "";

  if (status == "ok" && msgId.length() > 0) {
    lastAckMsgId = msgId;
    ackReceived = true;
    logMsg("✅ ACK: " + msgId);
  }
}

bool connectMQTT() {
  ackTopic = String("kutai/fleet/ack/") + EXCA_ID;

  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setBufferSize(1024);
  mqtt.setCallback(mqttCallback);
  mqtt.setKeepAlive(30);

  if (mqtt.connected())
    return true;

  String clientId = String(EXCA_ID) + "_" + String(millis() % 10000);
  logMsg("Connecting MQTT as " + clientId + "...");

  if (!mqtt.connect(clientId.c_str())) {
    logMsg("❌ MQTT fail, state=" + String(mqtt.state()));
    return false;
  }

  if (!mqtt.subscribe(ackTopic.c_str())) {
    logMsg("❌ MQTT sub fail");
    mqtt.disconnect();
    return false;
  }

  logMsg("✅ MQTT connected, sub: " + ackTopic);
  return true;
}

bool publishOneWithAck(const String &line, const String &msgId,
                       int maxRetry = 3) {
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
    while (millis() - t0 < 5000) {
      esp_task_wdt_reset();
      mqtt.loop();

      // Anti-blocking: tetap proses GPS saat nunggu ACK
      handleGPS();

      if (ackReceived && lastAckMsgId == msgId) {
        statMqttSent++;
        return true;
      }

      delay(5);
    }

    logMsg("🔁 ACK timeout #" + String(attempt));
  }

  return false;
}

bool publishQueueFile(const char *logPath, const char *offsetPath) {
  File f = SD.open(logPath, FILE_READ);
  if (!f) {
    logMsg(String("❌ open queue: ") + logPath);
    return false;
  }

  uint32_t offset = readUint(offsetPath);
  uint32_t fileSize = f.size();

  if (offset > fileSize) {
    logMsg("⚠️ offset(" + String(offset) + ") > fileSize(" + String(fileSize) +
           "), reset 0");
    offset = 0;
  }

  if (!f.seek(offset)) {
    logMsg("⚠️ seek fail, reset 0");
    f.seek(0);
    offset = 0;
  }

  if (offset >= fileSize) {
    f.close();
    return true;
  }

  uint32_t currentPos = offset;
  int sentCount = 0;

  while (f.available()) {
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
      logMsg("⚠️ no id/msg_id, skip");
      writeUint(offsetPath, currentPos);
      continue;
    }

    if (!publishOneWithAck(line, msgId, 3)) {
      logMsg("❌ publish stop at line " + String(sentCount));
      f.close();
      return false;
    }

    writeUint(offsetPath, currentPos);
    sentCount++;

    handleGPS();
  }

  f.close();
  logMsg("✅ Published direct: " + String(sentCount) + " lines");
  return true;
}

void tryInternetAndPublishDirect() {
  if (!connectKnownInternet()) {
    return;
  }

  if (!connectMQTT()) {
    WiFi.disconnect(false, true);
    return;
  }

  publishQueueFile(LOG_FILE, OFFSET_FILE);

  mqtt.disconnect();
  WiFi.disconnect(false, true);
  logMsg("🌐 Disconnected internet WiFi, returned to AP mode.");
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

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(1000);
  logMsg("=== " + String(EXCA_ID) + " STARTING ===");
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

  delay(1500);
  while (Serial2.available())
    Serial2.read();

  initSD();

  // Mode AP+STA
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  server.begin();

  esp_task_wdt_add(NULL);

  logMsg("EXCA HYBRID READY | IGN cooldown=" + String(IGN_COOLDOWN_MS / 1000) +
         "s");
}

// ================= LOOP =================
void loop() {
  esp_task_wdt_reset();

  handleGPS();

  if (digitalRead(LED_LOG) == HIGH && millis() - ledLogTimer > 100) {
    digitalWrite(LED_LOG, LOW);
  }

  updateLedRec();

  if (recordState == REC_COOLDOWN && millis() - ignOffTime >= IGN_COOLDOWN_MS) {
    recordState = REC_IDLE;
    logMsg("⏹️ → IDLE (cooldown selesai)");
  }

  // Handle client local TCP (DT connecting)
  WiFiClient c = server.available();
  if (c) {
    handleClient(c);
  }

  // Periodically try internet upload
  unsigned long now = millis();
  if (!busy && now - lastInternetTry >= INTERNET_INTERVAL) {
    lastInternetTry = now;
    busy = true;
    tryInternetAndPublishDirect();
    busy = false;
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
