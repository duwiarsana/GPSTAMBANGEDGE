#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <SD.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>

// ================= WATCHDOG =================
#define WDT_TIMEOUT_SEC  30       // Hardware watchdog: 30 detik
#define GPS_TIMEOUT_MS   300000   // GPS timeout: 5 menit tanpa data → restart
#define HEAP_MIN_BYTES   20000    // Heap minimum: 20KB → restart
#define SD_RETRY_INTERVAL 60000   // SD re-init: coba tiap 1 menit

// ================= PIN =================
#define GPS_RX 16
#define GPS_TX 17
#define SD_CS  5
#define LED_LOG 2

// ================= ID DEVICE =================
const char* DT_ID = "DT01";

// ================= UART =================
#define GPS_BAUD 115200

// ================= WIFI AP =================
const char* AP_SSID = "DT01_MONITOR";
const char* AP_PASS = "12345678";

WebServer webServer(80);

// ================= FILE =================
const char* LOG_FILE = "/dt_log.jsonl";
const char* SEQ_FILE = "/dt_seq.txt";

// ================= PARSER =================
#define BUF_SIZE 4096
char buf[BUF_SIZE];
int bufLen = 0;
int brace = 0;
bool collecting = false;
unsigned long startJson = 0;

// ================= STATE =================
File logFile;
uint32_t seq = 0;
uint32_t totalRecords = 0;
bool sdReady = false;
unsigned long ledLogTimer = 0;

// ================= WATCHDOG STATE =================
unsigned long lastGpsReceived = 0;
unsigned long lastSdRetry = 0;
uint32_t restartCount = 0;
const char* RESTART_FILE = "/restart_count.txt";

// ================= HISTORY RING BUFFER =================
#define MAX_HISTORY 30
String history[MAX_HISTORY];
int historyHead = 0;
int historyCount = 0;

String latestJson = "{}";

// ================= HTML PAGE =================
const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="id">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1">
  <title>DT01 Monitor</title>
  <style>
    *{margin:0;padding:0;box-sizing:border-box}
    body{font-family:-apple-system,system-ui,sans-serif;background:#0b0d17;color:#e0e0e0;padding:12px;max-width:600px;margin:0 auto;-webkit-font-smoothing:antialiased}

    .refresh-bar{height:3px;border-radius:2px;margin-bottom:10px;overflow:hidden;background:#1a1d2e}
    .refresh-bar .fill{height:100%;background:linear-gradient(90deg,#ff9500,#ff5e3a);animation:shrink 3s linear infinite}
    @keyframes shrink{0%{width:100%}100%{width:0%}}

    .header{text-align:center;padding:18px 12px;background:linear-gradient(135deg,#1a1008,#2a1810);border-radius:14px;margin-bottom:14px;border:1px solid #3a2518}
    .header h1{color:#ff9500;font-size:1.3em;letter-spacing:1px}
    .header .sub{color:#667;font-size:0.75em;margin-top:4px}
    .header .live{display:inline-block;width:8px;height:8px;border-radius:50%;background:#ff9500;box-shadow:0 0 8px #ff9500;margin-right:6px;animation:pulse 2s infinite}
    @keyframes pulse{0%,100%{opacity:1}50%{opacity:0.4}}

    .card{background:#111528;border-radius:12px;padding:14px;margin-bottom:12px;border:1px solid #1e2744}
    .card h2{color:#ff9500;font-size:0.9em;margin-bottom:10px;padding-bottom:8px;border-bottom:1px solid #1e2744;letter-spacing:0.5px}

    .stat-grid{display:grid;grid-template-columns:1fr 1fr;gap:8px}
    .stat{background:#0b0e1a;border-radius:8px;padding:10px;text-align:center}
    .stat .label{font-size:0.65em;color:#667;text-transform:uppercase;letter-spacing:1px}
    .stat .value{font-size:1.15em;font-weight:700;color:#ff9500;margin-top:4px}
    .stat .value.warn{color:#ff5e3a}

    .field{display:flex;justify-content:space-between;align-items:center;padding:8px 0;border-bottom:1px solid #0b0e1a}
    .field:last-child{border-bottom:none}
    .field .lbl{color:#667;font-size:0.8em}
    .field .val{color:#fff;font-weight:500;font-size:0.85em;text-align:right;max-width:60%;word-break:break-all}
    .field .val.coord{color:#ff9500;font-size:1em;font-weight:700}

    .log-wrap{overflow-x:auto;-webkit-overflow-scrolling:touch}
    table{width:100%;font-size:0.7em;border-collapse:collapse}
    th{background:#1a1008;padding:8px 6px;text-align:left;color:#ff9500;position:sticky;top:0;font-weight:600}
    td{padding:6px;border-bottom:1px solid #0f1223;white-space:nowrap}
    tr:hover td{background:#1a1008}

    .badge{display:inline-block;padding:2px 6px;border-radius:4px;font-size:0.7em;font-weight:600}
    .badge-ok{background:#00ff8822;color:#00ff88}
    .badge-fail{background:#ff444422;color:#ff4455}

    .empty{text-align:center;color:#445;padding:20px;font-size:0.85em}

    .footer{text-align:center;color:#334;font-size:0.65em;padding:12px 0;margin-top:8px}
  </style>
</head>
<body>

  <div class="refresh-bar"><div class="fill"></div></div>

  <div class="header">
    <h1>🚛 DT01 MONITOR</h1>
    <div class="sub"><span class="live"></span>GPS Logger Dashboard — Live</div>
  </div>

  <div class="card">
    <h2>⚙️ System Status</h2>
    <div class="stat-grid">
      <div class="stat">
        <div class="label">Uptime</div>
        <div class="value" id="uptime">—</div>
      </div>
      <div class="stat">
        <div class="label">Total Records</div>
        <div class="value" id="records">—</div>
      </div>
      <div class="stat">
        <div class="label">Free Memory</div>
        <div class="value" id="heap">—</div>
      </div>
      <div class="stat">
        <div class="label">SD Card</div>
        <div class="value" id="sd">—</div>
      </div>
    </div>
  </div>

  <div class="card">
    <h2>📍 Data GPS Terbaru</h2>
    <div id="gpsFields">
      <div class="empty">Menunggu data GPS...</div>
    </div>
  </div>

  <div class="card">
    <h2>📋 Riwayat Log <span style="color:#667;font-size:0.85em" id="logInfo"></span></h2>
    <div class="log-wrap" style="max-height:350px;overflow-y:auto">
      <table>
        <thead><tr><th>#</th><th>Waktu</th><th>Lat</th><th>Lon</th><th>Speed</th><th>Sat</th></tr></thead>
        <tbody id="logBody"><tr><td colspan="6" class="empty">Memuat...</td></tr></tbody>
      </table>
    </div>
  </div>

  <div class="footer">DT01 GPS Logger &copy; 2026 — Duwi Arsana</div>

<script>
function fmtUp(ms){
  var s=Math.floor(ms/1000),d=Math.floor(s/86400);s%=86400;
  var h=Math.floor(s/3600);s%=3600;var m=Math.floor(s/60);s%=60;
  return (d>0?d+'d ':'')+h+'h '+m+'m '+s+'s';
}

function update(){
  fetch('/api/status').then(r=>r.json()).then(d=>{
    document.getElementById('uptime').textContent=fmtUp(d.uptime);
    document.getElementById('records').textContent=d.totalRecords.toLocaleString();
    var heapKB=(d.freeHeap/1024).toFixed(0);
    var heapEl=document.getElementById('heap');
    heapEl.textContent=heapKB+'KB';
    heapEl.className='value'+(heapKB<30?' warn':'');
    document.getElementById('sd').innerHTML=d.sdReady
      ?'<span class="badge badge-ok">OK</span>'
      :'<span class="badge badge-fail">FAIL</span>';
  }).catch(()=>{});

  fetch('/api/latest').then(r=>r.json()).then(d=>{
    var keys=[
      ['imei','📱 IMEI',''],
      ['timestamp','🕐 Waktu',''],
      ['latitude','🌍 Latitude','coord'],
      ['longitude','🌍 Longitude','coord'],
      ['speed','🚀 Kecepatan (km/h)','coord'],
      ['satellites','🛰️ Satelit','coord'],
      ['protocol','📡 Protocol',''],
      ['model','📟 Model',''],
      ['msg_id','🆔 Msg ID',''],
      ['source','🏷️ Source','']
    ];
    var h='';
    for(var i=0;i<keys.length;i++){
      var k=keys[i][0],lbl=keys[i][1],cls=keys[i][2];
      if(d[k]!==undefined&&d[k]!==null){
        h+='<div class="field"><span class="lbl">'+lbl+'</span><span class="val'+(cls?' '+cls:'')+'">'+d[k]+'</span></div>';
      }
    }
    if(!h) h='<div class="empty">Belum ada data GPS</div>';
    document.getElementById('gpsFields').innerHTML=h;
  }).catch(()=>{});

  fetch('/api/logs?n=30').then(r=>r.json()).then(d=>{
    document.getElementById('logInfo').textContent='('+d.length+' terbaru)';
    var h='';
    for(var i=0;i<d.length;i++){
      var r=d[i];
      h+='<tr><td style="color:#445">'+(i+1)+'</td><td>'
        +(r.timestamp||'—')+'</td><td style="color:#ff9500">'
        +(r.latitude!=null?r.latitude.toFixed(6):'—')+'</td><td style="color:#ff9500">'
        +(r.longitude!=null?r.longitude.toFixed(6):'—')+'</td><td>'
        +(r.speed!=null?r.speed:'—')+'</td><td>'
        +(r.satellites!=null?r.satellites:'—')+'</td></tr>';
    }
    if(!h) h='<tr><td colspan="6" class="empty">Belum ada data</td></tr>';
    document.getElementById('logBody').innerHTML=h;
  }).catch(()=>{});
}

update();
setInterval(update,3000);
</script>
</body>
</html>
)rawliteral";

// ================= DEBUG =================
void logMsg(const String &s) {
  Serial.print("[");
  Serial.print(millis());
  Serial.print("] ");
  Serial.println(s);
}

// ================= FILE UTIL =================
uint32_t readUint(const char* path) {
  File f = SD.open(path);
  if (!f) return 0;
  String s = f.readString();
  f.close();
  return s.toInt();
}

void writeUint(const char* path, uint32_t v) {
  SD.remove(path);
  File f = SD.open(path, FILE_WRITE);
  if (f) {
    f.print(v);
    f.close();
  }
}

// ================= HISTORY =================
void addToHistory(const String &record) {
  history[historyHead] = record;
  historyHead = (historyHead + 1) % MAX_HISTORY;
  if (historyCount < MAX_HISTORY) historyCount++;
}

// ================= UID =================
String makeUID(JsonDocument &doc) {
  seq++;
  writeUint(SEQ_FILE, seq);

  String imei = doc["imei"] | "0";
  String ts   = doc["timestamp"] | "0";

  ts.replace("-", "");
  ts.replace(":", "");

  return String(DT_ID) + "-" + imei + "-" + ts + "-" + String(seq);
}

// ================= INIT SD =================
void initSD() {
  if (!SD.begin(SD_CS)) {
    logMsg("❌ SD FAIL");
    return;
  }

  sdReady = true;

  if (!SD.exists(SEQ_FILE)) writeUint(SEQ_FILE, 0);
  seq = readUint(SEQ_FILE);

  // Hitung record yang sudah ada & isi history buffer
  File f = SD.open(LOG_FILE);
  if (f) {
    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.length() > 0) {
        totalRecords++;
        addToHistory(line);
      }
    }
    f.close();

    if (historyCount > 0) {
      int lastIdx = (historyHead - 1 + MAX_HISTORY) % MAX_HISTORY;
      latestJson = history[lastIdx];
    }
  }

  logFile = SD.open(LOG_FILE, FILE_APPEND);

  logMsg("✅ SD READY, records=" + String(totalRecords) + ", seq=" + String(seq));
}

// ================= LOG =================
void appendLog(const String &line) {
  if (!logFile) {
    logFile = SD.open(LOG_FILE, FILE_APPEND);
    if (!logFile) {
      logMsg("❌ LOG FAIL");
      sdReady = false;  // tandai SD bermasalah
      return;
    }
  }

  logFile.println(line);
  logFile.flush();

  totalRecords++;
  addToHistory(line);
  latestJson = line;
  lastGpsReceived = millis();  // update watchdog GPS

  digitalWrite(LED_LOG, HIGH);
  ledLogTimer = millis();

  logMsg("📍 LOGGED #" + String(totalRecords));
}

// ================= JSON PROCESS =================
bool processJSON(const char* json, String &out) {
  StaticJsonDocument<3072> doc;

  if (deserializeJson(doc, json)) {
    logMsg("❌ JSON ERROR");
    return false;
  }

  if (!doc["imei"] || !doc["timestamp"]) {
    logMsg("❌ INVALID FIELD");
    return false;
  }

  doc["msg_id"]      = makeUID(doc);
  doc["source"]      = DT_ID;
  doc["record_type"] = "dt";

  serializeJson(doc, out);
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
      logMsg("⚠️ OVERFLOW");
      resetParser();
      continue;
    }

    if (c == '{') brace++;
    if (c == '}') brace--;

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
      logMsg("⏱️ TIMEOUT");
      resetParser();
    }
  }
}

// ================= WEB HANDLERS =================
void handleRoot() {
  webServer.send_P(200, "text/html", HTML_PAGE);
}

void handleApiStatus() {
  StaticJsonDocument<512> doc;
  doc["uptime"]        = millis();
  doc["totalRecords"]  = totalRecords;
  doc["freeHeap"]      = ESP.getFreeHeap();
  doc["sdReady"]       = sdReady;
  doc["seq"]           = seq;
  doc["deviceId"]      = DT_ID;
  doc["restartCount"]  = restartCount;
  doc["lastGpsAgo"]    = (lastGpsReceived > 0) ? (millis() - lastGpsReceived) : -1;
  doc["wdtEnabled"]    = true;

  String out;
  serializeJson(doc, out);
  webServer.send(200, "application/json", out);
}

void handleApiRestart() {
  webServer.send(200, "text/plain", "RESTARTING...");
  delay(500);
  ESP.restart();
}

void handleApiLatest() {
  webServer.send(200, "application/json", latestJson);
}

void handleApiLogs() {
  int n = 30;
  if (webServer.hasArg("n")) {
    n = webServer.arg("n").toInt();
    if (n > MAX_HISTORY) n = MAX_HISTORY;
    if (n < 1) n = 1;
  }

  int count = min(n, historyCount);

  String out = "[";
  for (int i = 0; i < count; i++) {
    int idx = (historyHead - 1 - i + MAX_HISTORY) % MAX_HISTORY;
    if (i > 0) out += ",";
    out += history[idx];
  }
  out += "]";

  webServer.send(200, "application/json", out);
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  pinMode(LED_LOG, OUTPUT);
  digitalWrite(LED_LOG, LOW);

  // Hardware Watchdog Timer
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = WDT_TIMEOUT_SEC * 1000,
    .idle_core_mask = 0,
    .trigger_panic = true
  };
  esp_task_wdt_reconfigure(&wdt_config);
  logMsg("🐕 Watchdog configured: " + String(WDT_TIMEOUT_SEC) + "s");

  Serial2.begin(GPS_BAUD);
  Serial2.setPins(GPS_RX, GPS_TX);

  delay(1500);
  while (Serial2.available()) Serial2.read();

  initSD();

  // Baca & increment restart counter
  if (sdReady) {
    restartCount = readUint(RESTART_FILE) + 1;
    writeUint(RESTART_FILE, restartCount);
    logMsg("🔄 Restart count: " + String(restartCount));
  }

  // WiFi Access Point
  WiFi.softAP(AP_SSID, AP_PASS);

  logMsg("📶 WiFi AP: " + String(AP_SSID));
  logMsg("📶 IP: " + WiFi.softAPIP().toString());

  // Web server routes
  webServer.on("/", handleRoot);
  webServer.on("/api/status", handleApiStatus);
  webServer.on("/api/latest", handleApiLatest);
  webServer.on("/api/logs", handleApiLogs);
  webServer.on("/api/restart", handleApiRestart);
  webServer.begin();

  lastGpsReceived = millis();

  // Aktifkan watchdog untuk loop task SETELAH setup selesai
  esp_task_wdt_add(NULL);

  logMsg("✅ " + String(DT_ID) + " MONITOR READY");
  logMsg("🌐 Buka http://" + WiFi.softAPIP().toString() + " di browser HP");
}

// ================= LOOP =================
void loop() {
  // Feed hardware watchdog
  esp_task_wdt_reset();

  handleGPS();

  if (digitalRead(LED_LOG) == HIGH && millis() - ledLogTimer > 100) {
    digitalWrite(LED_LOG, LOW);
  }

  webServer.handleClient();

  unsigned long now = millis();

  // GPS Timeout
  if (lastGpsReceived > 0 && now - lastGpsReceived > GPS_TIMEOUT_MS) {
    logMsg("❌ GPS timeout " + String(GPS_TIMEOUT_MS / 60000) + " menit, RESTARTING...");
    delay(1000);
    ESP.restart();
  }

  // Heap Monitor
  if (ESP.getFreeHeap() < HEAP_MIN_BYTES) {
    logMsg("❌ Heap kritis: " + String(ESP.getFreeHeap()) + " bytes, RESTARTING...");
    delay(1000);
    ESP.restart();
  }

  // SD Card Recovery
  if (!sdReady && now - lastSdRetry > SD_RETRY_INTERVAL) {
    lastSdRetry = now;
    logMsg("🔧 SD re-init...");
    initSD();
  }

  delay(2);
}
