#include "connectivity.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "config.h"
#include "globals.h"
#include "supabase_auth.h"
#include "supabase_api.h"
#include "time_utils.h"

void net_wifiReconnectIfNeeded() {
  if (WiFi.status() == WL_CONNECTED) return;
  unsigned long now = millis();
  if (now - lastWifiAttemptMs < WIFI_RETRY_INTERVAL_MS) return;
  lastWifiAttemptMs = now;
  Serial.println(F("[WIFI] Reconnecting (non-blocking)"));
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

// ───────────────────────────────────────────────────────────────
// net_checkInternet()
// Hits GoTrue's /health endpoint (needs no login) so it never depends on
// login_status. If WiFi is associated but this fails (router's WAN down,
// captive portal, etc.), force a WiFi.disconnect() so control passes back
// to net_wifiReconnectIfNeeded() on its normal retry cadence — exactly
// like a real WiFi drop — instead of polling a dead link forever.
// ───────────────────────────────────────────────────────────────
bool net_checkInternet() {
  if (WiFi.status() != WL_CONNECTED) { internetAvailable = false; return false; }

  bool wasAvailable = internetAvailable;

  WiFiClientSecure cl;
  cl.setInsecure();
  cl.setTimeout(HTTP_CONNECT_TIMEOUT_MS);
  HTTPClient https;
  https.setTimeout(HTTP_CONNECT_TIMEOUT_MS);
  String url = String(SUPABASE_URL) + "/auth/v1/health";
  if (!https.begin(cl, url.c_str())) { internetAvailable = false; return false; }
  https.addHeader(F("apikey"), SUPABASE_KEY);
  int code = https.GET();
  https.end();
  internetAvailable = (code > 0);
  Serial.printf("[NET] Internet check: %s (code %d)\n", internetAvailable ? "UP" : "DOWN", code);

  if (!internetAvailable) {
    Serial.println(F("[NET] No internet on this WiFi link - disconnecting WiFi to force a fresh reconnect"));
    WiFi.disconnect();
    login_status = false;
  } else if (!wasAvailable) {
    // Internet just came back after being down. If there's no RTC, the
    // schedule engine has been forcibly stopped (see checkSchedules()) and
    // is waiting on a fresh, trustworthy "now" — don't make it wait for
    // net_compareAndSyncTime()'s hourly timer, pull the real time right away.
    Serial.println(F("[NET] Internet just came back - forcing an immediate time re-sync"));
    timeSynced = false;
  }
  return internetAvailable;
}

// ───────────────────────────────────────────────────────────────
// net_manageConnectivity()
// Single entry point for everything network-related, called once per
// loop(). See config.h / globals.h for the timers this drives.
// ───────────────────────────────────────────────────────────────
void net_manageConnectivity() {
  net_wifiReconnectIfNeeded();

  if (WiFi.status() != WL_CONNECTED) {
    internetAvailable = false;
    login_status      = false;
    return;
  }

  unsigned long now = millis();

  if (now - lastInternetCheckMs >= INTERNET_CHECK_INTERVAL_MS) {
    lastInternetCheckMs = now;
    net_checkInternet();

    if (internetAvailable && !login_status) {
      net_login();
      if (login_status) {
        int actualState = (digitalRead(MOTOR_PIN) == HIGH) ? 1 : 0;
        net_setDeviceCommand(actualState, 0);
      }
    }
  }

  if (!internetAvailable) return;

  if (login_status) checkTokenRefresh();

  lastCallFailedHard = false;

  if (login_status && (now - lastScheduleFetchMs >= SCHEDULE_FETCH_INTERVAL_MS)) {
    lastScheduleFetchMs = now;
    net_fetchSchedules();
  }
  if (login_status && (now - lastCommandPollMs >= COMMAND_POLL_INTERVAL_MS)) {
    lastCommandPollMs = now;
    net_pollCommandState();
  }
  if (login_status && (now - lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS)) {
    lastHeartbeatMs = now;
    net_deviceReport();
  }

  net_compareAndSyncTime();
}
