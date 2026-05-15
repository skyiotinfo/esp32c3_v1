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

// EEPROM addresses
#define EEPROM_SIZE 128
#define EEPROM_START_UNIX_ADDR   10
#define EEPROM_STOP_UNIX_ADDR    25
#define EEPROM_LOCAL_STATE       40
#define EEPROM_APP_MANUAL_STATUS 42
#define EEPROM_UPDATED_BY        44

const int device_id = 110001;

// WiFi & Supabase credentials
char WIFI_SSID[20] = "anupam";
char WIFI_PASS[20] = "12345678";
const char *supabase_device_url = "https://fkgfdgwpqqfxhnyuwtwe.supabase.co/rest/v1/pump_motor?device_id=eq.110004";
char SUPABASE_URL[100] = "https://fkgfdgwpqqfxhnyuwtwe.supabase.co";
char AUTH_URL[100] = "https://fkgfdgwpqqfxhnyuwtwe.supabase.co/auth/v1/token?grant_type=password";
char SUPABASE_KEY[300] = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImZrZ2ZkZ3dwcXFmeGhueXV3dHdlIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NjAzMzQzNzQsImV4cCI6MjA3NTkxMDM3NH0.Dn805WO5wyPa25yD5fYYcCzB4TgDbnTCb4zBuCiczZU";
char USER_EMAIL[30] = "9630852741@gmail.com";
char USER_PASS[10] = "123456";

// Schedule structure
struct sch {
  int device_id;
  uint32_t startUnix;
  uint32_t stopUnix;
  uint8_t startTime;
  uint8_t stopTime;
  int duration;
  int active;
  int state;
  int ack;
  int sch1_en;
  int updated_by;
  int local_state;
};
sch sch1;
sch updated_sch1;

// Global variables
String USER_TOKEN;
int login_status = 0;
int authTimeout = 0;
uint32_t lastSavedStartUnix = 0, lastSavedStopUnix = 0;
int appManualStop = 0;
int ot_sensorcount = 0;
const int OT_TRIP_COUNT = 5;
unsigned long lastExecutionTime = 0;
long EXECUTION_INTERVAL = 10000; // ms
int count = 0;                   // runtime seconds
int lastAppState = -1;
unsigned long lastSync = 0;
const unsigned long SYNC_INTERVAL = 1 * 60 * 60 * 1000UL;
const long DRIFT_THRESHOLD = 30;

// ========== Function prototypes (forward declarations) ==========
time_t getCurrentUnixTime();
uint32_t hourMinuteToUnixUTC(uint8_t hour, uint8_t minute);
bool isOnline();
void updateTable(String token, int st, int ds);
void updateackTable(String token, int ack);
void heartbeat(String token, int value);
void getTableData(String token);
bool getInternetUnixTime(time_t &unixTime);
void compareAndSyncTime();
int login_email(String email_a, String password_a);
void saveScheduleToEEPROM();
void loadScheduleFromEEPROM();
void loadSchedules();
void processOTSensor();
void checkSch(uint32_t nowUnix);
void process_LocalEvents();

// ========== Implementation ==========

time_t getCurrentUnixTime() {
  if (rtc.isrunning()) {
    return rtc.now().unixtime();
  } else {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    time_t now = time(nullptr);
    if (now < 1000000) return 0;
    return now;
  }
}

uint32_t hourMinuteToUnixUTC(uint8_t hour, uint8_t minute) {
  time_t now = getCurrentUnixTime();
  if (now == 0) return 0;
  struct tm *tm_now = localtime(&now);
  struct tm tm_target = *tm_now;
  tm_target.tm_hour = hour;
  tm_target.tm_min = minute;
  tm_target.tm_sec = 0;
  return mktime(&tm_target);
}

bool isOnline() {
  if (WiFi.status() != WL_CONNECTED) return false;
  https.begin(client, "https://api.skyiottech.com/time");
  https.setTimeout(5000);
  int code = https.GET();
  https.end();
  return (code == 200);
}

void updateTable(String token, int st, int ds) {
  if (!isOnline()) return;
  https.begin(client, supabase_device_url);
  https.setTimeout(3000);
  https.addHeader("apikey", SUPABASE_KEY);
  https.addHeader("Authorization", "Bearer " + token);
  https.addHeader("Content-Type", "application/json");
  https.addHeader("Prefer", "return=minimal");
  String payload = "{\"state\": " + String(st) + ", \"device_state\": " + String(ds) + "}";
  int httpCode = https.sendRequest("PATCH", payload);
  Serial.print("updateTable HTTP: ");
  Serial.println(httpCode);
  https.end();
}

void updateackTable(String token, int ack) {
  if (!isOnline()) return;
  https.begin(client, supabase_device_url);
  https.setTimeout(3000);
  https.addHeader("apikey", SUPABASE_KEY);
  https.addHeader("Authorization", "Bearer " + token);
  https.addHeader("Content-Type", "application/json");
  https.addHeader("Prefer", "return=minimal");
  String payload = "{\"ack\": " + String(ack) + "}";
  int httpCode = https.sendRequest("PATCH", payload);
  Serial.print("updateackTable HTTP: ");
  Serial.println(httpCode);
  https.end();
}

void heartbeat(String token, int value) {
  if (!isOnline()) return;
  https.begin(client, supabase_device_url);
  https.setTimeout(3000);
  https.addHeader("apikey", SUPABASE_KEY);
  https.addHeader("Authorization", "Bearer " + token);
  https.addHeader("Content-Type", "application/json");
  https.addHeader("Prefer", "return=minimal");
  String payload = "{\"heart_beat_count\": " + String(value) + "}";
  int httpCode = https.sendRequest("PATCH", payload);
  Serial.print("Heartbeat HTTP: ");
  Serial.println(httpCode);
  https.end();
}

void getTableData(String token) {
  if (!isOnline()) return;
  https.begin(client, supabase_device_url);
  https.setTimeout(3000);
  https.addHeader("apikey", SUPABASE_KEY);
  https.addHeader("Authorization", "Bearer " + token);
  https.addHeader("Content-Type", "application/json");

  int httpCode = https.GET();
  Serial.print("GET Table HTTP: ");
  Serial.println(httpCode);

  if (httpCode == 200) {
    // Use DynamicJsonDocument (or JsonDocument) for modern ArduinoJson
    DynamicJsonDocument doc(512);
    deserializeJson(doc, https.getString());
    heartbeat(token, 10);

    uint32_t duration = doc[0]["sch1_duration"];
    updated_sch1.state = doc[0]["state"];
    sch1.ack = doc[0]["ack"];
    sch1.sch1_en = doc[0]["sch1_en"];
    int sync_duration = doc[0]["sync_duration"];
    if (sync_duration > 0) EXECUTION_INTERVAL = sync_duration * 1000UL;

    lastAppState = updated_sch1.state;
    Serial.print("App State: ");
    Serial.println(updated_sch1.state);

    if (sch1.local_state == 1) {
      Serial.println("Local state change – ignoring app command");
      updateTable(USER_TOKEN, sch1.state, sch1.state);
      sch1.local_state = 0;
      https.end();
      return;
    }

    if (updated_sch1.state == 1 && digitalRead(MOTOR_PIN) == LOW && sch1.ack == 1) {
      digitalWrite(MOTOR_PIN, HIGH);
      sch1.state = 1;
      count = 0;
      updateTable(USER_TOKEN, 1, 1);
      updateackTable(USER_TOKEN, 0);
      Serial.println("Motor ON from App");
      appManualStop = 0;
      EEPROM.put(EEPROM_APP_MANUAL_STATUS, appManualStop);
      EEPROM.commit();
    }

    if (updated_sch1.state == 0 && digitalRead(MOTOR_PIN) == HIGH && sch1.ack == 1) {
      digitalWrite(MOTOR_PIN, LOW);
      updateTable(USER_TOKEN, 0, 0);
      updateackTable(USER_TOKEN, 0);
      Serial.println("Motor OFF from App");
      sch1.active = 0;
      sch1.state = 0;
    }

    String schTime = doc[0]["sch1_start"];
    u_int16_t s1 = schTime.substring(0, 2).toInt();
    u_int16_t s2 = schTime.substring(3, 5).toInt();
    updated_sch1.startUnix = hourMinuteToUnixUTC(s1, s2);
    updated_sch1.stopUnix = updated_sch1.startUnix + (duration * 60);

    if (sch1.startUnix != updated_sch1.startUnix || sch1.stopUnix != updated_sch1.stopUnix) {
      sch1.startUnix = updated_sch1.startUnix;
      sch1.stopUnix = updated_sch1.stopUnix;
      sch1.active = 0;
      saveScheduleToEEPROM();
    }
  }
  https.end();
}

bool getInternetUnixTime(time_t &unixTime) {
  if (!isOnline()) return false;
  https.begin(client, "https://api.skyiottech.com/time");
  https.setTimeout(3000);
  int code = https.GET();
  if (code != 200) {
    https.end();
    return false;
  }
  DynamicJsonDocument doc(512);
  deserializeJson(doc, https.getString());
  https.end();
  unixTime = doc["unix_time"];
  return true;
}

void compareAndSyncTime() {
  if (!rtc.isrunning()) {
    Serial.println("RTC not running, cannot sync.");
    return;
  }
  if (lastSync != 0 && millis() - lastSync < SYNC_INTERVAL) {
    Serial.println("Time sync not needed yet.");
    return;
  }
  time_t internetTime;
  if (!getInternetUnixTime(internetTime)) {
    Serial.println("Failed to get internet time.");
    return;
  }
  time_t rtcTime = rtc.now().unixtime();
  long drift = abs((long)(internetTime - rtcTime));
  Serial.print("Time drift: ");
  Serial.println(drift);
  if (drift > DRIFT_THRESHOLD) {
    rtc.adjust(DateTime(internetTime));
    Serial.println("RTC resynced");
  }
  lastSync = millis();
}

int _login_process() {
  int httpCode;
  DynamicJsonDocument doc(512);
  Serial.println("Logging in...");
  https.setTimeout(3000);
  if (https.begin(client, AUTH_URL)) {
    https.addHeader("apikey", SUPABASE_KEY);
    https.addHeader("Content-Type", "application/json");
    String query = "{\"email\": \"" + String(USER_EMAIL) + "\", \"password\": \"" + String(USER_PASS) + "\"}";
    httpCode = https.POST(query);
    if (httpCode > 0) {
      String data = https.getString();
      deserializeJson(doc, data);
      if (doc["access_token"].is<String>()) {
        USER_TOKEN = doc["access_token"].as<String>();
        authTimeout = doc["expires_in"];
        Serial.println("Login Success");
      } else {
        Serial.println("Login Failed – invalid token");
      }
    } else {
      Serial.print("Login HTTP error: ");
      Serial.println(httpCode);
    }
    https.end();
  } else {
    return -100;
  }
  return httpCode;
}

int login_email(String email_a, String password_a) {
  // Use the global credentials directly; parameters ignored for simplicity
  int httpCode = 0;
  int attempts = 0;
  while (httpCode <= 0 && attempts < 3) {
    httpCode = _login_process();
    attempts++;
    if (httpCode <= 0) delay(1000);
  }
  return httpCode;
}

void saveScheduleToEEPROM() {
  if (sch1.startUnix != lastSavedStartUnix || sch1.stopUnix != lastSavedStopUnix) {
    EEPROM.put(EEPROM_START_UNIX_ADDR, sch1.startUnix);
    EEPROM.put(EEPROM_STOP_UNIX_ADDR, sch1.stopUnix);
    EEPROM.put(EEPROM_APP_MANUAL_STATUS, appManualStop);
    EEPROM.commit();
    lastSavedStartUnix = sch1.startUnix;
    lastSavedStopUnix = sch1.stopUnix;
    Serial.println("Schedule saved to EEPROM");
  }
}

void loadScheduleFromEEPROM() {
  EEPROM.get(EEPROM_START_UNIX_ADDR, sch1.startUnix);
  EEPROM.get(EEPROM_STOP_UNIX_ADDR, sch1.stopUnix);
  EEPROM.get(EEPROM_LOCAL_STATE, sch1.updated_by);
  EEPROM.get(EEPROM_APP_MANUAL_STATUS, appManualStop);

  if (sch1.startUnix < 1000000000 || sch1.stopUnix < sch1.startUnix) {
    Serial.println("Invalid EEPROM data, using defaults");
    sch1.startUnix = hourMinuteToUnixUTC(0, 0);
    sch1.stopUnix = sch1.startUnix + 600;
    appManualStop = 0;
  }
  sch1.active = 0;
  sch1.state = 0;
  lastSavedStartUnix = sch1.startUnix;
  lastSavedStopUnix = sch1.stopUnix;
}

void loadSchedules() {
  sch1.device_id = device_id;
  sch1.duration = 10;
  sch1.sch1_en = 1;
  sch1.ack = 0;
  sch1.local_state = 0;
  sch1.updated_by = 0;
}

void processOTSensor() {
  int ot_sensorstatus = digitalRead(OT_SENSOR_PIN);
  if (digitalRead(MOTOR_PIN) == HIGH) {
    if (ot_sensorstatus == LOW) {  // active LOW
      ot_sensorcount++;
      Serial.print("OT Sensor Count: ");
      Serial.println(ot_sensorcount);
      if (ot_sensorcount >= OT_TRIP_COUNT) {
        Serial.println("OT TRIP! Motor OFF");
        digitalWrite(MOTOR_PIN, LOW);
        sch1.active = 0;
        sch1.local_state = 1;
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
  } else {
    ot_sensorcount = 0;
  }
}

void checkSch(uint32_t nowUnix) {
  if (sch1.active == 0 &&
      nowUnix >= sch1.startUnix &&
      nowUnix < sch1.stopUnix &&
      sch1.sch1_en == 1 &&
      appManualStop == 0) {
    sch1.active = 1;
    digitalWrite(MOTOR_PIN, HIGH);
    count = 0;
    Serial.println("SCHEDULE START");
    sch1.local_state = 1;
    sch1.state = 1;
    if (isOnline()) {
      updateTable(USER_TOKEN, 1, 1);
      updateackTable(USER_TOKEN, 0);
      sch1.local_state = 0;
    }
  }

  if (sch1.active == 1 && nowUnix >= sch1.stopUnix) {
    sch1.active = 0;
    digitalWrite(MOTOR_PIN, LOW);
    sch1.state = 0;
    sch1.local_state = 1;
    Serial.println("SCHEDULE STOP");
    if (isOnline()) {
      updateTable(USER_TOKEN, 0, 0);
      appManualStop = 0;
      sch1.local_state = 0;
    }
  }
}

void process_LocalEvents() {
  processOTSensor();

  static int lastDay = -1;
  DateTime now = rtc.now();
  if (now.day() != lastDay) {
    appManualStop = 0;
    lastDay = now.day();
    Serial.println("New day – appManualStop reset");
  }

  // Manual button (active LOW)
  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(50); // simple debounce
    if (digitalRead(BUTTON_PIN) == LOW) {
      Serial.println("Manual Button Pressed");
      if (sch1.active == 1 && digitalRead(MOTOR_PIN) == HIGH) {
        digitalWrite(MOTOR_PIN, LOW);
        sch1.local_state = 1;
        sch1.state = 0;
        if (isOnline()) updateTable(USER_TOKEN, 0, 0);
        sch1.local_state = 0;
      } else if (sch1.active == 0 && digitalRead(MOTOR_PIN) == HIGH) {
        digitalWrite(MOTOR_PIN, LOW);
        sch1.local_state = 1;
        sch1.state = 0;
        if (isOnline()) updateTable(USER_TOKEN, 0, 0);
        sch1.local_state = 0;
      } else if (sch1.active == 0 && digitalRead(MOTOR_PIN) == LOW) {
        digitalWrite(MOTOR_PIN, HIGH);
        count = 0;
        sch1.local_state = 1;
        sch1.state = 1;
        if (isOnline()) updateTable(USER_TOKEN, 1, 1);
        sch1.local_state = 0;
      }
      while (digitalRead(BUTTON_PIN) == LOW) delay(20); // wait for release
    }
  }

  unsigned long currentMillis = millis();
  if (currentMillis - lastExecutionTime >= EXECUTION_INTERVAL) {
    // Update runtime counter
    if (digitalRead(MOTOR_PIN) == HIGH) {
      count += EXECUTION_INTERVAL / 1000;
      Serial.print("Motor runtime (seconds): ");
      Serial.println(count);
      if (count >= (sch1.duration * 60)) {
        sch1.active = 0;
        sch1.state = 0;
        digitalWrite(MOTOR_PIN, LOW);
        if (isOnline()) {
          updateTable(USER_TOKEN, 0, 0);
          updateackTable(USER_TOKEN, 0);
        }
        count = 0;
      }
    }

    time_t nowUnix = getCurrentUnixTime();
    if (nowUnix != 0) checkSch(nowUnix);

    // Debug prints
    Serial.print("Current time: ");
    if (rtc.isrunning()) {
      DateTime dt = rtc.now();
      Serial.printf("%02d:%02d:%02d\n", dt.hour(), dt.minute(), dt.second());
    } else {
      Serial.println("RTC unavailable");
    }
    Serial.print("Auth timeout: ");
    Serial.println(authTimeout);

    if (authTimeout > 20) authTimeout -= 10;

    // WiFi reconnection
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi disconnected. Reconnecting...");
      WiFi.begin(WIFI_SSID, WIFI_PASS);
      unsigned long startAttempt = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 15000) {
        delay(500);
        Serial.print(".");
      }
      if (WiFi.status() == WL_CONNECTED) Serial.println("\nWiFi connected");
      else Serial.println("\nWiFi connection failed");
    }

    if (WiFi.status() == WL_CONNECTED) {
      if (lastSync == 0 && rtc.isrunning()) compareAndSyncTime();
      if (login_status == 0 && isOnline()) {
        int n1 = login_email(USER_EMAIL, USER_PASS);
        Serial.print("Login HTTP code: ");
        Serial.println(n1);
        if (n1 > 0) login_status = 1;
      }
      if (authTimeout <= 100 && login_status == 1) {
        Serial.println("Token expired, need re-login");
        login_status = 0;
      }
      if (login_status == 1) getTableData(USER_TOKEN);
    }

    lastExecutionTime = currentMillis;
  }
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);

  // Initialize I2C for RTC
  Wire.begin(RTC_SDA, RTC_SCL);
  if (!rtc.begin()) {
    Serial.println("RTC not found – NTP fallback will be used");
  } else {
    Serial.println("RTC found");
    if (!rtc.isrunning()) Serial.println("RTC not running – will sync from internet later");
  }

  pinMode(MOTOR_PIN, OUTPUT);
  digitalWrite(MOTOR_PIN, LOW);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(OT_SENSOR_PIN, INPUT_PULLUP);

  display.setBrightness(0x0f);
  display.clear();

  client.setInsecure();

  loadSchedules();
  loadScheduleFromEEPROM();

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) Serial.println("\nWiFi connected");
  else Serial.println("\nWiFi connection failed – will retry later");
}

void loop() {
  process_LocalEvents();
  delay(500);
}