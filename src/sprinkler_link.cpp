#include "sprinkler_link.h"
#include <SoftwareSerial.h>   // plerup/EspSoftwareSerial - already in platformio.ini
#include "config.h"

static SoftwareSerial motorSerial;
static String         rxBuffer;
static String         lastSentCommand;

// feedback[0] = pump bit, feedback[1..MAX_VALVES] = valve bits.
// -1 means "no feedback received yet" (distinct from a real 0/1 reading).
static int  feedback[MAX_VALVES + 1];
static bool haveFeedback[MAX_VALVES + 1];
static bool feedbackChangedFlag = false;

void sprinklerLink_init() {
  for (int i = 0; i <= MAX_VALVES; i++) { feedback[i] = -1; haveFeedback[i] = false; }
  motorSerial.begin(SPRINKLER_MOTOR_BAUD, SWSERIAL_8N1,
                     SPRINKLER_MOTOR_RX_PIN, SPRINKLER_MOTOR_TX_PIN, false, 128, 512);
  Serial.printf("[SPRINKLER] Motor-board link up: %d baud, RX=GPIO%d, TX=GPIO%d\n",
                SPRINKLER_MOTOR_BAUD, SPRINKLER_MOTOR_RX_PIN, SPRINKLER_MOTOR_TX_PIN);
}

// ───────────────────────────────────────────────────────────────
// handleLine()
// Parses one "{P v1..v8}" line from the motor board into the feedback[]
// array, and ONLY sets feedbackChangedFlag if at least one bit actually
// differs from what was already stored there - so downstream code (Supabase
// reporting) only ever reacts to genuine changes, never every repeated line.
// ───────────────────────────────────────────────────────────────
static void handleLine(String line) {
  line.trim();
  if (line.length() == 0) return;

  int s = line.indexOf('{');
  int e = line.indexOf('}');
  if (s == -1 || e == -1 || e <= s) return;

  String bits = line.substring(s + 1, e);
  if ((int)bits.length() < MAX_VALVES + 1) return;

  for (int i = 0; i <= MAX_VALVES; i++) {
    char c = bits[i];
    if (c != '0' && c != '1') return; // malformed frame - ignore the whole line
  }

  for (int i = 0; i <= MAX_VALVES; i++) {
    int newVal = bits[i] - '0';
    if (!haveFeedback[i] || feedback[i] != newVal) feedbackChangedFlag = true;
    feedback[i]     = newVal;
    haveFeedback[i] = true;
  }
}

void sprinklerLink_service() {
  while (motorSerial.available()) {
    char c = motorSerial.read();
    if (c == '\n') {
      handleLine(rxBuffer);
      rxBuffer = "";
    } else if (rxBuffer.length() < 80) {
      rxBuffer += c;
    } else {
      rxBuffer = ""; // runaway line with no newline (noise) - discard and resync
    }
  }
}

void sprinklerLink_sendCommand(int pumpBit, const int *valveBits) {
  String s = "{";
  s += String(pumpBit ? 1 : 0);
  for (int v = 1; v <= MAX_VALVES; v++) s += String(valveBits[v] ? 1 : 0);
  s += "}";

  if (s == lastSentCommand) return; // only act on differences, same as the motor board itself
  motorSerial.println(s);
  lastSentCommand = s;
  Serial.printf("[SPRINKLER] TX->MOTOR %s\n", s.c_str());
}

bool sprinklerLink_feedbackChanged() { return feedbackChangedFlag; }
void sprinklerLink_clearFeedbackChanged() { feedbackChangedFlag = false; }

int sprinklerLink_getFeedback(int valveIndex) {
  if (valveIndex < 1 || valveIndex > MAX_VALVES) return -1;
  return haveFeedback[valveIndex] ? feedback[valveIndex] : -1;
}

int sprinklerLink_getFeedbackPump() {
  return haveFeedback[0] ? feedback[0] : -1;
}
