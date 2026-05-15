#include <Arduino.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <time.h>
#include <TM1637Display.h>
#include <Wire.h>
#include "RTClib.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// ================== PIN MAPPING (ESP32‑C3) ==================
// I2C for RTC DS1307 – GPIO4 (SDA) & GPIO5 (SCL)
#define RTC_SDA       5
#define RTC_SCL       4

// I/O pins
#define MOTOR_PIN     0     // Relay control
#define CLK_PIN       1     // TM1637 Clock
#define DIO_PIN       2     // TM1637 Data
#define OT_SENSOR_PIN 7     // Over‑temperature sensor (active LOW)
#define BUTTON_PIN    9     // Manual push button (INPUT_PULLUP)
// ============================================================

// Global objects
RTC_DS1307 rtc;
TM1637Display display(CLK_PIN, DIO_PIN);
WiFiClientSecure client;
HTTPClient https;

// EEPROM layout (no overlaps):
//   Addr 10 : uint32_t startUnix     (4 bytes → ends at 13)
//   Addr 14 : uint32_t stopUnix      (4 bytes → ends at 17)
//   Addr 18 : int      appManualStop (4 bytes → ends at 21)
//   Addr 22 : int      local_state   (4 bytes → ends at 25)
#define EEPROM_SIZE              64
#define EEPROM_START_UNIX_ADDR   10
#define EEPROM_STOP_UNIX_ADDR    14
#define EEPROM_APP_MANUAL_STATUS 18
#define EEPROM_LOCAL_STATE       22

const int DEVICE_ID = 110001;

// ── Hardcoded credentials ────────────────────────────────────
const char *WIFI_SSID          = "anupam";
const char *WIFI_PASS          = "12345678";
const char *SUPABASE_DEVICE_URL =
  "https://fkgfdgwpqqfxhnyuwtwe.supabase.co/rest/v1/pump_motor?device_id=eq.110004";
const char *SUPABASE_URL       = "https://fkgfdgwpqqfxhnyuwtwe.supabase.co";
const char *AUTH_URL           =
  "https://fkgfdgwpqqfxhnyuwtwe.supabase.co/auth/v1/token?grant_type=password";
const char *SUPABASE_KEY       =
  "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImZrZ2ZkZ3dwcXFmeGhueXV3dHdlIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NjAzMzQzNzQsImV4cCI6MjA3NTkxMDM3NH0.Dn805WO5wyPa25yD5fYYcCzB4TgDbnTCb4zBuCiczZU";
const char *USER_EMAIL         = "9630852741@gmail.com";
const char *USER_PASS          = "123456";
// ─────────────────────────────────────────────────────────────

// Schedule structure
struct Sch {
  int      device_id;
  uint32_t startUnix;
  uint32_t stopUnix;
  uint32_t duration;   // minutes
  int      active;
  int      state;
  int      ack;
  int      sch1_en;
  int      updated_by;
  int      local_state;
};
Sch sch1;
Sch updated_sch1;

// Global variables
String   USER_TOKEN;
int      login_status    = 0;
int      authTimeout     = 0;     // seconds remaining on token
uint32_t lastSavedStartUnix = 0;
uint32_t lastSavedStopUnix  = 0;
int      appManualStop   = 0;
int      ot_sensorcount  = 0;
const int OT_TRIP_COUNT  = 5;
unsigned long lastExecutionTime = 0;
unsigned long EXECUTION_INTERVAL = 10000UL;  // ms
int      count           = 0;    // motor runtime seconds
int      lastAppState    = -1;
unsigned long lastSync   = 0;
int      heartbeatCount  = 0;
const unsigned long SYNC_INTERVAL    = 1UL * 60 * 60 * 1000;  // 1 hour
const long          DRIFT_THRESHOLD  = 30;                     // seconds

// ========== Function prototypes ==========
time_t   getCurrentUnixTime();
uint32_t hourMinuteToUnixUTC(uint8_t hour, uint8_t minute);
bool     isOnline();
void     updateTable(int st, int ds);
void     updateackTable(int ack);
void     heartbeat(int value);
void     getTableData();
bool     getInternetUnixTime(time_t &unixTime);
void     compareAndSyncTime();
bool     login_process();
void     saveScheduleToEEPROM();
void     loadScheduleFromEEPROM();
void     loadSchedules();
void     processOTSensor();
void     checkSch(uint32_t nowUnix);
void     process_LocalEvents();
void     motorON(const char *reason);
void     motorOFF(const char *reason);

// ========== Helpers ==========

// Centralised motor ON – always resets count and logs reason
void motorON(const char *reason) {
  digitalWrite(MOTOR_PIN, HIGH);
  count = 0;
  sch1.state = 1;
  Serial.print("Motor ON – ");
  Serial.println(reason);
}

// Centralised motor OFF
void motorOFF(const char *reason) {
  digitalWrite(MOTOR_PIN, LOW);
  sch1.state = 0;
  sch1.active = 0;
  Serial.print("Motor OFF – ");
  Serial.println(reason);
}

time_t getCurrentUnixTime() {
  if (rtc.isrunning()) {
    return (time_t)rtc.now().unixtime();
  }
  // NTP fallback
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  time_t now = time(nullptr);
  return (now > 1000000UL) ? now : 0;
}

uint32_t hourMinuteToUnixUTC(uint8_t hour, uint8_t minute) {
  time_t now = getCurrentUnixTime();
  if (now == 0) return 0;
  struct tm tm_target;
  localtime_r(&now, &tm_target);
  tm_target.tm_hour = hour;
  tm_target.tm_min  = minute;
  tm_target.tm_sec  = 0;
  time_t t = mktime(&tm_target);
  return (t > 0) ? (uint32_t)t : 0;
}

bool isOnline() {
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClientSecure c;
  c.setInsecure();
  HTTPClient h;
  h.begin(c, "https://api.skyiottech.com/time");
  h.setTimeout(5000);
  int code = h.GET();
  h.end();
  return (code == 200);
}

// ── Supabase helpers (token taken from global USER_TOKEN) ────

void updateTable(int st, int ds) {
  if (!isOnline()) return;
  https.begin(client, SUPABASE_DEVICE_URL);
  https.setTimeout(5000);
  https.addHeader("apikey",        SUPABASE_KEY);
  https.addHeader("Authorization", "Bearer " + USER_TOKEN);
  https.addHeader("Content-Type",  "application/json");
  https.addHeader("Prefer",        "return=minimal");
  String payload = "{\"state\": " + String(st) + ", \"device_state\": " + String(ds) + "}";
  int code = https.sendRequest("PATCH", payload);
  Serial.print("updateTable HTTP: "); Serial.println(code);
  https.end();
}

void updateackTable(int ack) {
  if (!isOnline()) return;
  https.begin(client, SUPABASE_DEVICE_URL);
  https.setTimeout(5000);
  https.addHeader("apikey",        SUPABASE_KEY);
  https.addHeader("Authorization", "Bearer " + USER_TOKEN);
  https.addHeader("Content-Type",  "application/json");
  https.addHeader("Prefer",        "return=minimal");
  String payload = "{\"ack\": " + String(ack) + "}";
  int code = https.sendRequest("PATCH", payload);
  Serial.print("updateackTable HTTP: "); Serial.println(code);
  https.end();
}

void heartbeat(int value) {
  if (!isOnline()) return;
  https.begin(client, SUPABASE_DEVICE_URL);
  https.setTimeout(5000);
  https.addHeader("apikey",        SUPABASE_KEY);
  https.addHeader("Authorization", "Bearer " + USER_TOKEN);
  https.addHeader("Content-Type",  "application/json");
  https.addHeader("Prefer",        "return=minimal");
  String payload = "{\"heart_beat_count\": " + String(value) + "}";
  int code = https.sendRequest("PATCH", payload);
  Serial.print("Heartbeat HTTP: "); Serial.println(code);
  https.end();
}

void getTableData() {
  if (!isOnline()) return;
  https.begin(client, SUPABASE_DEVICE_URL);
  https.setTimeout(5000);
  https.addHeader("apikey",        SUPABASE_KEY);
  https.addHeader("Authorization", "Bearer " + USER_TOKEN);
  https.addHeader("Content-Type",  "application/json");

  int httpCode = https.GET();
  Serial.print("GET Table HTTP: "); Serial.println(httpCode);

  if (httpCode == 200) {
    DynamicJsonDocument doc(512);
    DeserializationError err = deserializeJson(doc, https.getString());
    if (err || doc.size() == 0) {
      Serial.println("JSON parse error or empty response");
      https.end();
      return;
    }

    heartbeatCount++;
    heartbeat(heartbeatCount);

    updated_sch1.duration = doc[0]["sch1_duration"].as<uint32_t>();
    updated_sch1.state    = doc[0]["state"].as<int>();
    sch1.ack              = doc[0]["ack"].as<int>();
    sch1.sch1_en          = doc[0]["sch1_en"].as<int>();

    int sync_duration = doc[0]["sync_duration"] | 0;
    if (sync_duration > 0) EXECUTION_INTERVAL = (unsigned long)sync_duration * 1000UL;

    lastAppState = updated_sch1.state;
    Serial.print("App State: "); Serial.println(updated_sch1.state);

    // Local state overrides app command – push real state back and clear flag
    if (sch1.local_state == 1) {
      Serial.println("Local state active – pushing real state to server");
      updateTable(sch1.state, sch1.state);
      sch1.local_state = 0;
      EEPROM.put(EEPROM_LOCAL_STATE, sch1.local_state);
      EEPROM.commit();
      https.end();
      return;
    }

    // App command: turn ON
    if (updated_sch1.state == 1 && digitalRead(MOTOR_PIN) == LOW && sch1.ack == 1) {
      motorON("App command");
      appManualStop = 0;
      EEPROM.put(EEPROM_APP_MANUAL_STATUS, appManualStop);
      EEPROM.commit();
      updateTable(1, 1);
      updateackTable(0);
    }

    // App command: turn OFF
    if (updated_sch1.state == 0 && digitalRead(MOTOR_PIN) == HIGH && sch1.ack == 1) {
      motorOFF("App command");
      appManualStop = 1;                          // FIX: prevent schedule from restarting immediately
      EEPROM.put(EEPROM_APP_MANUAL_STATUS, appManualStop);
      EEPROM.commit();
      updateTable(0, 0);
      updateackTable(0);
    }

    // Parse schedule time "HH:MM"
    String schTime = doc[0]["sch1_start"].as<String>();
    if (schTime.length() >= 5) {
      uint8_t s1 = (uint8_t)schTime.substring(0, 2).toInt();
      uint8_t s2 = (uint8_t)schTime.substring(3, 5).toInt();
      uint32_t newStart = hourMinuteToUnixUTC(s1, s2);
      uint32_t newStop  = newStart + (updated_sch1.duration * 60UL);

      if (newStart != 0 && (sch1.startUnix != newStart || sch1.stopUnix != newStop)) {
        sch1.startUnix    = newStart;
        sch1.stopUnix     = newStop;
        sch1.duration     = updated_sch1.duration;
        sch1.active       = 0;
        saveScheduleToEEPROM();
        Serial.println("Schedule updated from server");
      }
    }
  }
  https.end();
}

bool getInternetUnixTime(time_t &unixTime) {
  if (!isOnline()) return false;
  WiFiClientSecure c;
  c.setInsecure();
  HTTPClient h;
  h.begin(c, "https://api.skyiottech.com/time");
  h.setTimeout(5000);
  int code = h.GET();
  if (code != 200) { h.end(); return false; }
  DynamicJsonDocument doc(256);
  deserializeJson(doc, h.getString());
  h.end();
  unixTime = doc["unix_time"].as<time_t>();
  return (unixTime > 1000000UL);
}

void compareAndSyncTime() {
  if (!rtc.isrunning()) {
    Serial.println("RTC not running – cannot sync");
    return;
  }
  if (lastSync != 0 && millis() - lastSync < SYNC_INTERVAL) return;

  time_t internetTime;
  if (!getInternetUnixTime(internetTime)) {
    Serial.println("Internet time fetch failed");
    return;
  }
  time_t rtcTime = (time_t)rtc.now().unixtime();
  long drift = abs((long)(internetTime - rtcTime));
  Serial.print("RTC drift (s): "); Serial.println(drift);
  if (drift > DRIFT_THRESHOLD) {
    rtc.adjust(DateTime((uint32_t)internetTime));
    Serial.println("RTC resynced from internet");
  }
  lastSync = millis();
}

// Returns true on success; sets USER_TOKEN and authTimeout
bool login_process() {
  DynamicJsonDocument doc(512);
  Serial.println("Logging in...");
  if (!https.begin(client, AUTH_URL)) {
    Serial.println("HTTPClient.begin failed");
    return false;
  }
  https.setTimeout(5000);
  https.addHeader("apikey",       SUPABASE_KEY);
  https.addHeader("Content-Type", "application/json");
  String query = "{\"email\": \"" + String(USER_EMAIL) + "\", \"password\": \"" + String(USER_PASS) + "\"}";
  int httpCode = https.POST(query);
  bool success = false;
  if (httpCode == 200) {
    DeserializationError err = deserializeJson(doc, https.getString());
    if (!err && doc["access_token"].is<String>()) {
      USER_TOKEN  = doc["access_token"].as<String>();
      authTimeout = doc["expires_in"].as<int>();
      Serial.println("Login success");
      success = true;
    } else {
      Serial.println("Login failed – token not found in response");
    }
  } else {
    Serial.print("Login HTTP error: "); Serial.println(httpCode);
  }
  https.end();
  return success;
}

void saveScheduleToEEPROM() {
  if (sch1.startUnix == lastSavedStartUnix && sch1.stopUnix == lastSavedStopUnix) return;
  EEPROM.put(EEPROM_START_UNIX_ADDR,   sch1.startUnix);
  EEPROM.put(EEPROM_STOP_UNIX_ADDR,    sch1.stopUnix);
  EEPROM.put(EEPROM_APP_MANUAL_STATUS, appManualStop);
  EEPROM.put(EEPROM_LOCAL_STATE,       sch1.local_state);
  EEPROM.commit();
  lastSavedStartUnix = sch1.startUnix;
  lastSavedStopUnix  = sch1.stopUnix;
  Serial.println("Schedule saved to EEPROM");
}

void loadScheduleFromEEPROM() {
  EEPROM.get(EEPROM_START_UNIX_ADDR,   sch1.startUnix);
  EEPROM.get(EEPROM_STOP_UNIX_ADDR,    sch1.stopUnix);
  EEPROM.get(EEPROM_APP_MANUAL_STATUS, appManualStop);
  EEPROM.get(EEPROM_LOCAL_STATE,       sch1.local_state);  // FIX: was reading into wrong field

  // Sanity check
  if (sch1.startUnix < 1000000000UL || sch1.stopUnix <= sch1.startUnix) {
    Serial.println("Invalid EEPROM data – using safe defaults");
    sch1.startUnix = hourMinuteToUnixUTC(0, 0);
    sch1.stopUnix  = sch1.startUnix + 600;
    appManualStop  = 0;
    sch1.local_state = 0;
  }
  sch1.active          = 0;
  sch1.state           = 0;
  lastSavedStartUnix   = sch1.startUnix;
  lastSavedStopUnix    = sch1.stopUnix;
  Serial.println("Schedule loaded from EEPROM");
}

void loadSchedules() {
  sch1.device_id   = DEVICE_ID;
  sch1.duration    = 10;
  sch1.sch1_en     = 1;
  sch1.ack         = 0;
  sch1.local_state = 0;
  sch1.updated_by  = 0;
  sch1.active      = 0;
  sch1.state       = 0;
}

void processOTSensor() {
  if (digitalRead(MOTOR_PIN) != HIGH) {
    ot_sensorcount = 0;
    return;
  }
  if (digitalRead(OT_SENSOR_PIN) == LOW) {  // active LOW
    ot_sensorcount++;
    Serial.print("OT count: "); Serial.println(ot_sensorcount);
    if (ot_sensorcount >= OT_TRIP_COUNT) {
      Serial.println("OT TRIP – Motor OFF");
      motorOFF("OT protection");
      sch1.local_state = 1;
      if (isOnline()) {
        updateTable(0, 0);
        updateackTable(0);
        sch1.local_state = 0;
      }
      EEPROM.put(EEPROM_LOCAL_STATE, sch1.local_state);
      EEPROM.commit();
      ot_sensorcount = 0;
    }
  } else {
    ot_sensorcount = 0;
  }
}

void checkSch(uint32_t nowUnix) {
  // Schedule start
  if (sch1.active == 0 &&
      nowUnix >= sch1.startUnix &&
      nowUnix <  sch1.stopUnix  &&
      sch1.sch1_en == 1          &&
      appManualStop == 0) {
    sch1.active = 1;
    motorON("Schedule start");
    sch1.local_state = 1;
    if (isOnline()) {
      updateTable(1, 1);
      updateackTable(0);
      sch1.local_state = 0;
    }
    EEPROM.put(EEPROM_LOCAL_STATE, sch1.local_state);
    EEPROM.commit();
  }

  // Schedule stop
  if (sch1.active == 1 && nowUnix >= sch1.stopUnix) {
    motorOFF("Schedule end");
    sch1.local_state = 1;
    if (isOnline()) {
      updateTable(0, 0);
      appManualStop = 0;
      EEPROM.put(EEPROM_APP_MANUAL_STATUS, appManualStop);
      sch1.local_state = 0;
      EEPROM.commit();
    }
    EEPROM.put(EEPROM_LOCAL_STATE, sch1.local_state);
    EEPROM.commit();
  }
}

void process_LocalEvents() {
  processOTSensor();

  // Reset appManualStop at midnight so schedule resumes next day
  static int lastDay = -1;
  if (rtc.isrunning()) {
    DateTime now = rtc.now();
    if (now.day() != lastDay) {
      appManualStop = 0;
      EEPROM.put(EEPROM_APP_MANUAL_STATUS, appManualStop);
      EEPROM.commit();
      lastDay = now.day();
      Serial.println("New day – appManualStop reset");
    }
  }

  // Manual button (active LOW, debounced)
  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(50);
    if (digitalRead(BUTTON_PIN) == LOW) {
      Serial.println("Manual button pressed");
      bool motorIsOn = (digitalRead(MOTOR_PIN) == HIGH);
      if (motorIsOn) {
        motorOFF("Manual button");
        appManualStop = 1;                         // FIX: prevent schedule restart
        EEPROM.put(EEPROM_APP_MANUAL_STATUS, appManualStop);
        EEPROM.commit();
        sch1.local_state = 1;
        if (isOnline()) { updateTable(0, 0); sch1.local_state = 0; }
        EEPROM.put(EEPROM_LOCAL_STATE, sch1.local_state);
        EEPROM.commit();
      } else {
        motorON("Manual button");
        appManualStop = 0;
        EEPROM.put(EEPROM_APP_MANUAL_STATUS, appManualStop);
        EEPROM.commit();
        sch1.local_state = 1;
        if (isOnline()) { updateTable(1, 1); sch1.local_state = 0; }
        EEPROM.put(EEPROM_LOCAL_STATE, sch1.local_state);
        EEPROM.commit();
      }
      while (digitalRead(BUTTON_PIN) == LOW) delay(20);  // wait for release
    }
  }

  unsigned long currentMillis = millis();
  if (currentMillis - lastExecutionTime >= EXECUTION_INTERVAL) {
    lastExecutionTime = currentMillis;

    // Runtime duration cut-off (app-set duration, not schedule window)
    if (digitalRead(MOTOR_PIN) == HIGH) {
      count += (int)(EXECUTION_INTERVAL / 1000UL);
      Serial.print("Motor runtime (s): "); Serial.println(count);
      if (sch1.duration > 0 && count >= (int)(sch1.duration * 60)) {
        Serial.println("Max duration reached – Motor OFF");
        motorOFF("Duration limit");
        count = 0;
        if (isOnline()) { updateTable(0, 0); updateackTable(0); }
      }
    }

    // Schedule check
    time_t nowUnix = getCurrentUnixTime();
    if (nowUnix != 0) checkSch((uint32_t)nowUnix);

    // Debug
    if (rtc.isrunning()) {
      DateTime dt = rtc.now();
      Serial.printf("RTC time: %02d:%02d:%02d\n", dt.hour(), dt.minute(), dt.second());
    } else {
      Serial.println("RTC unavailable");
    }
    Serial.printf("authTimeout: %d | login: %d | motor: %s\n",
                  authTimeout, login_status,
                  digitalRead(MOTOR_PIN) == HIGH ? "ON" : "OFF");

    // Count down token lifetime (one EXECUTION_INTERVAL ≈ 10 s by default)
    if (authTimeout > 0) authTimeout -= (int)(EXECUTION_INTERVAL / 1000UL);

    // WiFi reconnection
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi disconnected – reconnecting...");
      WiFi.disconnect(true);
      WiFi.begin(WIFI_SSID, WIFI_PASS);
      unsigned long t0 = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
        delay(500); Serial.print(".");
      }
      Serial.println(WiFi.status() == WL_CONNECTED ? "\nWiFi OK" : "\nWiFi failed");
    }

    if (WiFi.status() == WL_CONNECTED) {
      // One-shot initial RTC sync
      if (lastSync == 0 && rtc.isrunning()) compareAndSyncTime();

      // Login / re-login
      if (login_status == 0 && isOnline()) {
        if (login_process()) {
          login_status = 1;
          // authTimeout is set inside login_process()
        }
      }

      // Token expiry check – FIX: was <= 100 (fired too early); now <= 60 s
      if (login_status == 1 && authTimeout <= 60) {
        Serial.println("Token expiring – re-logging in");
        login_status = 0;  // will trigger re-login next cycle
      }

      if (login_status == 1) getTableData();
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);
  EEPROM.begin(EEPROM_SIZE);

  // I2C for RTC
  Wire.begin(RTC_SDA, RTC_SCL);
  if (!rtc.begin()) {
    Serial.println("RTC not found – NTP fallback active");
  } else {
    Serial.println("RTC found");
    if (!rtc.isrunning()) Serial.println("RTC not running – will sync from internet");
  }

  pinMode(MOTOR_PIN,     OUTPUT);
  digitalWrite(MOTOR_PIN, LOW);
  pinMode(BUTTON_PIN,    INPUT_PULLUP);
  pinMode(OT_SENSOR_PIN, INPUT_PULLUP);

  display.setBrightness(0x0f);
  display.clear();

  client.setInsecure();  // Accept self-signed / any TLS cert

  loadSchedules();
  loadScheduleFromEEPROM();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
    delay(500); Serial.print(".");
  }
  Serial.println(WiFi.status() == WL_CONNECTED ? "\nWiFi connected" : "\nWiFi failed – will retry in loop");
}

void loop() {
  process_LocalEvents();
  delay(200);  // slightly tighter loop so button feel is better
}
