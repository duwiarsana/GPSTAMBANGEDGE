#include <WiFi.h>
#include <SD.h>
#include <SPI.h>

/**
 * SIMULASI SENDER (EXCA ROLE)
 * 
 * Tugas:
 * 1. Membuat WiFi Access Point
 * 2. Menunggu koneksi dari Receiver
 * 3. Mengirimkan data dari /gps_log.jsonl di SD Card
 * 4. Mendukung resume transfer menggunakan offset
 */

// --- KONFIGURASI ---
const char* WIFI_SSID = "SIMULASI_SENDER";
const char* WIFI_PASS = "12345678";
const uint16_t SERVER_PORT = 5000;

const int PIN_SD_CS = 5;
const int PIN_LED_TRANS = 13; // LED Indikator Transfer (User requested GPIO 13)

// --- GLOBAL ---
WiFiServer server(SERVER_PORT);
File logFile;

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED_TRANS, OUTPUT);
  digitalWrite(PIN_LED_TRANS, LOW);

  // 1. Inisialisasi SD Card
  Serial.print("Initializing SD Card...");
  if (!SD.begin(PIN_SD_CS)) {
    Serial.println("FAILED!");
    while (1);
  }
  Serial.println("OK.");

  // Cek apakah file ada
  if (!SD.exists("/gps_log.jsonl")) {
    Serial.println("WARNING: /gps_log.jsonl tidak ditemukan di SD!");
    Serial.println("Pastikan file dari PC sudah dicopy ke SD Card.");
  }

  // 2. Setup WiFi AP
  WiFi.softAP(WIFI_SSID, WIFI_PASS);
  Serial.print("AP Created. IP: ");
  Serial.println(WiFi.softAPIP());

  // 3. Start Server
  server.begin();
  Serial.println("Server started, waiting for receiver...");
}

void loop() {
  WiFiClient client = server.available();

  if (client) {
    Serial.println("\n[CONN] Receiver connected!");
    handleTransfer(client);
    Serial.println("[CONN] Receiver disconnected.");
  }
}

void handleTransfer(WiFiClient& client) {
  uint32_t currentOffset = 0;
  bool isTransferring = false;

  while (client.connected()) {
    if (client.available()) {
      String request = client.readStringUntil('\n');
      request.trim();
      
      Serial.print("-> ");
      Serial.println(request);

      if (request == "HELLO") {
        client.println("READY");
      } 
      else if (request.startsWith("GET")) {
        // Format: GET {offset}
        int spaceIdx = request.indexOf(' ');
        if (spaceIdx != -1) {
          currentOffset = request.substring(spaceIdx + 1).toInt();
        } else {
          currentOffset = 0;
        }

        logFile = SD.open("/gps_log.jsonl", FILE_READ);
        if (logFile) {
          if (currentOffset < logFile.size()) {
            logFile.seek(currentOffset);
            client.println("OK_START");
            isTransferring = true;
            digitalWrite(PIN_LED_TRANS, HIGH);
          } else {
            client.println("NO_DATA");
            logFile.close();
          }
        } else {
          client.println("ERROR_FILE");
        }
      }
      else if (request == "NEXT" && isTransferring) {
        if (logFile.available()) {
          String line = logFile.readStringUntil('\n');
          client.println(line);
          // LED blink saat kirim data
          digitalWrite(PIN_LED_TRANS, !digitalRead(PIN_LED_TRANS));
        } else {
          client.println("END");
          isTransferring = false;
          logFile.close();
          digitalWrite(PIN_LED_TRANS, LOW);
          Serial.println("[INFO] Transfer finished (EOF).");
        }
      }
      else if (request == "OK") {
        Serial.println("[INFO] Receiver confirmed success.");
        break;
      }
    }
    
    // Safety timeout
    static uint32_t lastActivity = millis();
    if (client.available()) lastActivity = millis();
    if (millis() - lastActivity > 10000) {
      Serial.println("[ERR] Connection timeout.");
      break;
    }
  }

  if (logFile) logFile.close();
  digitalWrite(PIN_LED_TRANS, LOW);
}
