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

  // Nothing saved in NVS yet (brand-new unit, never BLE-provisioned) -> fall
  // back to the compiled-in default WiFi so the board still comes up on a
  // network. Supabase login (email/password) has no such fallback: it stays
  // empty until real BLE provisioning supplies it.
  if (strlen(WIFI_SSID) == 0) {
    strlcpy(WIFI_SSID, DEFAULT_WIFI_SSID, sizeof(WIFI_SSID));
    strlcpy(WIFI_PASS, DEFAULT_WIFI_PASS, sizeof(WIFI_PASS));
    Serial.println(F("[CRED] No saved WiFi - using compiled-in default WiFi"));
  }

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
// NOTE: no longer called from the boot-time provisioning flow (pump_main.cpp)
// - holding PROVISION_BUTTON_PIN now enters BLE provisioning WITHOUT wiping
// existing credentials first, so a timed-out/abandoned provisioning attempt
// always leaves the board able to boot on its previous, working WiFi. This
// function is kept available for a possible future explicit "factory reset"
// command.
