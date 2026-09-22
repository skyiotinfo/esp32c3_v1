#include "ble_provisioning.h"
#include <ArduinoJson.h>
#include <NimBLEDevice.h>
#include "config.h"
#include "globals.h"
#include "credentials.h"

static NimBLEServer         *pBleServer = nullptr;
static NimBLECharacteristic *pBleChar   = nullptr;

class BleServerCB : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *) override {
    bleClientConnected = true;
    Serial.println(F("[BLE] Client connected"));
  }
  void onDisconnect(NimBLEServer *) override {
    bleClientConnected = false;
    if (!bleDone) NimBLEDevice::startAdvertising();
    Serial.println(F("[BLE] Client disconnected"));
  }
};

class BleCharCB : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *pC) override {
    std::string raw = pC->getValue();
    if (raw.empty()) return;
    Serial.printf("[BLE] RX: %s\n", raw.c_str());

    JsonDocument doc;
    if (deserializeJson(doc, raw)) {
      pC->setValue("ERR:JSON");
      pC->notify();
      return;
    }
    if (!doc["ssid"].is<const char *>()  || !doc["pass"].is<const char *>() ||
        !doc["email"].is<const char *>() || !doc["upass"].is<const char *>()) {
      pC->setValue("ERR:MISSING_FIELDS");
      pC->notify();
      return;
    }
    saveCredentials(doc["ssid"], doc["pass"], doc["email"], doc["upass"]);
    pC->setValue("OK:SAVED_REBOOTING");
    pC->notify();
    bleDone = true;
    delay(800);
    ESP.restart();
  }
};

void startBleProvisioning() {
  Serial.println(F("[BLE] Starting provisioning"));
  const uint8_t s[4] = { 0x7C, 0x38, 0x79, 0x00 }; // "PROU"-ish glyph pattern on TM1637
  dispObj.setSegments(s);

  NimBLEDevice::init(BLE_DEVICE_NAME);
  pBleServer = NimBLEDevice::createServer();
  pBleServer->setCallbacks(new BleServerCB());

  NimBLEService *svc = pBleServer->createService(BLE_SERVICE_UUID);
  pBleChar = svc->createCharacteristic(
      BLE_CHAR_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
  pBleChar->setCallbacks(new BleCharCB());
  svc->start();

  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(BLE_SERVICE_UUID);
  adv->setScanResponse(true);
  NimBLEDevice::startAdvertising();
  Serial.printf("[BLE] Advertising as '%s'\n", BLE_DEVICE_NAME);
}

void stopBleProvisioning() {
  NimBLEDevice::stopAdvertising();
  NimBLEDevice::deinit(true);
}
