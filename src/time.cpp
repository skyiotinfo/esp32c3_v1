#include <Wire.h>
#include <RTClib.h>
#include <WiFi.h>
#include "time.h"

#define RTC_SDA 5
#define RTC_SCL 4

// --- WiFi Credentials ---
const char *ssid = "anupam";
const char *password = "12345678";

// --- NTP Server Settings ---
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;     // Use 0 for UTC (recommended for RTC)
const int daylightOffset_sec = 19800;

RTC_DS1307 rtc; // Change to 'RTC_DS3231 rtc;' if you are using a DS3231

void setup()
{
    Serial.begin(115200);

    // Initialize I2C and RTC
    Wire.begin(RTC_SDA, RTC_SCL); // For custom I2C pins, use: Wire.begin(SDA_PIN, SCL_PIN);
    if (!rtc.begin())
    {
        Serial.println("Couldn't find RTC");
        Serial.flush();
        while (1)
            delay(10);
    }

    Serial.println("RTC found!");

    // Connect to Wi-Fi
    Serial.printf("Connecting to %s ", ssid);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println(" CONNECTED");

    // Configure and get time from NTP server
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

    struct tm timeinfo;
    if (getLocalTime(&timeinfo))
    {
        // Convert the tm structure to a DateTime object for the RTC library
        DateTime now = DateTime(timeinfo.tm_year + 1900,
                                timeinfo.tm_mon + 1,
                                timeinfo.tm_mday,
                                timeinfo.tm_hour,
                                timeinfo.tm_min,
                                timeinfo.tm_sec);
        rtc.adjust(now); // Write the time to the RTC module
        Serial.println("RTC time has been set from the NTP server!");
    }
    else
    {
        Serial.println("Failed to obtain time from NTP server");
    }

    // Optionally, disconnect Wi-Fi to save power
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}

void loop()
{
    DateTime now = rtc.now();
    Serial.print(now.year(), DEC);
    Serial.print('/');
    Serial.print(now.month(), DEC);
    Serial.print('/');
    Serial.print(now.day(), DEC);
    Serial.print(" (");
    Serial.print(now.dayOfTheWeek());
    Serial.print(") ");
    Serial.print(now.hour(), DEC);
    Serial.print(':');
    Serial.print(now.minute(), DEC);
    Serial.print(':');
    Serial.print(now.second(), DEC);
    Serial.println();

    delay(1000);
}