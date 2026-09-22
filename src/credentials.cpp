#include "credentials.h"
#include <Preferences.h>
#include "globals.h"

void loadCredentials() {
  Preferences p;
  p.begin("pump_creds", true);
  p.getString("ssid",  WIFI_SSID,  sizeof(WIFI_SSID));
  p.getString("pass",  WIFI_PASS,  sizeof(WIFI_PASS));
  p.getString("email", USER_EMAIL, sizeof(USER_EMAIL));
  p.getString("upass", USER_PASS,  sizeof(USER_PASS));
  p.getString("surl",  SUPABASE_URL, sizeof(SUPABASE_URL));
  p.getString("skey",  SUPABASE_KEY, sizeof(SUPABASE_KEY));
  g_provisioned = p.getBool("prov", false);
  p.end();
  Serial.printf("[CRED] Loaded. SSID='%s' provisioned=%d\n", WIFI_SSID, g_provisioned);
}

void saveCredentials(const char *ssid, const char *pass,
                     const char *email, const char *upass) {
  strlcpy(WIFI_SSID,  ssid,  sizeof(WIFI_SSID));
  strlcpy(WIFI_PASS,  pass,  sizeof(WIFI_PASS));
  strlcpy(USER_EMAIL, email, sizeof(USER_EMAIL));
  strlcpy(USER_PASS,  upass, sizeof(USER_PASS));

  Preferences p;
  p.begin("pump_creds", false);
  p.putString("ssid",  ssid);
  p.putString("pass",  pass);
  p.putString("email", email);
  p.putString("upass", upass);
  p.putString("surl",  DEFAULT_SUPABASE_URL);
  p.putString("skey",  DEFAULT_SUPABASE_KEY);
  p.putBool("prov", true);
  p.end();
  g_provisioned = true;
  Serial.println(F("[CRED] Saved to NVS"));
}

void clearCredentials() {
  Preferences p;
  p.begin("pump_creds", false);
  p.clear();
  p.end();
  g_provisioned = false;
  Serial.println(F("[CRED] Cleared"));
}
