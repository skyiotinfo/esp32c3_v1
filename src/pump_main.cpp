// ============================================================================
// main.cpp — Pump Controller, ESP32-C3, new Supabase schema
//            (device / device_seq / sch, set_device_command / report_device_state)
//
// setup() does one-time init: watchdog, EEPROM, RTC, motor/button/OT pins,
// display, credential load, optional BLE provisioning, URL build, saved
// state, NTP config, and a blocking WiFi connect (the only place in the
// sketch allowed to block, since nothing else can run usefully before boot
// finishes anyway). If WiFi connects, it does one full network pass
// (internet check, login, time sync, schedule fetch, initial report)
// before handing off to loop().
//
// loop() is a short, readable list of function calls; all logic lives in
// the named modules under src/. Nothing here calls delay() except the
// fixed 100ms loop pace at the very end — every network wait is bounded
// inside the HTTP helpers, so a dead connection can't freeze button
// presses, schedules, or the display for more than one HTTP timeout.
// ============================================================================
#include <Arduino.h>
#include <EEPROM.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_task_wdt.h>

#include "config.h"
#include "globals.h"
#include "credentials.h"
#include "ble_provisioning.h"
#include "storage.h"
#include "time_utils.h"
#include "http_helpers.h"
#include "supabase_auth.h"
#include "supabase_api.h"
#include "connectivity.h"
#include "motor_control.h"
#include "ot_sensor.h"
#include "display.h"
#include "sprinkler_link.h"   // additive: optional motor-board/valve feature
#include "sprinkler_api.h"    // additive: optional motor-board/valve feature

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("\n=== Pump Controller ESP32-C3 v4.0 (new schema) ==="));

  esp_task_wdt_init(WDT_TIMEOUT_S, true);
  esp_task_wdt_add(NULL);

  EEPROM.begin(EEPROM_SIZE);

  Wire.begin(RTC_SDA, RTC_SCL);
  rtc_init();   // detects the DS1307; loop() keeps watching for it being removed/re-inserted

  pinMode(MOTOR_PIN,     OUTPUT);
  digitalWrite(MOTOR_PIN, LOW);
  pinMode(BUTTON_PIN,    INPUT_PULLUP);
  pinMode(OT_SENSOR_PIN, INPUT_PULLUP);
  pinMode(PROVISION_BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), buttonISR, CHANGE);

  dispObj.setBrightness(0x0a);
  dispObj.clear();

  sprinklerLink_init();   // additive: SoftwareSerial link to the (unchanged) motor board

  loadCredentials();

  // Hold the DEDICATED provisioning button (not the motor button) through
  // boot to enter BLE provisioning. Existing WiFi/Supabase credentials are
  // left completely untouched here - they are only ever overwritten if the
  // app actually finishes sending new ones (see ble_provisioning.cpp's
  // BleCharCB::onWrite -> saveCredentials()). If nothing arrives within
  // BLE_PROVISION_TIMEOUT_MS (10 minutes), provisioning is abandoned and
  // the board simply continues booting on whatever credentials were already
  // loaded above - so a timed-out or accidental provisioning attempt can
  // never strand the board without its previous, working WiFi.
  bool enterProvisioning = false;
  if (digitalRead(PROVISION_BUTTON_PIN) == LOW) {
    Serial.println(F("[BOOT] Provision button held - confirming..."));
    unsigned long t = millis();
    while (digitalRead(PROVISION_BUTTON_PIN) == LOW && millis() - t < BOOT_REPROVISION_HOLD_MS) {
      delay(50);
    }
    if (millis() - t >= BOOT_REPROVISION_HOLD_MS - 100) {
      enterProvisioning = true;
    }
  }
  buttonPressedFlag = false;

  if (enterProvisioning) {
    Serial.println(F("[BOOT] Entering BLE provisioning (old credentials kept unless new ones arrive)"));
    startBleProvisioning();
    unsigned long provStart = millis();
    while (!bleDone && millis() - provStart < BLE_PROVISION_TIMEOUT_MS) {
      esp_task_wdt_reset();
      delay(100);
    }
    // bleDone == true means the app already succeeded, and saveCredentials()
    // + ESP.restart() already ran inside the BLE write callback - this line
    // is only reached on a timeout, so we stop advertising and fall through
    // to the normal WiFi connect below using the SAME credentials that were
    // already loaded (old real ones, or the compiled-in default).
    if (!bleDone) {
      Serial.println(F("[BOOT] Provisioning window expired - continuing with existing credentials"));
      stopBleProvisioning();
    }
  }

  loadEeprom();
  buildUrls();

  setenv("TZ", "UTC0", 1);
  tzset();
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print(F("[WIFI] Connecting"));
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 20000) {
    delay(400);
    Serial.print(F("."));
    esp_task_wdt_reset();
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WIFI] Connected - IP: %s\n", WiFi.localIP().toString().c_str());
    net_checkInternet();
    net_login();
    net_compareAndSyncTime();
    net_fetchSchedules();
    lastScheduleFetchMs = millis();
    net_deviceReport();
  } else {
    Serial.println(F("\n[WIFI] Failed - will retry in loop"));
  }

  Serial.println(F("[BOOT] Setup done"));
}

void loop() {
  static unsigned long lastStatusPrintMs = 0;

  esp_task_wdt_reset();

  rtc_service();   // rate-limited RTC read + hot-plug detection (must run before any time use)

  handleButtonEvent();
  processOTSensor();
  updateDisplay();

  time_t t = getCurrentUnixTime();
  if (t > 1000000000UL) {
    checkSchedules((uint32_t)t);
    //checkSafetyCutoff((uint32_t)t);
  }

  updateMotorRuntimeCounter();
  net_manageConnectivity();
  sprinkler_service();   // additive: optional motor-board/valve feature - no-ops if no children exist

  if (millis() - lastStatusPrintMs >= STATUS_PRINT_INTERVAL_MS) {
    lastStatusPrintMs = millis();
    printStatusLine();
  }

  delay(100);
}
