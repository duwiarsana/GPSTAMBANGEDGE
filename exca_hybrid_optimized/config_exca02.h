/**
 * EXCA02 Configuration
 * Copy this file to exca_hybrid_v2.ino and update EXCA_ID/AP_SSID
 * 
 * NOTE: EXCA02 offline since 2026-08-21 (3 days)
 * This firmware will help catch up backlog faster when online
 */

// ================= DEVICE ID =================
const char *EXCA_ID = "EXCA02";
const char *AP_SSID = "EXCA02_DATA";
const char *AP_PASS = "12345678";

// ================= WIFI CREDENTIALS =================
WifiCredential wifiList[] = {{"WIFI_GATEWAY_MINING_11", "46448951"},
                             {"HOTSPOT_DT_KEAMANAN", "46448951"}};

// ================= TUNING (based on location) =================
// EXCA02: Unknown coverage (offline)
// Recommended: More aggressive to catch up backlog

const int MAX_UPLOAD_CHUNK = 50;
const unsigned long INTERNET_INTERVAL = 15000;  // Aggressive polling
const unsigned long COMPACT_INTERVAL = 1800000;
