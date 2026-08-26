/**
 * EXCA03 Configuration
 * Copy this file to exca_hybrid_v2.ino and update EXCA_ID/AP_SSID
 * 
 * NOTE: EXCA03 has worst backlog (75.7% heavy backlog, 16h avg delay)
 * This firmware should help significantly
 */

// ================= DEVICE ID =================
const char *EXCA_ID = "EXCA03";
const char *AP_SSID = "EXCA03_DATA";
const char *AP_PASS = "12345678";

// ================= WIFI CREDENTIALS =================
WifiCredential wifiList[] = {{"WIFI_GATEWAY_MINING_11", "46448951"},
                             {"HOTSPOT_DT_KEAMANAN", "46448951"}};

// ================= TUNING (based on location) =================
// EXCA03: Poor WiFi coverage (75% heavy backlog)
// Recommended: More aggressive to catch up backlog

const int MAX_UPLOAD_CHUNK = 50;
const unsigned long INTERNET_INTERVAL = 15000;  // Aggressive polling
const unsigned long COMPACT_INTERVAL = 1800000;
