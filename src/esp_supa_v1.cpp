#include <Arduino.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <Preferences.h>
#include <time.h>
#include <TM1637Display.h>
#include <Wire.h>
#include "RTClib.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <NimBLEDevice.h>
#include <esp_task_wdt.h>

// ================== PIN MAPPING (ESP32-C3) ==================
#define RTC_SDA 5
#define RTC_SCL 4
#define MOTOR_PIN 0
#define CLK_PIN 1
#define DIO_PIN 2
#define OT_SENSOR_PIN 7
#define BUTTON_PIN 9

// ── EEPROM layout ────────────────────────────────────────────
#define EEPROM_SIZE 64
#define EEPROM_ADDR_START_UNIX 0    // 4 bytes
#define EEPROM_ADDR_STOP_UNIX 4     // 4 bytes
#define EEPROM_ADDR_APP_MANUAL 8    // 1 byte
#define EEPROM_ADDR_OT_TRIPPED 9    // 1 byte
#define EEPROM_ADDR_MANUAL_UNTIL 10 // 4 bytes

unsigned long lastHeartbeatMs = 0;

// ── Constants ────────────────────────────────────────────────
#define DEVICE_ID 110001
#define OT_TRIP_COUNT 5
#define WDT_TIMEOUT_S 60
#define TOKEN_REFRESH_BEFORE_S 120 // re-login this many seconds before expiry
#define DRIFT_THRESHOLD_S 30
#define SYNC_INTERVAL_MS (1UL * 60 * 60 * 1000)

// ── BLE ──────────────────────────────────────────────────────
#define BLE_DEVICE_NAME "PumpCtrl-110001"
#define BLE_SERVICE_UUID "12345678-1234-1234-1234-1234567890ab"
#define BLE_CHAR_UUID "abcd1234-ab12-ab12-ab12-abcdef123456"
// Send this JSON to the BLE characteristic to provision:
// {"ssid":"MyWifi","pass":"wifipass","email":"u@x.com","upass":"supabase_pass"}

#define DEFAULT_SUPABASE_URL "https://fkgfdgwpqqfxhnyuwtwe.supabase.co"
#define DEFAULT_SUPABASE_KEY "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImZrZ2ZkZ3dwcXFmeGhueXV3dHdlIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NjAzMzQzNzQsImV4cCI6MjA3NTkxMDM3NH0.Dn805WO5wyPa25yD5fYYcCzB4TgDbnTCb4zBuCiczZU"

// ── Runtime credentials (loaded from NVS) ────────────────────
char WIFI_SSID[33] = "";
char WIFI_PASS[65] = "";
char USER_EMAIL[50] = "";
char USER_PASS[33] = "";
char SUPABASE_URL[120] = DEFAULT_SUPABASE_URL;
char SUPABASE_KEY[320] = DEFAULT_SUPABASE_KEY;
bool g_provisioned = false;

// Supabase device REST URL (built at runtime)
String supabase_device_url;

// ── Global objects ────────────────────────────────────────────
RTC_DS1307 rtc;
TM1637Display dispObj(CLK_PIN, DIO_PIN);
bool rtcAvail = false;

// ── Schedule struct ───────────────────────────────────────────
struct Schedule
{
  uint32_t startUnix = 0;
  uint32_t stopUnix = 0;
  int duration = 10; // minutes
  bool active = false;
  int state = 0;
  int ack = 0;
  bool sch_enabled = true;
  bool local_pending = false; // local change not yet synced to cloud
};
Schedule sch1;

// ── Global state ──────────────────────────────────────────────
String USER_TOKEN = "";
bool login_status = false;
unsigned long tokenExpiresAt = 0; // absolute millis() when token expires

// ★ CHANGED: replaced bool appManualStop with uint32_t appManualStopUntil
// When the user stops the motor from the app or button, we record the Unix
// timestamp of the END of the current schedule window. checkSch() will refuse
// to start the motor only while nowUnix < appManualStopUntil.
// When a new schedule window begins (nowUnix >= appManualStopUntil) the block
// expires automatically — no midnight reset needed.
// Set to 0 means "not blocked".
uint32_t appManualStopUntil = 0; // ★ NEW (replaces appManualStop)

bool otTripped = false;
int ot_sensorcount = 0;
long EXECUTION_INTERVAL = 10000;
int runtimeSecs = 0;
bool timeSynced = false;
unsigned long lastSyncMs = 0;
unsigned long lastExecMs = 0;
unsigned long lastWifiAttemptMs = 0;
int lastDay = -1;
int heartbeatCnt = 10;
bool colonBlink = false;

uint32_t savedStartUnix = 0;
uint32_t savedStopUnix = 0;
uint8_t savedManual = 0xFF; // kept for EEPROM layout compatibility
uint8_t savedOtTrip = 0xFF;
uint32_t savedManualUntil = 0xFFFFFFFF;

// ── BLE state ─────────────────────────────────────────────────
NimBLEServer *pBleServer = nullptr;
NimBLECharacteristic *pBleChar = nullptr;
bool bleDone = false;
bool bleClientConnected = false;

// ── Forward declarations ──────────────────────────────────────
void loadCredentials();
void saveCredentials(const char *ssid, const char *pass,
                     const char *email, const char *upass);
void startBleProvisioning();
void stopBleProvisioning();
bool httpPatch(const char *url, const char *body);
bool httpGet(const char *url, String &responseOut);
time_t getCurrentUnixTime();
void buildDeviceUrl();
void saveEeprom();
void loadEeprom();
void checkSch(uint32_t nowUnix);
void processOTSensor();
bool checkButtonPress();
void handleButtonPress();
void net_login();
void net_getTableData();
void net_compareAndSyncTime();
void net_wifiReconnectIfNeeded();
void updateDisplay();

// ================================================================
//  CREDENTIAL STORE  (NVS via Preferences)
// ================================================================
void loadCredentials()
{
  Preferences p;
  p.begin("pump_creds", true);
  p.getString("ssid", WIFI_SSID, sizeof(WIFI_SSID));
  p.getString("pass", WIFI_PASS, sizeof(WIFI_PASS));
  p.getString("email", USER_EMAIL, sizeof(USER_EMAIL));
  p.getString("upass", USER_PASS, sizeof(USER_PASS));
  p.getString("surl", SUPABASE_URL, sizeof(SUPABASE_URL));
  p.getString("skey", SUPABASE_KEY, sizeof(SUPABASE_KEY));
  g_provisioned = p.getBool("prov", false);
  p.end();
  Serial.printf("[CRED] Loaded. SSID='%s' provisioned=%d\n", WIFI_SSID, g_provisioned);
}

void saveCredentials(const char *ssid, const char *pass,
                     const char *email, const char *upass)
{
  strlcpy(WIFI_SSID, ssid, sizeof(WIFI_SSID));
  strlcpy(WIFI_PASS, pass, sizeof(WIFI_PASS));
  strlcpy(USER_EMAIL, email, sizeof(USER_EMAIL));
  strlcpy(USER_PASS, upass, sizeof(USER_PASS));
  Preferences p;
  p.begin("pump_creds", false);
  p.putString("ssid", ssid);
  p.putString("pass", pass);
  p.putString("email", email);
  p.putString("upass", upass);
  p.putString("surl", DEFAULT_SUPABASE_URL);
  p.putString("skey", DEFAULT_SUPABASE_KEY);
  p.putBool("prov", true);
  p.end();
  g_provisioned = true;
  Serial.println(F("[CRED] Saved to NVS"));
}

//  BLE PROVISIONING  (NimBLE-Arduino)
class BleServerCB : public NimBLEServerCallbacks
{
  void onConnect(NimBLEServer *) override
  {
    bleClientConnected = true;
    Serial.println(F("[BLE] Client connected"));
  }
  void onDisconnect(NimBLEServer *) override
  {
    bleClientConnected = false;
    if (!bleDone)
      NimBLEDevice::startAdvertising();
    Serial.println(F("[BLE] Client disconnected"));
  }
};

class BleCharCB : public NimBLECharacteristicCallbacks
{
  void onWrite(NimBLECharacteristic *pC) override
  {
    std::string raw = pC->getValue();
    if (raw.empty())
      return;
    Serial.printf("[BLE] RX: %s\n", raw.c_str());

    JsonDocument doc;
    if (deserializeJson(doc, raw))
    {
      pC->setValue("ERR:JSON");
      pC->notify();
      return;
    }
    if (!doc["ssid"].is<const char *>() || !doc["pass"].is<const char *>() ||
        !doc["email"].is<const char *>() || !doc["upass"].is<const char *>())
    {
      pC->setValue("ERR:MISSING_FIELDS");
      pC->notify();
      return;
    }
    saveCredentials(doc["ssid"], doc["pass"], doc["email"], doc["upass"]);
    pC->setValue("OK:SAVED_REBOOTING");
    pC->notify();
    bleDone = true;
    delay(800);
    ESP.restart();
  }
};

void startBleProvisioning()
{
  Serial.println(F("[BLE] Starting provisioning"));
  const uint8_t s[4] = {0x7C, 0x38, 0x79, 0x00};
  dispObj.setSegments(s);

  NimBLEDevice::init(BLE_DEVICE_NAME);
  pBleServer = NimBLEDevice::createServer();
  pBleServer->setCallbacks(new BleServerCB());

  NimBLEService *svc = pBleServer->createService(BLE_SERVICE_UUID);
  pBleChar = svc->createCharacteristic(BLE_CHAR_UUID,
                                       NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
  pBleChar->setCallbacks(new BleCharCB());
  svc->start();

  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(BLE_SERVICE_UUID);
  adv->setScanResponse(true);
  NimBLEDevice::startAdvertising();
  Serial.printf("[BLE] Advertising as '%s'\n", BLE_DEVICE_NAME);
  Serial.println(F("[BLE] Write JSON: {\"ssid\":\"x\",\"pass\":\"x\",\"email\":\"x\",\"upass\":\"x\"}"));
}

void stopBleProvisioning()
{
  NimBLEDevice::stopAdvertising();
  NimBLEDevice::deinit(true);
}

// ================================================================
//  HTTP HELPERS  — local client per call, no global reuse
// ================================================================
bool httpPatch(const char *url, const char *body)
{
  if (WiFi.status() != WL_CONNECTED)
    return false;
  WiFiClientSecure cl;
  cl.setInsecure();
  HTTPClient https;
  https.setTimeout(6000);
  if (!https.begin(cl, url))
    return false;
  https.addHeader(F("apikey"), SUPABASE_KEY);
  https.addHeader(F("Authorization"), "Bearer " + USER_TOKEN);
  https.addHeader(F("Content-Type"), F("application/json"));
  https.addHeader(F("Prefer"), F("return=minimal"));
  int code = https.sendRequest("PATCH", body);
  https.end();
  Serial.printf("[HTTP] PATCH %d\n", code);
  return (code >= 200 && code < 300);
}

bool httpGet(const char *url, String &out)
{
  if (WiFi.status() != WL_CONNECTED)
    return false;
  WiFiClientSecure cl;
  cl.setInsecure();
  HTTPClient https;
  https.setTimeout(6000);
  if (!https.begin(cl, url))
    return false;
  https.addHeader(F("apikey"), SUPABASE_KEY);
  https.addHeader(F("Authorization"), "Bearer " + USER_TOKEN);
  https.addHeader(F("Content-Type"), F("application/json"));
  int code = https.GET();
  if (code == 200)
    out = https.getString();
  https.end();
  Serial.printf("[HTTP] GET %d\n", code);
  return (code == 200);
}

// Convenience wrappers
void updateTable(int st, int ds)
{
  String b = "{\"state\":" + String(st) + ",\"device_state\":" + String(ds) + "}";
  httpPatch(supabase_device_url.c_str(), b.c_str());
}
void updateAck(int ack)
{
  String b = "{\"ack\":" + String(ack) + "}";
  httpPatch(supabase_device_url.c_str(), b.c_str());
}

void sendHeartbeat(String token, int value)
{
  String body =
      "{\"heart_beat_count\":" + String(value) +
      ",\"runtime_secs\":" + String(runtimeSecs) + "}";

  if (!httpPatch(supabase_device_url.c_str(), body.c_str()))
  {
    Serial.println(F("[HB] Failed – queuing"));
  }
  else
  {
    Serial.println(F("[HB] OK"));
  }
}
time_t getCurrentUnixTime()
{
  if (rtcAvail && rtc.isrunning())
    return rtc.now().unixtime();
  time_t t = time(nullptr);
  return (t > 1000000000UL) ? t : 0;
}

uint32_t hourMinuteToUnixUTC(uint8_t hour, uint8_t minute)
{
  time_t now = getCurrentUnixTime();
  if (now == 0)
    return 0;
  struct tm t;
  gmtime_r(&now, &t);
  t.tm_hour = hour;
  t.tm_min = minute;
  t.tm_sec = 0;
  return (uint32_t)mktime(&t);
}

void net_compareAndSyncTime()
{
  if (!timeSynced && (millis() - lastSyncMs < SYNC_INTERVAL_MS))
    return;
  if (WiFi.status() != WL_CONNECTED)
    return;

  String resp;
  WiFiClientSecure cl;
  cl.setInsecure();
  HTTPClient https;
  https.setTimeout(5000);
  if (!https.begin(cl, "https://api.skyiottech.com/time"))
    return;
  int code = https.GET();
  String body = (code == 200) ? https.getString() : "";
  https.end();
  if (code != 200 || body.isEmpty())
  {
    Serial.println(F("[SYNC] Failed to get time"));
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, body))
    return;
  time_t internetTime = (time_t)doc["unix_time"].as<unsigned long>();

  if (rtcAvail && rtc.isrunning())
  {
    long drift = abs((long)(internetTime - (long)rtc.now().unixtime()));
    Serial.printf("[SYNC] Drift: %ld s\n", drift);
    if (drift > DRIFT_THRESHOLD_S)
    {
      rtc.adjust(DateTime((uint32_t)internetTime));
      Serial.println(F("[SYNC] RTC updated"));
    }
  }
  timeSynced = true;
  lastSyncMs = millis();
}

// ================================================================
//  AUTH  — absolute millis() token expiry
// ================================================================
void net_login()
{
  if (WiFi.status() != WL_CONNECTED)
    return;
  String authUrl = String(SUPABASE_URL) + "/auth/v1/token?grant_type=password";
  WiFiClientSecure cl;
  cl.setInsecure();
  HTTPClient https;
  https.setTimeout(8000);
  if (!https.begin(cl, authUrl.c_str()))
    return;
  https.addHeader(F("apikey"), SUPABASE_KEY);
  https.addHeader(F("Content-Type"), F("application/json"));
  String body = "{\"email\":\"" + String(USER_EMAIL) + "\",\"password\":\"" + String(USER_PASS) + "\"}";
  int code = https.POST(body);
  Serial.printf("[AUTH] Login HTTP %d\n", code);
  if (code == 200)
  {
    JsonDocument doc;
    if (!deserializeJson(doc, https.getString()) && doc["access_token"].is<const char *>())
    {
      USER_TOKEN = doc["access_token"].as<String>();
      unsigned long expiresIn = doc["expires_in"] | 3600UL;
      tokenExpiresAt = millis() + ((expiresIn - TOKEN_REFRESH_BEFORE_S) * 1000UL);
      login_status = true;
      Serial.printf("[AUTH] OK – expires in %lu s\n", expiresIn);
    }
  }
  else
  {
    login_status = false;
  }
  https.end();
}

void checkTokenRefresh()
{
  if (!login_status)
    return;
  if (millis() >= tokenExpiresAt)
  {
    Serial.println(F("[AUTH] Token expiring – re-logging in"));
    login_status = false;
    net_login();
  }
}

// ================================================================
//  SUPABASE GET — fetch device row, apply cloud commands
// ================================================================
void net_getTableData()
{
  if (!login_status || WiFi.status() != WL_CONNECTED)
    return;
  String resp;
  if (!httpGet(supabase_device_url.c_str(), resp))
    return;

  JsonDocument doc;
  if (deserializeJson(doc, resp))
  {
    Serial.println(F("[NET] JSON parse fail"));
    return;
  }

  // heartbeatCnt++;
  // sendHeartbeat();

  int desiredState = doc[0]["state"] | 0;
  sch1.ack = doc[0]["ack"] | 0;
  sch1.sch_enabled = (doc[0]["sch1_en"] == 1);
  uint32_t duration = doc[0]["sch1_duration"] | 10;
  sch1.duration = duration;
  int syncDur = doc[0]["sync_duration"] | 0;
  if (syncDur > 0)
    EXECUTION_INTERVAL = (long)syncDur * 1000L;

  if (sch1.local_pending)
  {
    Serial.println(F("[NET] Local pending – overriding cloud"));
    updateTable(sch1.state, sch1.state);
    sch1.local_pending = false;
    goto parse_schedule;
  }

  // Cloud ON command
  // ★ CHANGED: check appManualStopUntil instead of appManualStop
  if (desiredState == 1 && !otTripped && digitalRead(MOTOR_PIN) == LOW && sch1.ack == 1)
  {
    Serial.println(F("[MOTOR] ON from App"));
    digitalWrite(MOTOR_PIN, HIGH);
    sch1.state = 1;
    runtimeSecs = 0;
    appManualStopUntil = 0; // ★ CHANGED: clear block when app turns ON
    saveEeprom();
    updateTable(1, 1);
    updateAck(0);
  }

  // Cloud OFF command
  if (desiredState == 0 && digitalRead(MOTOR_PIN) == HIGH && sch1.ack == 1)
  {
    Serial.println(F("[MOTOR] OFF from App"));
    digitalWrite(MOTOR_PIN, LOW);
    sch1.state = 0;
    sch1.active = false;
    // ★ CHANGED: block only until the END of the current schedule window.
    // If stopUnix is set, block until then. Otherwise block for this duration only.
    appManualStopUntil = (sch1.stopUnix > 0)
                             ? sch1.stopUnix
                             : (uint32_t)getCurrentUnixTime() + (uint32_t)(sch1.duration * 60);
    Serial.printf("[MOTOR] App stop – blocked until unix %u\n", appManualStopUntil);
    saveEeprom();
    updateTable(0, 0);
    updateAck(0);
  }

parse_schedule:
  // Parse schedule time
  const char *schTime = doc[0]["sch1_start"] | "00:00";
  uint16_t h = 0, m = 0;
  if (schTime && strlen(schTime) >= 5)
  {
    h = (schTime[0] - '0') * 10 + (schTime[1] - '0');
    m = (schTime[3] - '0') * 10 + (schTime[4] - '0');
  }
  uint32_t newStart = hourMinuteToUnixUTC((uint8_t)h, (uint8_t)m);
  uint32_t newStop = newStart + (duration * 60);
  if (newStart != sch1.startUnix || newStop != sch1.stopUnix)
  {
    bool scheduleChanged = true;

    sch1.startUnix = newStart;
    sch1.stopUnix = newStop;
    sch1.active = false;

    // NEW SCHEDULE = CLEAR OLD MANUAL BLOCK
    appManualStopUntil = 0;

    saveEeprom();

    Serial.printf("[SCH] Updated: %02d:%02d for %u min\n",
                  h, m, duration);

    Serial.println(F("[SCH] Manual block cleared due to new schedule"));
  }
}

// ================================================================
//  SCHEDULE CHECK
// ================================================================
void checkSch(uint32_t nowUnix)
{
  // ★ CHANGED: auto-expire the manual-stop block when the blocked window ends
  if (appManualStopUntil > 0 && nowUnix >= appManualStopUntil)
  {
    Serial.println(F("[SCH] Manual-stop window expired – block cleared"));
    appManualStopUntil = 0;
    saveEeprom();
  }

  // Start
  // ★ CHANGED: check (appManualStopUntil > 0 && nowUnix < appManualStopUntil)
  // instead of appManualStop
  if (!sch1.active && nowUnix >= sch1.startUnix && nowUnix < sch1.stopUnix && sch1.sch_enabled && !(appManualStopUntil > 0 && nowUnix < appManualStopUntil) // ★ CHANGED
      && !otTripped)
  {
    Serial.println(F("[SCH] START"));
    digitalWrite(MOTOR_PIN, HIGH);
    sch1.active = true;
    sch1.state = 1;
    runtimeSecs = 0;
    sch1.local_pending = true;
    updateTable(1, 1);
    updateAck(0);
    sch1.local_pending = false;
  }
  // Stop
  if (sch1.active && nowUnix >= sch1.stopUnix)
  {
    Serial.println(F("[SCH] STOP"));
    digitalWrite(MOTOR_PIN, LOW);
    sch1.active = false;
    sch1.state = 0;
    sch1.local_pending = true;
    updateTable(0, 0);
    sch1.local_pending = false;
  }
}

// ================================================================
//  OT SENSOR
// ================================================================
void processOTSensor()
{
  if (digitalRead(MOTOR_PIN) == LOW)
  {
    ot_sensorcount = 0;
    return;
  }
  if (digitalRead(OT_SENSOR_PIN) == LOW)
  {
    ot_sensorcount++;
    Serial.printf("[OT] Count: %d/%d\n", ot_sensorcount, OT_TRIP_COUNT);
    if (ot_sensorcount >= OT_TRIP_COUNT)
    {
      Serial.println(F("[OT] TRIP – Motor OFF"));
      digitalWrite(MOTOR_PIN, LOW);
      sch1.active = false;
      sch1.state = 0;
      otTripped = true;
      // ★ CHANGED: OT also blocks the current window (same logic as app stop)
      appManualStopUntil = (sch1.stopUnix > 0)
                               ? sch1.stopUnix
                               : (uint32_t)getCurrentUnixTime() + (uint32_t)(sch1.duration * 60);
      saveEeprom();
      sch1.local_pending = true;
      updateTable(0, 0);
      updateAck(0);
      sch1.local_pending = false;
      ot_sensorcount = 0;
      const uint8_t s[4] = {0x79, 0x50, 0x50, 0x06};
      dispObj.setSegments(s);
    }
  }
  else
  {
    ot_sensorcount = 0;
  }
}

// ================================================================
//  BUTTON FSM — non-blocking, no while(LOW)/delay
// ================================================================
enum BtnState
{
  BTN_IDLE,
  BTN_DEBOUNCE,
  BTN_HELD
};
static BtnState btnState = BTN_IDLE;
static unsigned long btnTime = 0;

bool checkButtonPress()
{
  bool pinLow = (digitalRead(BUTTON_PIN) == LOW);
  unsigned long now = millis();
  switch (btnState)
  {
  case BTN_IDLE:
    if (pinLow)
    {
      btnState = BTN_DEBOUNCE;
      btnTime = now;
    }
    break;
  case BTN_DEBOUNCE:
    if (!pinLow)
    {
      btnState = BTN_IDLE;
      break;
    }
    if (now - btnTime >= 30)
      btnState = BTN_HELD;
    break;
  case BTN_HELD:
    if (!pinLow)
    {
      btnState = BTN_IDLE;
      if ((now - btnTime) < 3000)
        return true; // short press
    }
    break;
  }
  return false;
}

void handleButtonPress()
{
  Serial.println(F("[BTN] Short press"));
  if (otTripped)
  {
    otTripped = false;
    appManualStopUntil = 0; // ★ CHANGED: clear block on OT reset
    saveEeprom();
    dispObj.clear();
    Serial.println(F("[BTN] OT trip reset – press again to start motor"));
    return;
  }
  if (digitalRead(MOTOR_PIN) == HIGH)
  {
    digitalWrite(MOTOR_PIN, LOW);
    sch1.state = 0;
    sch1.active = false;
    // ★ CHANGED: button OFF blocks only the current schedule window
    appManualStopUntil = (sch1.stopUnix > 0)
                             ? sch1.stopUnix
                             : (uint32_t)getCurrentUnixTime() + (uint32_t)(sch1.duration * 60);
    Serial.printf("[BTN] Manual stop – blocked until unix %u\n", appManualStopUntil);
    saveEeprom();
    sch1.local_pending = true;
    updateTable(0, 0);
    sch1.local_pending = false;
    Serial.println(F("[MOTOR] OFF by button"));
  }
  else
  {
    digitalWrite(MOTOR_PIN, HIGH);
    sch1.state = 1;
    runtimeSecs = 0;
    appManualStopUntil = 0; // ★ CHANGED: clear block when manually turning ON
    sch1.local_pending = true;
    updateTable(1, 1);
    sch1.local_pending = false;
    Serial.println(F("[MOTOR] ON by button"));
  }
}

// ================================================================
//  EEPROM — write only when value actually changed
// ================================================================
void saveEeprom()
{
  bool dirty = false;
  if (sch1.startUnix != savedStartUnix)
  {
    EEPROM.put(EEPROM_ADDR_START_UNIX, sch1.startUnix);
    savedStartUnix = sch1.startUnix;
    dirty = true;
  }
  if (sch1.stopUnix != savedStopUnix)
  {
    EEPROM.put(EEPROM_ADDR_STOP_UNIX, sch1.stopUnix);
    savedStopUnix = sch1.stopUnix;
    dirty = true;
  }
  // Byte at EEPROM_ADDR_APP_MANUAL kept for layout compat but no longer drives logic
  uint8_t m = (appManualStopUntil > 0) ? 1 : 0;
  if (m != savedManual)
  {
    EEPROM.put(EEPROM_ADDR_APP_MANUAL, m);
    savedManual = m;
    dirty = true;
  }
  uint8_t o = otTripped ? 1 : 0;
  if (o != savedOtTrip)
  {
    EEPROM.put(EEPROM_ADDR_OT_TRIPPED, o);
    savedOtTrip = o;
    dirty = true;
  }
  // ★ NEW: persist appManualStopUntil so it survives reboot
  if (appManualStopUntil != savedManualUntil)
  {
    EEPROM.put(EEPROM_ADDR_MANUAL_UNTIL, appManualStopUntil);
    savedManualUntil = appManualStopUntil;
    dirty = true;
  }
  if (dirty)
  {
    EEPROM.commit();
    Serial.println(F("[EEPROM] Committed"));
  }
}

void loadEeprom()
{
  uint32_t s, e, mu;
  uint8_t m, o;
  EEPROM.get(EEPROM_ADDR_START_UNIX, s);
  EEPROM.get(EEPROM_ADDR_STOP_UNIX, e);
  EEPROM.get(EEPROM_ADDR_APP_MANUAL, m);
  EEPROM.get(EEPROM_ADDR_OT_TRIPPED, o);
  EEPROM.get(EEPROM_ADDR_MANUAL_UNTIL, mu);

  bool valid = (s > 1000000000UL) && (e > s);
  sch1.startUnix = valid ? s : 0;
  sch1.stopUnix = valid ? e : 0;
  otTripped = (o == 1);
  appManualStopUntil = (mu == 0xFFFFFFFF) ? 0 : mu;

  savedStartUnix = sch1.startUnix;
  savedStopUnix = sch1.stopUnix;
  savedManual = m;
  savedOtTrip = o;
  savedManualUntil = appManualStopUntil;

  // ★ NEW: rehydrate sch1.active after reboot
  // If current time falls inside the schedule window and no block is active,
  // resume the schedule as if it never rebooted.
  time_t nowT = getCurrentUnixTime();
  if (nowT > 1000000000UL)
  {
    uint32_t nowUnix = (uint32_t)nowT;
    bool inWindow = valid && (nowUnix >= sch1.startUnix) && (nowUnix < sch1.stopUnix);
    bool blocked = (appManualStopUntil > 0 && nowUnix < appManualStopUntil);

    if (inWindow && !blocked && !otTripped && sch1.sch_enabled)
    {
      Serial.println(F("[EEPROM] Reboot mid-schedule – resuming motor"));
      digitalWrite(MOTOR_PIN, HIGH);
      sch1.active = true;
      sch1.state = 1;
      // restore approximate runtime already consumed
      runtimeSecs = (int)(nowUnix - sch1.startUnix);
    }
  }

  Serial.printf("[EEPROM] start=%u stop=%u ot=%d manualUntil=%u\n",
                sch1.startUnix, sch1.stopUnix, (int)otTripped, appManualStopUntil);
}

// ================================================================
//  DISPLAY HELPERS
// ================================================================
void updateDisplay()
{
  colonBlink = !colonBlink;
  if (digitalRead(MOTOR_PIN) == HIGH)
  {
    int mm = runtimeSecs / 60, ss = runtimeSecs % 60;
    dispObj.showNumberDecEx(mm * 100 + ss, 0b01000000, true);
  }
  else if (rtcAvail && rtc.isrunning())
  {
    DateTime dt = rtc.now();
    dispObj.showNumberDecEx(dt.hour() * 100 + dt.minute(),
                            colonBlink ? 0b01000000 : 0, true);
  }
}

// ================================================================
//  WiFi reconnect — non-blocking, throttled
// ================================================================
void net_wifiReconnectIfNeeded()
{
  if (WiFi.status() == WL_CONNECTED)
    return;
  unsigned long now = millis();
  if (now - lastWifiAttemptMs < 30000)
    return;
  lastWifiAttemptMs = now;
  Serial.println(F("[WIFI] Reconnecting (non-blocking)"));
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

void buildDeviceUrl()
{
  supabase_device_url = String(SUPABASE_URL) + "/rest/v1/pump_motor?device_id=eq." + String(DEVICE_ID);
}

// ================================================================
//  SETUP
// ================================================================
void setup()
{
  Serial.begin(115200);
  delay(200);
  Serial.println(F("\n=== Pump Controller v2.0 ==="));

  esp_task_wdt_init(WDT_TIMEOUT_S, true);
  esp_task_wdt_add(NULL);

  EEPROM.begin(EEPROM_SIZE);

  Wire.begin(RTC_SDA, RTC_SCL);
  if (rtc.begin(&Wire))
  {
    rtcAvail = true;
    Serial.println(F("[RTC] DS1307 found"));
    if (!rtc.isrunning())
      Serial.println(F("[RTC] Not running"));
  }
  else
  {
    Serial.println(F("[RTC] Not found – NTP fallback"));
  }

  pinMode(MOTOR_PIN, OUTPUT);
  digitalWrite(MOTOR_PIN, LOW);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(OT_SENSOR_PIN, INPUT_PULLUP);

  dispObj.setBrightness(0x0a);
  dispObj.clear();

  loadCredentials();

  if (digitalRead(BUTTON_PIN) == LOW)
  {
    Serial.println(F("[BOOT] Button held – waiting 5 s for re-provision..."));
    unsigned long t = millis();
    while (digitalRead(BUTTON_PIN) == LOW && millis() - t < 5000)
      delay(50);
    if (millis() - t >= 4900)
    {
      Serial.println(F("[BOOT] Re-provisioning – clearing NVS"));
      Preferences p;
      p.begin("pump_creds", false);
      p.clear();
      p.end();
      g_provisioned = false;
    }
  }

  if (!g_provisioned || strlen(WIFI_SSID) == 0)
  {
    Serial.println(F("[BOOT] Not provisioned – starting BLE"));
    startBleProvisioning();
    while (!bleDone)
    {
      esp_task_wdt_reset();
      delay(100);
    }
    return;
  }

  loadEeprom();
  buildDeviceUrl();

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print(F("[WIFI] Connecting"));
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 20000)
  {
    delay(400);
    Serial.print(F("."));
    esp_task_wdt_reset();
  }
  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.printf("\n[WIFI] Connected – IP: %s\n", WiFi.localIP().toString().c_str());
    net_login();
    net_compareAndSyncTime();
  }
  else
  {
    Serial.println(F("\n[WIFI] Failed – will retry in loop"));
  }

  Serial.println(F("[BOOT] Setup done"));
}

// ================================================================
//  LOOP
// ================================================================
void loop()
{
  esp_task_wdt_reset();

  processOTSensor();

  if (checkButtonPress())
    handleButtonPress();

  // ★ REMOVED: daily reset of appManualStop – no longer needed.
  // appManualStopUntil expires automatically when the blocked window ends
  // (handled inside checkSch). The midnight reset code is intentionally
  // deleted here.

  updateDisplay();

  unsigned long now = millis();
  if (now - lastExecMs < (unsigned long)EXECUTION_INTERVAL)
  {
    delay(200);
    return;
  }
  lastExecMs = now;
  Serial.printf("\n[MAIN] Cycle (interval=%ld ms)\n", EXECUTION_INTERVAL);

  // Runtime counter
  if (digitalRead(MOTOR_PIN) == HIGH)
  {
    runtimeSecs += (int)(EXECUTION_INTERVAL / 1000);

    Serial.printf("[MAIN] Runtime: %d s / %d s\n",
                  runtimeSecs,
                  sch1.duration * 60);

    // ADD THIS
    Serial.printf("[DEBUG] runtimeSecs=%d duration=%d\n",
                  runtimeSecs,
                  sch1.duration * 60);

    if (runtimeSecs >= sch1.duration * 60)
    {
      Serial.println(F("[MOTOR] Runtime limit – OFF"));

      digitalWrite(MOTOR_PIN, LOW);
      sch1.active = false;
      sch1.state = 0;

      updateTable(0, 0);
      updateAck(0);

      runtimeSecs = 0;
    }
  }

  time_t t = getCurrentUnixTime();
  if (t > 1000000000UL)
    checkSch((uint32_t)t);

  net_wifiReconnectIfNeeded();

  if (WiFi.status() == WL_CONNECTED)
  {
    if (!login_status)
      net_login();
    else
      checkTokenRefresh();
    if (login_status)
    {
      sendHeartbeat(USER_TOKEN, 10);
      net_getTableData();
    }
    net_compareAndSyncTime();
  }

  if (rtcAvail && rtc.isrunning())
  {
    DateTime dt = rtc.now();
    Serial.printf("[MAIN] Time:%02d:%02d Motor:%s Runtime:%ds OT:%s WiFi:%s ManualUntil:%u\n",
                  dt.hour(), dt.minute(),
                  digitalRead(MOTOR_PIN) ? "ON" : "OFF",
                  runtimeSecs,
                  otTripped ? "TRIP" : "ok",
                  WiFi.status() == WL_CONNECTED ? "OK" : "X",
                  appManualStopUntil); // ★ added to debug log
  }
  delay(200);
}
