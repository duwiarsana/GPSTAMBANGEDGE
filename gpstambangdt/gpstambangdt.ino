#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <SD.h>
#include <ArduinoJson.h>

// ================= PIN =================
#define GPS_RX 16
#define GPS_TX 17
#define SD_CS  5

#define LED_GPS    2    // 🔵 GPS DT logging
#define LED_EXCA   4    // 🟢 EXCA transfer
#define LED_MQTT  15    // 🔴 MQTT publish

// ================= ID DEVICE =================
// GANTI INI SAJA UNTUK MULTIPLE DT
const char* DT_ID = "DT01";

// ================= UART =================
#define GPS_BAUD 115200

// ================= EXCA =================
const char* EXCA_PASS = "12345678";
const uint16_t EXCA_PORT = 5000;
IPAddress excaIP(192, 168, 4, 1);

// ================= MQTT =================
const char* MQTT_SERVER = "broker.hivemq.com";
const uint16_t MQTT_PORT = 1883;
const char* MQTT_DATA_TOPIC = "kutai/fleet/data";

// ACK topic dibentuk otomatis: kutai/fleet/ack/DT01

WiFiClient espClient;
PubSubClient mqtt(espClient);

// ================= INTERNET WIFI =================
// ISI SESUAI WIFI INTERNET YANG BOLEH DIPAKAI DT
struct WifiCredential {
  const char* ssid;
  const char* pass;
};

WifiCredential wifiList[] = {
  {"WIFI_KAMU", "PASSWORD_WIFI_KAMU"},
  {"HOTSPOT_KAMU", "PASSWORD_HOTSPOT_KAMU"}
};

const int wifiCount = sizeof(wifiList) / sizeof(wifiList[0]);

// ================= FILE =================
const char* DT_LOG_FILE       = "/dt_log.jsonl";
const char* RELAY_LOG_FILE    = "/relay_log.jsonl";

const char* DT_OFFSET_FILE    = "/dt_offset.txt";
const char* RELAY_OFFSET_FILE = "/relay_offset.txt";

const char* DT_SEQ_FILE       = "/dt_seq.txt";

// ================= PARSER GPS DT =================
#define BUF_SIZE 4096
char gpsBuf[BUF_SIZE];
int gpsBufLen = 0;
int gpsBrace = 0;
bool gpsCollecting = false;
unsigned long gpsStartJson = 0;

// ================= STATE =================
uint32_t dtSeq = 0;
bool excaTransferBusy = false;

unsigned long lastExcaScan    = 0;
unsigned long lastInternetTry = 0;
unsigned long lastCompact     = 0;
unsigned long lastHeartbeat   = 0;

const unsigned long EXCA_SCAN_INTERVAL = 10000;   // 10 detik
const unsigned long INTERNET_INTERVAL  = 30000;   // 30 detik
const unsigned long COMPACT_INTERVAL   = 120000;  // 2 menit
const unsigned long HEARTBEAT_INTERVAL = 60000;   // 1 menit

// LED timers
unsigned long ledGpsTimer  = 0;
unsigned long ledExcaTimer = 0;

// ACK state
String ackTopic = "";
String lastAckMsgId = "";
bool ackReceived = false;

// Stats counter
uint32_t statGpsLogged   = 0;
uint32_t statExcaRelayed = 0;
uint32_t statMqttSent    = 0;

// ================= DEBUG =================
void logMsg(const String &msg) {
  Serial.print("[");
  Serial.print(millis());
  Serial.print("] ");
  Serial.println(msg);
}

// ================= FILE UTIL =================
uint32_t readUint(const char* path, uint32_t fallback = 0) {
  File f = SD.open(path, FILE_READ);
  if (!f) return fallback;
  String s = f.readString();
  f.close();
  s.trim();
  if (s.length() == 0) return fallback;
  return (uint32_t)s.toInt();
}

bool writeUint(const char* path, uint32_t val) {
  SD.remove(path);
  File f = SD.open(path, FILE_WRITE);
  if (!f) return false;
  f.print(val);
  f.close();
  return true;
}

bool ensureUintFile(const char* path, uint32_t defaultVal) {
  if (SD.exists(path)) return true;
  return writeUint(path, defaultVal);
}

bool appendLine(const char* path, const String &line) {
  File f = SD.open(path, FILE_APPEND);
  if (!f) {
    logMsg(String("❌ open fail: ") + path);
    return false;
  }
  f.println(line);
  f.flush();
  f.close();
  return true;
}

// ================= UID DT =================
String makeDTUID(JsonDocument &doc) {
  dtSeq++;
  writeUint(DT_SEQ_FILE, dtSeq);

  String imei = doc["imei"] | "0";
  String ts   = doc["timestamp"] | "0";

  ts.replace("-", "");
  ts.replace(":", "");

  return String(DT_ID) + "-" + imei + "-" + ts + "-" + String(dtSeq);
}

// ================= STORAGE INIT =================
bool initStorage() {
  logMsg("Init SD...");

  if (!SD.begin(SD_CS)) {
    logMsg("❌ SD fail");
    return false;
  }

  if (!ensureUintFile(DT_OFFSET_FILE, 0)) return false;
  if (!ensureUintFile(RELAY_OFFSET_FILE, 0)) return false;
  if (!ensureUintFile(DT_SEQ_FILE, 0)) return false;

  dtSeq = readUint(DT_SEQ_FILE, 0);

  // Pastikan file log ada
  File f1 = SD.open(DT_LOG_FILE, FILE_APPEND);
  if (!f1) return false;
  f1.close();

  File f2 = SD.open(RELAY_LOG_FILE, FILE_APPEND);
  if (!f2) return false;
  f2.close();

  logMsg("✅ SD ready, seq=" + String(dtSeq));
  return true;
}

// ================= GPS DT LOGGER =================
void resetGpsParser() {
  gpsBufLen = 0;
  gpsBrace = 0;
  gpsCollecting = false;
}

bool processDTJson(const char* json, String &out) {
  StaticJsonDocument<3072> doc;

  if (deserializeJson(doc, json)) {
    logMsg("❌ DT JSON error");
    return false;
  }

  if (!doc["imei"] || !doc["timestamp"]) {
    logMsg("❌ DT invalid field");
    return false;
  }

  doc["msg_id"]      = makeDTUID(doc);
  doc["source"]      = DT_ID;
  doc["record_type"] = "dt";

  out = "";
  serializeJson(doc, out);
  return true;
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
        if (appendLine(DT_LOG_FILE, clean)) {
          statGpsLogged++;

          // LED flash
          digitalWrite(LED_GPS, HIGH);
          ledGpsTimer = millis();

          logMsg("📍 DT logged #" + String(statGpsLogged));
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

// ================= SERIAL FLUSH =================
// Buang data GPS lama di buffer serial setelah WiFi operation
// karena data di buffer sudah tidak lengkap/basi
void flushStaleGpsData() {
  resetGpsParser();
  unsigned long t0 = millis();
  while (Serial2.available() && millis() - t0 < 100) {
    Serial2.read();
  }
}

// ================= EXCA WIFI =================
bool isExcaSSID(const String &ssid) {
  return ssid.startsWith("EXCA") && ssid.endsWith("_DATA");
}

String findBestExcaSSID() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(200);

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
    logMsg("📡 Best EXCA: " + bestSSID + " RSSI:" + String(bestRSSI));
  }

  return bestSSID;
}

bool connectExca(const String &ssid) {
  logMsg("Connecting EXCA: " + ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), EXCA_PASS);

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - t0 > 12000) {
      logMsg("❌ EXCA wifi timeout");
      WiFi.disconnect(true, true);
      return false;
    }
    delay(300);
  }

  logMsg("✅ EXCA connected, IP:" + WiFi.localIP().toString());
  return true;
}

bool waitTcpLine(WiFiClient &client, String &out, unsigned long timeoutMs) {
  unsigned long t0 = millis();

  while (!client.available()) {
    if (!client.connected() || millis() - t0 > timeoutMs) {
      return false;
    }
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
  if (!waitTcpLine(client, line, 5000)) {
    logMsg("❌ EXCA no response to HELLO");
    client.stop();
    return false;
  }

  if (line == "BUSY") {
    logMsg("⚠️ EXCA busy, retry later");
    client.stop();
    return false;
  }

  if (line != "READY") {
    logMsg("❌ EXCA unexpected: " + line);
    client.stop();
    return false;
  }

  client.println("GET");

  int count = 0;

  while (true) {
    if (!waitTcpLine(client, line, 8000)) {
      logMsg("❌ EXCA data timeout after " + String(count) + " lines");
      client.stop();
      return false;
    }

    if (line == "NO_DATA" || line == "NO_LOG") {
      logMsg("ℹ️ EXCA no new data");
      client.stop();
      return true;
    }

    if (line == "END") {
      client.println("OK");
      delay(100);  // beri waktu OK terkirim sebelum disconnect
      client.stop();
      statExcaRelayed += count;
      logMsg("✅ EXCA sync: " + String(count) + " lines");
      return true;
    }

    if (line.length() > 0) {
      // Validasi bahwa data adalah JSON valid sebelum simpan
      StaticJsonDocument<512> checkDoc;
      if (deserializeJson(checkDoc, line)) {
        logMsg("⚠️ EXCA bad JSON, skip");
        client.println("NEXT");
        continue;
      }

      if (appendLine(RELAY_LOG_FILE, line)) {
        count++;
        client.println("NEXT");
      } else {
        logMsg("❌ SD write fail, abort transfer");
        client.stop();
        return false;
      }
    }
  }
}

// ================= MQTT / INTERNET =================
bool connectKnownInternet() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(200);

  int n = WiFi.scanNetworks();
  if (n <= 0) {
    WiFi.scanDelete();
    return false;
  }

  // Pilih WiFi internet dengan RSSI terbaik
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

  if (bestIdx < 0) return false;

  logMsg("🌐 Connecting: " + String(wifiList[bestIdx].ssid) + " RSSI:" + String(bestRSSI));
  WiFi.begin(wifiList[bestIdx].ssid, wifiList[bestIdx].pass);

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - t0 > 15000) {
      logMsg("❌ Internet timeout");
      WiFi.disconnect(true, true);
      return false;
    }
    delay(300);
  }

  logMsg("✅ Internet connected, IP:" + WiFi.localIP().toString());
  return true;
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String topicStr = String(topic);
  if (topicStr != ackTopic) return;

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

  String msgId  = doc["msg_id"] | "";
  String status = doc["status"] | "";

  if (status == "ok" && msgId.length() > 0) {
    lastAckMsgId = msgId;
    ackReceived = true;
    logMsg("✅ ACK: " + msgId);
  }
}

bool connectMQTT() {
  ackTopic = String("kutai/fleet/ack/") + DT_ID;

  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setBufferSize(1024);   // Default 256 terlalu kecil untuk JSON GPS
  mqtt.setCallback(mqttCallback);
  mqtt.setKeepAlive(30);

  if (mqtt.connected()) return true;

  // Client ID unik untuk mencegah collision antar DT
  String clientId = String(DT_ID) + "_" + String(millis() % 10000);
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

bool publishOneWithAck(const String &line, const String &msgId, int maxRetry = 3) {
  for (int attempt = 1; attempt <= maxRetry; attempt++) {
    ackReceived = false;
    lastAckMsgId = "";

    if (!mqtt.connected()) {
      if (!connectMQTT()) {
        delay(500 * attempt);  // exponential backoff
        continue;
      }
    }

    if (!mqtt.publish(MQTT_DATA_TOPIC, line.c_str())) {
      logMsg("❌ publish fail #" + String(attempt));
      delay(500 * attempt);
      continue;
    }

    logMsg("📤 published, wait ACK...");

    // Tunggu ACK dengan timeout 5 detik
    unsigned long t0 = millis();
    while (millis() - t0 < 5000) {
      mqtt.loop();

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

bool publishQueueFile(const char* logPath, const char* offsetPath) {
  File f = SD.open(logPath, FILE_READ);
  if (!f) {
    logMsg(String("❌ open queue: ") + logPath);
    return false;
  }

  uint32_t offset = readUint(offsetPath, 0);

  // Validasi offset tidak melebihi ukuran file
  uint32_t fileSize = f.size();
  if (offset > fileSize) {
    logMsg("⚠️ offset(" + String(offset) + ") > fileSize(" + String(fileSize) + "), reset 0");
    offset = 0;
  }

  if (!f.seek(offset)) {
    logMsg("⚠️ seek fail, reset 0");
    f.seek(0);
    offset = 0;
  }

  // Cek apakah ada data baru
  if (offset >= fileSize) {
    f.close();
    return true;  // tidak ada data baru, bukan error
  }

  uint32_t currentPos = offset;
  int sentCount = 0;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    currentPos = (uint32_t)f.position();

    if (line.length() == 0) continue;

    StaticJsonDocument<1024> doc;
    if (deserializeJson(doc, line)) {
      logMsg("⚠️ bad json, skip");
      writeUint(offsetPath, currentPos);
      continue;
    }

    String msgId = doc["msg_id"] | "";
    if (msgId.length() == 0) {
      logMsg("⚠️ no msg_id, skip");
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
  }

  f.close();
  logMsg("✅ Published " + String(logPath) + ": " + String(sentCount) + " lines");
  return true;
}

void tryInternetAndPublish() {
  if (!connectKnownInternet()) {
    return;
  }

  if (!connectMQTT()) {
    WiFi.disconnect(true, true);
    return;
  }

  digitalWrite(LED_MQTT, HIGH);

  // Relay data prioritas pertama (data EXCA)
  publishQueueFile(RELAY_LOG_FILE, RELAY_OFFSET_FILE);

  // Lalu data DT sendiri
  publishQueueFile(DT_LOG_FILE, DT_OFFSET_FILE);

  digitalWrite(LED_MQTT, LOW);

  mqtt.disconnect();
  WiFi.disconnect(true, true);
}

// ================= COMPACTION =================
bool compactQueueFile(const char* logPath, const char* offsetPath, const char* tempPath) {
  uint32_t offset = readUint(offsetPath, 0);

  // Hanya compact jika offset cukup besar (hemat write cycle SD)
  if (offset < 4096) return true;

  logMsg("🧹 Compacting " + String(logPath) + " offset=" + String(offset));

  File src = SD.open(logPath, FILE_READ);
  if (!src) return false;

  if (!src.seek(offset)) {
    src.close();
    return false;
  }

  // Jika sudah tidak ada sisa data, kosongkan file saja
  if (!src.available()) {
    src.close();
    SD.remove(logPath);
    File empty = SD.open(logPath, FILE_WRITE);
    if (empty) empty.close();
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
    String line = src.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
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
  logMsg("🧹 Compacted " + String(logPath) + ": " + String(lineCount) + " lines kept");
  return true;
}

// ================= HEARTBEAT =================
void printHeartbeat() {
  logMsg("💓 " + String(DT_ID) +
         " | GPS:" + String(statGpsLogged) +
         " | EXCA:" + String(statExcaRelayed) +
         " | MQTT:" + String(statMqttSent) +
         " | heap:" + String(ESP.getFreeHeap()));
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(1000);

  logMsg("=== " + String(DT_ID) + " STARTING ===");

  // LED init
  pinMode(LED_GPS, OUTPUT);
  pinMode(LED_EXCA, OUTPUT);
  pinMode(LED_MQTT, OUTPUT);

  digitalWrite(LED_GPS, LOW);
  digitalWrite(LED_EXCA, LOW);
  digitalWrite(LED_MQTT, LOW);

  // GPS serial init
  Serial2.begin(GPS_BAUD);
  Serial2.setPins(GPS_RX, GPS_TX);

  delay(1500);
  while (Serial2.available()) Serial2.read();

  // SD card init
  if (!initStorage()) {
    logMsg("⚠️ Storage init problem");
  }

  // WiFi init (mode STA, semua disconnect)
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(200);

  logMsg("✅ " + String(DT_ID) + " READY");
}

// ================= LOOP =================
void loop() {

  // -------- 1. Selalu proses GPS --------
  handleDTGps();

  // LED GPS auto off setelah 100ms
  if (digitalRead(LED_GPS) == HIGH && millis() - ledGpsTimer > 100) {
    digitalWrite(LED_GPS, LOW);
  }

  unsigned long now = millis();

  // -------- 2. Scan & transfer dari EXCA --------
  if (!excaTransferBusy && now - lastExcaScan >= EXCA_SCAN_INTERVAL) {
    lastExcaScan = now;

    String ssid = findBestExcaSSID();
    if (ssid.length() > 0) {
      excaTransferBusy = true;
      digitalWrite(LED_EXCA, HIGH);

      if (connectExca(ssid)) {
        transferFromExca();
      }

      WiFi.disconnect(true, true);
      flushStaleGpsData();    // buang data GPS basi di serial buffer

      digitalWrite(LED_EXCA, LOW);
      excaTransferBusy = false;
    }
  }

  // -------- 3. Internet & MQTT publish --------
  if (!excaTransferBusy && now - lastInternetTry >= INTERNET_INTERVAL) {
    lastInternetTry = now;

    tryInternetAndPublish();
    flushStaleGpsData();      // buang data GPS basi di serial buffer
  }

  // -------- 4. Compaction (interval-based) --------
  if (now - lastCompact >= COMPACT_INTERVAL) {
    lastCompact = now;
    compactQueueFile(DT_LOG_FILE, DT_OFFSET_FILE, "/dt_tmp.jsonl");
    compactQueueFile(RELAY_LOG_FILE, RELAY_OFFSET_FILE, "/relay_tmp.jsonl");
  }

  // -------- 5. Heartbeat --------
  if (now - lastHeartbeat >= HEARTBEAT_INTERVAL) {
    lastHeartbeat = now;
    printHeartbeat();
  }

  delay(5);
}