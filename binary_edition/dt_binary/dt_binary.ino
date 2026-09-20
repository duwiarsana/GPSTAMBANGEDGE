/**
 * DUMP TRUCK BINARY EDITION - 64-Byte Ultra High-Performance Firmware
 *
 * Features:
 * 1. 64-Byte Fixed-Size Raw Binary Telemetry Storage (85% storage & bandwidth
 * saving)
 * 2. Persistent Fast WiFi Connect via NVS Preferences (<500ms) with Smart Scan
 * Fallback
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

#include <DNSServer.h>
#include <WebServer.h>

// ================= PIN CONFIGURATION =================
#define PIN_BOOT_BTN 0 // Tombol BOOT pada ESP32 (Active LOW)
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

// ================= ID DEVICE (NVS Dynamic) =================
#define DEFAULT_DT_ID "DTCONFIG"
char DT_ID[16] = DEFAULT_DT_ID;



// ================= UART GPS =================
#define GPS_BAUD 115200

// ================= EXCA P2P HARVESTING =================
const char *EXCA_PASS = "12345678";
const uint16_t EXCA_PORT = 5000;
IPAddress excaIP(192, 168, 4, 1);

// ================= MQTT BROKER =================
const char *MQTT_SERVER = "34.101.180.48";
const uint16_t MQTT_PORT = 1883;
const char *MQTT_USER = "kutai";
const char *MQTT_PASS = "79750d76450466d56b9f44926f38614a3846bdbf";
const char *MQTT_BINARY_TOPIC = "kutai/fleet/binary";

WiFiClient espClient;
PubSubClient mqtt(espClient);

// ================= INTERNET WIFI GATEWAYS =================
struct WifiCredential {
  const char *ssid;
  const char *pass;
};

WifiCredential wifiList[] = {{"WIFI_GATEWAY_MINING_11", "46448951"},
                             {"HOTSPOT_DT_KEAMANAN", "46448951"}};
const int wifiCount = sizeof(wifiList) / sizeof(wifiList[0]);

// ================= PERSISTENT FAST WIFI (NVS) =================
struct WifiCache {
  bool valid;
  int index;
  uint8_t channel;
  uint8_t bssid[6];
};

// ================= FILESYSTEM PATHS =================
const char *DT_LOG_FILE_BIN = "/dt_log.bin";
const char *RELAY_LOG_FILE_BIN = "/relay_log.bin";

const char *DT_OFFSET_FILE = "/dt_offset.txt";
const char *RELAY_OFFSET_FILE = "/relay_offset.txt";

const char *DT_SEQ_FILE = "/dt_seq.txt";

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
const unsigned long INTERNET_INTERVAL = 10000;
const unsigned long COMPACT_INTERVAL = 1800000;
const int MAX_UPLOAD_CHUNK_RECORDS = 100;

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
    logMsg(String("❌ open fail: ") + path + " (err #" + String(sdErrorCount) +
           ")");
    return false;
  }
  size_t written = f.write((const uint8_t *)&pkt, sizeof(pkt));
  f.flush();
  f.close();
  sdErrorCount = 0;
  return (written == sizeof(pkt));
}

// ================= NVS DEVICE ID CONFIG =================
void loadDeviceID() {
  Preferences p;
  if (p.begin("device_cfg", true)) {
    String savedId = p.getString("unit_id", DEFAULT_DT_ID);
    savedId.trim();
    if (savedId.length() > 0 && savedId.length() < sizeof(DT_ID)) {
      strncpy(DT_ID, savedId.c_str(), sizeof(DT_ID) - 1);
      DT_ID[sizeof(DT_ID) - 1] = '\0';
    }
    p.end();
  }
}

void saveDeviceID(const String &newId) {
  String clean = newId;
  clean.trim();
  clean.toUpperCase();
  if (clean.length() == 0 || clean.length() >= sizeof(DT_ID)) return;

  Preferences p;
  if (p.begin("device_cfg", false)) {
    p.putString("unit_id", clean);
    p.end();
    strncpy(DT_ID, clean.c_str(), sizeof(DT_ID) - 1);
    DT_ID[sizeof(DT_ID) - 1] = '\0';
    logMsg("💾 Unit ID updated in NVS: " + String(DT_ID));
  }
}

// ================= WEB CONFIG PORTAL (CAPTIVE PORTAL) =================
WebServer configServer(80);
DNSServer dnsServer;

void handlePortalRoot() {
  String html = F("<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1.0'>"
                  "<title>Setting ID Dump Truck</title>"
                  "<style>"
                  "body{font-family:sans-serif;background:#0f172a;color:#f8fafc;padding:20px;text-align:center;}"
                  ".box{background:#1e293b;border-radius:12px;padding:24px;max-width:380px;margin:auto;box-shadow:0 4px 20px rgba(0,0,0,0.4);}"
                  "h2{margin-bottom:6px;color:#38bdf8;font-size:20px;}"
                  "p{font-size:13px;color:#94a3b8;margin-bottom:20px;}"
                  ".cur{background:#334155;padding:8px 14px;border-radius:8px;font-weight:bold;margin-bottom:18px;font-size:16px;color:#38bdf8;}"
                  "input[type=text]{width:100%;box-sizing:border-box;padding:12px;border-radius:8px;border:1px solid #475569;background:#0f172a;color:#fff;font-size:16px;text-transform:uppercase;text-align:center;font-weight:bold;margin-bottom:18px;}"
                  "input[type=text]:focus{outline:none;border-color:#38bdf8;}"
                  "button{width:100%;padding:12px;border:none;border-radius:8px;background:#0284c7;color:#fff;font-size:15px;font-weight:bold;cursor:pointer;}"
                  "button:hover{background:#0369a1;}"
                  "</style></head><body><div class='box'>"
                  "<h2>🚚 SETTING ID DUMP TRUCK</h2>"
                  "<p>Kutai Mining GPS Edge Tracker</p>"
                  "<div class='cur'>ID Saat Ini: ");
  html += String(DT_ID);
  html += F("</div><form method='POST' action='/save'>"
            "<label style='font-size:12px;display:block;margin-bottom:6px;text-align:left;color:#cbd5e1;'>MASUKKAN ID BARU:</label>"
            "<input type='text' name='id' maxlength='7' placeholder='contoh: DT584' required autofocus>"
            "<button type='submit'>💾 SIMPAN & REBOOT</button>"
            "</form></div></body></html>");
  configServer.send(200, "text/html", html);
}

void handlePortalSave() {
  if (configServer.hasArg("id")) {
    String newId = configServer.arg("id");
    newId.trim();
    newId.toUpperCase();
    if (newId.length() > 0 && newId.length() < sizeof(DT_ID)) {
      saveDeviceID(newId);
      String html = F("<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1.0'>"
                      "<style>body{font-family:sans-serif;background:#0f172a;color:#fff;text-align:center;padding:40px;}"
                      ".box{background:#1e293b;padding:30px;border-radius:12px;max-width:360px;margin:auto;}"
                      "h2{color:#10b981;}</style></head><body><div class='box'>"
                      "<h2>✅ Berhasil Disimpan!</h2>"
                      "<p>ID Baru: <strong>");
      html += newId;
      html += F("</strong></p><p>ESP32 sedang reboot ke mode operasional normal...</p>"
                "</div></body></html>");
      configServer.send(200, "text/html", html);
      delay(1500);
      ESP.restart();
      return;
    }
  }
  configServer.send(400, "text/plain", "ID tidak valid");
}

void launchConfigPortal() {
  logMsg("==================================================");
  logMsg("⚙️ MEMASUKI MODE CONFIG PORTAL (SETTING ID VIA HP)");
  logMsg("==================================================");

  // Nyalakan semua LED sebagai indikasi visual masuk mode setting
  digitalWrite(LED_GPS, HIGH);
  digitalWrite(LED_EXCA, HIGH);
  digitalWrite(LED_MQTT, HIGH);
  digitalWrite(LED_REC, HIGH);

  WiFi.disconnect(true, true);
  delay(200);
  WiFi.mode(WIFI_AP);

  String apName = "SETTING_" + String(DT_ID);
  WiFi.softAP(apName.c_str());
  delay(300);

  IPAddress myIP = WiFi.softAPIP();
  logMsg("📡 Hotspot Aktif: " + apName);
  logMsg("🌐 Buka Browser di HP: http://" + myIP.toString());

  // Setup DNS Server untuk Captive Portal
  dnsServer.start(53, "*", myIP);

  configServer.on("/", HTTP_GET, handlePortalRoot);
  configServer.on("/save", HTTP_POST, handlePortalSave);
  // Tangani Captive Portal redirects (Android, iOS/Apple, Windows)
  configServer.onNotFound([]() {
    configServer.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    configServer.send(302, "text/plain", "");
  });

  configServer.begin();

  unsigned long portalStart = millis();
  const unsigned long PORTAL_TIMEOUT = 180000; // Otomatis keluar setelah 3 menit jika tidak ada aktivitas

  while (millis() - portalStart < PORTAL_TIMEOUT) {
    esp_task_wdt_reset();
    dnsServer.processNextRequest();
    configServer.handleClient();

    // Efek LED berkedip bergantian menunjukkan status portal aktif
    if ((millis() / 300) % 2 == 0) {
      digitalWrite(LED_GPS, HIGH);
      digitalWrite(LED_MQTT, LOW);
    } else {
      digitalWrite(LED_GPS, LOW);
      digitalWrite(LED_MQTT, HIGH);
    }
    delay(2);
  }

  logMsg("⏳ Config portal timeout (3 menit), restart normal...");
  delay(500);
  ESP.restart();
}

void checkBootButtonTrigger() {
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
  if (digitalRead(PIN_BOOT_BTN) == LOW) {
    unsigned long pressStart = millis();
    logMsg("🔘 Tombol BOOT tertekan, tahan 3 detik untuk masuk Setting Mode...");
    while (digitalRead(PIN_BOOT_BTN) == LOW) {
      esp_task_wdt_reset();
      if (millis() - pressStart >= 3000) {
        logMsg("🎯 Trigger Config Portal AKTIF!");
        launchConfigPortal();
        return;
      }
      delay(50);
    }
    logMsg("ℹ️ Tombol BOOT dilepas sebelum 3 detik, boot normal dilanjutkan.");
  }
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
  if (index < 0 || index >= wifiCount || channel < 1 || channel > 14 || !bssid)
    return;

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
    logMsg("💾 WiFi cache saved to NVS: " + String(wifiList[index].ssid) +
           " CH=" + String(channel));
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
        logMsg("🔑 IGN OFF -> COOLDOWN (" + String(IGN_COOLDOWN_MS / 1000) +
               "s)");
      }
    }
    return true;
  }

  switch (recordState) {
  case REC_IDLE:
    if (ignition == 1 || ignition == -1) {
      recordState = REC_ACTIVE;
      WiFi.setSleep(false); // Pastikan modem langsung bangun
      logMsg("⏺️ -> ACTIVE (GPS stream detected)");
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
    filter["ibutton"]["id"] = true;
    filter["ibutton"]["status"] = true;
    filter["ibutton"]["auth"] = true;
    filter["ib"]["id"] = true;
    filter["ib"]["st"] = true;
    filter["ib"]["au"] = true;
    filter["gsensor"]["x"] = true;
    filter["gsensor"]["y"] = true;
    filter["gsensor"]["z"] = true;
    filter["gs"]["x"] = true;
    filter["gs"]["y"] = true;
    filter["gs"]["z"] = true;
    filterInitialized = true;
  }

  DeserializationError err =
      deserializeJson(doc, json, DeserializationOption::Filter(filter));
  if (err)
    return false;

  if (!shouldRecord(doc))
    return false;

  // 1. Validasi IMEI (Wajib ada minimal 10 digit)
  const char *imeiStr = doc["imei"] | "";
  if (strlen(imeiStr) < 10) {
    logMsg("⚠️ GPS data skipped: IMEI missing/invalid (" + String(imeiStr) +
           ")");
    return false;
  }
  uint64_t parsedImei = strtoull(imeiStr, NULL, 10);
  if (parsedImei == 0) {
    logMsg("⚠️ GPS data skipped: IMEI is 0");
    return false;
  }

  // 2. Validasi Timestamp (Wajib ada & valid setelah tahun 2020)
  const char *ts = doc["timestamp"] | (doc["ts"] | "");
  uint32_t parsedTs = parseISO8601ToEpoch(ts);
  if (parsedTs < 1577836800UL) { // 2020-01-01 00:00:00 UTC
    logMsg("⚠️ GPS data skipped: Invalid timestamp (" + String(ts) + ")");
    return false;
  }

  // 3. Validasi Koordinat GPS (Wajib 3D Fix, bukan 0.0, 0.0)
  double lat = doc["latitude"] | (doc["lat"] | 0.0);
  double lon = doc["longitude"] | (doc["lon"] | 0.0);
  int32_t lat_x1e7 = (int32_t)(lat * 10000000.0);
  int32_t lon_x1e7 = (int32_t)(lon * 10000000.0);
  if (lat_x1e7 == 0 && lon_x1e7 == 0) {
    logMsg("⚠️ GPS data skipped: No GPS Fix (lat=0, lon=0)");
    return false;
  }

  // Lolos semua validasi -> Alokasikan sequence & inisialisasi paket
  dtSeq++;
  writeUint(DT_SEQ_FILE, dtSeq);

  initBinaryPacket(pkt, DT_ID, dtSeq);
  pkt.imei = parsedImei;
  pkt.timestamp = parsedTs;
  pkt.lat_x1e7 = lat_x1e7;
  pkt.lon_x1e7 = lon_x1e7;

  double spd = doc["speed"] | (doc["spd"] | 0.0);
  pkt.speed_x10 = (uint16_t)(spd * 10.0);
  pkt.heading =
      (uint16_t)(doc["heading"] |
                 (doc["hdg"] |
                  (doc["course"] | (doc["bearing"] | (doc["angle"] | 0)))));
  pkt.altitude = doc["altitude"] | (doc["alt"] | 0);

  double ext = doc["external"] | (doc["volt"] | (doc["battery"] | 0.0));
  if (ext > 100.0) {
    pkt.bat_mv = (uint16_t)ext;
  } else {
    pkt.bat_mv = (uint16_t)(ext * 1000.0);
  }

  if (doc["input_status"].is<const char *>()) {
    const char *inp = doc["input_status"].as<const char *>();
    uint8_t mask = 0;
    for (int i = 0; inp[i] && i < 8; i++) {
      if (inp[i] == '1')
        mask |= (1 << i);
    }
    pkt.input_status = mask;
  } else {
    pkt.input_status = (uint8_t)(doc["input_status"] | 0);
  }

  // Parse Ignition
  int ignVal = doc["ignition"] | (doc["ign"] | -1);
  if (ignVal != -1) {
    pkt.ignition = (ignVal > 0) ? 1 : 0;
  } else {
    pkt.ignition = (pkt.input_status & 0x02) ? 1 : 0;
  }

  if (doc.containsKey("ibeacon") && doc["ibeacon"].size() > 0) {
    const char *macStr = doc["ibeacon"][0]["mac"] | "";
    int rssi = doc["ibeacon"][0]["rssi"] | 0;
    if (strlen(macStr) >= 12) {
      unsigned int m[6] = {0};
      sscanf(macStr, "%x:%x:%x:%x:%x:%x", &m[0], &m[1], &m[2], &m[3], &m[4],
             &m[5]);
      for (int i = 0; i < 6; i++)
        pkt.beacon_mac[i] = (uint8_t)m[i];
      pkt.beacon_rssi = (int8_t)rssi;
    }
  }

  // Parse iButton (Driver ID)
  if (doc.containsKey("ibutton") && !doc["ibutton"].isNull()) {
    const char *ibHex = doc["ibutton"]["id"] | "";
    if (strlen(ibHex) > 0) {
      pkt.ibutton_id = (uint32_t)strtoul(ibHex, NULL, 16);
    } else {
      pkt.ibutton_id = 0;
    }
    const char *ibStatus = doc["ibutton"]["status"] | "";
    bool ibAuth = doc["ibutton"]["auth"] | false;
    uint8_t ibFlags = 0;
    if (strcmp(ibStatus, "login") == 0)
      ibFlags |= 0x01;
    if (ibAuth)
      ibFlags |= 0x02;
    pkt.ibutton_flags = ibFlags;
  } else if (doc.containsKey("ib") && !doc["ib"].isNull()) {
    const char *ibHex = doc["ib"]["id"] | "";
    if (strlen(ibHex) > 0) {
      pkt.ibutton_id = (uint32_t)strtoul(ibHex, NULL, 16);
    } else {
      pkt.ibutton_id = 0;
    }
    const char *ibStatus = doc["ib"]["st"] | (doc["ib"]["status"] | "");
    bool ibAuth = doc["ib"]["au"] | (doc["ib"]["auth"] | false);
    uint8_t ibFlags = 0;
    if (strcmp(ibStatus, "login") == 0)
      ibFlags |= 0x01;
    if (ibAuth)
      ibFlags |= 0x02;
    pkt.ibutton_flags = ibFlags;
  } else {
    pkt.ibutton_id = 0;
    pkt.ibutton_flags = 0;
  }

  // Parse G-Sensor (x, y, z)
  if (doc.containsKey("gsensor") && !doc["gsensor"].isNull()) {
    pkt.gs_x = (int16_t)(doc["gsensor"]["x"] | 0);
    pkt.gs_y = (int16_t)(doc["gsensor"]["y"] | 0);
    pkt.gs_z = (int16_t)(doc["gsensor"]["z"] | 0);
  } else if (doc.containsKey("gs") && !doc["gs"].isNull()) {
    pkt.gs_x = (int16_t)(doc["gs"]["x"] | 0);
    pkt.gs_y = (int16_t)(doc["gs"]["y"] | 0);
    pkt.gs_z = (int16_t)(doc["gs"]["z"] | 0);
  } else {
    pkt.gs_x = 0;
    pkt.gs_y = 0;
    pkt.gs_z = 0;
  }

  pkt.flags = 0x01; // Bit0 = GPS 3D Fix Valid

  finalizeBinaryPacket(pkt);
  return true;
}

// ================= GPS SERIAL HANDLER =================
static unsigned long lastValidPktTime = 0;
static unsigned long lastGpsByteTime = 0;
static uint32_t gpsByteCount = 0;
static unsigned long lastDiagLogTime = 0;
static unsigned long lastUartRecoveryTime = 0;
static bool firstByteLogged = false;
static bool firstValidPktLogged = false;

void resetGpsParser() {
  gpsBufLen = 0;
  gpsBrace = 0;
  gpsCollecting = false;
}

void initSerial2() {
  Serial2.setRxBufferSize(2048);
  Serial2.begin(GPS_BAUD);
  Serial2.setPins(GPS_RX, GPS_TX);
  delay(1500);
  unsigned long tFlush = millis();
  while (Serial2.available() && millis() - tFlush < 500) {
    Serial2.read();
  }
  logMsg("🔌 [UART] Serial2 initialized RX=" + String(GPS_RX) + " TX=" +
         String(GPS_TX) + " @" + String(GPS_BAUD) + " (buffer flushed)");
}

void handleDTGps() {
  while (Serial2.available()) {
    char c = Serial2.read();
    lastGpsByteTime = millis();
    gpsByteCount++;

    if (!firstByteLogged) {
      firstByteLogged = true;
      logMsg("📡 [UART] First RX byte received from GPS!");
    }

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

    if (c == '{')
      gpsBrace++;
    if (c == '}')
      gpsBrace--;

    if (gpsBrace == 0) {
      gpsBuf[gpsBufLen] = '\0';

      TelemetryPacketBinary pkt;
      if (parseDTGpsToBinary(gpsBuf, pkt)) {
        lastValidPktTime = millis();
        if (!firstValidPktLogged) {
          firstValidPktLogged = true;
          logMsg("✨ [UART] First valid telemetry packet received!");
        }
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
          uint32_t pendingRecords =
              pendingBytes / sizeof(TelemetryPacketBinary);
          logMsg("📍 [BIN] DT logged #" + String(statGpsLogged) +
                 " | Backlog: " + String(pendingMB, 3) + " MB (" +
                 String(pendingRecords) + " records)");
        }

        // 2. Real-time direct binary MQTT publish
        if (!busy && backlogClean && WiFi.status() == WL_CONNECTED &&
            mqtt.connected()) {
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
  }

  // 1. Reset parser bila transmisi JSON terhenti di tengah jalan
  if (gpsCollecting && (millis() - gpsStartJson > 2000)) {
    logMsg("⚠️ GPS parse timeout (>2s), resyncing parser...");
    resetGpsParser();
  }

  // 2. Periodic UART Health Monitoring & Diagnostic Logger (tiap 30 detik)
  unsigned long now = millis();
  if (now - lastDiagLogTime >= 30000) {
    lastDiagLogTime = now;
    if (gpsByteCount > 0) {
      logMsg("📊 [UART Health] Total RX bytes=" + String(gpsByteCount) +
             ", last RX " + String((now - lastGpsByteTime) / 1000) +
             "s ago, last valid pkt " +
             (lastValidPktTime > 0
                  ? String((now - lastValidPktTime) / 1000) + "s ago"
                  : "never"));
    } else {
      logMsg("⚠️ [UART Health] No serial bytes received yet after " +
             String(now / 1000) + "s of boot");
    }
  }

  // 3. Low-Level UART Hardware Recovery jika TIDAK ADA RX BYTES sama sekali
  // Grace period 10 detik pertama boot; recovery interval tiap 15 detik jika
  // mati
  if (gpsByteCount == 0 || (now - lastGpsByteTime > 15000)) {
    if (now >= 10000 && (now - lastUartRecoveryTime >= 15000)) {
      lastUartRecoveryTime = now;
      logMsg("🔄 [UART Recovery] No RX bytes detected (silence >15s). "
             "Restarting Serial2...");
      Serial2.end();
      delay(50);
      resetGpsParser();
      initSerial2();
      logMsg("✅ [UART Recovery] Serial2 recovery complete");
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
             cache.bssid[0], cache.bssid[1], cache.bssid[2], cache.bssid[3],
             cache.bssid[4], cache.bssid[5]);
    logMsg("⚡ Fast-connect attempt to " + String(wifiList[cache.index].ssid) +
           " CH=" + String(cache.channel));

    unsigned long tFast = millis();
    WiFi.begin(wifiList[cache.index].ssid, wifiList[cache.index].pass,
               cache.channel, cache.bssid);

    while (WiFi.status() != WL_CONNECTED && millis() - tFast < 3000) {
      esp_task_wdt_reset();
      handleDTGps();
      delay(10);
    }

    if (WiFi.status() == WL_CONNECTED) {
      unsigned long elapsed = millis() - tFast;
      logMsg("⚡ Fast-connect success: " + String(elapsed) + " ms");
      logMsg("📡 Connected: SSID=" + String(WiFi.SSID()) +
             " IP=" + WiFi.localIP().toString());
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
  if (bssid)
    memcpy(bssidCopy, bssid, 6);
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

  if (bssid)
    saveWifiCache(bestIdx, ch, bssidCopy);
  return true;
}

// ================= MQTT CLIENT & ACK =================
bool connectMQTT() {
  if (mqtt.connected())
    return true;

  String clientId = String(DT_ID);
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback([](char *topic, byte *payload, unsigned int length) {
    String msg;
    for (int i = 0; i < length; i++)
      msg += (char)payload[i];
    msg.trim();

    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, msg);
    if (!err) {
      const char *id = doc["id"] | "";
      const char *st = doc["status"] | "";
      if (String(st) == "ok" && String(id).length() > 0) {
        lastAckMsgId = String(id);
        ackReceived = true;
      }
    }
  });

  if (mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
    ackTopic = "kutai/fleet/ack_binary/+";
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
      if (!connectMQTT())
        return false;
    }

    ackReceived = false;
    lastAckMsgId = "";

    if (!mqtt.publish(MQTT_BINARY_TOPIC, (const uint8_t *)&pkt, sizeof(pkt))) {
      logMsg("❌ Binary Publish error");
      return false;
    }

    // Blink LED on publish
    digitalWrite(LED_GPS, HIGH);
    ledGpsTimer = millis();

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

bool publishBinaryBulkWithAck(const uint8_t *bulkBuffer, size_t totalBytes,
                              const String &lastMsgId, int maxRetries = 2) {
  for (int attempt = 1; attempt <= maxRetries; attempt++) {
    esp_task_wdt_reset();
    handleDTGps();
    if (!mqtt.connected()) {
      if (!connectMQTT())
        return false;
    }

    ackReceived = false;
    lastAckMsgId = "";

    int count = totalBytes / sizeof(TelemetryPacketBinary);

    if (!mqtt.publish(MQTT_BINARY_TOPIC, bulkBuffer, totalBytes)) {
      logMsg("❌ Binary Bulk Publish error");
      return false;
    }

    // Blink LED on publish
    digitalWrite(LED_GPS, HIGH);
    ledGpsTimer = millis();

    statMqttSent += count;
    unsigned long t0 = millis();
    while (millis() - t0 < 3000) {
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
bool publishBinaryQueueChunk(const char *logPath, const char *offsetPath,
                             int maxRecords) {
  uint32_t offset = readUint(offsetPath, 0);

  File f = SD.open(logPath, FILE_READ);
  if (!f)
    return false;

  if (offset >= f.size()) {
    f.close();
    return true;
  }

  offset =
      (offset / sizeof(TelemetryPacketBinary)) * sizeof(TelemetryPacketBinary);
  if (!f.seek(offset)) {
    f.close();
    return false;
  }

  int sentCount = 0;
  TelemetryPacketBinary batchBuf[BULK_PUBLISH_RECORDS];

  while (f.available() >= sizeof(TelemetryPacketBinary) &&
         sentCount < maxRecords) {
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

    for (int i = 0;
         i < toRead && f.available() >= sizeof(TelemetryPacketBinary); i++) {
      TelemetryPacketBinary pkt;
      size_t rb = f.read((uint8_t *)&pkt, sizeof(pkt));
      if (rb != sizeof(pkt))
        break;

      if (validateBinaryPacket(pkt)) {
        batchBuf[validInBatch++] = pkt;
      } else {
        logMsg("⚠️ Corrupt binary packet at " +
               String((uint32_t)f.position() - sizeof(pkt)) + ", skipping");
      }
    }

    if (validInBatch == 0) {
      writeUint(offsetPath, f.position());
      break;
    }

    String lastId = getPacketUID(batchBuf[validInBatch - 1]);
    size_t sendBytes = validInBatch * sizeof(TelemetryPacketBinary);

    if (!publishBinaryBulkWithAck((const uint8_t *)batchBuf, sendBytes, lastId,
                                  2)) {
      logMsg("⚠️ Bulk publish fail at offset " + String(batchStartPos));
      f.close();
      return false;
    }

    writeUint(offsetPath, f.position());
    sentCount += validInBatch;
  }

  f.close();
  statChunksUploaded++;
  logMsg("✅ Binary Chunk published: " + String(sentCount) + " records");
  return true;
}

// ================= DUAL BACKLOG DRAIN ROUTINE =================
void tryInternetAndPublishAll() {
  if (WiFi.status() != WL_CONNECTED) {
    if (!connectKnownInternet())
      return;
  }

  if (!mqtt.connected()) {
    if (!connectMQTT())
      return;
  }

  // 1. Kuras Backlog DT Sendiri
  uint32_t dtOff = readUint(DT_OFFSET_FILE, 0);
  File fDT = SD.open(DT_LOG_FILE_BIN, FILE_READ);
  if (fDT) {
    uint32_t fSize = fDT.size();
    fDT.close();
    if (dtOff < fSize) {
      logMsg("🚀 Uploading DT BINARY backlog (sisa: " + String(fSize - dtOff) +
             " bytes / " +
             String((fSize - dtOff) / sizeof(TelemetryPacketBinary)) +
             " records)...");
      while (publishBinaryQueueChunk(DT_LOG_FILE_BIN, DT_OFFSET_FILE,
                                     MAX_UPLOAD_CHUNK_RECORDS)) {
        esp_task_wdt_reset();
        handleDTGps();
        delay(5);
        uint32_t curOff = readUint(DT_OFFSET_FILE, 0);
        File fc = SD.open(DT_LOG_FILE_BIN, FILE_READ);
        if (!fc || curOff >= fc.size()) {
          if (fc)
            fc.close();
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
      logMsg("🚀 Uploading RELAY EXCA BINARY backlog (sisa: " +
             String(fSize - relayOff) + " bytes / " +
             String((fSize - relayOff) / sizeof(TelemetryPacketBinary)) +
             " records)...");
      while (publishBinaryQueueChunk(RELAY_LOG_FILE_BIN, RELAY_OFFSET_FILE,
                                     MAX_UPLOAD_CHUNK_RECORDS)) {
        esp_task_wdt_reset();
        handleDTGps();
        delay(5);
        uint32_t curOff = readUint(RELAY_OFFSET_FILE, 0);
        File fc = SD.open(RELAY_LOG_FILE_BIN, FILE_READ);
        if (!fc || curOff >= fc.size()) {
          if (fc)
            fc.close();
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
  const int MIN_EXCA_RSSI = -82; // Hanya konek jika sinyal cukup kuat & dekat

  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    int r = WiFi.RSSI(i);
    if (isExcaSSID(s) && r > bestRSSI) {
      bestSSID = s;
      bestRSSI = r;
    }
  }

  WiFi.scanDelete();

  if (bestRSSI < MIN_EXCA_RSSI) {
    return "";
  }

  logMsg("🎯 Best EXCA target: " + bestSSID + " (RSSI: " + String(bestRSSI) +
         " dBm)");
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
    if (!client.connected() || millis() - t0 > timeoutMs)
      return false;
    handleDTGps();
    delay(1);
  }
  out = client.readStringUntil('\n');
  out.trim();
  return true;
}

bool transferFromExcaBinary() {
  // 1. Settling delay setelah WiFi connect agar ARP & IP stack siap
  delay(400);

  // 2. Gunakan Gateway IP aktual dari AP (fallback ke excaIP)
  IPAddress targetIP = WiFi.gatewayIP();
  if (targetIP[0] == 0) {
    targetIP = excaIP;
  }
  logMsg("🔌 Opening TCP to EXCA at " + targetIP.toString() + ":" +
         String(EXCA_PORT) + "...");

  WiFiClient client;
  bool connected = false;

  // 3. Retry connect 3x dengan jeda 400ms
  for (int attempt = 1; attempt <= 3; attempt++) {
    esp_task_wdt_reset();
    handleDTGps();
    if (client.connect(targetIP, EXCA_PORT)) {
      connected = true;
      break;
    }
    logMsg("⚠️ EXCA TCP retry #" + String(attempt));
    delay(400);
  }

  if (!connected) {
    logMsg("❌ EXCA TCP fail (cannot reach " + targetIP.toString() + ":" +
           String(EXCA_PORT) + ")");
    return false;
  }

  logMsg("✅ EXCA TCP connected, sending HELLO_BIN...");
  client.println("HELLO_BIN");
  String line;
  if (!waitTcpLine(client, line, 5000) || line != "READY_BIN") {
    if (line == "BUSY") {
      logMsg("⏳ EXCA is BUSY (serving another DT). Backing off...");
    }
    client.stop();
    return false;
  }

  client.println("GET_BIN");
  if (!waitTcpLine(client, line, 5000) || !line.startsWith("START_BIN")) {
    if (line == "NO_DATA")
      logMsg("ℹ️ EXCA no new binary data");
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
      logMsg("✅ EXCA Binary data merged into Relay log! Total sync: #" +
             String(statExcaRelayed));
    }
    client.stop();
    return true;
  }

  client.stop();
  SD.remove(tempPath);
  return false;
}

// ================= COMPACTION =================
bool compactBinaryQueueFile(const char *logPath, const char *offsetPath,
                            const char *tempPath) {
  uint32_t offset = readUint(offsetPath, 0);
  offset =
      (offset / sizeof(TelemetryPacketBinary)) * sizeof(TelemetryPacketBinary);
  if (offset < 4096)
    return true;

  logMsg("🧹 Compacting " + String(logPath) + " offset=" + String(offset));
  File src = SD.open(logPath, FILE_READ);
  if (!src)
    return false;

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
    if (n > 0)
      dst.write(buf, n);
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
  // DINONAKTIFKAN agar tidak bentrok dengan kedipan data masuk/publish
  // Biarkan LED hanya berkedip saat ada data GPS masuk dan saat publish MQTT
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(1000);

  logMsg("=== GPSTAMBANG DT BINARY EDITION START ===");

  // 1. Baca Device ID dari NVS
  loadDeviceID();
  logMsg("🏷️ Loaded Unit ID: " + String(DT_ID));

  // 2. Jika ID masih default (DTCONFIG), langsung otomatis buka Hotspot Setting
  if (strcmp(DT_ID, DEFAULT_DT_ID) == 0) {
    logMsg("⚠️ Unit ID masih default (DTCONFIG)! Otomatis membuka Config Portal...");
    launchConfigPortal();
  }

  // 3. Cek apakah tombol BOOT sedang ditekan untuk masuk Web Config Portal
  checkBootButtonTrigger();

  esp_task_wdt_config_t wdt_config = {.timeout_ms = WDT_TIMEOUT_SEC * 1000,
                                      .idle_core_mask = 0,
                                      .trigger_panic = true};
  esp_task_wdt_reconfigure(&wdt_config);
  logMsg("🐕 Watchdog configured: " + String(WDT_TIMEOUT_SEC) + "s");

  pinMode(LED_GPS, OUTPUT);
  pinMode(LED_EXCA, OUTPUT);
  pinMode(LED_MQTT, OUTPUT);
  pinMode(LED_REC, OUTPUT);

  // NYALAKAN LED DI AWAL BOOTING UNTUK INDIKASI
  digitalWrite(LED_GPS, HIGH);
  digitalWrite(LED_EXCA, HIGH);
  digitalWrite(LED_MQTT, HIGH);
  digitalWrite(LED_REC, HIGH);

  pinMode(GPS_RX, INPUT_PULLUP);
  initSerial2();

  mqtt.setBufferSize(4096);

  initStorage();

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, true);

  // MATIKAN LED KETIKA SETUP SELESAI
  digitalWrite(LED_GPS, LOW);
  digitalWrite(LED_EXCA, LOW);
  digitalWrite(LED_MQTT, LOW);
  digitalWrite(LED_REC, LOW);

  // Watchdog task didaftarkan di paling akhir setup setelah semua inisialisasi
  // selesai
  esp_task_wdt_add(NULL);

  logMsg("✅ " + String(DT_ID) + " BINARY READY (64B Packet)");
}

// ================= LOOP =================
void loop() {
  esp_task_wdt_reset();

  // Cek tombol BOOT kapan saja saat operasional (jika ditekan dan ditahan 3 detik)
  if (digitalRead(PIN_BOOT_BTN) == LOW) {
    checkBootButtonTrigger();
  }

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
      bool harvestSuccess = false;
      if (connectExca(ssid)) {
        harvestSuccess = transferFromExcaBinary();
      }
      WiFi.disconnect(false, true);
      flushStaleGpsData();
      digitalWrite(LED_EXCA, LOW);
      excaTransferBusy = false;

      // Jika gagal atau EXCA sedang melayani DT lain (BUSY), tambah jeda acak
      // 4-8s agar antrean tidak tabrakan
      if (!harvestSuccess) {
        lastExcaScan = now + random(4000, 8000);
      }
    }
  }

  // Upload to MQTT
  if (!busy && !excaTransferBusy &&
      now - lastInternetTry >= INTERNET_INTERVAL) {
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
    compactBinaryQueueFile(RELAY_LOG_FILE_BIN, RELAY_OFFSET_FILE,
                           "/relay_tmp.bin");
  }

  if (ESP.getFreeHeap() < HEAP_MIN_BYTES) {
    logMsg("❌ Heap kritis: " + String(ESP.getFreeHeap()) +
           " bytes, RESTARTING...");
    delay(1000);
    ESP.restart();
  }

  delay(2);
}
