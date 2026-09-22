#pragma once
// ============================================================================
// config.h — all compile-time constants for one physical unit.
//
// The only things you should ever need to edit per physical device are in
// the "DEVICE IDENTITY" block below. Everything else (WiFi credentials,
// Supabase login email/password) is provisioned at first boot over BLE by
// the mobile app — it is NOT hardcoded here, unlike the old ESP8266 sketch.
// ============================================================================

// ─────────────────────────── PIN MAP (ESP32-C3-DevKitC-02) ────────────────
// Carried over unchanged from the working esp32c3 board file (esp_supa_v4).
#define RTC_SDA        5
#define RTC_SCL        4
#define MOTOR_PIN      0
#define CLK_PIN        1
#define DIO_PIN        2
#define OT_SENSOR_PIN  7
#define BUTTON_PIN     9   // active-LOW with INPUT_PULLUP; also the BOOT button

// ─────────────────────────── DEVICE IDENTITY ───────────────────────────────
// DEVICE_ID must be unique per physical unit and must match the device_id
// row the mobile app creates via claim_device_direct() when the user scans
// and provisions this board over BLE (BLE advertises "PumpCtrl-<DEVICE_ID>").
#define DEVICE_ID              1001
#define FIRMWARE_VERSION       "4.0.0-esp32c3"

#define BLE_DEVICE_NAME        "PumpCtrl-1001"   // keep in sync with DEVICE_ID above
#define BLE_SERVICE_UUID       "12345678-1234-1234-1234-1234567890ab"
#define BLE_CHAR_UUID          "abcd1234-ab12-ab12-ab12-abcdef123456"

// ─────────────────────────── DEFAULT SUPABASE PROJECT ──────────────────────
// These are only *defaults* seeded into NVS the first time the device boots
// with no saved credentials; the BLE provisioning payload from the app can
// override them (see credentials.h). They point at the same project the
// mobile app (.env) and the new schema (supabase_all_tables.sql) use.
#define DEFAULT_SUPABASE_URL   "https://pzwatyfdvltsvelppdew.supabase.co"
#define DEFAULT_SUPABASE_KEY   "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InB6d2F0eWZkdmx0c3ZlbHBwZGV3Iiwicm9sZSI6ImFub24iLCJpYXQiOjE3ODg1ODY4NzQsImV4cCI6MjEwNDE2Mjg3NH0.v1ilcWbF62gpnBvN4gBB6_qEETqie_mNabEa-K28oGw"

// ─────────────────────────── EEPROM (emulated flash) LAYOUT ───────────────
#define EEPROM_SIZE                 32
#define EEPROM_ADDR_MANUAL_UNTIL     0   // 4 bytes  uint32_t (0 = no block)
#define EEPROM_ADDR_OT_TRIPPED       4   // 1 byte   uint8_t  (1 = tripped)
#define EEPROM_ADDR_LAST_STATE1      5   // 1 byte   uint8_t  (0/1)
#define EEPROM_ADDR_MOTOR_ON_SINCE   6   // 4 bytes  uint32_t (0 = not running)

// ─────────────────────────── TUNABLES ──────────────────────────────────────
#define OT_TRIP_COUNT               5
#define WDT_TIMEOUT_S               60
#define TOKEN_REFRESH_BEFORE_S      120
#define DRIFT_THRESHOLD_S           30
#define TIME_SYNC_INTERVAL_MS       (1UL * 60 * 60 * 1000)   // 1 hour
#define COMMAND_POLL_INTERVAL_MS    (10UL * 1000)            // read state_1/2
#define HEARTBEAT_INTERVAL_MS       (30UL * 1000)            // report_device_state
#define SCHEDULE_FETCH_INTERVAL_MS  (2UL * 60 * 1000)        // re-read device_seq/sch
#define WIFI_RETRY_INTERVAL_MS      (30UL * 1000)
#define INTERNET_CHECK_INTERVAL_MS  (10UL * 1000)

// FIX vs. the ESP8266 source: there MAX_SAFETY_RUNTIME_MIN was defined as a
// *millisecond* value (30UL*60*1000) but compared directly against a count
// of *minutes* in checkSafetyCutoff(), so the cutoff could never fire
// (it needed ~3.4 years of continuous runtime). Here it is a plain minute
// count, matched correctly against elapsedMin below.
#define MAX_SAFETY_RUNTIME_MIN      30

#define MAX_SCHEDULES               8
#define HTTP_CONNECT_TIMEOUT_MS     3000    // bounds the TCP/TLS connect phase of every call
#define HTTP_RESPONSE_TIMEOUT_MS    5000    // bounds waiting for a response after connecting

#define BOOT_REPROVISION_HOLD_MS    5000    // hold BUTTON_PIN at boot this long to re-provision
