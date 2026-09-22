#include "ot_sensor.h"
#include "config.h"
#include "globals.h"
#include "storage.h"
#include "supabase_api.h"
#include "motor_control.h"

void processOTSensor() {
  if (digitalRead(MOTOR_PIN) == LOW) {
    ot_sensorcount = 0;
    return;
  }
  if (digitalRead(OT_SENSOR_PIN) == LOW) {
    ot_sensorcount++;
    Serial.printf("[OT] Count: %d/%d\n", ot_sensorcount, OT_TRIP_COUNT);
    if (ot_sensorcount >= OT_TRIP_COUNT) {
      Serial.println(F("[OT] TRIP - Motor OFF"));
      otTripped = true;
      pendingLastError = "Overtemperature trip";
      net_setDeviceCommand(0, 0);
      applyMotorCommand(false, false);
      activeSchIdx      = -1;
      manualBlockUntil  = 0;
      ot_sensorcount    = 0;
      saveEeprom();
      // Show "ERRO" on display
      const uint8_t s[4] = { 0x79, 0x50, 0x50, 0x06 };
      dispObj.setSegments(s);
    }
  } else {
    ot_sensorcount = 0;
  }
}
