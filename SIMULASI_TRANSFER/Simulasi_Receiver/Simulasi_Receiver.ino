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

  // 2. Baca Offset terakhir
  readOffset();
  Serial.print("Last Offset: ");
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
    Serial.println("Sender Ready. Requesting data from offset...");
    client.print("GET ");
    client.println(lastOffset);

    res = client.readStringUntil('\n');
    res.trim();

    if (res == "OK_START") {
      Serial.println("Transfer started!");
      
      File receivedFile = SD.open("/received_log.jsonl", FILE_APPEND);
      if (!receivedFile) {
        Serial.println("Failed to open /received_log.jsonl for writing!");
        return;
      }

      int lineCount = 0;
      while (client.connected()) {
        client.println("NEXT");
        
        String dataLine = client.readStringUntil('\n');
        dataLine.trim();

        if (dataLine == "END") {
          Serial.println("\nTransfer complete (EOF).");
          client.println("OK");
          break;
        } else if (dataLine == "") {
          // Timeout or empty
          Serial.println("\nEmpty data or timeout.");
          break;
        } else {
          // Simpan data
          receivedFile.println(dataLine);
          receivedFile.flush();
          
          // Update offset
          lastOffset += (dataLine.length() + 1); // +1 untuk newline
          saveOffset(lastOffset);
          
          // Blink LED 13 as transfer indicator
          digitalWrite(PIN_LED_CONN, !digitalRead(PIN_LED_CONN));
          
          lineCount++;
          if (lineCount % 10 == 0) {
            Serial.print("#"); // Progress indicator
            if (lineCount % 500 == 0) Serial.printf(" [%d lines]\n", lineCount);
          }
        }
      }
      receivedFile.close();
      Serial.printf("\nSuccess! Received %d lines.\n", lineCount);
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

void readOffset() {
  if (SD.exists("/offset.txt")) {
    File f = SD.open("/offset.txt", FILE_READ);
    if (f) {
      String val = f.readStringUntil('\n');
      lastOffset = val.toInt();
      f.close();
    }
  } else {
    lastOffset = 0;
  }
}

void saveOffset(uint32_t offset) {
  File f = SD.open("/offset.txt", FILE_WRITE);
  if (f) {
    f.println(offset);
    f.close();
  }
}
