/**
 * ESP32 GPS Serial Tester (Mengikuti Logika Pembacaan & Parsing gpstambangdt_realtime)
 * Pin RX2 = GPIO 16 (Dihubungkan ke TX Modul GPS)
 * Pin TX2 = GPIO 17 (Dihubungkan ke RX Modul GPS)
 */

#include <Arduino.h>
#include <ArduinoJson.h>

#define GPS_RX 16
#define GPS_TX 17
#define GPS_BAUD 115200
#define BUF_SIZE 4096

char gpsBuf[BUF_SIZE];
int gpsBufLen = 0;
int gpsBrace = 0;
bool gpsCollecting = false;

unsigned long totalBytesReceived = 0;
unsigned long validJsonCount = 0;

void resetGpsParser() {
  gpsBufLen = 0;
  gpsBrace = 0;
  gpsCollecting = false;
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println("\n\n==================================================");
  Serial.println("=== 🛠️ GPS TESTER (LOGIKA SAMPAI PARSING DT) ===");
  Serial.println("==================================================");
  Serial.println("  • Pin RX2: GPIO 16 | Pin TX2: GPIO 17");
  Serial.println("  • Baud Rate: 115200 bps");
  Serial.println("==================================================\n");

  Serial2.setRxBufferSize(2048);
  Serial2.begin(GPS_BAUD, SERIAL_8N1, GPS_RX, GPS_TX);
}

void loop() {
  while (Serial2.available()) {
    char c = Serial2.read();
    totalBytesReceived++;

    // Tampilkan data mentah di Serial Monitor USB
    Serial.write(c);

    // Persis Logika handleDTGps() dari gpstambangdt_realtime
    if (!gpsCollecting) {
      if (c == '{') {
        gpsCollecting = true;
        gpsBrace = 1;
        gpsBufLen = 0;
        gpsBuf[gpsBufLen++] = c;
      }
      continue;
    }

    if (gpsBufLen < BUF_SIZE - 1) {
      gpsBuf[gpsBufLen++] = c;
    } else {
      Serial.println("\n⚠️ [DEBUG] GPS JSON Buffer Overflow!");
      resetGpsParser();
      continue;
    }

    if (c == '{')
      gpsBrace++;
    if (c == '}')
      gpsBrace--;

    if (gpsBrace == 0) {
      gpsBuf[gpsBufLen] = '\0';
      
      // Tes parsing JSON persis seperti DT
      StaticJsonDocument<1024> doc;
      DeserializationError err = deserializeJson(doc, gpsBuf);
      
      if (err) {
        Serial.print("\n❌ [DEBUG] JSON Invalid: ");
        Serial.println(err.c_str());
      } else {
        validJsonCount++;
        Serial.print("\n✅ [DEBUG] JSON Valid Terdeteksi #");
        Serial.println(validJsonCount);
      }

      resetGpsParser();
    }
  }
}
