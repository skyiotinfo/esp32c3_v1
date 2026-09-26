#pragma once
#include <Arduino.h>
// ============================================================================
// sprinkler_link.h — SoftwareSerial link to the (unchanged) ESP8266 motor
// board. Wire protocol matches the motor board's existing firmware exactly:
//
//   TX (this board -> motor board):  "{P v1v2v3v4v5v6v7v8}\n"
//     P     = pump bit (1 = at least one valve should be on)
//     v1..8 = desired state of valve N (1 = on, 0 = off)
//
//   RX (motor board -> this board):  "{P v1v2v3v4v5v6v7v8}\n"
//     Same shape, but this is the motor board's CONFIRMED actual state
//     (aggregated from its ESP-NOW valve nodes), not a request.
//
// Nothing about the motor board or valve-node firmware changes - this is
// purely the ESP32-C3 side of the link.
// ============================================================================

void sprinklerLink_init();

// Reads any bytes waiting from the motor board, parses complete "{...}"
// lines into an internal array (see below), and detects whether feedback
// actually changed since the last parsed line. Call once per loop().
void sprinklerLink_service();

// Sends the desired state for all valves to the motor board (only actually
// writes to the serial port when the bits differ from what was last sent -
// mirrors the motor board's own "only act on differences" behavior).
void sprinklerLink_sendCommand(int pumpBit, const int *valveBits /* [1..MAX_VALVES] */);

// True once new feedback has been parsed since the last call to
// sprinklerLink_clearFeedbackChanged(). Feedback is stored in one array
// (see sprinklerLink_getFeedback) and compared against the previous array
// snapshot before this flag is ever set - so a caller only ever sees
// genuine changes, never a no-op re-parse of the same line.
bool sprinklerLink_feedbackChanged();
void sprinklerLink_clearFeedbackChanged();

// Latest known feedback for one valve (1..MAX_VALVES), or -1 if no feedback
// has ever been received yet for that valve.
int sprinklerLink_getFeedback(int valveIndex);

// Latest known feedback for the pump bit (index 0), or -1 if none yet.
int sprinklerLink_getFeedbackPump();
