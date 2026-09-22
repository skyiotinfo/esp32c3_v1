#pragma once
#include <Arduino.h>

// The single place that actually toggles MOTOR_PIN. Records
// motorStartMs/motorOnSinceUnix, persists to EEPROM, and immediately fires
// a device report. No-op if the motor is already in the requested state.
void applyMotorCommand(bool on, bool viaSchedule);

// Runs every loop() with the current unix time: clears an expired manual
// block, stops the motor if the active schedule's window ended, and starts
// the motor if we've entered a valid/enabled schedule window.
void checkSchedules(uint32_t nowUnix);

// Independent hard cutoff: if the motor has been on longer than
// MAX_SAFETY_RUNTIME_MIN minutes, force it off regardless of what the app
// or cloud say, and clear any manual block so schedules re-evaluate.
void checkSafetyCutoff(uint32_t nowUnix);

// buttonISR() has already flipped MOTOR_PIN directly. This finishes the
// bookkeeping that isn't safe/fast enough for an ISR: RTC read, manual-block
// math, EEPROM write, cloud notify.
void syncButtonMotorState();

// Local manual control dispatcher: clears an OT trip on press if tripped,
// otherwise finishes the button-press bookkeeping via syncButtonMotorState().
void handleButtonPress();

// Called once per loop(); clears the ISR flag and dispatches to
// handleButtonPress() only when the button was actually pressed.
void handleButtonEvent();

// Hardware ISR on BUTTON_PIN (FALLING). Flips the physical relay
// synchronously and instantly; everything else is deferred to loop().
void IRAM_ATTR buttonISR();
