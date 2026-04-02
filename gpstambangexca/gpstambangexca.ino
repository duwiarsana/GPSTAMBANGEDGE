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

  logMsg("LOGGED");
}

// ================= JSON =================
bool processJSON(const char* json, String &out){

  StaticJsonDocument<3072> doc;

  if(deserializeJson(doc, json)){
    logMsg("JSON ERROR");
    return false;
  }

  if(!doc["imei"] || !doc["timestamp"]){
    logMsg("INVALID FIELD");
    return false;
  }

  doc["msg_id"] = makeUID(doc);
  doc["source"] = "EXCA01";

  serializeJson(doc, out);
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

// ================= SNAPSHOT =================
uint32_t readOffset(){
  return readUint(OFFSET_FILE);
}

void writeOffset(uint32_t v){
  writeUint(OFFSET_FILE, v);
}

bool createSnapshot(uint32_t &newOffset){

  File src = SD.open(LOG_FILE);
  if(!src) return false;

  uint32_t off = readOffset();

  if(!src.seek(off)){
    src.seek(0);
    off = 0;
  }

  SD.remove(SNAP_FILE);
  File snap = SD.open(SNAP_FILE, FILE_WRITE);

  while(src.available()){
    String line = src.readStringUntil('\n');
    line.trim();
    if(line.length()==0) continue;

    snap.println(line);
  }

  newOffset = src.position();

  snap.close();
  src.close();

  return true;
}

// ================= SEND =================
bool waitAck(WiFiClient &c, String expect){

  unsigned long t = millis();

  while(!c.available()){
    if(!c.connected() || millis()-t>3000) return false;
    delay(1);
  }

  String s = c.readStringUntil('\n');
  s.trim();

  return s==expect;
}

bool sendSnap(WiFiClient &c){

  File f = SD.open(SNAP_FILE);
  if(!f){
    c.println("NO_DATA");
    return false;
  }

  while(f.available()){

    String line = f.readStringUntil('\n');
    line.trim();

    if(line.length()==0) continue;

    c.println(line);

    if(!waitAck(c,"NEXT")){
      f.close();
      return false;
    }
  }

  c.println("END");
  f.close();
  return true;
}

// ================= CLIENT =================
void handleClient(WiFiClient c){

  if(busy){
    c.println("BUSY");
    c.stop();
    return;
  }

  busy = true;
  digitalWrite(LED_TRANSFER, HIGH);

  String cmd;

  if(!waitAck(c,"HELLO")){
    c.stop();
    digitalWrite(LED_TRANSFER, LOW);
    busy=false;
    return;
  }

  c.println("READY");

  if(!waitAck(c,"GET")){
    c.stop();
    digitalWrite(LED_TRANSFER, LOW);
    busy=false;
    return;
  }

  uint32_t newOffset=0;

  if(!createSnapshot(newOffset)){
    c.println("NO_DATA");
    c.stop();
    digitalWrite(LED_TRANSFER, LOW);
    busy=false;
    return;
  }

  if(!sendSnap(c)){
    c.stop();
    digitalWrite(LED_TRANSFER, LOW);
    busy=false;
    return;
  }

  if(!waitAck(c,"OK")){
    c.stop();
    digitalWrite(LED_TRANSFER, LOW);
    busy=false;
    return;
  }

  writeOffset(newOffset);

  logMsg("TRANSFER OK");

  c.stop();
  digitalWrite(LED_TRANSFER, LOW);
  busy = false;
}

// ================= SETUP =================
void setup(){

  Serial.begin(115200);

  pinMode(LED_LOG, OUTPUT);
  pinMode(LED_TRANSFER, OUTPUT);

  digitalWrite(LED_LOG, LOW);
  digitalWrite(LED_TRANSFER, LOW);

  Serial2.begin(GPS_BAUD);
  Serial2.setPins(RXD2, TXD2);

  delay(1500);
  while(Serial2.available()) Serial2.read();

  initSD();

  WiFi.softAP(AP_SSID, AP_PASS);
  server.begin();

  logMsg("EXCA READY");
}

// ================= LOOP =================
void loop(){

  handleGPS();

  // LED LOG auto off
  if(digitalRead(LED_LOG)==HIGH && millis()-ledLogTimer>100){
    digitalWrite(LED_LOG, LOW);
  }

  WiFiClient c = server.available();
  if(c){
    handleClient(c);
  }

  delay(2);
}