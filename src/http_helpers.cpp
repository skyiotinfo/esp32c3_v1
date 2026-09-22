#include "http_helpers.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "config.h"
#include "globals.h"

bool getHttpdata(const char *url, String &out) {
  if (WiFi.status() != WL_CONNECTED || !internetAvailable) return false;

  WiFiClientSecure cl;
  cl.setInsecure();
  cl.setTimeout(HTTP_CONNECT_TIMEOUT_MS);
  HTTPClient https;
  https.setTimeout(HTTP_RESPONSE_TIMEOUT_MS);
  if (!https.begin(cl, url)) return false;
  https.addHeader(F("apikey"),        SUPABASE_KEY);
  https.addHeader(F("Authorization"), "Bearer " + USER_TOKEN);
  https.addHeader(F("Content-Type"),  F("application/json"));
  int code = https.GET();
  if (code < 0) {
    lastCallFailedHard = true;
    internetAvailable  = false;
  }
  if (code == 200) out = https.getString();
  https.end();
  Serial.printf("[HTTP] GET %d %s\n", code, url);
  return (code == 200);
}

bool httpPostJson(const char *url, const char *body, const char *bearerToken, String *out) {
  if (WiFi.status() != WL_CONNECTED || !internetAvailable) return false;

  WiFiClientSecure cl;
  cl.setInsecure();
  cl.setTimeout(HTTP_CONNECT_TIMEOUT_MS);
  HTTPClient https;
  https.setTimeout(HTTP_RESPONSE_TIMEOUT_MS);
  if (!https.begin(cl, url)) return false;
  https.addHeader(F("apikey"), SUPABASE_KEY);
  String bearer = bearerToken ? String(bearerToken) : String(SUPABASE_KEY);
  https.addHeader(F("Authorization"), "Bearer " + bearer);
  https.addHeader(F("Content-Type"),  F("application/json"));
  https.addHeader(F("Prefer"),        F("return=minimal"));
  int code = https.POST(body);
  if (code < 0) {
    lastCallFailedHard = true;
    internetAvailable  = false;
  }
  if (out && code >= 200 && code < 300) *out = https.getString();
  https.end();
  Serial.printf("[HTTP] POST %d %s\n", code, url);
  return (code >= 200 && code < 300);
}
