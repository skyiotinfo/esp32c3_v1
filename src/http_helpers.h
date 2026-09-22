#pragma once
#include <Arduino.h>

// Generic authenticated GET, used by every "read" call (poll command state,
// fetch schedules). Bails out instantly if WiFi/internet isn't confirmed up.
// On a hard failure (code < 0) immediately marks internetAvailable = false so
// nothing else queues up behind another slow timeout this loop.
bool getHttpdata(const char *url, String &out);

// Generic authenticated POST, used by every "write" call (set_device_command,
// report_device_state). Same guards as getHttpdata().
bool httpPostJson(const char *url, const char *body, const char *bearerToken, String *out);
