#include <Arduino.h>
#include "esp_sleep.h"

static const int MONITOR_PIN = 10;
static const uint64_t POLL_SLEEP_US = 10ULL * 1000000ULL;  // 10 seconds
static const unsigned long ALIVE_DURATION_MS = 60UL * 1000UL;
static const unsigned long ALIVE_PRINT_INTERVAL_MS = 1000UL;

static int lastPinState = LOW;

static const char *stateName(int state) {
  return state == HIGH ? "HIGH" : "LOW";
}

// Print "alive" once per second for one minute, then return to monitoring.
void displayAlive() {
  Serial.println("Pin 10 changed — running displayAlive() for 60 seconds");
  const unsigned long startMs = millis();

  while (millis() - startMs < ALIVE_DURATION_MS) {
    Serial.println("alive");
    delay(ALIVE_PRINT_INTERVAL_MS);
  }

  Serial.println("displayAlive() finished — returning to sleep/monitor");
  Serial.flush();
}

static void sleepTenSeconds() {
  Serial.println("Sleeping 10 seconds; will re-check pin 10 on wake");
  Serial.flush();

  esp_sleep_enable_timer_wakeup(POLL_SLEEP_US);
  esp_light_sleep_start();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(MONITOR_PIN, INPUT_PULLUP);
  lastPinState = digitalRead(MONITOR_PIN);

  Serial.println("Pin 10 change monitor started");
  Serial.printf("Initial pin 10 state: %s (%d)\n", stateName(lastPinState), lastPinState);
  Serial.flush();
}

void loop() {
  sleepTenSeconds();

  const int currentState = digitalRead(MONITOR_PIN);
  Serial.printf("Pin 10: %s (%d)\n", stateName(currentState), currentState);

  if (currentState != lastPinState) {
    Serial.printf("Status changed: %s -> %s\n", stateName(lastPinState), stateName(currentState));
    lastPinState = currentState;
    displayAlive();
  }
}
