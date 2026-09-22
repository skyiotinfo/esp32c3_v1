#pragma once
// ============================================================================
// ble_provisioning.h — NimBLE GATT server the mobile app connects to on
// first boot (or after a held-button re-provision) to hand the device its
// WiFi + Supabase login credentials.
//
// Protocol is unchanged from the original ESP32 build so the existing app
// (constants/ble.ts, screens/AddDeviceScreen.tsx) works with no changes:
//   - Device advertises as "PumpCtrl-<DEVICE_ID>"
//   - Service UUID / characteristic UUID as in config.h
//   - App writes a JSON blob: {"ssid":"...","pass":"...","email":"...","upass":"..."}
//   - Device replies over the same characteristic via notify():
//       "OK:SAVED_REBOOTING" on success, or "ERR:JSON" / "ERR:MISSING_FIELDS"
//   - On success the device saves credentials, then ESP.restart()s.
// ============================================================================

void startBleProvisioning();
void stopBleProvisioning();
