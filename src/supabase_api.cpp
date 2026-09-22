#include "supabase_api.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include "config.h"
#include "globals.h"
#include "http_helpers.h"
#include "time_utils.h"
#include "storage.h"         // saveEeprom()
#include "motor_control.h"   // applyMotorCommand()

// ───────────────────────────────────────────────────────────────
// buildUrls()
// Same shape as the ESP8266 build: device_seq + sch(start,duration) join,
// filtered to this DEVICE_ID, ordered by seq_id. Pre-built once so we never
// re-concatenate the same base URL on every single HTTP call.
// ───────────────────────────────────────────────────────────────
void buildUrls() {
  String base = String(SUPABASE_URL);
  url_device_select = base + "/rest/v1/device?device_id=eq." + String(DEVICE_ID) +
                       "&select=state_1,state_2";
  url_device_seq     = base + "/rest/v1/device_seq?device_id=eq." + String(DEVICE_ID) +
                       "&select=seq_id,duration,start_offset_sec,sch_enable,sch(start,duration)"
                       "&order=seq_id";
  url_rpc_command    = base + "/rest/v1/rpc/set_device_command";
  url_rpc_report     = base + "/rest/v1/rpc/report_device_state";
}

// ───────────────────────────────────────────────────────────────
// net_fetchSchedules()
// Reads the device_seq -> sch join and recomputes each schedule's today's
// start/stop unix timestamps into schedules[]. Skipped without login +
// confirmed internet.
// ───────────────────────────────────────────────────────────────
void net_fetchSchedules() {
  if (!login_status || WiFi.status() != WL_CONNECTED || !internetAvailable) return;

  String resp;
  if (!getHttpdata(url_device_seq.c_str(), resp)) return;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, resp);
  if (err) {
    Serial.println(F("[SCH] JSON parse fail"));
    return;
  }
  if (!doc.is<JsonArray>()) return;

  int n = 0;
  for (JsonObject row : doc.as<JsonArray>()) {
    if (n >= MAX_SCHEDULES) break;

    JsonObject schObj = row["sch"];
    if (schObj.isNull()) continue; // row filtered out by RLS join / deleted parent

    const char *startStr = schObj["start"] | "00:00:00";
    uint8_t h = 0, m = 0;
    if (strlen(startStr) >= 5) {
      h = (startStr[0] - '0') * 10 + (startStr[1] - '0');
      m = (startStr[3] - '0') * 10 + (startStr[4] - '0');
    }
    int32_t  offsetSec  = row["start_offset_sec"] | 0;
    uint32_t durationMin = row["duration"] | (schObj["duration"] | 10);
    bool     enabled     = row["sch_enable"] | false;

    uint32_t startUnix = hourMinuteSecToUnixUTC(h, m, offsetSec);
    uint32_t stopUnix  = startUnix + (durationMin * 60UL);

    schedules[n].seqId     = row["seq_id"] | -1;
    schedules[n].startUnix = startUnix;
    schedules[n].stopUnix  = stopUnix;
    schedules[n].enabled   = enabled;
    schedules[n].valid     = (startUnix > 1000000000UL) && (stopUnix > startUnix);
    n++;
  }
  scheduleCount = n;
  Serial.printf("[SCH] Loaded %d schedule(s)\n", scheduleCount);

  if (activeSchIdx >= scheduleCount) activeSchIdx = -1;
}

// ───────────────────────────────────────────────────────────────
// net_pollCommandState()
// Applies a cloud-side state_1 change (from the app) locally.
// ───────────────────────────────────────────────────────────────
void net_pollCommandState() {
  if (!login_status || WiFi.status() != WL_CONNECTED || !internetAvailable) return;

  String resp;
  if (!getHttpdata(url_device_select.c_str(), resp)) return;

  JsonDocument doc;
  if (deserializeJson(doc, resp)) {
    Serial.println(F("[POLL] JSON parse fail"));
    return;
  }
  if (!doc.is<JsonArray>() || doc.size() == 0) return;

  int cloudState1 = doc[0]["state_1"] | 0;

  if (lastAppliedState1 == -1) {
    bool wantOn = (cloudState1 == 1) && !otTripped;
    if (wantOn != (digitalRead(MOTOR_PIN) == HIGH)) {
      applyMotorCommand(wantOn, false);
    }
    saveEeprom();
    return;
  }

  if (cloudState1 != lastAppliedState1) {
    Serial.printf("[POLL] state_1 changed %d -> %d\n", lastAppliedState1, cloudState1);
    lastAppliedState1 = cloudState1;

    if (cloudState1 == 1 && !otTripped) {
      applyMotorCommand(true, false);
    } else if (cloudState1 == 0) {
      if (activeSchIdx >= 0) {
        manualBlockUntil = schedules[activeSchIdx].stopUnix;
        Serial.printf("[POLL] App stop mid-schedule - blocked until %u\n", manualBlockUntil);
        activeSchIdx = -1;
      }
      applyMotorCommand(false, false);
    }
    saveEeprom();
  }
}

// ───────────────────────────────────────────────────────────────
// net_setDeviceCommand()
// ───────────────────────────────────────────────────────────────
void net_setDeviceCommand(int state1, int state2) {
  if (!login_status || WiFi.status() != WL_CONNECTED || !internetAvailable) {
    Serial.println(F("[CMD] Skipped - not logged in / no WiFi"));
    return;
  }
  String body = "{\"p_device_id\":" + String(DEVICE_ID) +
                ",\"p_state_1\":" + String(state1) +
                ",\"p_state_2\":" + String(state2) + "}";
  if (httpPostJson(url_rpc_command.c_str(), body.c_str(), USER_TOKEN.c_str(), nullptr)) {
    lastAppliedState1 = state1;
  } else {
    Serial.println(F("[CMD] set_device_command failed"));
  }
}

// ───────────────────────────────────────────────────────────────
// net_deviceReport()
// ───────────────────────────────────────────────────────────────
void net_deviceReport() {
  if (!login_status || WiFi.status() != WL_CONNECTED || !internetAvailable) {
    Serial.println(F("[REPORT] Skipped - not logged in / no WiFi"));
    return;
  }

  int deviceState = (digitalRead(MOTOR_PIN) == HIGH) ? 1 : 0;
  String mac = WiFi.macAddress();

  String body = "{\"p_device_id\":" + String(DEVICE_ID) +
                ",\"p_device_state\":" + String(deviceState) +
                ",\"p_online\":10" +
                ",\"p_firmware_version\":\"" + String(FIRMWARE_VERSION) + "\"" +
                ",\"p_mac_address\":\"" + mac + "\"";
  if (pendingLastError.length() > 0) {
    body += ",\"p_last_error\":\"" + pendingLastError + "\"";
  } else {
    body += ",\"p_last_error\":null";
  }
  body += "}";

  if (httpPostJson(url_rpc_report.c_str(), body.c_str(), USER_TOKEN.c_str(), nullptr)) {
    pendingLastError = "";
  } else {
    Serial.println(F("[REPORT] report_device_state failed"));
  }
}
