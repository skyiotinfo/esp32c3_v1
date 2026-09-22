#pragma once
#include <Arduino.h>
#include <time.h>

// ---- RTC hot-plug handling -------------------------------------------------
// The DS1307 is NOT accessed directly from the rest of the code any more.
// rtc_service() talks to it at most once per second (and only after an I2C
// probe confirms it answers), keeps a cached copy of the time, and marks the
// RTC as lost the moment it stops answering. Everything else reads the cache
// through getCurrentUnixTime(), so a missing RTC can never spam the I2C bus.
void rtc_init();      // call once in setup(), after Wire.begin()
void rtc_service();   // call once per loop() - cheap, rate-limited internally
void rtc_setTime(uint32_t unixTime);   // write time to the RTC (no-op if absent)
bool rtc_hasValidTime();               // true if the cached RTC time is usable

// Returns "now" as a unix timestamp. Prefers the hardware RTC (works even
// without WiFi); falls back to ESP32's internal clock (set via NTP) if
// there's no RTC. Returns 0 if neither source has a valid time yet.
time_t getCurrentUnixTime();

// Converts a schedule's "start" time (hour:minute) plus a signed offset in
// seconds into a full unix timestamp for *today*, based on the current
// RTC/NTP date.
//
// FIX (carried over from the ESP8266 build): mktime() returns (time_t)-1 on
// failure. Without the check below, (uint32_t)(-1 + offsetSec) wraps around
// to a huge number near UINT32_MAX, which then looks like a "valid"
// far-future timestamp downstream. If that garbage value ever ends up in
// manualBlockUntil (via a manual stop mid-schedule), it would permanently
// block every future schedule since nowUnix would never reach it. Returning
// 0 instead lets net_fetchSchedules()'s validity check correctly reject
// this schedule instead of silently accepting corrupted data.
uint32_t hourMinuteSecToUnixUTC(uint8_t hour, uint8_t minute, int32_t offsetSec);

// Once an hour, fetches real time from an external time API and:
//   - RTC present and running: corrects it if it drifted > DRIFT_THRESHOLD_S.
//   - RTC present but halted/unset: starts it from the internet time.
//   - No RTC: sets the ESP32's internal clock directly from the fetched
//     internet time, so getCurrentUnixTime() is sourced straight from the
//     time server rather than only relying on background NTP.
// Also runs right away after the internet returns, or after the RTC is lost
// (both reset timeSynced). Skipped without a confirmed WiFi + internet path.
void net_compareAndSyncTime();

// True when the current value of getCurrentUnixTime() can be trusted enough
// to drive the schedule engine:
//   - the hardware RTC is present, running and readable -> always trusted,
//     with or without internet (it keeps its own time independently), OR
//   - there's no usable RTC, but the internet is currently up (so "now"
//     is coming fresh from net_compareAndSyncTime()'s internet time server).
// If neither is true (no RTC AND no internet), the software clock is only
// coasting on its last known value with nothing to verify or correct it,
// so schedule start/stop decisions should not be trusted - see
// checkSchedules() in motor_control.cpp, which force-stops an active
// schedule the moment this goes false.
// The "no internet" half is confirmed for NO_RTC_NET_GRACE_MS (default 15 s,
// see time_utils.cpp) before it counts, so one failed HTTP call can't stop
// the pump. An RTC that is present and running is never subject to this.
bool scheduleTimeTrustworthy();
