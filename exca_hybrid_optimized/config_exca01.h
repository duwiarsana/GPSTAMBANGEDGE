/**
 * EXCA01 Configuration
 * Copy this file to exca_hybrid_v2.ino and update EXCA_ID/AP_SSID
 */

// ================= DEVICE ID =================
const char *EXCA_ID = "EXCA01";
const char *AP_SSID = "EXCA01_DATA";
const char *AP_PASS = "12345678";

// ================= WIFI CREDENTIALS =================
WifiCredential wifiList[] = {{"WIFI_GATEWAY_MINING_11", "46448951"},
                             {"HOTSPOT_DT_KEAMANAN", "46448951"}};

// ================= TUNING (based on location) =================
// EXCA01: Good WiFi coverage (based on 47% backlog vs EXCA03's 75%)
// Recommended: Keep default settings

const int MAX_UPLOAD_CHUNK = 50;
const unsigned long INTERNET_INTERVAL = 15000;
const unsigned long COMPACT_INTERVAL = 1800000;
