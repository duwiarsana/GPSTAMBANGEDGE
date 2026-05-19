#include <WiFi.h>
#include <SD.h>
#include <SPI.h>

/**
 * SIMULASI RECEIVER (DT ROLE)
 * 
 * Tugas:
 * 1. Mencari WiFi "SIMULASI_SENDER"
 * 2. Connect ke server di IP 192.168.4.1 Port 5000
 * 3. Membaca offset terakhir dari /offset.txt
 * 4. Mendownload data line-by-line dan menyimpannya ke /received_log.jsonl
 * 5. Update offset setiap baris sukses diterima
 */

// --- KONFIGURASI ---
const char* WIFI_SSID = "SIMULASI_SENDER";
const char* WIFI_PASS = "12345678";
const char* SERVER_IP = "192.168.4.1";
const uint16_t SERVER_PORT = 5000;

const int PIN_SD_CS = 5;
const int PIN_LED_CONN = 13; // LED Indikator Transfer (User requested GPIO 13)

// --- GLOBAL ---
uint32_t lastOffset = 0;

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED_CONN, OUTPUT);
  digitalWrite(PIN_LED_CONN, LOW);

  // 1. Inisialisasi SD Card
  Serial.print("Initializing SD Card...");
  if (!SD.begin(PIN_SD_CS)) {
    Serial.println("FAILED!");
    while (1);
  }
  Serial.println("OK.");

  // Cek offset awal
  lastOffset = getLastValidOffset();
  Serial.print("Initial Last Offset from SD: ");
  Serial.println(lastOffset);

  Serial.println("System Ready. Will start scanning for Sender...");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Connecting to Sender...");
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    
    int retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry < 20) {
      delay(500);
      Serial.print(".");
      retry++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi Connected!");
      digitalWrite(PIN_LED_CONN, HIGH);
      startTransfer();
    } else {
      Serial.println("\nSender not found. Retrying in 5s...");
      delay(5000);
    }
  }
  
  delay(1000);
}

void startTransfer() {
  WiFiClient client;
  
  if (!client.connect(SERVER_IP, SERVER_PORT)) {
    Serial.println("Connection to server failed!");
    return;
  }

  Serial.println("Handshake...");
  client.println("HELLO");
  
  String res = client.readStringUntil('\n');
  res.trim();
  
  if (res == "READY") {
    // Tentukan offset valid terbaru dari SD Card sebelum meminta data
    lastOffset = getLastValidOffset();
    Serial.printf("Requesting data streaming from offset: %u bytes\n", lastOffset);
    
    client.printf("STREAM %u\n", lastOffset);

    res = client.readStringUntil('\n');
    res.trim();

    if (res.startsWith("OK_START")) {
      uint32_t totalFileSize = 0;
      int spaceIdx = res.indexOf(' ');
      if (spaceIdx != -1) {
        totalFileSize = res.substring(spaceIdx + 1).toInt();
      }

      Serial.printf("Transfer started! Sender file size: %u bytes\n", totalFileSize);
      
      File receivedFile;
      if (lastOffset > 0) {
        // Buka dengan mode "r+" (read/write) agar bisa seek ke offset valid dan menimpa jika ada baris rusak
        receivedFile = SD.open("/received_log.jsonl", "r+");
        if (receivedFile) {
          receivedFile.seek(lastOffset);
          Serial.printf("Opened existing file, seeked to %u\n", lastOffset);
        }
      } else {
        // Buat file baru jika dari awal
        receivedFile = SD.open("/received_log.jsonl", FILE_WRITE);
        Serial.println("Created new file /received_log.jsonl");
      }

      if (!receivedFile) {
        Serial.println("Failed to open /received_log.jsonl for writing!");
        client.stop();
        return;
      }

      uint8_t buffer[2048];
      uint32_t totalReceived = lastOffset;
      uint32_t bytesReceivedThisSession = 0;
      uint32_t lastProgress = millis();
      uint32_t startTime = millis();

      while (client.connected() || client.available()) {
        if (client.available()) {
          int bytesRead = client.read(buffer, sizeof(buffer));
          if (bytesRead > 0) {
            receivedFile.write(buffer, bytesRead);
            totalReceived += bytesRead;
            bytesReceivedThisSession += bytesRead;
            
            // Blink LED sebagai indikator transfer sedang aktif
            digitalWrite(PIN_LED_CONN, !digitalRead(PIN_LED_CONN));
            
            // Tampilkan progress setiap 1 detik
            if (millis() - lastProgress > 1000) {
              float pct = (totalFileSize > 0) ? (((float)totalReceived / totalFileSize) * 100.0) : 0.0;
              Serial.printf("[INFO] Received %u bytes (%.1f%%)\n", totalReceived, pct);
              lastProgress = millis();
            }
          }
        }
      }

      // Pastikan data tersimpan sempurna di SD Card
      receivedFile.flush();
      receivedFile.close();

      uint32_t duration = millis() - startTime;
      float speedKB = (duration > 0) ? ((float)bytesReceivedThisSession / duration) : 0.0;

      Serial.println("\n=== Transfer Result ===");
      Serial.printf("Duration: %.2f seconds\n", (float)duration / 1000.0);
      Serial.printf("Received in this session: %u bytes\n", bytesReceivedThisSession);
      Serial.printf("Total File Size on SD: %u bytes\n", totalReceived);
      Serial.printf("Average Speed: %.2f KB/s\n", speedKB);

      if (totalFileSize > 0 && totalReceived < totalFileSize) {
        Serial.println("[WARNING] Connection closed before transfer was complete!");
      } else {
        Serial.println("Success! Transfer complete (EOF reached).");
      }
      Serial.println("=======================\n");

    } else {
      Serial.print("Sender response: ");
      Serial.println(res);
    }
  }

  client.stop();
  WiFi.disconnect();
  digitalWrite(PIN_LED_CONN, LOW);
  Serial.println("Job done. Disconnected.");
}

uint32_t getLastValidOffset() {
  if (!SD.exists("/received_log.jsonl")) {
    return 0;
  }
  
  File f = SD.open("/received_log.jsonl", FILE_READ);
  if (!f) {
    return 0;
  }
  
  uint32_t size = f.size();
  if (size == 0) {
    f.close();
    return 0;
  }
  
  // Lakukan backtrack mundur maksimal 2000 byte untuk mencari newline
  uint32_t searchStart = (size > 2000) ? (size - 2000) : 0;
  uint32_t lastNewlinePos = 0;
  bool found = false;
  
  for (int32_t pos = size - 1; pos >= (int32_t)searchStart; pos--) {
    f.seek(pos);
    char c = f.read();
    if (c == '\n') {
      lastNewlinePos = pos;
      found = true;
      break;
    }
  }
  f.close();
  
  if (found) {
    // Posisi setelah '\n' adalah awal baris baru yang valid
    return lastNewlinePos + 1;
  }
  
  // Jika file terisi tapi tidak ada newline sama sekali, timpa dari 0
  return 0;
}
