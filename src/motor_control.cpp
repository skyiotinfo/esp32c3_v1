#include "motor_control.h"
#include "config.h"
#include "globals.h"
#include "storage.h"
#include "time_utils.h"
#include "supabase_api.h"

void applyMotorCommand(bool on, bool viaSchedule) {
  bool currentlyOn = (digitalRead(MOTOR_PIN) == HIGH);
  if (on == currentlyOn) return;

  digitalWrite(MOTOR_PIN, on ? HIGH : LOW);
  if (on) {
    motorStartMs     = millis();
    runtimeSecs      = 0;
    time_t nowT = getCurrentUnixTime();
    motorOnSinceUnix = (nowT > 1000000000UL) ? (uint32_t)nowT : 0;
    Serial.println(viaSchedule ? F("[MOTOR] ON (schedule)") : F("[MOTOR] ON"));
  } else {
    runtimeSecs      = 0;
    motorOnSinceUnix = 0;
    Serial.println(viaSchedule ? F("[MOTOR] OFF (schedule)") : F("[MOTOR] OFF"));
  }
  saveEeprom();
  net_deviceReport(); // push actual state immediately; no-ops cleanly if WiFi/internet is down
}

void checkSchedules(uint32_t nowUnix) {
  // Safety gate: with no RTC, "now" is only as good as the last internet
  // time sync. If the RTC is missing/stopped AND the internet just isn't
  // there right now, don't trust nowUnix enough to run the schedule engine
  // on it — and if a schedule is actively driving the motor when this
  // happens, stop it rather than let it keep running (or stop) on a clock
  // we can no longer verify or correct.
  if (!scheduleTimeTrustworthy()) {
    if (activeSchIdx >= 0) {
      Serial.println(F("[SCH] No RTC and no internet - stopping active schedule (untrusted time)"));
      pendingLastError = "Schedule stopped: no RTC and no internet";
      net_setDeviceCommand(0, 0);      // no-ops cleanly if we're actually offline
      applyMotorCommand(false, true);
      activeSchIdx = -1;
    }
    return; // don't touch manual-block expiry or evaluate new starts either
  }

  // Clear an expired manual block.
  if (manualBlockUntil > 0 && nowUnix >= manualBlockUntil) {
    Serial.println(F("[SCH] Manual-stop window expired - block cleared"));
    manualBlockUntil = 0;
    saveEeprom();
  }

  // Stop condition for whichever schedule is currently active.
  if (activeSchIdx >= 0) {
    if (nowUnix >= schedules[activeSchIdx].stopUnix) {
      Serial.printf("[SCH] STOP seq_id=%d\n", schedules[activeSchIdx].seqId);
      net_setDeviceCommand(0, 0);
      applyMotorCommand(false, true);
      activeSchIdx = -1;
    }
    return; // don't evaluate start conditions while one schedule is running
  }

  if (otTripped) return;
  if (manualBlockUntil > 0 && nowUnix < manualBlockUntil) return;
  if (digitalRead(MOTOR_PIN) == HIGH) return; // already on for some other reason (e.g. button)

  for (int i = 0; i < scheduleCount; i++) {
    if (!schedules[i].valid || !schedules[i].enabled) continue;
    if (nowUnix >= schedules[i].startUnix && nowUnix < schedules[i].stopUnix) {
      Serial.printf("[SCH] START seq_id=%d\n", schedules[i].seqId);
      net_setDeviceCommand(1, 0);
      applyMotorCommand(true, true);
      activeSchIdx = i;
      break;
    }
  }
}

void checkSafetyCutoff(uint32_t nowUnix) {
  if (digitalRead(MOTOR_PIN) != HIGH) return;
  if (motorOnSinceUnix == 0) return; // no RTC / unknown start time, can't evaluate
  uint32_t elapsedMin = (nowUnix - motorOnSinceUnix) / 60UL;
  if (elapsedMin >= (uint32_t)MAX_SAFETY_RUNTIME_MIN) {
    Serial.println(F("[SAFETY] Max runtime exceeded - forcing OFF"));
    pendingLastError = "Safety runtime cutoff (" + String(MAX_SAFETY_RUNTIME_MIN) + " min)";
    net_setDeviceCommand(0, 0);
    applyMotorCommand(false, false);
    activeSchIdx = -1;
    manualBlockUntil = 0; // let schedules re-evaluate normally next window
  }
}

void syncButtonMotorState() {
  bool nowOn = (digitalRead(MOTOR_PIN) == HIGH);   // reflects the ISR's toggle
  runtimeSecs = 0;

  if (nowOn) {
    motorStartMs = millis();
    time_t nowT = getCurrentUnixTime();
    motorOnSinceUnix = (nowT > 1000000000UL) ? (uint32_t)nowT : 0;
    net_setDeviceCommand(1, 0);
    Serial.println(F("[MOTOR] ON"));
  } else {
    if (activeSchIdx >= 0) {
      manualBlockUntil = schedules[activeSchIdx].stopUnix;
      Serial.printf("[BTN] Manual stop mid-schedule - blocked until %u\n", manualBlockUntil);
      activeSchIdx = -1;
    }
    motorOnSinceUnix = 0;
    net_setDeviceCommand(0, 0);
    Serial.println(F("[MOTOR] OFF"));
  }
  saveEeprom();
  net_deviceReport();
}

void handleButtonPress() {
  Serial.println(F("[BTN] Press"));

  if (otTripped) {
    otTripped = false;
    pendingLastError = ""; // clears last_error on next report
    saveEeprom();
    dispObj.clear();
    net_deviceReport();
    Serial.println(F("[BTN] OT reset"));
    return;
  }

  syncButtonMotorState();
}

void handleButtonEvent() {
  if (!buttonPressedFlag) return;
  buttonPressedFlag = false;
  handleButtonPress();
}

void IRAM_ATTR buttonISR() {
  unsigned long now = millis();
  if (now - lastButtonIsrMs > 200) {
    lastButtonIsrMs = now;
    if (!otTripped) {
      bool currentlyOn = (digitalRead(MOTOR_PIN) == HIGH);
      digitalWrite(MOTOR_PIN, currentlyOn ? LOW : HIGH);   // instant, non-blocking pump response
    }
    buttonPressedFlag = true;
  }
}
