#include "supabase_auth.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "config.h"
#include "globals.h"

void net_login() {
  if (WiFi.status() != WL_CONNECTED || !internetAvailable) return;

  String authUrl = String(SUPABASE_URL) + "/auth/v1/token?grant_type=password";
  WiFiClientSecure cl;
  cl.setInsecure();
  cl.setTimeout(HTTP_CONNECT_TIMEOUT_MS);
  HTTPClient https;
  https.setTimeout(8000);
  if (!https.begin(cl, authUrl.c_str())) return;
  https.addHeader(F("apikey"),       SUPABASE_KEY);
  https.addHeader(F("Content-Type"), F("application/json"));
  String body = "{\"email\":\"" + String(USER_EMAIL) +
                "\",\"password\":\"" + String(USER_PASS) + "\"}";
  int code = https.POST(body);
  Serial.printf("[AUTH] Login HTTP %d\n", code);
  if (code < 0) internetAvailable = false;

  if (code == 200) {
    JsonDocument doc;
    if (!deserializeJson(doc, https.getString()) &&
        doc["access_token"].is<const char *>()) {
      USER_TOKEN     = doc["access_token"].as<String>();
      unsigned long expiresIn = doc["expires_in"] | 3600UL;
      tokenExpiresAt = millis() + ((expiresIn - TOKEN_REFRESH_BEFORE_S) * 1000UL);
      login_status   = true;
      Serial.printf("[AUTH] OK - token expires in %lu s\n", expiresIn);
    }
  } else {
    login_status = false;
    Serial.println(F("[AUTH] Login failed"));
  }
  https.end();
}

void checkTokenRefresh() {
  if (!login_status) return;
  if (millis() >= tokenExpiresAt) {
    Serial.println(F("[AUTH] Token expiring - re-logging in"));
    login_status = false;
    net_login();
  }
}
