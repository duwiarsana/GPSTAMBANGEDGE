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
#define RXD2 16
#define TXD2 17
#define SD_CS 5
#define LED_LOG 2

// ================= UART =================
#define GPS_BAUD 115200

// ================= WIFI AP =================
const char* AP_SSID = "EXCA01_MONITOR";
const char* AP_PASS = "12345678";

WebServer webServer(80);

// ================= FILE =================
const char* LOG_FILE = "/gps_log.jsonl";
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
  <title>EXCA01 Monitor</title>
  <style>
    *{margin:0;padding:0;box-sizing:border-box}
    body{font-family:-apple-system,system-ui,sans-serif;background:#0b0d17;color:#e0e0e0;padding:12px;max-width:600px;margin:0 auto;-webkit-font-smoothing:antialiased}

    .refresh-bar{height:3px;border-radius:2px;margin-bottom:10px;overflow:hidden;background:#1a1d2e}
    .refresh-bar .fill{height:100%;background:linear-gradient(90deg,#00d4ff,#00ff88);animation:shrink 3s linear infinite}
    @keyframes shrink{0%{width:100%}100%{width:0%}}

    .header{text-align:center;padding:18px 12px;background:linear-gradient(135deg,#0f1628,#162040);border-radius:14px;margin-bottom:14px;border:1px solid #1e2744}
    .header h1{color:#00d4ff;font-size:1.3em;letter-spacing:1px}
    .header .sub{color:#667;font-size:0.75em;margin-top:4px}
    .header .live{display:inline-block;width:8px;height:8px;border-radius:50%;background:#00ff88;box-shadow:0 0 8px #00ff88;margin-right:6px;animation:pulse 2s infinite}
    @keyframes pulse{0%,100%{opacity:1}50%{opacity:0.4}}

    .card{background:#111528;border-radius:12px;padding:14px;margin-bottom:12px;border:1px solid #1e2744}
    .card h2{color:#00d4ff;font-size:0.9em;margin-bottom:10px;padding-bottom:8px;border-bottom:1px solid #1e2744;letter-spacing:0.5px}

    .stat-grid{display:grid;grid-template-columns:1fr 1fr;gap:8px}
    .stat{background:#0b0e1a;border-radius:8px;padding:10px;text-align:center}
    .stat .label{font-size:0.65em;color:#667;text-transform:uppercase;letter-spacing:1px}
    .stat .value{font-size:1.15em;font-weight:700;color:#00ff88;margin-top:4px}
    .stat .value.warn{color:#ff9900}
    .stat .value.err{color:#ff4455}

    .field{display:flex;justify-content:space-between;align-items:center;padding:8px 0;border-bottom:1px solid #0b0e1a}
    .field:last-child{border-bottom:none}
    .field .lbl{color:#667;font-size:0.8em}
    .field .val{color:#fff;font-weight:500;font-size:0.85em;text-align:right;max-width:60%;word-break:break-all}
    .field .val.coord{color:#00ff88;font-size:1em;font-weight:700}

    .log-wrap{overflow-x:auto;-webkit-overflow-scrolling:touch}
    table{width:100%;font-size:0.7em;border-collapse:collapse}
    th{background:#0f1628;padding:8px 6px;text-align:left;color:#00d4ff;position:sticky;top:0;font-weight:600}
    td{padding:6px;border-bottom:1px solid #0f1223;white-space:nowrap}
    tr:hover td{background:#0f1628}

    .badge{display:inline-block;padding:2px 6px;border-radius:4px;font-size:0.7em;font-weight:600}
    .badge-ok{background:#00ff8822;color:#00ff88}
    .badge-fail{background:#ff444422;color:#ff4455}

    .empty{text-align:center;color:#445;padding:20px;font-size:0.85em}

    .footer{text-align:center;color:#334;font-size:0.65em;padding:12px 0;margin-top:8px}

    /* Download buttons */
    .dl-card{background:linear-gradient(135deg,#0f1628,#162040);border-radius:12px;padding:16px;margin-bottom:12px;border:1px solid #1e2744}
    .dl-card h2{color:#00d4ff;font-size:0.9em;margin-bottom:12px;padding-bottom:8px;border-bottom:1px solid #1e2744;letter-spacing:0.5px}
    .dl-grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}
    .dl-btn{display:flex;align-items:center;justify-content:center;gap:6px;padding:14px 10px;border-radius:10px;text-decoration:none;font-weight:700;font-size:0.85em;letter-spacing:0.3px;transition:all 0.3s ease;border:none;cursor:pointer;text-align:center}
    .dl-btn:active{transform:scale(0.96)}
    .dl-jsonl{background:linear-gradient(135deg,#00d4ff,#0088cc);color:#fff;box-shadow:0 4px 15px #00d4ff33}
    .dl-jsonl:hover{box-shadow:0 6px 20px #00d4ff55}
    .dl-csv{background:linear-gradient(135deg,#00ff88,#00cc66);color:#0b0d17;box-shadow:0 4px 15px #00ff8833}
    .dl-csv:hover{box-shadow:0 6px 20px #00ff8855}
    .dl-info{text-align:center;color:#556;font-size:0.7em;margin-top:10px}
  </style>
</head>
<body>

  <div class="refresh-bar"><div class="fill"></div></div>

  <div class="header">
    <h1>📡 EXCA01 MONITOR</h1>
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

  <div class="dl-card">
    <h2>💾 Download Data Logger</h2>
    <div class="dl-grid">
      <a href="/api/download" class="dl-btn dl-jsonl" id="dlJsonl">📄 JSONL</a>
      <a href="/api/download/csv" class="dl-btn dl-csv" id="dlCsv">📊 CSV</a>
    </div>
    <div class="dl-info" id="dlInfo">Memuat info file...</div>
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

  <div class="footer">EXCA01 GPS Logger &copy; 2026 — Duwi Arsana</div>

<script>
function fmtUp(ms){
  var s=Math.floor(ms/1000),d=Math.floor(s/86400);s%=86400;
  var h=Math.floor(s/3600);s%=3600;var m=Math.floor(s/60);s%=60;
  return (d>0?d+'d ':'')+h+'h '+m+'m '+s+'s';
}

function fmtSize(b){
  if(b<1024) return b+' B';
  if(b<1048576) return (b/1024).toFixed(1)+' KB';
  return (b/1048576).toFixed(2)+' MB';
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

    // Update download info
    var info=document.getElementById('dlInfo');
    if(d.sdReady && d.totalRecords>0){
      info.textContent='📁 '+d.totalRecords.toLocaleString()+' record';
      if(d.fileSize!==undefined) info.textContent+=' • '+fmtSize(d.fileSize);
    } else if(!d.sdReady){
      info.textContent='⚠️ SD Card tidak tersedia';
    } else {
      info.textContent='📁 Belum ada data untuk didownload';
    }
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
    if(d['input_status']!==undefined&&d['input_status']!==null){
      var is = String(d['input_status']);
      var d1 = (is.length > 0 && is[0] == '1') ? 'ON' : 'OFF';
      var d2 = (is.length > 1 && is[1] == '1') ? 'ON' : 'OFF';
      h+='<div class="field"><span class="lbl">⚙️ PTO</span><span class="val">'+d1+'</span></div>';
      h+='<div class="field"><span class="lbl">🔑 Ignition</span><span class="val">'+d2+'</span></div>';
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
        +(r.timestamp||'—')+'</td><td style="color:#00ff88">'
        +(r.latitude!=null?r.latitude.toFixed(6):'—')+'</td><td style="color:#00ff88">'
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

  return "EXCA01-" + imei + "-" + ts + "-" + String(seq);
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

    // Simpan record terakhir sebagai latestJson
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

  doc["msg_id"] = makeUID(doc);
  doc["source"] = "EXCA01";

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
  doc["deviceId"]      = "EXCA01";
  doc["restartCount"]  = restartCount;
  doc["lastGpsAgo"]    = (lastGpsReceived > 0) ? (millis() - lastGpsReceived) : -1;
  doc["wdtEnabled"]    = true;

  // Tambahkan info ukuran file untuk download info
  if (sdReady) {
    File f = SD.open(LOG_FILE);
    if (f) {
      doc["fileSize"] = f.size();
      f.close();
    }
  }

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

  // Bangun JSON array — data terbaru di atas
  String out = "[";
  for (int i = 0; i < count; i++) {
    int idx = (historyHead - 1 - i + MAX_HISTORY) % MAX_HISTORY;
    if (i > 0) out += ",";
    out += history[idx];
  }
  out += "]";

  webServer.send(200, "application/json", out);
}

// ================= DOWNLOAD HANDLERS =================
void handleDownloadJsonl() {
  if (!sdReady) {
    webServer.send(503, "text/plain", "SD Card tidak tersedia");
    return;
  }

  File f = SD.open(LOG_FILE);
  if (!f) {
    webServer.send(404, "text/plain", "File log tidak ditemukan");
    return;
  }

  size_t fileSize = f.size();
  logMsg("📥 Download JSONL, size=" + String(fileSize));

  // Kirim header untuk download file
  webServer.sendHeader("Content-Disposition", "attachment; filename=\"EXCA01_gps_log.jsonl\"");
  webServer.sendHeader("Cache-Control", "no-cache");

  // Stream file langsung ke client
  webServer.streamFile(f, "application/x-jsonlines");
  f.close();

  logMsg("✅ Download JSONL selesai");
}

void handleDownloadCsv() {
  if (!sdReady) {
    webServer.send(503, "text/plain", "SD Card tidak tersedia");
    return;
  }

  File f = SD.open(LOG_FILE);
  if (!f) {
    webServer.send(404, "text/plain", "File log tidak ditemukan");
    return;
  }

  logMsg("📥 Download CSV mulai...");

  // Hitung ukuran estimasi untuk Content-Length (opsional, bisa pakai chunked)
  // Kita gunakan chunked transfer encoding untuk hemat memori
  webServer.sendHeader("Content-Disposition", "attachment; filename=\"EXCA01_gps_log.csv\"");
  webServer.sendHeader("Cache-Control", "no-cache");
  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.send(200, "text/csv", "");

  // Header CSV
  WiFiClient client = webServer.client();
  client.println("timestamp,latitude,longitude,speed,satellites,ignition,pto,imei,protocol,model,msg_id,source");

  // Parse setiap baris JSON dan convert ke CSV
  StaticJsonDocument<1024> doc;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    doc.clear();
    if (deserializeJson(doc, line) != DeserializationError::Ok) continue;

    String is = doc["input_status"] | "000000";
    String d1 = (is.length() > 0 && is.charAt(0) == '1') ? "1" : "0";
    String d2 = (is.length() > 1 && is.charAt(1) == '1') ? "1" : "0";

    String csvLine = "";
    csvLine += "\"" + String(doc["timestamp"] | "") + "\",";
    csvLine += String(doc["latitude"] | 0.0, 6) + ",";
    csvLine += String(doc["longitude"] | 0.0, 6) + ",";
    csvLine += String(doc["speed"] | 0) + ",";
    csvLine += String(doc["satellites"] | 0) + ",";
    csvLine += d2 + ",";
    csvLine += d1 + ",";
    csvLine += "\"" + String(doc["imei"] | "") + "\",";
    csvLine += "\"" + String(doc["protocol"] | "") + "\",";
    csvLine += "\"" + String(doc["model"] | "") + "\",";
    csvLine += "\"" + String(doc["msg_id"] | "") + "\",";
    csvLine += "\"" + String(doc["source"] | "") + "\"";

    client.println(csvLine);

    // Feed watchdog selama download besar
    esp_task_wdt_reset();
    yield();
  }

  f.close();
  logMsg("✅ Download CSV selesai");
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  pinMode(LED_LOG, OUTPUT);
  digitalWrite(LED_LOG, LOW);

  // Hardware Watchdog Timer — restart otomatis jika loop macet > 30 detik
  // Di ESP32 Core v3.x, TWDT sudah aktif, jadi pakai reconfigure
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = WDT_TIMEOUT_SEC * 1000,
    .idle_core_mask = 0,
    .trigger_panic = true
  };
  esp_task_wdt_reconfigure(&wdt_config);
  logMsg("🐕 Watchdog configured: " + String(WDT_TIMEOUT_SEC) + "s");

  Serial2.begin(GPS_BAUD);
  Serial2.setPins(RXD2, TXD2);

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
  webServer.on("/api/download", handleDownloadJsonl);
  webServer.on("/api/download/csv", handleDownloadCsv);
  webServer.begin();

  lastGpsReceived = millis();  // mulai hitung GPS timeout

  // Aktifkan watchdog untuk loop task SETELAH setup selesai
  esp_task_wdt_add(NULL);

  logMsg("✅ EXCA MONITOR READY");
  logMsg("🌐 Buka http://" + WiFi.softAPIP().toString() + " di browser HP");
}

// ================= LOOP =================
void loop() {
  // Feed hardware watchdog — jika tidak dipanggil 30 detik, ESP32 restart
  esp_task_wdt_reset();

  // 1. Proses GPS
  handleGPS();

  // 2. LED auto off
  if (digitalRead(LED_LOG) == HIGH && millis() - ledLogTimer > 100) {
    digitalWrite(LED_LOG, LOW);
  }

  // 3. Handle web requests
  webServer.handleClient();

  unsigned long now = millis();

  // 4. GPS Timeout — restart jika tidak ada data GPS > 5 menit
  if (lastGpsReceived > 0 && now - lastGpsReceived > GPS_TIMEOUT_MS) {
    logMsg("❌ GPS timeout " + String(GPS_TIMEOUT_MS / 60000) + " menit, RESTARTING...");
    delay(1000);
    ESP.restart();
  }

  // 5. Heap Monitor — restart jika memori kritis
  if (ESP.getFreeHeap() < HEAP_MIN_BYTES) {
    logMsg("❌ Heap kritis: " + String(ESP.getFreeHeap()) + " bytes, RESTARTING...");
    delay(1000);
    ESP.restart();
  }

  // 6. SD Card Recovery — coba re-init jika SD gagal
  if (!sdReady && now - lastSdRetry > SD_RETRY_INTERVAL) {
    lastSdRetry = now;
    logMsg("🔧 SD re-init...");
    initSD();
  }

  delay(2);
}
