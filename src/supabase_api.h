#pragma once
// ============================================================================
// supabase_api.h — everything that talks to the NEW schema:
//   device            (state_1/state_2 desired, device_state actual, online,
//                       last_seen, firmware_version, mac_address, last_error)
//   device_seq + sch  (per-device schedule join)
//   RPCs: set_device_command(), report_device_state()
// ============================================================================

// Builds every REST/RPC URL the device calls, once, right after credentials
// are loaded (SUPABASE_URL must already be populated).
void buildUrls();

// Reads the device_seq -> sch join and recomputes each schedule's today's
// start/stop unix timestamps into the local schedules[] array.
void net_fetchSchedules();

// Reads state_1 from the cloud "device" row and applies it locally if it
// differs from what we last applied (handles the phone app turning the
// pump on/off).
void net_pollCommandState();

// Tells the cloud what state_1/state_2 the device is now driving, via the
// set_device_command RPC.
void net_setDeviceCommand(int state1, int state2);

// Sends a heartbeat/status report to the cloud via report_device_state:
// actual motor state, online pulse, firmware version, MAC address, and any
// pending error message.
void net_deviceReport();
