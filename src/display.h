#pragma once

// Drives the TM1637: while the motor is running, shows elapsed MM:SS
// runtime; otherwise shows the current HH:MM from the RTC with a blinking
// colon. Does nothing if there's no running RTC and the motor is off.
void updateDisplay();

// Called once per loop(). While the motor is on, recomputes runtimeSecs
// from motorStartMs so updateDisplay() always shows a fresh elapsed time.
void updateMotorRuntimeCounter();

// One compact debug line per loop() showing RTC time, motor/OT/WiFi state
// and the manual-block deadline. Purely cosmetic — safe to remove.
void printStatusLine();
