#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <SD.h>
#include <ArduinoJson.h>

// ================= PIN =================
#define RXD2 16
#define TXD2 17
#define SD_CS 5

#define LED_LOG 2        // 🔵 GPS + SD
#define LED_TRANSFER 4   // 🔴 Transfer DT
#define LED_REC 13       // 🟡 Recording state

// ================= UART =================
#define GPS_BAUD 115200

// ================= WIFI =================
const char* AP_SSID = "EXCA01_DATA";
const char* AP_PASS = "12345678";
WiFiServer server(5000);

// ================= FILE =================
const char* LOG_FILE = "/gps_log.jsonl";
const char* SNAP_FILE = "/snap.jsonl";
const char* OFFSET_FILE = "/offset.txt";
const char* SEQ_FILE = "/seq.txt";

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

// ================= IGNITION STATE =================
enum RecordState { REC_IDLE, REC_ACTIVE, REC_COOLDOWN };
RecordState recordState = REC_IDLE;
unsigned long ignOffTime = 0;
const unsigned long IGN_COOLDOWN_MS = 180000;  // 3 menit
uint32_t statSkipped = 0;
uint32_t statLogged  = 0;

// LED REC blink
unsigned long ledRecLastToggle = 0;
bool ledRecOn = false;

// ================= DEBUG =================
void logMsg(String s){
  Serial.print("["); Serial.print(millis()); Serial.print("] ");
  Serial.println(s);
}

// ================= FILE =================
uint32_t readUint(const char* path){
  File f = SD.open(path);
  if(!f) return 0;
  String s = f.readString();
  f.close();
  return s.toInt();
}

void writeUint(const char* path, uint32_t v){
  SD.remove(path);
  File f = SD.open(path, FILE_WRITE);
  f.print(v);
  f.close();
}

// ================= UID =================
String makeUID(JsonDocument &doc){

  seq++;
  writeUint(SEQ_FILE, seq);

  String imei = doc["imei"] | "0";
  String ts   = doc["timestamp"] | "0";

  ts.replace("-", "");
  ts.replace(":", "");

  return "EXCA01-" + imei + "-" + ts + "-" + String(seq);
}

// ================= INIT =================
void initSD(){

  if(!SD.begin(SD_CS)){
    logMsg("SD FAIL");
    return;
  }

  if(!SD.exists(OFFSET_FILE)) writeUint(OFFSET_FILE,0);
  if(!SD.exists(SEQ_FILE)) writeUint(SEQ_FILE,0);

  seq = readUint(SEQ_FILE);

  logFile = SD.open(LOG_FILE, FILE_APPEND);

  logMsg("SD READY");
}

// ================= LOG =================
void appendLog(String line){

  if(!logFile){
    logFile = SD.open(LOG_FILE, FILE_APPEND);
    if(!logFile){
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
const char* recStateStr(RecordState s) {
  switch (s) {
    case REC_IDLE:     return "IDLE";
    case REC_ACTIVE:   return "ACTIVE";
    case REC_COOLDOWN: return "COOLDOWN";
    default:           return "?";
  }
}

bool shouldRecord(JsonDocument &doc) {
  int eventCode = doc["event_code"] | 0;
  int ignition  = doc["ignition"]  | -1;

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
    return true;  // selalu catat event ignition
  }

  // State machine berdasarkan field ignition
  switch (recordState) {
    case REC_IDLE:
      if (ignition == 1) {
        recordState = REC_ACTIVE;
        logMsg("⏺️ → ACTIVE");
        return true;
      }
      return false;  // skip data saat idle

    case REC_ACTIVE:
      if (ignition == 0) {
        recordState = REC_COOLDOWN;
        ignOffTime = millis();
        logMsg("⏸️ → COOLDOWN");
      }
      return true;  // catat termasuk data pertama ignition=0

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
      return true;  // masih dalam cooldown, tetap catat

    default:
      return false;
  }
}

// ================= JSON =================
bool processJSON(const char* json, String &out){

  StaticJsonDocument<1536> doc;

  static StaticJsonDocument<256> filter;
  static bool filterInitialized = false;
  if (!filterInitialized) {
    filter["imei"] = true;
    filter["event_code"] = true;
    filter["timestamp"] = true;
    filter["latitude"] = true;
    filter["longitude"] = true;
    filter["fix"] = true;
    filter["speed"] = true;
    filter["heading"] = true;
    filter["odometer"] = true;
    filter["altitude"] = true;
    filter["ignition"] = true;
    filter["input_status"] = true;
    filter["external"] = true;
    filter["ibutton"]["id"] = true;
    filter["ibutton"]["status"] = true;
    filter["ibutton"]["auth"] = true;
    filter["ibeacon"][0]["mac"] = true;
    filter["ibeacon"][0]["battery"] = true;
    filter["ibeacon"][0]["major"] = true;
    filter["ibeacon"][0]["minor"] = true;
    filter["ibeacon"][0]["rssi"] = true;
    filterInitialized = true;
  }

  if (deserializeJson(doc, json, DeserializationOption::Filter(filter))) {
    logMsg("JSON ERROR");
    return false;
  }

  if(!doc["imei"] || !doc["timestamp"]){
    logMsg("INVALID FIELD");
    return false;
  }

  // Filter berdasarkan ignition state
  if (!shouldRecord(doc)) {
    statSkipped++;
    return false;
  }

  StaticJsonDocument<1024> optDoc;
  optDoc["id"]   = makeUID(doc);
  optDoc["imei"] = doc["imei"];
  optDoc["ev"]   = doc["event_code"];
  optDoc["ts"]   = doc["timestamp"];
  optDoc["lat"]  = doc["latitude"];
  optDoc["lon"]  = doc["longitude"];
  optDoc["fix"]  = doc["fix"];
  optDoc["spd"]  = doc["speed"];
  optDoc["hdg"]  = doc["heading"];
  optDoc["odo"]  = doc["odometer"];
  optDoc["alt"]  = doc["altitude"];
  optDoc["ign"]  = doc["ignition"];
  optDoc["in"]   = doc["input_status"];
  optDoc["volt"] = doc["external"];

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
      b["mac"]  = beacon["mac"];
      b["bat"]  = beacon["battery"];
      b["maj"]  = beacon["major"];
      b["min"]  = beacon["minor"];
      b["rssi"] = beacon["rssi"];
    }
  }

  out = "";
  serializeJson(optDoc, out);
  return true;
}

// ================= PARSER =================
void resetParser(){
  bufLen = 0;
  brace = 0;
  collecting = false;
}

void handleGPS(){

  while(Serial2.available()){

    char c = Serial2.read();

    // cari start JSON
    if(!collecting){
      if(c=='{'){
        collecting = true;
        brace = 1;
        bufLen = 0;
        buf[bufLen++] = c;
        startJson = millis();
      }
      continue;
    }

    // simpan data
    if(bufLen < BUF_SIZE-1){
      buf[bufLen++] = c;
    } else {
      logMsg("OVERFLOW");
      resetParser();
      continue;
    }

    if(c=='{') brace++;
    if(c=='}') brace--;

    // JSON lengkap
    if(brace == 0){

      buf[bufLen] = '\0';

      String clean;

      if(processJSON(buf, clean)){
        appendLog(clean);
      }

      resetParser();
      continue;
    }

    // timeout JSON
    if(millis() - startJson > 4000){
      logMsg("TIMEOUT");
      resetParser();
    }
  }
}

// ================= SNAPSHOT UTILS =================
uint32_t readOffset(){
  return readUint(OFFSET_FILE);
}

void writeOffset(uint32_t v){
  writeUint(OFFSET_FILE, v);
}

// ================= SEND =================
bool waitAck(WiFiClient &c, String expect){

  unsigned long t = millis();

  while(!c.available()){
    if(!c.connected() || millis()-t>3000) return false;
    
    // Anti-blocking: tetap proses GPS saat menunggu response WiFi
    handleGPS();
    delay(1);
  }

  String s = c.readStringUntil('\n');
  s.trim();

  return s==expect;
}

// ================= CLIENT =================
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

  // Buka log file asli untuk streaming
  File f = SD.open(LOG_FILE, FILE_READ);
  if (!f) {
    c.println("NO_DATA");
    c.stop();
    digitalWrite(LED_TRANSFER, LOW);
    busy = false;
    return;
  }

  uint32_t startOffset = readOffset();
  uint32_t totalSize = f.size();

  if (startOffset >= totalSize) {
    c.println("NO_DATA");
    f.close();
    c.stop();
    digitalWrite(LED_TRANSFER, LOW);
    busy = false;
    return;
  }

  // Kirim informasi startOffset dan totalSize
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
    // Anti-blocking: tetap proses GPS
    handleGPS();
  }

  f.close();

  if (success && bytesSent == totalToSend) {
    c.println("END");
    if (waitAck(c, "OK")) {
      writeOffset(totalSize);
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

// ================= LED RECORDING =================
void updateLedRec() {
  unsigned long now = millis();
  unsigned long interval = 0;

  switch (recordState) {
    case REC_IDLE:
      // LED mati total
      if (ledRecOn) {
        digitalWrite(LED_REC, LOW);
        ledRecOn = false;
      }
      return;

    case REC_ACTIVE:
      // Blink lambat: 1s ON, 1s OFF
      interval = 1000;
      break;

    case REC_COOLDOWN:
      // Blink cepat: 200ms ON, 200ms OFF
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
void setup(){

  Serial.begin(115200);

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
  while(Serial2.available()) Serial2.read();

  initSD();

  WiFi.softAP(AP_SSID, AP_PASS);
  server.begin();

  logMsg("EXCA READY | IGN cooldown=" + String(IGN_COOLDOWN_MS / 1000) + "s");
}

// ================= LOOP =================
void loop(){

  handleGPS();

  // LED LOG auto off setelah 100ms
  if(digitalRead(LED_LOG)==HIGH && millis()-ledLogTimer>100){
    digitalWrite(LED_LOG, LOW);
  }

  // Update LED recording state (non-blocking blink)
  updateLedRec();

  // Cek cooldown timeout di loop juga (untuk kasus tidak ada data masuk)
  if (recordState == REC_COOLDOWN && millis() - ignOffTime >= IGN_COOLDOWN_MS) {
    recordState = REC_IDLE;
    logMsg("⏹️ → IDLE (cooldown selesai)");
  }

  WiFiClient c = server.available();
  if(c){
    handleClient(c);
  }

  delay(2);
}