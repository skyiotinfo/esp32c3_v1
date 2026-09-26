#pragma once
// ============================================================================
// sprinkler_api.h — the Supabase side of the valve/motor-board feature.
//
// Reuses the EXISTING device/device_seq/sch schema and the EXISTING
// set_device_command / report_device_state RPCs - just called with a
// CHILD's device_id instead of this board's own DEVICE_ID. No new Supabase
// table, column, or RPC is required: every valve is simply an ordinary
// child `device` row (device_type = 2 'valve', parent_device_id =
// DEVICE_ID), exactly what the mobile app's MotorValvesScreen already
// expects and manages.
//
// Call sprinkler_service() once per loop(), right after net_manageConnectivity()
// so it only does network work when WiFi/internet/login are already confirmed
// up. It is entirely additive - it changes nothing about how the parent
// device's own single-relay logic behaves.
// ============================================================================

void sprinkler_service();
