#include "time_utils.h"
#include <ArduinoJson.h>
#include <Wire.h>
#include <sys/time.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "config.h"
#include "globals.h"

// ─────────────────────────── RTC (DS1307) HOT-PLUG SAFE ACCESS ────────────
#define RTC_I2C_ADDR              0x68
#define RTC_POLL_INTERVAL_MS      1000UL   // read the chip this often while it is present
#define RTC_REPROBE_INTERVAL_MS   5000UL   // look for the chip this often while it is missing

static bool          rtcTimeValid  = false; // cached time below is trustworthy
static uint32_t      rtcUnixAtRead = 0;     // unix time at the last successful read
static unsigned long rtcReadMs     = 0;     // millis() at that read
static unsigned long rtcLastPollMs = 0;

static void ensureUtcTimebase() {
  static bool initialized = false;
  if (initialized) return;
  setenv("TZ", "UTC0", 1);
  tzset();
  initialized = true;
}

// Address-only probe. A missing chip just NACKs, which endTransmission()
// reports quietly - unlike requestFrom(), which prints an error every call.
static bool rtcProbe() {
  Wire.beginTransmission(RTC_I2C_ADDR);
  return Wire.endTransmission() == 0;
}

void rtc_service() {
  unsigned long ms = millis();
  unsigned long interval = rtcAvail ? RTC_POLL_INTERVAL_MS : RTC_REPROBE_INTERVAL_MS;
  if (ms - rtcLastPollMs < interval) return;
  rtcLastPollMs = ms;

  if (!rtcProbe()) {                       // chip removed / not answering
    if (rtcAvail) {
      Serial.println(F("[RTC] Lost - switching to internal/NTP clock"));
      timeSynced = false;   // pull fresh internet time on the next net_compareAndSyncTime()
    }
    rtcAvail     = false;
    rtcTimeValid = false;
    return;
  }

  if (!rtcAvail) {                         // chip (re)appeared
    if (!rtc.begin(&Wire)) return;
    rtcAvail = true;
    Serial.println(F("[RTC] DS1307 detected"));
  }

  if (!rtc.isrunning()) {                  // present but halted / never set
    rtcTimeValid = false;
    return;
  }
  uint32_t u = rtc.now().unixtime();
  if (u < 1000000000UL) {                  // garbage or unset (year 2000) time
    rtcTimeValid = false;
    return;
  }

  rtcUnixAtRead = u;
  rtcReadMs     = ms;
  rtcTimeValid  = true;

  // Seed the ESP32's own clock once from the RTC if it has no valid time yet,
  // so the display/log keep a sensible time if the RTC is later removed.
  // (Schedules are NOT run on that coasting clock while offline - see
  // scheduleTimeTrustworthy() below.)
  if (time(nullptr) < 1000000000L) {
    struct timeval tv = { (time_t)u, 0 };
    settimeofday(&tv, nullptr);
  }
}

void rtc_init() {
  rtcAvail      = false;
  rtcTimeValid  = false;
  rtcLastPollMs = millis() - RTC_REPROBE_INTERVAL_MS;   // poll immediately
  rtc_service();
  if (!rtcAvail) Serial.println(F("[RTC] Not found - using NTP fallback (will keep checking)"));
}

void rtc_setTime(uint32_t unixTime) {
  if (!rtcAvail) return;
  rtc.adjust(DateTime(unixTime));
  rtcUnixAtRead = unixTime;
  rtcReadMs     = millis();
  rtcTimeValid  = true;
}

bool rtc_hasValidTime() { return rtcTimeValid; }

time_t getCurrentUnixTime() {
  if (rtcTimeValid)                        // cached RTC time, advanced by millis()
    return (time_t)(rtcUnixAtRead + (millis() - rtcReadMs) / 1000UL);
  time_t t = time(nullptr);                // ESP32 internal clock (NTP / seeded)
  return (t > 1000000000UL) ? t : 0;
}

uint32_t hourMinuteSecToUnixUTC(uint8_t hour, uint8_t minute, int32_t offsetSec) {
  ensureUtcTimebase();

  time_t now = getCurrentUnixTime();
  if (now == 0) return 0;
  struct tm t;
  gmtime_r(&now, &t);
  t.tm_hour = hour;
  t.tm_min  = minute;
  t.tm_sec  = 0;
  time_t base = mktime(&t);
  if (base == (time_t)-1) return 0;   // mktime failed - don't return wraparound garbage
  return (uint32_t)(base + offsetSec);
}

void net_compareAndSyncTime() {
  if (timeSynced && (millis() - lastSyncMs < TIME_SYNC_INTERVAL_MS)) return;
  if (WiFi.status() != WL_CONNECTED || !internetAvailable) return;

  WiFiClientSecure cl;
  cl.setInsecure();
  cl.setTimeout(HTTP_CONNECT_TIMEOUT_MS);
  HTTPClient https;
  https.setTimeout(HTTP_RESPONSE_TIMEOUT_MS);
  if (!https.begin(cl, "https://api.skyiottech.com/time")) return;
  int    code = https.GET();
  String body = (code == 200) ? https.getString() : "";
  https.end();
  if (code < 0) internetAvailable = false;

  if (code != 200 || body.isEmpty()) {
    Serial.println(F("[SYNC] Failed to get internet time"));
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) return;

  time_t internetTime = (time_t)doc["unix_time"].as<unsigned long>();

  if (rtcAvail && rtcTimeValid) {
    long drift = labs((long)internetTime - (long)getCurrentUnixTime());
    Serial.printf("[SYNC] Drift: %ld s\n", drift);
    if (drift > DRIFT_THRESHOLD_S) {
      rtc_setTime((uint32_t)internetTime);
      Serial.println(F("[SYNC] RTC updated"));
    }
  } else {
    // No usable RTC: set the ESP32's own internal clock directly from the
    // internet time server's answer. getCurrentUnixTime() falls back to
    // time(nullptr), so this is what makes the schedule engine run off
    // "internet time" without an RTC chip, rather than only hoping the
    // background NTP client has synced on its own.
    struct timeval tv;
    tv.tv_sec  = internetTime;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
    if (rtcAvail) {
      // RTC is present but halted/unset - start it too.
      rtc_setTime((uint32_t)internetTime);
      Serial.println(F("[SYNC] RTC was not running - set from internet time (system clock too)"));
    } else {
      Serial.println(F("[SYNC] No RTC - system clock set directly from internet time server"));
    }
  }
  timeSynced = true;
  lastSyncMs = millis();
}

// With NO usable RTC the schedule engine runs on internet time, so it must stop
// when the internet is really gone. But internetAvailable is also flipped to
// false by a single failed HTTP call (one slow heartbeat / poll), and it comes
// back on the next 10 s internet check. Without a grace period one such blip
// would stop and restart the pump. So "internet gone" only counts once it has
// stayed down for NO_RTC_NET_GRACE_MS. Set it to 0 for an immediate stop.
#ifndef NO_RTC_NET_GRACE_MS
#define NO_RTC_NET_GRACE_MS  15000UL
#endif

static bool          netEverUp   = false;
static unsigned long netLastUpMs = 0;

bool scheduleTimeTrustworthy() {
  unsigned long ms = millis();
  if (internetAvailable) { netEverUp = true; netLastUpMs = ms; }

  if (rtcTimeValid)     return true;   // RTC keeps its own time, internet or not
  if (internetAvailable) return true;  // no usable RTC -> internet time is fresh

  // No RTC and internet is down: trust the coasting clock only for the grace period.
  return netEverUp && (ms - netLastUpMs) < NO_RTC_NET_GRACE_MS;
}
