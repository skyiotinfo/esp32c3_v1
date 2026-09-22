#pragma once
// ============================================================================
// storage.h — small EEPROM-emulated-flash block that survives reboot:
// manual-block window, OT-trip flag, last applied motor state, and the
// unix time the motor turned on. Schedules themselves are NOT persisted
// here — they're re-fetched from device_seq/sch right after boot, same as
// the ESP8266 build, which is simpler and always cloud-authoritative.
// ============================================================================

void saveEeprom();
void loadEeprom();
