#include "storage.h"
#include <EEPROM.h>
#include "config.h"
#include "globals.h"
#include "time_utils.h"

// ───────────────────────────────────────────────────────────────
// saveEeprom()
// Only actually writes/commits when something changed, to avoid
// unnecessary flash wear (ESP32's EEPROM library also emulates
// flash sectors, so this matters here too).
// ───────────────────────────────────────────────────────────────
void saveEeprom() {
  bool dirty = false;

  uint32_t curManualUntil; EEPROM.get(EEPROM_ADDR_MANUAL_UNTIL, curManualUntil);
  if (curManualUntil != manualBlockUntil) {
    EEPROM.put(EEPROM_ADDR_MANUAL_UNTIL, manualBlockUntil);
    dirty = true;
  }
  uint8_t curOt; EEPROM.get(EEPROM_ADDR_OT_TRIPPED, curOt);
  uint8_t o = otTripped ? 1 : 0;
  if (curOt != o) {
    EEPROM.put(EEPROM_ADDR_OT_TRIPPED, o);
    dirty = true;
  }
  uint8_t curState1; EEPROM.get(EEPROM_ADDR_LAST_STATE1, curState1);
  uint8_t s1 = (lastAppliedState1 == 1) ? 1 : 0;
  if (curState1 != s1) {
    EEPROM.put(EEPROM_ADDR_LAST_STATE1, s1);
    dirty = true;
  }
  uint32_t curOnSince; EEPROM.get(EEPROM_ADDR_MOTOR_ON_SINCE, curOnSince);
  if (curOnSince != motorOnSinceUnix) {
    EEPROM.put(EEPROM_ADDR_MOTOR_ON_SINCE, motorOnSinceUnix);
    dirty = true;
  }
  if (dirty) EEPROM.commit();
}

// ───────────────────────────────────────────────────────────────
// loadEeprom()
// If the motor was ON when power was lost (and OT hasn't tripped),
// re-energizes the motor immediately so the pump doesn't stay off
// through a reboot, then waits for the cloud to confirm/override.
//
// Rejects both the blank-EEPROM sentinel (0xFFFFFFFF) and any
// manualBlockUntil absurdly far in the future (> ~year 2096) —
// the latter guards against a stored value that could only have
// been produced by a mktime() failure wrapping around to a huge
// uint32_t, which would otherwise permanently block every future
// schedule (see the guard in hourMinuteSecToUnixUTC()).
// ───────────────────────────────────────────────────────────────
void loadEeprom() {
  EEPROM.get(EEPROM_ADDR_MANUAL_UNTIL,   manualBlockUntil);
  uint8_t o, s1;
  EEPROM.get(EEPROM_ADDR_OT_TRIPPED,     o);
  EEPROM.get(EEPROM_ADDR_LAST_STATE1,    s1);
  EEPROM.get(EEPROM_ADDR_MOTOR_ON_SINCE, motorOnSinceUnix);

  otTripped = (o == 1);
  if (manualBlockUntil == 0xFFFFFFFF || manualBlockUntil > 4000000000UL) manualBlockUntil = 0;
  if (motorOnSinceUnix == 0xFFFFFFFF) motorOnSinceUnix = 0;

  if (s1 == 1 && !otTripped) {
    Serial.println(F("[BOOT] Resuming motor ON from EEPROM until cloud confirms"));
    digitalWrite(MOTOR_PIN, HIGH);
    motorStartMs      = millis();
    lastAppliedState1 = 1;
    if (motorOnSinceUnix == 0) {
      time_t nowT = getCurrentUnixTime();
      if (nowT > 1000000000UL) motorOnSinceUnix = (uint32_t)nowT;
    }
  } else {
    lastAppliedState1 = 0;
    motorOnSinceUnix  = 0;
  }
  Serial.printf("[EEPROM] manualBlockUntil=%u ot=%d lastState1=%d onSince=%u\n",
                manualBlockUntil, (int)otTripped, s1, motorOnSinceUnix);
}
