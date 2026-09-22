#pragma once

// Non-blocking WiFi watchdog: if not connected, and it's been at least
// WIFI_RETRY_INTERVAL_MS since the last attempt, kicks off a fresh (async)
// WiFi.begin(). Never uses delay().
void net_wifiReconnectIfNeeded();

// Lightweight, unauthenticated check: is there an actual path to the
// internet, not just an associated WiFi AP? Sets internetAvailable.
// Force-disconnects WiFi if the check fails so the watchdog above retries
// the whole connection instead of idling forever on a dead uplink.
bool net_checkInternet();

// Single entry point for everything network-related, called once per
// loop(). No confirmed internet path = NOTHING network-related runs.
void net_manageConnectivity();
