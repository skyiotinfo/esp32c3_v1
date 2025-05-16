#include <WiFi.h>
#include <HTTPClient.h>
#include <SoftwareSerial.h>
#include <ArduinoJson.h>
  
const char* ssid = "Galaxy M42";
const char* password =  "Chai1111";
HTTPClient http;
SoftwareSerial receiverSerial1(4, 5);
String receivedMessage = "";
char str[50];
String input;
JsonDocument doc;
const int pin1 = 0;
  
void setup() {
  Serial.begin(115200);
  receiverSerial1.begin(9600);
  pinMode(pin1, OUTPUT);
  digitalWrite(pin1, HIGH);
  delay(4000);   //Delay needed before calling the WiFi.begin
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { //Check for the connection
    delay(1000);
    Serial.println("Connecting to WiFi..");
  }
  Serial.println("Connected to the WiFi network"); 

  doc["id"]="0";
  doc["c_date"]="111111";
  doc["c_state"]="0";
  doc["m_state"]="0";

  //http.begin("https://esp-02.asia-southeast1.firebasedatabase.app/users/uid/10103.json");  //Specify destination for HTTP request
  http.begin("https://moltnvb2bk.execute-api.ap-south-1.amazonaws.com/items");
  //https://moltnvb2bk.execute-api.ap-south-1.amazonaws.com/items
}
  
void loop() {
 if ((WiFi.status() != WL_CONNECTED)) {
    Serial.println("Reconnecting to WiFi...");
    WiFi.disconnect();
    WiFi.reconnect();
 }

 if(receiverSerial1.available() > 0) {
    input = receiverSerial1.readStringUntil('\r');
    Serial.print("Received: ");
    Serial.println(input);
    //doc["TI_DATA"] = input; 
    doc["id"]="3";
    doc["c_date"]="010101";
    doc["c_state"]="1";
    doc["m_state"]="0";
    receiverSerial1.flush();
  }else{
  }
  
 if(WiFi.status()== WL_CONNECTED){   //Check WiFi connection status
   http.addHeader("Content-Type", "application/json");
   //String data1 = "{\"DATA\":\"tPmAT5Ab3j7F9\"}";
   String d1;
   serializeJson(doc, d1); 
   //int httpResponseCode = http.PUT("{\"DATA\":\"tPmAT5Ab3j7F9\"}");
   int httpResponseCode = http.POST(d1);
   if(httpResponseCode>0){
    String response = http.getString();                       //Get the response to the request
    Serial.println(httpResponseCode);   //Print return code
    Serial.println(response);           //Print request answer
   }else{
    Serial.print("Error on sending POST: ");
    Serial.println(httpResponseCode);
   }
   http.end();  //Free resources
 }else{
    Serial.println("Error in WiFi connection");   
 }
 
  delay(10000);  //Send a request every 30 seconds
}