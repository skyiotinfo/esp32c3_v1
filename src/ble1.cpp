#include <Arduino.h>

#include <BLEDevice.h>

#include <BLEUtils.h>

#include <BLEServer.h>

#include <BLE2902.h>
 
// BLE Configuration - MUST match the React Native app

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"

#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
 
// Device name - this is what appears when scanning

#define DEVICE_NAME "ESP32_Agri_Device"
 
// LED pins for visual feedback

#define LED_PIN 2  // Built-in LED on most ESP32 boards
 
// BLE objects

BLEServer* pServer = NULL;

BLECharacteristic* pCharacteristic = NULL;

bool deviceConnected = false;

bool oldDeviceConnected = false;
 
// Timing variables

unsigned long lastMessageTime = 0;

const unsigned long PRINT_INTERVAL = 1000; // Print status every second
 
// Callback class for server connection events

class MyServerCallbacks: public BLEServerCallbacks {

  void onConnect(BLEServer* pServer) {

    deviceConnected = true;

    Serial.println("✅ Device Connected!");

    digitalWrite(LED_PIN, HIGH); // Turn on LED when connected

  };
 
  void onDisconnect(BLEServer* pServer) {

    deviceConnected = false;

    Serial.println("❌ Device Disconnected!");

    digitalWrite(LED_PIN, LOW); // Turn off LED when disconnected

  }

};
 void processWiFiConfig(const char* data) {

  Serial.println("\n=== 📶 PROCESSING WIFI CONFIGURATION ===");

  // Parse the WiFi data (format: WIFI:SSID=name,PASS=password,SEC=security,HIDDEN=true/false)

  char ssid[64] = "";

  char password[64] = "";

  char security[16] = "";

  char hidden[8] = "";

  // Simple parsing - in production you'd want better parsing

  if (sscanf(data, "WIFI:SSID=%63[^,],PASS=%63[^,],SEC=%15[^,],HIDDEN=%7s", 

             ssid, password, security, hidden) == 4) {

    Serial.print("SSID: ");

    Serial.println(ssid);

    Serial.print("Password: ");

    Serial.println(password);

    Serial.print("Security: ");

    Serial.println(security);

    Serial.print("Hidden: ");

    Serial.println(hidden);

    // Here you would connect to WiFi or store credentials

    Serial.println("✅ WiFi configuration received and parsed");

    // You could attempt to connect to WiFi here

    // WiFi.begin(ssid, password);

  } else {

    Serial.println("❌ Failed to parse WiFi configuration");

    Serial.print("Raw data: ");

    Serial.println(data);

  }

  Serial.println("========================================\n");

}
// Callback class for characteristic write events

class MyCharacteristicCallbacks: public BLECharacteristicCallbacks {

  void onWrite(BLECharacteristic *pCharacteristic) {

    std::string value = pCharacteristic->getValue();

    if (value.length() > 0) {

      Serial.println("\n=== 📥 DATA RECEIVED FROM APP ===");

      Serial.print("Raw data (hex): ");

      for (int i = 0; i < value.length(); i++) {

        Serial.printf("%02X ", (unsigned char)value[i]);

      }

      Serial.println();

      Serial.print("Data as string: ");

      Serial.println(value.c_str());

      Serial.print("Data length: ");

      Serial.println(value.length());

      Serial.println("================================\n");

      // Send acknowledgment back to app

      String response = "ESP32 received: " + String(value.c_str());

      pCharacteristic->setValue(response.c_str());

      pCharacteristic->notify();

      // Flash LED to indicate data received

      digitalWrite(LED_PIN, LOW);

      delay(100);

      digitalWrite(LED_PIN, HIGH);

      delay(100);

      digitalWrite(LED_PIN, HIGH);

      // Check if this is WiFi configuration data

      if (strstr(value.c_str(), "WIFI:") != NULL) {

        processWiFiConfig(value.c_str());

      }

    }

  }

};
 
// Function to process WiFi configuration


 
void setup() {

  Serial.begin(115200);

  Serial.println("\n\n=== ESP32 BLE DEVICE STARTING ===");

  Serial.println("Device Name: " + String(DEVICE_NAME));

  Serial.println("Service UUID: " + String(SERVICE_UUID));

  Serial.println("Characteristic UUID: " + String(CHARACTERISTIC_UUID));

  pinMode(LED_PIN, OUTPUT);

  digitalWrite(LED_PIN, LOW); // Start with LED off

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

                      BLECharacteristic::PROPERTY_NOTIFY |

                      BLECharacteristic::PROPERTY_INDICATE

                    );

  // Add descriptor for notifications

  pCharacteristic->addDescriptor(new BLE2902());

  // Set callback for writes

  pCharacteristic->setCallbacks(new MyCharacteristicCallbacks());

  // Set initial characteristic value

  pCharacteristic->setValue("ESP32 Ready. Send me data!");

  // Start the service

  pService->start();

  // Start advertising

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();

  pAdvertising->addServiceUUID(SERVICE_UUID);

  pAdvertising->setScanResponse(true);

  pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections

  pAdvertising->setMinPreferred(0x12);

  BLEDevice::startAdvertising();

  Serial.println("✅ BLE Advertising Started!");

  Serial.println("Waiting for connections...\n");

}
 
void loop() {

  // Handle disconnections

  if (!deviceConnected && oldDeviceConnected) {

    delay(500); // Give the bluetooth stack the chance to get things ready

    pServer->startAdvertising(); // Restart advertising

    Serial.println("Restarting advertising...");

    oldDeviceConnected = deviceConnected;

  }

  // Handle new connections

  if (deviceConnected && !oldDeviceConnected) {

    oldDeviceConnected = deviceConnected;

    Serial.println("New connection established");

  }

  // Send periodic status update when connected

  if (deviceConnected && (millis() - lastMessageTime > PRINT_INTERVAL)) {

    lastMessageTime = millis();

    // Create a status message

    char statusMsg[100];

    snprintf(statusMsg, sizeof(statusMsg), 

             "ESP32 Status - Uptime: %lu s, Free heap: %u bytes", 

             millis() / 1000, ESP.getFreeHeap());

    // Send to connected device

    pCharacteristic->setValue(statusMsg);

    pCharacteristic->notify();

    // Also print to Serial

  }

  delay(10);

}
 