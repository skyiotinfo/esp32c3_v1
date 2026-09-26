#pragma once
// ============================================================================
// config.h — all compile-time constants for one physical unit.
//
// The only things you should ever need to edit per physical device are in
// the "DEVICE IDENTITY" block below. WiFi has a hardcoded fallback for the
// very first boot (see DEFAULT_WIFI_SSID/PASS); Supabase login (email/
// password) has no hardcoded fallback and is only ever set by BLE
// provisioning.
// ============================================================================

// ─────────────────────────── PIN MAP (ESP32-C3-DevKitC-02) ────────────────
// Carried over unchanged from the working esp32c3 board file (esp_supa_v4).
#define RTC_SDA        5
#define RTC_SCL        4
#define MOTOR_PIN      0
#define CLK_PIN        1
#define DIO_PIN        2
#define OT_SENSOR_PIN  7
#define BUTTON_PIN     9   // active-LOW with INPUT_PULLUP; manual motor on/off
#define BUTTON_DEBOUNCE_MS 200

// Dedicated BLE-provisioning trigger button — deliberately a DIFFERENT pin
// from BUTTON_PIN. GPIO9 (BUTTON_PIN, also the board's silkscreened "BOOT"
// button) is one of the ESP32-C3's three strapping pins (GPIO2, GPIO8,
// GPIO9): if it's held LOW at the exact instant of power-on/reset, the ROM
// bootloader can drop into UART download mode instead of running this
// firmware at all - a risk that's entirely a hardware/ROM behavior, outside
// this code's control. PROVISION_BUTTON_PIN=10 avoids all three strapping
// pins (2, 8, 9), so holding it through power-up is always safe and always
// reaches setup() normally.
#define PROVISION_BUTTON_PIN  10   // active-LOW with INPUT_PULLUP; hold at boot to provision

// ─────────────────────────── DEVICE IDENTITY ───────────────────────────────
// DEVICE_ID must be unique per physical unit and must match the device_id
// row the mobile app creates via claim_device_direct() when the user scans
// and provisions this board over BLE (BLE advertises "PumpCtrl-<DEVICE_ID>").
#define DEVICE_ID              1001
#define FIRMWARE_VERSION       "4.0.0-esp32c3"

#define BLE_DEVICE_NAME        "PumpCtrl-1001"   // keep in sync with DEVICE_ID above
#define BLE_SERVICE_UUID       "12345678-1234-1234-1234-1234567890ab"
#define BLE_CHAR_UUID          "abcd1234-ab12-ab12-ab12-abcdef123456"

// ─────────────────────────── DEFAULT WIFI (first boot only) ────────────────
// Used only when NVS has no saved WiFi SSID yet (a brand-new, never-BLE-
// provisioned unit) - e.g. a factory/staging network so the device comes up
// on WiFi (and can time-sync, be reached, etc.) before the end customer ever
// runs BLE provisioning. The moment real BLE provisioning succeeds, the
// app-supplied SSID/password overwrite these in NVS and take over for good.
#define DEFAULT_WIFI_SSID      "anupam"
#define DEFAULT_WIFI_PASS      "12345678"

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
#define STATUS_PRINT_INTERVAL_MS    (1UL * 1000)             // serial diagnostics only

// FIX vs. the ESP8266 source: there MAX_SAFETY_RUNTIME_MIN was defined as a
// *millisecond* value (30UL*60*1000) but compared directly against a count
// of *minutes* in checkSafetyCutoff(), so the cutoff could never fire
// (it needed ~3.4 years of continuous runtime). Here it is a plain minute
// count, matched correctly against elapsedMin below.
#define MAX_SAFETY_RUNTIME_MIN      30

#define MAX_SCHEDULES               8
#define HTTP_CONNECT_TIMEOUT_MS     3000    // bounds the TCP/TLS connect phase of every call
#define HTTP_RESPONSE_TIMEOUT_MS    5000    // bounds waiting for a response after connecting

#define BOOT_REPROVISION_HOLD_MS    5000              // hold PROVISION_BUTTON_PIN this long at boot to enter provisioning
#define BLE_PROVISION_TIMEOUT_MS    (10UL * 60 * 1000) // give up waiting for the app after 10 minutes

// ─────────────────────────── SPRINKLER / MOTOR-BOARD LINK (additive) ───────
// Optional, additional feature: this same ESP32 can ALSO act as a "Supabase
// board" driving a separate ESP8266 motor board (over SoftwareSerial), which
// fans out to up to MAX_VALVES ESP-NOW valve nodes. Nothing above this is
// touched by this feature - the existing single-relay pump/device behavior
// (schedules, BLE provisioning, EEPROM, reports) runs exactly as before.
//
// A valve is just an ordinary child `device` row (device_type = 2 'valve',
// parent_device_id = DEVICE_ID) - the app's existing MotorValvesScreen /
// listChildDevices() / device_seq flow already fully supports this, so no
// new Supabase table is needed. Each child's position in the list (ordered
// by device_id) is its motor-board node number (1..MAX_VALVES).
//
// The motor board and valve-node firmware are UNCHANGED: the wire protocol
// below ("{P v1v2...v8}\n" in both directions) matches their existing code
// byte for byte.
#define MAX_VALVES                    8
#define SPRINKLER_MOTOR_RX_PIN        6   // ESP32 RX <- motor board TX (its D1/GPIO5)
#define SPRINKLER_MOTOR_TX_PIN        7   // ESP32 TX -> motor board RX (its D2/GPIO4)
#define SPRINKLER_MOTOR_BAUD          9600
#define SPRINKLER_CHILD_POLL_MS       (10UL * 1000)      // refresh children + state_1 (mirrors COMMAND_POLL_INTERVAL_MS)
#define SPRINKLER_SCHEDULE_POLL_MS    (2UL * 60 * 1000)  // refresh each child's device_seq/sch (mirrors SCHEDULE_FETCH_INTERVAL_MS)
