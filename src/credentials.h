#pragma once
// ============================================================================
// credentials.h — persists WiFi + Supabase login credentials in NVS.
//
// Unlike the ESP8266 sketch (WIFI_SSID/USER_EMAIL/etc. hardcoded as consts),
// this ESP32 build ships with no credentials at all: they are written by
// ble_provisioning.cpp the first time the mobile app pairs with the device,
// and reloaded from flash on every subsequent boot.
// ============================================================================

// Loads WIFI_SSID/WIFI_PASS/USER_EMAIL/USER_PASS/SUPABASE_URL/SUPABASE_KEY
// and g_provisioned from NVS namespace "pump_creds".
void loadCredentials();

// Saves the four user-supplied fields and marks the device provisioned.
// SUPABASE_URL/SUPABASE_KEY are always (re)written from the compiled-in
// defaults here, matching the original design: the app's BLE payload only
// ever carries {ssid, pass, email, upass}.
void saveCredentials(const char *ssid, const char *pass,
                     const char *email, const char *upass);

// Wipes the "pump_creds" namespace and clears g_provisioned, forcing BLE
// provisioning again on the next boot. Used by the boot-time button-hold.
void clearCredentials();
