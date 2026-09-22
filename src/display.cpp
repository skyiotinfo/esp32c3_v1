#include "display.h"
#include <WiFi.h>
#include <time.h>
#include "config.h"
#include "globals.h"
#include "time_utils.h"

void updateDisplay() {
  colonBlink = !colonBlink;
  if (digitalRead(MOTOR_PIN) == HIGH) {
    int mm = runtimeSecs / 60, ss = runtimeSecs % 60;
    dispObj.showNumberDecEx(mm * 100 + ss, 0b01000000, true);
  } else {
    // Uses the same clock the schedules use (RTC if present, else NTP/internal),
    // so the display never freezes when the RTC is removed.
    time_t t = getCurrentUnixTime();
    if (t > 1000000000UL) {
      struct tm tmv;
      gmtime_r(&t, &tmv);
      dispObj.showNumberDecEx(tmv.tm_hour * 100 + tmv.tm_min,
                              colonBlink ? 0b01000000 : 0, true);
    }
  }
}

void updateMotorRuntimeCounter() {
  if (digitalRead(MOTOR_PIN) == HIGH) {
    runtimeSecs = (int)((millis() - motorStartMs) / 1000UL);
  }
}

void printStatusLine() {
  time_t t = getCurrentUnixTime();
  if (t > 1000000000UL) {
    struct tm tmv;
    gmtime_r(&t, &tmv);
    Serial.printf("[MAIN] Time:%02d:%02d Motor:%s Runtime:%ds OT:%s WiFi:%s RTC:%s ManualUntil:%u\n",
                  tmv.tm_hour, tmv.tm_min,
                  digitalRead(MOTOR_PIN) ? "ON" : "OFF",
                  runtimeSecs,
                  otTripped ? "TRIP" : "ok",
                  WiFi.status() == WL_CONNECTED ? "OK" : "X",
                  rtcAvail ? "ok" : "none",
                  manualBlockUntil);
  }
}
