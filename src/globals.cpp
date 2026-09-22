#include "globals.h"

RTC_DS1307    rtc;
TM1637Display dispObj(CLK_PIN, DIO_PIN);
bool          rtcAvail = false;

char WIFI_SSID[33]     = "";
char WIFI_PASS[65]     = "";
char USER_EMAIL[50]    = "";
char USER_PASS[33]     = "";
char SUPABASE_URL[120] = DEFAULT_SUPABASE_URL;
char SUPABASE_KEY[320] = DEFAULT_SUPABASE_KEY;
bool g_provisioned     = false;

String url_device_select;
String url_device_seq;
String url_rpc_command;
String url_rpc_report;

ScheduleEntry schedules[MAX_SCHEDULES];
int           scheduleCount = 0;
int           activeSchIdx  = -1;

String        USER_TOKEN     = "";
bool          login_status   = false;
unsigned long tokenExpiresAt = 0;

uint32_t      manualBlockUntil    = 0;
volatile bool otTripped           = false;
int           ot_sensorcount      = 0;
int           lastAppliedState1   = -1;
uint32_t      motorOnSinceUnix    = 0;
int           runtimeSecs         = 0;
bool          timeSynced          = false;
unsigned long lastSyncMs          = 0;
unsigned long lastCommandPollMs   = 0;
unsigned long lastHeartbeatMs     = 0;
unsigned long lastScheduleFetchMs = 0 - SCHEDULE_FETCH_INTERVAL_MS; // force fetch on first loop
unsigned long lastWifiAttemptMs   = 0;
unsigned long motorStartMs        = 0;
bool          colonBlink          = false;
String        pendingLastError    = "";

volatile bool          buttonPressedFlag = false;
volatile unsigned long lastButtonIsrMs   = 0;

bool          lastCallFailedHard  = false;
bool          internetAvailable   = false;
unsigned long lastInternetCheckMs = 0;

bool bleDone            = false;
bool bleClientConnected = false;
