#pragma once
// ============================================================================
// globals.h — every piece of state shared across modules, declared once here
// (extern) and defined exactly once in globals.cpp. Keeps main.cpp a short,
// readable list of setup()/loop() calls, and keeps every other .cpp free of
// its own copy of the same declarations.
// ============================================================================
#include <Arduino.h>
#include <TM1637Display.h>
#include "RTClib.h"
#include "config.h"

// ─────────────────────────── HARDWARE OBJECTS ──────────────────────────────
extern RTC_DS1307    rtc;
extern TM1637Display dispObj;
extern bool          rtcAvail;

// ─────────────────────────── PROVISIONED CREDENTIALS ───────────────────────
// Loaded from / saved to NVS (Preferences) by credentials.cpp; filled in by
// BLE provisioning (ble_provisioning.cpp) the first time the unit boots.
extern char WIFI_SSID[33];
extern char WIFI_PASS[65];
extern char USER_EMAIL[50];
extern char USER_PASS[33];
extern char SUPABASE_URL[120];
extern char SUPABASE_KEY[320];
extern bool g_provisioned;

// Pre-built REST/RPC URLs (built once, right after credentials load).
extern String url_device_select;   // GET  state_1,state_2 (light poll)
extern String url_device_seq;      // GET  device_seq + sch join (schedules)
extern String url_rpc_command;     // POST set_device_command
extern String url_rpc_report;      // POST report_device_state

// ─────────────────────────── SCHEDULE MODEL ────────────────────────────────
struct ScheduleEntry {
  int32_t  seqId     = -1;
  uint32_t startUnix = 0;
  uint32_t stopUnix  = 0;
  bool     enabled   = false;
  bool     valid     = false;
};
extern ScheduleEntry schedules[MAX_SCHEDULES];
extern int           scheduleCount;
extern int           activeSchIdx;   // index into schedules[] driving the motor, -1 = none

// ─────────────────────────── AUTH STATE ────────────────────────────────────
extern String        USER_TOKEN;
extern bool          login_status;
extern unsigned long tokenExpiresAt;

// ─────────────────────────── RUNTIME STATE ─────────────────────────────────
extern uint32_t      manualBlockUntil;   // schedule re-trigger suppressed until this unix time
extern volatile bool otTripped;          // read directly inside buttonISR()
extern int           ot_sensorcount;
extern int           lastAppliedState1; // -1 = unknown (forces first apply)
extern uint32_t      motorOnSinceUnix;  // for safety cutoff + display runtime
extern int           runtimeSecs;
extern bool          timeSynced;
extern unsigned long lastSyncMs;
extern unsigned long lastCommandPollMs;
extern unsigned long lastHeartbeatMs;
extern unsigned long lastScheduleFetchMs;
extern unsigned long lastWifiAttemptMs;
extern unsigned long motorStartMs;
extern bool          colonBlink;
extern String        pendingLastError;   // sent on next heartbeat, then cleared

extern volatile bool          buttonPressedFlag;
extern volatile unsigned long lastButtonIsrMs;

extern bool          lastCallFailedHard; // set true the instant any HTTP call can't reach the server
extern bool          internetAvailable;  // true only after a real internet check (or call) succeeds
extern unsigned long lastInternetCheckMs;

// ─────────────────────────── BLE PROVISIONING STATE ────────────────────────
extern bool bleDone;
extern bool bleClientConnected;
