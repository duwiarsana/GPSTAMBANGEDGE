#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <SD.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>

// ================= WATCHDOG =================
#define WDT_TIMEOUT_SEC  30
#define GPS_TIMEOUT_MS   300000
#define HEAP_MIN_BYTES   20000
#define SD_RETRY_INTERVAL 60000

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
  <meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
  <title>DT01 Monitor</title>
  <style>
    :root{--bg:#080a12;--card:#0f1220;--card2:#0a0d18;--border:#181d35;--amber:#f59e0b;--amber2:#d97706;--green:#22c55e;--red:#ef4444;--blue:#3b82f6;--txt:#c8cdd8;--dim:#4a5068;--dimmer:#2d3248}
    *{margin:0;padding:0;box-sizing:border-box}
    body{font-family:-apple-system,system-ui,'Segoe UI',Roboto,sans-serif;background:var(--bg);color:var(--txt);padding:10px;max-width:540px;margin:0 auto;-webkit-font-smoothing:antialiased;font-size:13px;min-height:100vh}

    .bar{height:3px;border-radius:2px;margin-bottom:10px;overflow:hidden;background:var(--card)}
    .bar .f{height:100%;background:linear-gradient(90deg,var(--amber),var(--red));animation:s 3s linear infinite;will-change:width}
    @keyframes s{0%{width:100%}100%{width:0%}}

    .hdr{text-align:center;padding:20px 12px 16px;background:linear-gradient(160deg,#1a1508 0%,#0f1220 50%,#0d0a18 100%);border-radius:16px;margin-bottom:12px;border:1px solid #2a1f14;position:relative;overflow:hidden}
    .hdr::before{content:'';position:absolute;top:-50%;left:-50%;width:200%;height:200%;background:radial-gradient(circle at 30% 40%,rgba(245,158,11,.06) 0%,transparent 50%);pointer-events:none}
    .hdr h1{color:var(--amber);font-size:1.25em;letter-spacing:1.5px;font-weight:800;position:relative}
    .hdr .sub{color:var(--dim);font-size:.72em;margin-top:5px;position:relative}
    .dot{display:inline-block;width:7px;height:7px;border-radius:50%;background:var(--green);box-shadow:0 0 8px var(--green);margin-right:5px;animation:bk 2s infinite}
    @keyframes bk{0%,100%{opacity:1}50%{opacity:.3}}

    .c{background:var(--card);border-radius:14px;padding:14px;margin-bottom:10px;border:1px solid var(--border);position:relative;overflow:hidden}
    .c::after{content:'';position:absolute;top:0;left:0;right:0;height:1px;background:linear-gradient(90deg,transparent,rgba(245,158,11,.15),transparent)}
    .c h2{color:var(--amber);font-size:.82em;margin-bottom:10px;padding-bottom:7px;border-bottom:1px solid var(--border);letter-spacing:.5px;font-weight:700}

    .sg{display:grid;grid-template-columns:1fr 1fr;gap:7px}
    .s{background:var(--card2);border-radius:10px;padding:10px 8px;text-align:center;border:1px solid var(--border);transition:border-color .4s}
    .s .l{font-size:.58em;color:var(--dim);text-transform:uppercase;letter-spacing:1px;font-weight:600}
    .s .v{font-size:1.1em;font-weight:700;color:var(--amber);margin-top:3px}
    .s .v.ok{color:var(--green)}.s .v.wr{color:var(--red)}

    .io{display:grid;grid-template-columns:1fr 1fr;gap:8px}
    .ib{background:var(--card2);border-radius:12px;padding:12px 6px;text-align:center;border:1px solid var(--border);transition:all .4s ease}
    .ib.on{border-color:var(--green);background:linear-gradient(180deg,#0a1a10,var(--card2));box-shadow:0 0 12px rgba(34,197,94,.08)}
    .ib .ic{font-size:1.5em;margin-bottom:4px;filter:grayscale(80%);transition:filter .3s}
    .ib.on .ic{filter:grayscale(0%)}
    .ib .nm{font-size:.58em;color:var(--dim);text-transform:uppercase;letter-spacing:1px;font-weight:600}
    .ib .iv{font-size:.78em;font-weight:700;margin-top:3px;color:var(--dimmer);transition:color .3s}
    .ib.on .iv{color:var(--green)}

    .fl{display:flex;justify-content:space-between;align-items:center;padding:7px 2px;border-bottom:1px solid rgba(24,29,53,.7)}
    .fl:last-child{border-bottom:none}
    .fl .lk{color:var(--dim);font-size:.76em;font-weight:500;white-space:nowrap}
    .fl .vv{color:#e8ecf2;font-weight:600;font-size:.82em;text-align:right;max-width:60%;word-break:break-all}
    .fl .vv.hi{color:var(--amber);font-size:.95em;font-weight:800}
    .fl .vv.mn{font-family:'SF Mono',Menlo,monospace;font-size:.75em;color:#8892a8}

    .tag{display:inline-block;padding:2px 8px;border-radius:6px;font-size:.68em;font-weight:700;letter-spacing:.5px}
    .tag.ok{background:rgba(34,197,94,.12);color:var(--green)}.tag.fl2{background:rgba(239,68,68,.12);color:var(--red)}

    .ric{background:var(--card2);border-radius:10px;padding:12px;border:1px solid var(--border);margin-top:6px}
    .rr{display:flex;justify-content:space-between;align-items:center;padding:5px 0}
    .rr .rl{color:var(--dim);font-size:.72em;font-weight:500}
    .rr .rv{color:#dde1ea;font-size:.78em;font-weight:600;font-family:'SF Mono',Menlo,monospace}
    .rr .rv.go{color:var(--green)}.rr .rv.no{color:var(--red)}

    .sb{display:inline-block;width:50px;height:7px;border-radius:4px;background:var(--border);overflow:hidden;vertical-align:middle;margin-left:8px}
    .sf{height:100%;border-radius:4px;transition:width .5s}

    .lw{overflow-x:auto;-webkit-overflow-scrolling:touch}
    table{width:100%;font-size:.66em;border-collapse:collapse}
    th{background:linear-gradient(180deg,#1a1508,#12101a);padding:8px 5px;text-align:left;color:var(--amber);position:sticky;top:0;font-weight:700;letter-spacing:.5px}
    td{padding:5px;border-bottom:1px solid rgba(13,16,32,.8);white-space:nowrap}
    tr:hover td{background:rgba(245,158,11,.03)}

    .emp{text-align:center;color:var(--dimmer);padding:20px;font-size:.82em}
    .ft{text-align:center;color:#1e2030;font-size:.58em;padding:12px 0;margin-top:8px}
  </style>
</head>
<body>

<div class="bar"><div class="f"></div></div>

<div class="hdr">
  <h1>🚛 DT01 MONITOR</h1>
  <div class="sub"><span class="dot"></span>Fleet GPS Dashboard — Live</div>
</div>

<div class="c">
  <h2>⚙️ Sistem</h2>
  <div class="sg">
    <div class="s"><div class="l">Uptime</div><div class="v" id="uptime">—</div></div>
    <div class="s"><div class="l">Records</div><div class="v" id="records">—</div></div>
    <div class="s"><div class="l">Free Heap</div><div class="v" id="heap">—</div></div>
    <div class="s"><div class="l">SD Card</div><div class="v" id="sd">—</div></div>
  </div>
</div>

<div class="c">
  <h2>🔌 Status I/O</h2>
  <div class="io">
    <div class="ib" id="x-pto"><div class="ic">⚙️</div><div class="nm">PTO</div><div class="iv" id="v-pto">OFF</div></div>
    <div class="ib" id="x-acc"><div class="ic">🔑</div><div class="nm">ACC</div><div class="iv" id="v-acc">OFF</div></div>
  </div>
</div>

<div class="c">
  <h2>📍 GPS Terbaru</h2>
  <div id="gps"><div class="emp">Menunggu data GPS...</div></div>
</div>

<div class="c" id="cIb" style="display:none">
  <h2>🔐 iButton / RFID</h2>
  <div class="ric" id="dIb"></div>
</div>

<div class="c" id="cBe" style="display:none">
  <h2>📶 Bluetooth Beacon</h2>
  <div id="dBe"></div>
</div>

<div class="c">
  <h2>📋 Log Terbaru <span style="color:var(--dim);font-size:.85em" id="li"></span></h2>
  <div class="lw" style="max-height:340px;overflow-y:auto">
    <table>
      <thead><tr><th>#</th><th>Waktu</th><th>Lat</th><th>Lon</th><th>Spd</th><th>PTO</th><th>ACC</th></tr></thead>
      <tbody id="lb"><tr><td colspan="7" class="emp">Memuat...</td></tr></tbody>
    </table>
  </div>
</div>

<div class="ft">DT01 GPS Logger &copy; 2026 — Duwi Arsana</div>

<script>
function fu(ms){var s=Math.floor(ms/1e3),d=Math.floor(s/86400);s%=86400;var h=Math.floor(s/3600);s%=3600;var m=Math.floor(s/60);s%=60;return(d>0?d+'d ':'')+h+'h '+m+'m '+s+'s'}
function pi(v){if(!v||typeof v!='string')return{p:0,a:0};return{p:parseInt(v[0])||0,a:parseInt(v[1])||0}}
function sio(xid,vid,on){var b=document.getElementById(xid),v=document.getElementById(vid);if(on){b.classList.add('on');v.textContent='ON'}else{b.classList.remove('on');v.textContent='OFF'}}
function rc(r){return r>-50?'#22c55e':r>-70?'#f59e0b':'#ef4444'}
function rp(r){return Math.max(0,Math.min(100,(r+100)*2))}

function upd(){
  fetch('/api/status').then(function(r){return r.json()}).then(function(d){
    document.getElementById('uptime').textContent=fu(d.uptime);
    document.getElementById('records').textContent=d.totalRecords.toLocaleString();
    var k=(d.freeHeap/1024).toFixed(0);
    var e=document.getElementById('heap');e.textContent=k+'KB';e.className='v'+(k<30?' wr':'');
    document.getElementById('sd').innerHTML=d.sdReady?'<span class="tag ok">OK</span>':'<span class="tag fl2">FAIL</span>';
  }).catch(function(){});

  fetch('/api/latest').then(function(r){return r.json()}).then(function(d){
    var inv=d['in'],io=pi(inv);
    sio('x-pto','v-pto',io.p==1);sio('x-acc','v-acc',io.a==1);

    var bv=d.bat!=null?(d.bat>1000?(d.bat/1000).toFixed(1)+'V':d.bat+'mV'):'—';
    var rows=[
      ['🕐 Waktu',d.ts||'—',''],
      ['🏷️ Source / ID',d.src||'—',''],
      ['📋 Event',d.type!=null?(d.type+' ('+d.ev+')'):'—',''],
      ['🌍 Latitude',d.lat!=null?d.lat.toFixed(6):'—','hi'],
      ['🌍 Longitude',d.lon!=null?d.lon.toFixed(6):'—','hi'],
      ['🚀 Kecepatan',d.spd!=null?d.spd+' km/h':'—',''],
      ['🧭 Heading',d.hdg!=null?d.hdg+'°':'—',''],
      ['⛰️ Altitude',d.alt!=null?d.alt+' m':'—',''],
      ['🔋 Baterai',bv,''],
      ['📏 Odometer',d.odo!=null?d.odo.toLocaleString()+' m':'—',''],
      ['📡 HDOP',d.hdop!=null?d.hdop:'—',''],
      ['🌡️ Suhu',d.temp!=null?d.temp+'°C':'—',''],
      ['📱 IMEI',d.imei||'—','mn'],
      ['🆔 Msg ID',d.id||'—','mn']
    ];
    var h='';for(var i=0;i<rows.length;i++){h+='<div class="fl"><span class="lk">'+rows[i][0]+'</span><span class="vv'+(rows[i][2]?' '+rows[i][2]:'')+'">'+ rows[i][1]+'</span></div>'}
    document.getElementById('gps').innerHTML=h;

    var ib=d.ib,cIb=document.getElementById('cIb');
    if(ib&&typeof ib=='object'){
      cIb.style.display='';
      var au=ib.au,at=au===true?'✅ Authorized':au===false?'❌ Unauthorized':'—',ac=au===true?'go':au===false?'no':'';
      document.getElementById('dIb').innerHTML=
        '<div class="rr"><span class="rl">🔑 ID Tag</span><span class="rv">'+( ib.id||'—')+'</span></div>'+
        '<div class="rr"><span class="rl">📋 Status</span><span class="rv">'+(ib.st||'—')+'</span></div>'+
        '<div class="rr"><span class="rl">🔐 Auth</span><span class="rv '+ac+'">'+at+'</span></div>';
    }else{cIb.style.display='none'}

    var be=d.be,cBe=document.getElementById('cBe');
    if(be&&Array.isArray(be)&&be.length>0){
      cBe.style.display='';var bh='';
      for(var i=0;i<be.length;i++){var b=be[i],cl=rc(b.rssi),pct=rp(b.rssi);
        bh+='<div class="ric" style="margin-top:'+(i>0?'6':'0')+'px"><div class="rr"><span class="rl">📶 MAC</span><span class="rv">'+(b.mac||'—')+'</span></div>'+
        '<div class="rr"><span class="rl">📊 RSSI</span><span class="rv" style="color:'+cl+'">'+b.rssi+' dBm<span class="sb"><span class="sf" style="width:'+pct+'%;background:'+cl+'"></span></span></span></div></div>'}
      document.getElementById('dBe').innerHTML=bh;
    }else{cBe.style.display='none'}
  }).catch(function(){});

  fetch('/api/logs?n=30').then(function(r){return r.json()}).then(function(d){
    document.getElementById('li').textContent='('+d.length+' terbaru)';
    var h='';
    for(var i=0;i<d.length;i++){var r=d[i],io=pi(r['in']);
      var pt=io.p==1?'<span style="color:#22c55e">ON</span>':'<span style="color:#4a5068">OFF</span>';
      var ac=io.a==1?'<span style="color:#22c55e">ON</span>':'<span style="color:#4a5068">OFF</span>';
      h+='<tr><td style="color:#3a3f52">'+(i+1)+'</td><td>'+(r.ts||'—')+'</td><td style="color:var(--amber)">'+(r.lat!=null?r.lat.toFixed(6):'—')+'</td><td style="color:var(--amber)">'+(r.lon!=null?r.lon.toFixed(6):'—')+'</td><td>'+(r.spd!=null?r.spd:'—')+'</td><td>'+pt+'</td><td>'+ac+'</td></tr>'}
    if(!h)h='<tr><td colspan="7" class="emp">Belum ada data</td></tr>';
    document.getElementById('lb').innerHTML=h;
  }).catch(function(){});
}
upd();setInterval(upd,3000);
</script>
</body>
</html>
)rawliteral";

// ================= DEBUG =================
void logMsg(const String &s) {
  Serial.print("["); Serial.print(millis()); Serial.print("] ");
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
  if (f) { f.print(v); f.close(); }
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
      sdReady = false;
      return;
    }
  }

  logFile.println(line);
  logFile.flush();

  totalRecords++;
  addToHistory(line);
  latestJson = line;
  lastGpsReceived = millis();

  digitalWrite(LED_LOG, HIGH);
  ledLogTimer = millis();

  logMsg("📍 LOGGED #" + String(totalRecords));
}

// ================= JSON PROCESS (FORMAT PRODUKSI) =================
bool processJSON(const char* json, String &out) {
  StaticJsonDocument<1536> doc;

  // Filter hanya field yang dibutuhkan (hemat RAM)
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
    filter["ibutton"]["status"] = true;
    filter["ibutton"]["auth"] = true;
    filter["ibeacon"][0]["mac"] = true;
    filter["ibeacon"][0]["rssi"] = true;
    filter["gsensor"]["x"] = true;
    filter["gsensor"]["y"] = true;
    filter["gsensor"]["z"] = true;
    filterInit = true;
  }

  if (deserializeJson(doc, json, DeserializationOption::Filter(filter))) {
    logMsg("❌ JSON ERROR");
    return false;
  }

  if (!doc["imei"] || !doc["timestamp"]) {
    logMsg("❌ INVALID FIELD");
    return false;
  }

  // Build optimized JSON (format produksi)
  StaticJsonDocument<1024> optDoc;
  optDoc["id"]   = makeUID(doc);
  optDoc["imei"] = doc["imei"];
  optDoc["src"]  = DT_ID;
  optDoc["type"] = doc["event_info"];
  optDoc["ev"]   = doc["event_code"];
  optDoc["ts"]   = doc["timestamp"];
  optDoc["lat"]  = doc["latitude"];
  optDoc["lon"]  = doc["longitude"];
  optDoc["spd"]  = doc["speed"];
  optDoc["hdg"]  = doc["heading"];
  optDoc["alt"]  = doc["altitude"];
  optDoc["bat"]  = doc["external"];
  optDoc["odo"]  = doc["odometer"];
  optDoc["ign"]  = doc["ignition"];
  optDoc["in"]   = doc["input_status"];
  optDoc["out"]  = doc["output_status"];
  optDoc["hdop"] = doc["hdop"];
  optDoc["temp"] = doc["mcu_temp"];

  if (doc.containsKey("gsensor")) {
    JsonObject gs = optDoc.createNestedObject("gs");
    gs["x"] = doc["gsensor"]["x"];
    gs["y"] = doc["gsensor"]["y"];
    gs["z"] = doc["gsensor"]["z"];
  }

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
      b["rssi"] = beacon["rssi"];
    }
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
  doc["uptime"]       = millis();
  doc["totalRecords"] = totalRecords;
  doc["freeHeap"]     = ESP.getFreeHeap();
  doc["sdReady"]      = sdReady;
  doc["seq"]          = seq;
  doc["deviceId"]     = DT_ID;
  doc["restartCount"] = restartCount;
  doc["lastGpsAgo"]   = (lastGpsReceived > 0) ? (millis() - lastGpsReceived) : -1;

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
  delay(1000);

  logMsg("=== " + String(DT_ID) + " MONITOR STARTING ===");
  logMsg("MAC Address: " + WiFi.macAddress());

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

  Serial2.setRxBufferSize(2048);
  Serial2.begin(GPS_BAUD);
  Serial2.setPins(GPS_RX, GPS_TX);

  delay(1500);
  while (Serial2.available()) Serial2.read();

  initSD();

  if (sdReady) {
    restartCount = readUint(RESTART_FILE) + 1;
    writeUint(RESTART_FILE, restartCount);
    logMsg("🔄 Restart count: " + String(restartCount));
  }

  WiFi.softAP(AP_SSID, AP_PASS);
  logMsg("📶 WiFi AP: " + String(AP_SSID));
  logMsg("📶 IP: " + WiFi.softAPIP().toString());

  webServer.on("/", handleRoot);
  webServer.on("/api/status", handleApiStatus);
  webServer.on("/api/latest", handleApiLatest);
  webServer.on("/api/logs", handleApiLogs);
  webServer.on("/api/restart", handleApiRestart);
  webServer.begin();

  lastGpsReceived = millis();

  esp_task_wdt_add(NULL);

  logMsg("✅ " + String(DT_ID) + " MONITOR READY");
  logMsg("🌐 Buka http://" + WiFi.softAPIP().toString() + " di browser HP");
}

// ================= LOOP =================
void loop() {
  esp_task_wdt_reset();

  handleGPS();

  if (digitalRead(LED_LOG) == HIGH && millis() - ledLogTimer > 100) {
    digitalWrite(LED_LOG, LOW);
  }

  webServer.handleClient();

  unsigned long now = millis();

  if (lastGpsReceived > 0 && now - lastGpsReceived > GPS_TIMEOUT_MS) {
    logMsg("❌ GPS timeout " + String(GPS_TIMEOUT_MS / 60000) + " menit, RESTARTING...");
    delay(1000);
    ESP.restart();
  }

  if (ESP.getFreeHeap() < HEAP_MIN_BYTES) {
    logMsg("❌ Heap kritis: " + String(ESP.getFreeHeap()) + " bytes, RESTARTING...");
    delay(1000);
    ESP.restart();
  }

  if (!sdReady && now - lastSdRetry > SD_RETRY_INTERVAL) {
    lastSdRetry = now;
    logMsg("🔧 SD re-init...");
    initSD();
  }

  delay(2);
}
