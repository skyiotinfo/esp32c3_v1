#include "sprinkler_api.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include "config.h"
#include "globals.h"
#include "http_helpers.h"
#include "time_utils.h"
#include "sprinkler_link.h"

// ─────────────────────────── PER-VALVE STATE ────────────────────────────────
// One slot per child `device` row (device_type = 2 'valve') whose
// parent_device_id = DEVICE_ID. A valve's position in this array (ordered by
// device_id, same order the app's listChildDevices() shows) is its
// motor-board node number: slot i <-> node/valve index (i+1).
struct ValveNode {
  long     deviceId          = 0;    // Supabase device_id of this child row
  int      cloudState1       = -1;   // last state_1 read from Supabase
  int      appliedState1     = -1;   // last state_1 we've already reacted to (-1 = never)
  bool     schValid          = false;
  bool     schEnabled        = false;
  uint32_t startUnix         = 0;
  uint32_t stopUnix          = 0;
  uint32_t manualBlockUntil  = 0;
  int      commandedState    = 0;    // 0/1 - what we want the motor board driving right now
  int      lastSyncedCommand = -1;   // last commandedState value pushed to set_device_command
  int      lastReportedState = -1;   // last device_state value pushed to report_device_state
};

static ValveNode     sprinklerValves[MAX_VALVES];
static int           sprinklerValveCount = 0;
static unsigned long sprinklerLastChildPollMs    = 0 - SPRINKLER_CHILD_POLL_MS;    // fetch on first call
static unsigned long sprinklerLastScheduleFetchMs = 0 - SPRINKLER_SCHEDULE_POLL_MS; // fetch on first call

// ───────────────────────────────────────────────────────────────
// fetchChildren()
// One GET for every valve at once: this device's children, filtered to
// device_type=2 ('valve'), ordered by device_id. Re-fetching rebuilds the
// list but carries forward each existing child's cached schedule/manual-
// block/report state by matching on device_id, so a routine re-poll never
// throws away state a schedule fetch just populated.
// ───────────────────────────────────────────────────────────────
static void fetchChildren() {
  if (!login_status || WiFi.status() != WL_CONNECTED || !internetAvailable) return;

  String url = String(SUPABASE_URL) + "/rest/v1/device?parent_device_id=eq." + String(DEVICE_ID) +
               "&device_type=eq.2&select=device_id,state_1&order=device_id";
  String resp;
  if (!getHttpdata(url.c_str(), resp)) return;

  JsonDocument doc;
  if (deserializeJson(doc, resp)) {
    Serial.println(F("[SPRINKLER] children JSON parse fail"));
    return;
  }
  if (!doc.is<JsonArray>()) return;

  ValveNode fresh[MAX_VALVES];
  int n = 0;
  for (JsonObject row : doc.as<JsonArray>()) {
    if (n >= MAX_VALVES) break;
    long id = row["device_id"] | 0L;
    int  s1 = row["state_1"]  | 0;

    fresh[n].deviceId    = id;
    fresh[n].cloudState1 = s1;

    for (int i = 0; i < sprinklerValveCount; i++) {
      if (sprinklerValves[i].deviceId == id) {
        fresh[n].appliedState1     = sprinklerValves[i].appliedState1;
        fresh[n].schValid          = sprinklerValves[i].schValid;
        fresh[n].schEnabled        = sprinklerValves[i].schEnabled;
        fresh[n].startUnix         = sprinklerValves[i].startUnix;
        fresh[n].stopUnix          = sprinklerValves[i].stopUnix;
        fresh[n].manualBlockUntil  = sprinklerValves[i].manualBlockUntil;
        fresh[n].commandedState    = sprinklerValves[i].commandedState;
        fresh[n].lastSyncedCommand = sprinklerValves[i].lastSyncedCommand;
        fresh[n].lastReportedState = sprinklerValves[i].lastReportedState;
        break;
      }
    }
    n++;
  }

  for (int i = 0; i < MAX_VALVES; i++) sprinklerValves[i] = fresh[i];
  if (n != sprinklerValveCount) Serial.printf("[SPRINKLER] Child valve count: %d\n", n);
  sprinklerValveCount = n;
}

// ───────────────────────────────────────────────────────────────
// fetchChildSchedule()
// Same device_seq/sch join the parent uses for itself (net_fetchSchedules
// in supabase_api.cpp), scoped to one child's device_id. The app's own
// selectDeviceSequence() only ever leaves ONE row with sch_enable=true per
// device, so the first enabled row found is authoritative.
// ───────────────────────────────────────────────────────────────
static void fetchChildSchedule(int idx) {
  if (idx < 0 || idx >= sprinklerValveCount) return;
  if (!login_status || WiFi.status() != WL_CONNECTED || !internetAvailable) return;

  long id = sprinklerValves[idx].deviceId;
  String url = String(SUPABASE_URL) + "/rest/v1/device_seq?device_id=eq." + String(id) +
               "&select=duration,start_offset_sec,sch_enable,sch(start,duration)&order=seq_id";
  String resp;
  if (!getHttpdata(url.c_str(), resp)) return;

  JsonDocument doc;
  if (deserializeJson(doc, resp)) return;
  if (!doc.is<JsonArray>()) return;

  sprinklerValves[idx].schValid   = false;
  sprinklerValves[idx].schEnabled = false;

  for (JsonObject row : doc.as<JsonArray>()) {
    bool enabled = row["sch_enable"] | false;
    if (!enabled) continue;
    JsonObject schObj = row["sch"];
    if (schObj.isNull()) continue;

    const char *startStr = schObj["start"] | "00:00:00";
    uint8_t h = 0, m = 0;
    if (strlen(startStr) >= 5) {
      h = (startStr[0] - '0') * 10 + (startStr[1] - '0');
      m = (startStr[3] - '0') * 10 + (startStr[4] - '0');
    }
    int32_t  offsetSec   = row["start_offset_sec"] | 0;
    uint32_t durationMin = row["duration"] | (schObj["duration"] | 10);

    uint32_t startUnix = hourMinuteSecToUnixUTC(h, m, offsetSec);
    uint32_t stopUnix  = startUnix + durationMin * 60UL;

    sprinklerValves[idx].startUnix   = startUnix;
    sprinklerValves[idx].stopUnix    = stopUnix;
    sprinklerValves[idx].schEnabled  = true;
    sprinklerValves[idx].schValid    = (startUnix > 1000000000UL) && (stopUnix > startUnix);
    break; // only one enabled row is expected
  }
}

// ───────────────────────────────────────────────────────────────
// setChildCommand() / reportChildState()
// Exactly the existing set_device_command / report_device_state RPCs
// (see supabase_api.cpp) - called here with a CHILD's device_id instead of
// this board's own DEVICE_ID. No new RPC needed.
// ───────────────────────────────────────────────────────────────
static bool setChildCommand(long childId, int state1) {
  if (!login_status || WiFi.status() != WL_CONNECTED || !internetAvailable) return false;
  String url  = String(SUPABASE_URL) + "/rest/v1/rpc/set_device_command";
  String body = "{\"p_device_id\":" + String(childId) +
                ",\"p_state_1\":" + String(state1) + ",\"p_state_2\":0}";
  return httpPostJson(url.c_str(), body.c_str(), USER_TOKEN.c_str(), nullptr);
}

static bool reportChildState(long childId, int deviceState) {
  if (!login_status || WiFi.status() != WL_CONNECTED || !internetAvailable) return false;
  String url  = String(SUPABASE_URL) + "/rest/v1/rpc/report_device_state";
  String body = "{\"p_device_id\":" + String(childId) +
                ",\"p_device_state\":" + String(deviceState) +
                ",\"p_online\":10" +
                ",\"p_firmware_version\":null" +
                ",\"p_mac_address\":null" +
                ",\"p_last_error\":null}";
  return httpPostJson(url.c_str(), body.c_str(), USER_TOKEN.c_str(), nullptr);
}

// ───────────────────────────────────────────────────────────────
// applyManualOverrides()
// Mirrors net_pollCommandState() in supabase_api.cpp, one valve at a time:
// whenever a valve's state_1 changes on Supabase (the app toggled it), honor
// it immediately. Turning a schedule-driven valve off this way blocks that
// valve's own schedule until the window would have ended anyway - exactly
// the parent device's existing manual-stop-mid-schedule behavior.
// ───────────────────────────────────────────────────────────────
static void applyManualOverrides() {
  for (int i = 0; i < sprinklerValveCount; i++) {
    ValveNode &v = sprinklerValves[i];
    if (v.appliedState1 == -1) {
      v.commandedState = (v.cloudState1 == 1) ? 1 : 0;
      v.appliedState1  = v.cloudState1;
      continue;
    }
    if (v.cloudState1 != v.appliedState1) {
      v.appliedState1 = v.cloudState1;
      if (v.cloudState1 == 1) {
        v.commandedState = 1;
      } else {
        if (v.schValid && v.schEnabled) v.manualBlockUntil = v.stopUnix;
        v.commandedState = 0;
      }
    }
  }
}

// ───────────────────────────────────────────────────────────────
// applySchedules()
// Mirrors checkSchedules() in motor_control.cpp, per valve. Uses the same
// scheduleTimeTrustworthy() gate as the parent's own schedule engine, so
// valves are held off (never auto-started) under the exact same no-RTC/
// no-internet conditions that already stop the parent's own motor.
// ───────────────────────────────────────────────────────────────
static void applySchedules(uint32_t nowUnix) {
  if (!scheduleTimeTrustworthy()) return;

  for (int i = 0; i < sprinklerValveCount; i++) {
    ValveNode &v = sprinklerValves[i];
    if (v.manualBlockUntil > 0 && nowUnix >= v.manualBlockUntil) v.manualBlockUntil = 0;
    if (!v.schValid || !v.schEnabled) continue;
    if (v.manualBlockUntil > 0 && nowUnix < v.manualBlockUntil) continue;

    bool inWindow = (nowUnix >= v.startUnix && nowUnix < v.stopUnix);
    if (inWindow && v.commandedState == 0)       v.commandedState = 1;
    else if (!inWindow && v.commandedState == 1) v.commandedState = 0;
  }
}

// Pushes any changed commandedState to Supabase (state_1), so the app UI
// reflects a schedule-driven start/stop the same way it already reflects a
// manual one - mirrors checkSchedules() calling net_setDeviceCommand().
static void syncCommandToCloud() {
  for (int i = 0; i < sprinklerValveCount; i++) {
    ValveNode &v = sprinklerValves[i];
    if (v.commandedState != v.lastSyncedCommand) {
      if (setChildCommand(v.deviceId, v.commandedState)) {
        v.lastSyncedCommand = v.commandedState;
        v.appliedState1     = v.commandedState; // so the next fetch doesn't re-treat our own change as a manual override
      }
    }
  }
}

// Builds the single 9-bit ("P v1..v8") word from every valve's commandedState
// and hands it to sprinkler_link - which only actually writes to the serial
// port if it differs from what was last sent.
static void buildAndSendCommand() {
  int valveBits[MAX_VALVES + 1] = {0};
  int pumpBit = 0;
  for (int i = 0; i < sprinklerValveCount; i++) {
    if (sprinklerValves[i].commandedState) { valveBits[i + 1] = 1; pumpBit = 1; }
  }
  sprinklerLink_sendCommand(pumpBit, valveBits);
}

// Whenever the motor board's feedback array changed, report each changed
// valve's ACTUAL state back to Supabase via report_device_state - this is
// the "parse into one array, diff, then act" step for the RX direction.
static void reportFeedbackChanges() {
  if (!sprinklerLink_feedbackChanged()) return;

  for (int i = 0; i < sprinklerValveCount; i++) {
    int fb = sprinklerLink_getFeedback(i + 1);
    if (fb < 0) continue;
    if (fb != sprinklerValves[i].lastReportedState) {
      if (reportChildState(sprinklerValves[i].deviceId, fb)) {
        sprinklerValves[i].lastReportedState = fb;
      }
    }
  }
  sprinklerLink_clearFeedbackChanged();
}

void sprinkler_service() {
  sprinklerLink_service();

  unsigned long now = millis();

  if (now - sprinklerLastChildPollMs >= SPRINKLER_CHILD_POLL_MS) {
    sprinklerLastChildPollMs = now;
    fetchChildren();
  }

  if (sprinklerValveCount > 0 &&
      now - sprinklerLastScheduleFetchMs >= SPRINKLER_SCHEDULE_POLL_MS) {
    sprinklerLastScheduleFetchMs = now;
    for (int i = 0; i < sprinklerValveCount; i++) fetchChildSchedule(i);
  }

  time_t t = getCurrentUnixTime();
  if (t > 1000000000UL && sprinklerValveCount > 0) {
    applyManualOverrides();
    applySchedules((uint32_t)t);
    syncCommandToCloud();
    buildAndSendCommand();
  }

  reportFeedbackChanges();
}
