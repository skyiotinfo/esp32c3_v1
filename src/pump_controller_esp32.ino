// ================================================================
//  Pump Controller — ESP8266 → ESP32 Port + Bug Fixes
//  Changes from v1 (ESP8266):
//   1. ESP8266 D-pin macros → ESP32 GPIO numbers
//   2. ESP8266WiFi / ESP8266HTTPClient → WiFi / HTTPClient (ESP32)
//   3. WiFiClientSecure added (client.setInsecure())
//   4. Global HTTPClient https reuse REMOVED → local client per call
//      (global reuse causes hard crashes on ESP32)
//   5. StaticJsonDocument → JsonDocument  (ArduinoJson v7)
//   6. u_int16_t → uint16_t  (non-portable, fails on ESP32 toolchain)
//   7. login_email(): unreachable https.end() after return removed
//   8. authTimeout countdown logic fixed: decrements properly &
//      re-login triggers at <= 0, not the broken <= 100 check
//   9. isOnline(): redundant double-HTTP replaced with single WiFi
//      status check + lightweight HEAD to skyiottech
//  10. hourMinuteToUnixUTC: uses gmtime_r() for correct UTC math
//      (mktime() on ESP32 uses local TZ; gmtime_r is TZ-safe)
//  11. Hardware watchdog (WDT) added — 60 s auto-reboot on stall
//  12. esp_task_wdt_reset() called in loop + blocking waits
//  13. millis() overflow-safe sync flag (timeSynced bool) from v2
//  14. OT trip persisted across reboot via EEPROM flag
//  15. appManualStop replaced with appManualStopUntil (unix stamp)
//      so only the CURRENT schedule window is blocked on app-stop,
//      not all future schedules (ported from v2 fix #18)
// ================================================================

#include <Arduino.h>
#include <ArduinoJson.h>        // v7 — JsonDocument replaces StaticJsonDocument
#include <EEPROM.h>
#include <time.h>
#include <TM1637Display.h>
//#include <PZEM004Tv30.h>
#include <Wire.h>
#include "RTClib.h"
#include <WiFi.h>               // FIX 2: ESP32 WiFi
#include <HTTPClient.h>         // FIX 2: ESP32 HTTPClient
#include <WiFiClientSecure.h>   // FIX 3: required for https on ESP32
#include <esp_task_wdt.h>       // FIX 11: hardware watchdog

// ── Forward declarations ──────────────────────────────────────
void compareAndSyncTime();
void updateTable(String token, int st, int ds);
void updateackTable(String token, int ack);
int  login_email(String email_a, String password_a);
bool isOnline();
void saveEepromIfDirty();
void loadScheduleFromEEPROM();

// ── Device ID ────────────────────────────────────────────────
const int device_id = 110001;

RTC_DS1307 rtc;

// ── Pin mapping  (FIX 1: D-pin macros removed, ESP32 GPIO) ───
#define MOTOR_PIN       13   // was D8  (GPIO 15 on 8266, use 13 on C3/S3 as needed)
#define CLK_PIN          1   // was D3
#define DIO_PIN          2   // was D4
#define OT_SENSOR_PIN    7   // was D7
#define OT_ACTIVE_LEVEL LOW
#define BUTTON_PIN       9   // was D9 / input1

TM1637Display display(CLK_PIN, DIO_PIN);

// ── EEPROM layout ─────────────────────────────────────────────
#define EEPROM_SIZE              128
#define EEPROM_START_UNIX_ADDR    10
#define EEPROM_STOP_UNIX_ADDR     25
#define EEPROM_LOCAL_STATE        40
#define EEPROM_APP_MANUAL_STATUS  42  // kept for layout compat
#define EEPROM_UPDATED_BY         44
#define EEPROM_ADDR_OT_TRIPPED    46  // FIX 14: OT persistence
#define EEPROM_ADDR_MANUAL_UNTIL  50  // FIX 15: 4-byte unix stamp

// ── WDT ───────────────────────────────────────────────────────
#define WDT_TIMEOUT_S 60            // FIX 11

// ── OT sensor ─────────────────────────────────────────────────
int ot_sensorcount = 0;
const int OT_TRIP_COUNT = 5;
bool otTripped = false;             // FIX 14

// ── Schedule struct ───────────────────────────────────────────
struct sch {
  int      device_id;
  uint32_t startUnix;
  uint32_t stopUnix;
  uint8_t  startTime;
  uint8_t  stopTime;
  int      duration;
  int      active;
  int      state;
  int      ack;
  int      sch1_en;
  int      updated_by;
  int      local_state;
};

sch sch1;
sch updated_sch1;

// ── Auth / network state ──────────────────────────────────────
int    login_status  = 0;
int    authTimeout   = 0;          // seconds remaining; FIX 8
String phone_or_email;
String password;
String loginMethod;
String USER_TOKEN;

// FIX 15: replaces bool appManualStop.
// Holds the unix timestamp until which the schedule is blocked.
// 0 = not blocked. Auto-expires when nowUnix >= appManualStopUntil.
uint32_t appManualStopUntil = 0;

// ── Timing ────────────────────────────────────────────────────
unsigned long mili_now;
unsigned long lastExecutionTime = 0;
long          EXECUTION_INTERVAL = 10000;
uint32_t      lastSavedStartUnix = 0;
uint32_t      lastSavedStopUnix  = 0;
uint8_t       savedOtTrip        = 0xFF;
uint32_t      savedManualUntil   = 0xFFFFFFFF;
int           count              = 0;
bool          timeSynced         = false; // FIX 13: replaces lastSync==0
unsigned long lastSyncMs         = 0;
const unsigned long SYNC_INTERVAL_MS = 1UL * 60 * 60 * 1000;

// ── Supabase / WiFi credentials ───────────────────────────────
char WIFI_SSID[20]     = "4G-UFI-E929";
char WIFI_PASS[20]     = "1234567890";
char SUPABASE_URL[100] = "https://fkgfdgwpqqfxhnyuwtwe.supabase.co";
char AUTH_URL[120]     = "https://fkgfdgwpqqfxhnyuwtwe.supabase.co/auth/v1/token?grant_type=password";
char SUPABASE_KEY[300] = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImZrZ2ZkZ3dwcXFmeGhueXV3dHdlIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NjAzMzQzNzQsImV4cCI6MjA3NTkxMDM3NH0.Dn805WO5wyPa25yD5fYYcCzB4TgDbnTCb4zBuCiczZU";
char USER_EMAIL[30]    = "9630852741@gmail.com";
char USER_PASS[10]     = "123456";

const char* supabase_device_url =
  "https://fkgfdgwpqqfxhnyuwtwe.supabase.co/rest/v1/pump_motor?device_id=eq.110001";

// ── Drift / sync ──────────────────────────────────────────────
const long DRIFT_THRESHOLD = 30;

int lastAppState = -1;

// ================================================================
//  HTTP HELPERS — local client per call (FIX 4)
//  On ESP32 reusing a global HTTPClient across calls causes crashes.
//  Each helper creates its own WiFiClientSecure + HTTPClient,
//  uses it, and destroys it when the function returns.
// ================================================================

// Generic PATCH helper
static bool _httpPatch(const char* url, const String& body, const String& token) {
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClientSecure cl;
  cl.setInsecure();                 // FIX 3
  HTTPClient https;
  https.setTimeout(4000);
  if (!https.begin(cl, url)) return false;
  https.addHeader("apikey",        SUPABASE_KEY);
  https.addHeader("Authorization", "Bearer " + token);
  https.addHeader("Content-Type",  "application/json");
  https.addHeader("Prefer",        "return=minimal");
  int code = https.sendRequest("PATCH", body);
  Serial.printf("[HTTP PATCH] %d\n", code);
  https.end();
  return (code >= 200 && code < 300);
}

// Generic GET helper — returns body via out param
static bool _httpGet(const char* url, const String& token, String& out) {
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClientSecure cl;
  cl.setInsecure();
  HTTPClient https;
  https.setTimeout(4000);
  if (!https.begin(cl, url)) return false;
  https.addHeader("apikey",        SUPABASE_KEY);
  https.addHeader("Authorization", "Bearer " + token);
  https.addHeader("Content-Type",  "application/json");
  int code = https.GET();
  Serial.printf("[HTTP GET] %d\n", code);
  if (code == 200) out = https.getString();
  https.end();
  return (code == 200);
}

// ── Public API wrappers (same names as v1 for compatibility) ──
void updateTable(String token, int st, int ds) {
  String payload = "{\"state\":" + String(st) +
                   ",\"device_state\":" + String(ds) + "}";
  _httpPatch(supabase_device_url, payload, token);
}

void updateackTable(String token, int ack) {
  String payload = "{\"ack\":" + String(ack) + "}";
  _httpPatch(supabase_device_url, payload, token);
}

void heartbeat(String token, int value) {
  String payload = "{\"heart_beat_count\":" + String(value) + "}";
  _httpPatch(supabase_device_url, payload, token);
}

// ================================================================
//  isOnline()  (FIX 9)
//  v1 made two HTTP calls: one in isOnline() and one for the actual
//  request. Replaced with WiFi check + single lightweight GET.
// ================================================================
bool isOnline() {
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClientSecure cl;
  cl.setInsecure();
  HTTPClient https;
  https.setTimeout(5000);
  if (!https.begin(cl, "https://api.skyiottech.com/time")) return false;
  int code = https.GET();
  https.end();
  return (code == 200);
}

// ================================================================
//  Schedule defaults
// ================================================================
void loadSchedules() {
  sch1 = {
    110001,      // device_id
    1767225600,  // startUnix
    1767225600,  // stopUnix
    0, 0,        // startTime, stopTime
    10,          // duration (minutes)
    0,           // active
    0,           // state
    0,           // ack
    0,           // sch1_en
    0,           // updated_by
    0            // local_state
  };
}

// ================================================================
//  hourMinuteToUnixUTC  (FIX 10)
//  v1 used DateTime arithmetic which is UTC-safe only if the RTC
//  itself is set to UTC. On ESP32 mktime() uses the local TZ
//  configured by configTime(). Using gmtime_r gives explicit UTC.
// ================================================================
uint32_t hourMinuteToUnixUTC(uint8_t hour, uint8_t minute) {
  time_t now = rtc.now().unixtime();
  struct tm t;
  gmtime_r(&now, &t);   // decompose as UTC
  t.tm_hour = hour;
  t.tm_min  = minute;
  t.tm_sec  = 0;
  // timegm() not available on ESP32; calculate UTC offset manually
  // Count days since epoch and convert to seconds
  static const int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int days = 0;
  for (int y = 1970; y < 1900 + t.tm_year; y++) {
    days += (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
  }
  for (int m = 0; m < t.tm_mon; m++) {
    days += daysInMonth[m];
    if (m == 1 && (1900 + t.tm_year) % 4 == 0 && ((1900 + t.tm_year) % 100 != 0 || (1900 + t.tm_year) % 400 == 0)) days++;
  }
  days += t.tm_mday - 1;
  return (uint32_t)(days * 86400UL + hour * 3600UL + minute * 60UL);
}

// ================================================================
//  EEPROM helpers  (FIX 14, FIX 15)
// ================================================================
void saveEepromIfDirty() {
  bool dirty = false;

  if (sch1.startUnix != lastSavedStartUnix) {
    EEPROM.put(EEPROM_START_UNIX_ADDR, sch1.startUnix);
    lastSavedStartUnix = sch1.startUnix;
    dirty = true;
  }
  if (sch1.stopUnix != lastSavedStopUnix) {
    EEPROM.put(EEPROM_STOP_UNIX_ADDR, sch1.stopUnix);
    lastSavedStopUnix = sch1.stopUnix;
    dirty = true;
  }

  // OT trip persistence (FIX 14)
  uint8_t o = otTripped ? 1 : 0;
  if (o != savedOtTrip) {
    EEPROM.put(EEPROM_ADDR_OT_TRIPPED, o);
    savedOtTrip = o;
    dirty = true;
  }

  // appManualStopUntil persistence (FIX 15)
  if (appManualStopUntil != savedManualUntil) {
    EEPROM.put(EEPROM_ADDR_MANUAL_UNTIL, appManualStopUntil);
    savedManualUntil = appManualStopUntil;
    dirty = true;
  }

  if (dirty) {
    EEPROM.commit();
    Serial.println("[EEPROM] Committed");
  } else {
    Serial.println("[EEPROM] No change");
  }
}

// Kept for API compatibility — now delegates to saveEepromIfDirty
void saveScheduleToEEPROM() { saveEepromIfDirty(); }

void loadScheduleFromEEPROM() {
  uint32_t s, e, mu; uint8_t o;
  EEPROM.get(EEPROM_START_UNIX_ADDR,    s);
  EEPROM.get(EEPROM_STOP_UNIX_ADDR,     e);
  EEPROM.get(EEPROM_ADDR_OT_TRIPPED,    o);
  EEPROM.get(EEPROM_ADDR_MANUAL_UNTIL,  mu);

  bool valid = (s > 1000000000UL) && (e > s);
  if (valid) {
    sch1.startUnix = s;
    sch1.stopUnix  = e;
  } else {
    Serial.println("Invalid EEPROM data – using defaults");
    loadSchedules();
  }

  otTripped           = (o == 1);
  // 0xFFFFFFFF = uninitialized flash → treat as no block
  appManualStopUntil  = (mu == 0xFFFFFFFF) ? 0 : mu;

  sch1.active = 0;
  sch1.state  = 0;

  lastSavedStartUnix = sch1.startUnix;
  lastSavedStopUnix  = sch1.stopUnix;
  savedOtTrip        = o;
  savedManualUntil   = appManualStopUntil;

  Serial.printf("[EEPROM] start=%u stop=%u ot=%d manualUntil=%u\n",
                sch1.startUnix, sch1.stopUnix, (int)otTripped, appManualStopUntil);
}

// ================================================================
//  OT Sensor  (FIX 14: otTripped persisted + blocks restart)
// ================================================================
void processOTSensor() {
  if (digitalRead(MOTOR_PIN) == LOW) { ot_sensorcount = 0; return; }

  if (digitalRead(OT_SENSOR_PIN) == OT_ACTIVE_LEVEL) {
    ot_sensorcount++;
    Serial.print("OT Sensor Count: "); Serial.println(ot_sensorcount);

    if (ot_sensorcount >= OT_TRIP_COUNT) {
      Serial.println("OT Sensor → MOTOR OFF");
      digitalWrite(MOTOR_PIN, LOW);
      sch1.active = 0;
      sch1.state  = 0;
      sch1.local_state = 1;
      otTripped = true;
      // FIX 15: block the current schedule window
      appManualStopUntil = (sch1.stopUnix > 0)
                         ? sch1.stopUnix
                         : (uint32_t)rtc.now().unixtime() + (uint32_t)(sch1.duration * 60);
      saveEepromIfDirty();
      if (isOnline()) {
        updateTable(USER_TOKEN, 0, 0);
        updateackTable(USER_TOKEN, 0);
        sch1.local_state = 0;
      }
      ot_sensorcount = 0;
    }
  } else {
    ot_sensorcount = 0;
  }
}

// ================================================================
//  checkSch  (FIX 15: uses appManualStopUntil, auto-expiry)
// ================================================================
void checkSch(uint32_t nowUnix) {
  // Auto-expire the manual-stop block when the blocked window ends
  if (appManualStopUntil > 0 && nowUnix >= appManualStopUntil) {
    Serial.println("[SCH] Manual-stop window expired – block cleared");
    appManualStopUntil = 0;
    saveEepromIfDirty();
  }

  // START schedule
  if (sch1.active == 0 &&
      nowUnix >= sch1.startUnix &&
      nowUnix <  sch1.stopUnix  &&
      sch1.sch1_en == 1         &&
      !(appManualStopUntil > 0 && nowUnix < appManualStopUntil) && // FIX 15
      !otTripped) {                                                  // FIX 14
    sch1.active      = 1;
    sch1.state       = 1;
    sch1.local_state = 1;
    digitalWrite(MOTOR_PIN, HIGH);
    Serial.println("SCHEDULE START");
    if (isOnline()) {
      updateTable(USER_TOKEN, 1, 1);
      updateackTable(USER_TOKEN, 0);
      sch1.local_state = 0;
    }
  }

  // STOP schedule
  if (sch1.active == 1 && nowUnix >= sch1.stopUnix) {
    sch1.active      = 0;
    sch1.state       = 0;
    sch1.local_state = 1;
    digitalWrite(MOTOR_PIN, LOW);
    Serial.println("SCHEDULE STOP");
    if (isOnline()) {
      updateTable(USER_TOKEN, 0, 0);
      appManualStopUntil = 0;  // clear block at natural stop
      sch1.local_state   = 0;
      saveEepromIfDirty();
    }
  }
}

// ================================================================
//  getTableData  (FIX 4: local client; FIX 5: JsonDocument;
//                 FIX 6: uint16_t; FIX 15: appManualStopUntil)
// ================================================================
void getTableData(String token, String field) {
  String resp;
  if (!_httpGet(supabase_device_url, token, resp)) return;

  JsonDocument doc;            // FIX 5: was StaticJsonDocument<512>
  if (deserializeJson(doc, resp)) { Serial.println("JSON parse fail"); return; }

  heartbeat(token, 10);

  uint32_t duration      = doc[0]["sch1_duration"] | (uint32_t)10;
  updated_sch1.state     = doc[0]["state"]         | 0;
  sch1.ack               = doc[0]["ack"]            | 0;
  sch1.sch1_en           = doc[0]["sch1_en"]        | 0;
  int sync_duration      = doc[0]["sync_duration"]  | 0;
  if (sync_duration > 0) EXECUTION_INTERVAL = (long)sync_duration * 1000L;

  bool appOffPressed = (lastAppState == 1 && updated_sch1.state == 0);
  lastAppState = updated_sch1.state;

  if (sch1.local_state == 1) {
    Serial.println("Local state change → skipping app command");
    updateTable(USER_TOKEN, sch1.state, sch1.state);
    sch1.local_state = 0;
    // still parse schedule below
  } else {
    // App ON command
    if (updated_sch1.state == 1 && !otTripped &&     // FIX 14
        digitalRead(MOTOR_PIN) == LOW && sch1.ack == 1) {
      digitalWrite(MOTOR_PIN, HIGH);
      sch1.state         = 1;
      count              = 0;
      appManualStopUntil = 0;  // FIX 15: clear block when app turns ON
      saveEepromIfDirty();
      updateTable(USER_TOKEN, 1, 1);
      updateackTable(USER_TOKEN, 0);
      Serial.println("Motor ON from App");
    }

    // App OFF command
    if (updated_sch1.state == 0 && digitalRead(MOTOR_PIN) == HIGH && sch1.ack == 1) {
      digitalWrite(MOTOR_PIN, LOW);
      sch1.active = 0;
      sch1.state  = 0;
      // FIX 15: block only the current window, not all future schedules
      appManualStopUntil = (sch1.stopUnix > 0)
                           ? sch1.stopUnix
                           : (uint32_t)rtc.now().unixtime() + (uint32_t)(sch1.duration * 60);
      Serial.printf("Motor OFF from App – blocked until unix %u\n", appManualStopUntil);
      saveEepromIfDirty();
      updateTable(USER_TOKEN, 0, 0);
      updateackTable(USER_TOKEN, 0);
    }
  }

  // Parse schedule time
  const char* schTime = doc[0]["sch1_start"] | "00:00";
  uint16_t s1 = 0, s2 = 0;                    // FIX 6: was u_int16_t
  if (schTime && strlen(schTime) >= 5) {
    s1 = (schTime[0]-'0')*10 + (schTime[1]-'0');
    s2 = (schTime[3]-'0')*10 + (schTime[4]-'0');
  }
  updated_sch1.startUnix = hourMinuteToUnixUTC((uint8_t)s1, (uint8_t)s2);
  updated_sch1.stopUnix  = updated_sch1.startUnix + (duration * 60);

  if (sch1.startUnix != updated_sch1.startUnix ||
      sch1.stopUnix  != updated_sch1.stopUnix) {
    sch1.startUnix = updated_sch1.startUnix;
    sch1.stopUnix  = updated_sch1.stopUnix;
    sch1.active    = 0;
    // FIX 15: if schedule window changed, expire any stale block
    if (appManualStopUntil > 0 && appManualStopUntil <= sch1.startUnix) {
      appManualStopUntil = 0;
    }
    saveEepromIfDirty();
  }
}

// ================================================================
//  Internet time  (FIX 4: local client)
// ================================================================
bool getInternetUnixTime(time_t &unixTime) {
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClientSecure cl;
  cl.setInsecure();
  HTTPClient https;
  https.setTimeout(4000);
  if (!https.begin(cl, "https://api.skyiottech.com/time")) return false;
  int code = https.GET();
  if (code != 200) { https.end(); return false; }
  JsonDocument doc;      // FIX 5
  deserializeJson(doc, https.getString());
  https.end();
  unixTime = doc["unix_time"].as<unsigned long>();
  return true;
}

time_t getRtcUnixTime() {
  DateTime now = rtc.now();
  Serial.print("RTC Time: "); Serial.println(now.unixtime());
  return now.unixtime();
}

// ================================================================
//  compareAndSyncTime  (FIX 13: timeSynced bool, not lastSync==0)
// ================================================================
void compareAndSyncTime() {
  if (!rtc.isrunning()) { Serial.println("RTC not running"); return; }
  if (timeSynced && (millis() - lastSyncMs < SYNC_INTERVAL_MS)) {
    Serial.println("Time sync not needed yet.");
    return;
  }
  time_t internetTime;
  if (!getInternetUnixTime(internetTime)) {
    Serial.println("Failed to get internet time.");
    return;
  }
  time_t rtcTime = getRtcUnixTime();
  long drift = abs((long)(internetTime - rtcTime));
  Serial.print("RTC drift: "); Serial.println(drift);
  if (drift > DRIFT_THRESHOLD) {
    rtc.adjust(DateTime((uint32_t)internetTime));
    Serial.println("RTC resynced");
  }
  timeSynced = true;       // FIX 13
  lastSyncMs = millis();
}

// ================================================================
//  Login  (FIX 4: local client; FIX 7: removed unreachable end();
//          FIX 8: authTimeout fixed)
// ================================================================
int _login_process() {
  if (WiFi.status() != WL_CONNECTED) return -1;
  WiFiClientSecure cl;
  cl.setInsecure();
  HTTPClient https;
  https.setTimeout(5000);
  if (!https.begin(cl, AUTH_URL)) return -100;

  https.addHeader("apikey",       SUPABASE_KEY);
  https.addHeader("Content-Type", "application/json");
  String query = "{\"email\": \"" + phone_or_email + "\", \"password\": \"" + password + "\"}";
  int httpCode = https.POST(query);

  if (httpCode > 0) {
    String data = https.getString();
    JsonDocument doc;        // FIX 5
    deserializeJson(doc, data);
    if (doc.containsKey("access_token") &&
        !doc["access_token"].isNull() &&
        doc["access_token"].is<const char*>()) {
      USER_TOKEN  = doc["access_token"].as<String>();
      authTimeout = doc["expires_in"].as<int>(); // FIX 8: store full seconds
      Serial.println("Login Success");
    } else {
      Serial.println("Login Failed: Invalid access token");
    }
  } else {
    Serial.print("Login Failed: "); Serial.println(httpCode);
  }

  https.end();
  // FIX 7: https.end() was unreachable in v1 (after return). Fixed here.
  return httpCode;
}

int login_email(String email_a, String password_a) {
  loginMethod   = "email";
  phone_or_email = email_a;
  password       = password_a;
  int httpCode   = 0;
  while (httpCode <= 0) {
    esp_task_wdt_reset();   // FIX 12: pet watchdog in blocking loop
    httpCode = _login_process();
  }
  return httpCode;
  // FIX 7: removed unreachable https.end() that existed in v1
}

// ================================================================
//  process_LocalEvents  (FIX 8: authTimeout fixed;
//                         FIX 15: no midnight reset needed;
//                         FIX 13: timeSynced flag)
// ================================================================
void process_LocalEvents() {
  processOTSensor();

  // FIX 15: daily appManualStop reset REMOVED.
  // appManualStopUntil expires automatically in checkSch().

  // ── Manual button override ────────────────────────────────
  if (digitalRead(BUTTON_PIN) == LOW) {
    Serial.println("Manual Button Pressed");

    if (otTripped) {
      // Reset OT trip on button press (FIX 14)
      otTripped          = false;
      appManualStopUntil = 0;
      saveEepromIfDirty();
      Serial.println("OT trip reset by button");
      delay(300);
      return;
    }

    if (sch1.active == 1 && digitalRead(MOTOR_PIN) == HIGH) {
      digitalWrite(MOTOR_PIN, LOW);
      sch1.local_state = 1; sch1.state = 0;
      // FIX 15: block the current window
      appManualStopUntil = (sch1.stopUnix > 0)
                           ? sch1.stopUnix
                           : (uint32_t)rtc.now().unixtime() + (uint32_t)(sch1.duration * 60);
      saveEepromIfDirty();
      if (isOnline()) { updateTable(USER_TOKEN, 0, 0); sch1.local_state = 0; }
    }
    if (sch1.active == 0 && digitalRead(MOTOR_PIN) == HIGH) {
      digitalWrite(MOTOR_PIN, LOW);
      sch1.local_state = 1; sch1.state = 0;
      appManualStopUntil = (sch1.stopUnix > 0)
                           ? sch1.stopUnix
                           : (uint32_t)rtc.now().unixtime() + (uint32_t)(sch1.duration * 60);
      saveEepromIfDirty();
      Serial.println("Motor OFF by Button");
      if (isOnline()) { updateTable(USER_TOKEN, 0, 0); sch1.local_state = 0; }
      delay(200);
    } else if (sch1.active == 0 && digitalRead(MOTOR_PIN) == LOW) {
      if (!otTripped) {
        digitalWrite(MOTOR_PIN, HIGH);
        count = 0; sch1.local_state = 1; sch1.state = 1;
        appManualStopUntil = 0;
        Serial.println("Motor ON by Button");
        if (isOnline()) { updateTable(USER_TOKEN, 1, 1); sch1.local_state = 0; }
        delay(200);
      }
    }
  }

  if (mili_now - lastExecutionTime >= (unsigned long)EXECUTION_INTERVAL) {
    esp_task_wdt_reset();   // FIX 12

    // Runtime duration limit
    if (digitalRead(MOTOR_PIN) == HIGH) {
      count += EXECUTION_INTERVAL / 1000;
      Serial.println("Count: " + String(count));
      if (count >= sch1.duration * 60) {
        sch1.active = 0; sch1.state = 0;
        digitalWrite(MOTOR_PIN, LOW);
        if (isOnline()) { updateTable(USER_TOKEN, 0, 0); updateackTable(USER_TOKEN, 0); }
        count = 0;
      }
    }

    Serial.println("Checking Local Events...");
    lastExecutionTime = mili_now;
    checkSch(rtc.now().unixtime());

    DateTime dt(sch1.startUnix);
    Serial.printf("Current Time: %02d:%02d:%02d\n",
                  rtc.now().hour(), rtc.now().minute(), rtc.now().second());
    Serial.printf("Schedule Start: %02d:%02d\n", dt.hour(), dt.minute());
    Serial.printf("ManualUntil: %u  OT: %s\n",
                  appManualStopUntil, otTripped ? "TRIP" : "ok");

    // FIX 8: authTimeout — decrement properly, re-login when <= 0
    if (authTimeout > 0) {
      authTimeout -= (int)(EXECUTION_INTERVAL / 1000);
      Serial.print("Auth Timeout remaining (s): "); Serial.println(authTimeout);
    }

    // ── WiFi reconnect ─────────────────────────────────────
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi Disconnected – Reconnecting...");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASS);
      authTimeout  = 0;
      login_status = 0;
    }

    // ── Online tasks ───────────────────────────────────────
    if (WiFi.status() == WL_CONNECTED) {
      if (!timeSynced) compareAndSyncTime();   // FIX 13

      // FIX 8: re-login when authTimeout expired (was broken <= 100 check)
      if (login_status == 0 || authTimeout <= 0) {
        if (isOnline()) {
          int n1 = login_email(USER_EMAIL, USER_PASS);
          Serial.print("Login HTTP Code: "); Serial.println(n1);
          login_status = 1;
        }
      }

      if (login_status == 1) {
        getTableData(USER_TOKEN, "");
      }
    }
  }
}

// ================================================================
//  SETUP
// ================================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== Pump Controller ESP32 ===");

  // FIX 11: hardware watchdog
  esp_task_wdt_init(WDT_TIMEOUT_S, true);
  esp_task_wdt_add(NULL);

  EEPROM.begin(EEPROM_SIZE);

  // FIX 1: explicit SDA/SCL for ESP32 (adjust pins as needed)
  Wire.begin(5, 4);  // SDA=5, SCL=4

  pinMode(MOTOR_PIN,     OUTPUT); digitalWrite(MOTOR_PIN, LOW);
  pinMode(BUTTON_PIN,    INPUT_PULLUP);
  pinMode(OT_SENSOR_PIN, INPUT_PULLUP);

  display.setBrightness(0x0f);
  display.clear();

  bool rtc_status = rtc.begin(&Wire);
  delay(500);
  if (rtc_status) {
    Serial.println("RTC Found");
    if (!rtc.isrunning()) Serial.println("RTC not running!");
  } else {
    Serial.println("RTC Not Found");
    sch1.sch1_en = 0;
  }

  // Use NTP as fallback time source on ESP32
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  loadSchedules();
  loadScheduleFromEEPROM();

  // WiFi connect (blocking in setup, WDT reset inside)
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("[WIFI] Connecting");
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 20000) {
    esp_task_wdt_reset();   // FIX 12
    delay(400);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WIFI] Connected – IP: %s\n",
                  WiFi.localIP().toString().c_str());
    login_email(USER_EMAIL, USER_PASS);
    login_status = 1;
    compareAndSyncTime();
  } else {
    Serial.println("\n[WIFI] Failed – will retry in loop");
  }

  Serial.println("[BOOT] Setup done");
}

// ================================================================
//  LOOP
// ================================================================
void loop() {
  esp_task_wdt_reset();   // FIX 12: pet watchdog every cycle
  mili_now = millis();
  process_LocalEvents();
  delay(500);
}
