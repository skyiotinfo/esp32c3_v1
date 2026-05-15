#include <Arduino.h>
#include <BLEDevice.h>

#include <BLEUtils.h>

#include <BLEServer.h>

#include <BLEAdvertising.h>
 
// BLE Configuration

#define DEVICE_NAME "Agricultural_Device"

#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"

#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
 
// Device type configuration - change this based on your device

#define DEVICE_TYPE "Pump Motor"  // Options: "Pump Motor", "Water Tank", "Valve", "Sprinkler", "Light", "Sensor"
 
// Pin definitions for sensors/actuators

const int LED_PIN = 2;  // Built-in LED

const int SENSOR_PIN = 34;  // Example sensor pin
 
// Variables for sensor readings

float sensorValue = 0;

bool deviceState = false;
 
// BLE objects

BLEServer* pServer = NULL;

BLECharacteristic* pCharacteristic = NULL;

BLEAdvertising* pAdvertising = NULL;

bool deviceConnected = false;
 
// Timer variables

unsigned long lastSensorRead = 0;

const unsigned long sensorReadInterval = 5000; // Read sensor every 5 seconds
 
// Callback class for server connection events

class MyServerCallbacks: public BLEServerCallbacks {

  void onConnect(BLEServer* pServer) {

    deviceConnected = true;

    Serial.println("Device connected");

  }
 
  void onDisconnect(BLEServer* pServer) {

    deviceConnected = false;

    Serial.println("Device disconnected");

    // Restart advertising when device disconnects

    pAdvertising->start();

    Serial.println("Advertising restarted");

  }

};
 
// Callback class for characteristic write events

class MyCharacteristicCallbacks: public BLECharacteristicCallbacks {

  void onWrite(BLECharacteristic *pCharacteristic) {

    std::string value = pCharacteristic->getValue();

    if (value.length() > 0) {

      Serial.print("Received value: ");

      for (int i = 0; i < value.length(); i++) {

        Serial.print(value[i]);

      }

      Serial.println();

      // Handle commands

      if (value == "ON" || value == "on") {

        digitalWrite(LED_PIN, HIGH);

        deviceState = true;

        Serial.println("Device turned ON");

      } 

      else if (value == "OFF" || value == "off") {

        digitalWrite(LED_PIN, LOW);

        deviceState = false;

        Serial.println("Device turned OFF");

      }

      else if (value == "STATUS") {

        // Send status back to client

        String status = "Device: " + String(DEVICE_NAME) + 

                       "\nType: " + String(DEVICE_TYPE) +

                       "\nState: " + String(deviceState ? "ON" : "OFF") +

                       "\nSensor: " + String(sensorValue);

        pCharacteristic->setValue(status.c_str());

        pCharacteristic->notify();

      }

    }

  }

};
 
void setup() {

  // Initialize Serial communication

  Serial.begin(115200);

  Serial.println();

  Serial.println("ESP32 BLE Agricultural Device Starting...");

  // Initialize pins

  pinMode(LED_PIN, OUTPUT);

  pinMode(SENSOR_PIN, INPUT);

  // Turn off LED initially

  digitalWrite(LED_PIN, LOW);
 
  // Initialize BLE

  BLEDevice::init(DEVICE_NAME);

  // Create BLE Server

  pServer = BLEDevice::createServer();

  pServer->setCallbacks(new MyServerCallbacks());
 
  // Create BLE Service

  BLEService *pService = pServer->createService(SERVICE_UUID);
 
  // Create BLE Characteristic

  pCharacteristic = pService->createCharacteristic(

                      CHARACTERISTIC_UUID,

                      BLECharacteristic::PROPERTY_READ |

                      BLECharacteristic::PROPERTY_WRITE |

                      BLECharacteristic::PROPERTY_NOTIFY

                    );
 
  // Set characteristic callbacks

  pCharacteristic->setCallbacks(new MyCharacteristicCallbacks());
 
  // Set initial characteristic value

  pCharacteristic->setValue("Hello from ESP32 Agricultural Device");
 
  // Start the service

  pService->start();
 
  // Setup advertising

  pAdvertising = BLEDevice::getAdvertising();

  // Configure advertising parameters

  BLEAdvertisementData advertisementData;

  // Set device name in advertising data

  advertisementData.setName(DEVICE_NAME);

  // Add manufacturer data (optional - can include device type)

  uint8_t manufacturerData[2];

  manufacturerData[0] = 0x01; // Manufacturer ID low byte

  manufacturerData[1] = 0x02; // Manufacturer ID high byte

  advertisementData.setManufacturerData(std::string((char*)manufacturerData, 2));

  // Add service UUID to advertising data

  advertisementData.setCompleteServices(BLEUUID(SERVICE_UUID));

  pAdvertising->setAdvertisementData(advertisementData);
 
  // Start advertising

  pAdvertising->start();
 
  Serial.println("BLE Device is now advertising as: " + String(DEVICE_NAME));

  Serial.println("Device Type: " + String(DEVICE_TYPE));

  Serial.println("Service UUID: " + String(SERVICE_UUID));

  Serial.println("Waiting for connections...");

}
 
void loop() {

  // Read sensor data periodically

  if (millis() - lastSensorRead >= sensorReadInterval) {

    lastSensorRead = millis();

    // Simulate sensor reading (replace with actual sensor reading)

    sensorValue = analogRead(SENSOR_PIN) / 4095.0 * 100.0; // Convert to percentage

    Serial.print("Sensor value: ");

    Serial.println(sensorValue);

    // Update characteristic with sensor data if device is connected

    if (deviceConnected) {

      String sensorData = "SENSOR:" + String(sensorValue);

      pCharacteristic->setValue(sensorData.c_str());

      pCharacteristic->notify();

    }

  }

  // Check for serial commands

  if (Serial.available()) {

    String command = Serial.readStringUntil('\n');

    command.trim();

    if (command == "status") {

      Serial.println("=== Device Status ===");

      Serial.println("Name: " + String(DEVICE_NAME));

      Serial.println("Type: " + String(DEVICE_TYPE));

      Serial.println("State: " + String(deviceState ? "ON" : "OFF"));

      Serial.println("Sensor: " + String(sensorValue));

      Serial.println("Connected: " + String(deviceConnected ? "Yes" : "No"));

      Serial.println("====================");

    }

    else if (command == "on") {

      digitalWrite(LED_PIN, HIGH);

      deviceState = true;

      Serial.println("Device turned ON");

    }

    else if (command == "off") {

      digitalWrite(LED_PIN, LOW);

      deviceState = false;

      Serial.println("Device turned OFF");

    }

    else if (command == "reset") {

      Serial.println("Resetting device...");

      ESP.restart();

    }

  }

}
 