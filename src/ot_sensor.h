#pragma once

// Debounced over-temperature trip logic: counts consecutive LOW readings on
// OT_SENSOR_PIN while the motor is running, and once OT_TRIP_COUNT is
// reached, forces the motor off, sets otTripped, reports it, and shows
// "ERRO" on the display. Cleared by a button press (see motor_control.cpp).
//
// NOTE: in the source ESP8266 sketch this function existed but was never
// called from loop() — over-temperature protection was effectively dead
// code. It IS called every loop() here; see main.cpp.
void processOTSensor();
