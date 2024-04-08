#include <ESP8266WiFi.h>
#include <SoftwareSerial.h>
#include <PubSubClient.h>

// Define the software serial port. RX on pin D2, TX on pin D3
SoftwareSerial Arduino(D2, D3);

// Wi-Fi credentials
const char* ssid = "Cancer Connection";
const char* password = "hotdog1155";
// const char* ssid = "Nicolas";
// const char* password = "11223344";

// MQTT broker details
const char* mqtt_server = "192.168.121.222";
const int mqtt_port = 1883; // Default MQTT port

WiFiClient espClient;
PubSubClient client(espClient);

void setup() {
 Serial.begin(115200);
 Serial.println("Starting setup...");
 Arduino.begin(4800); // Ensure this matches the baud rate on the Arduino
 setup_wifi();
 client.setServer(mqtt_server, mqtt_port);
 Serial.println("Setup complete.");
}

void setup_wifi() {
 Serial.println("Connecting to WiFi...");
 delay(10);
 Serial.println();
 Serial.print("Connecting to ");
 Serial.println(ssid);

 WiFi.begin(ssid, password);

 while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
 }

 Serial.println("");
 Serial.println("WiFi connected");
 Serial.println("IP address: ");
 Serial.println(WiFi.localIP());
}

void loop() {

 if (!client.connected()) {
    reconnect();
 }
 client.loop();

 // Check if data is available to read
 if (Arduino.available() > 0) {
    Serial.println("Data available on serial port.");
    uint8_t dataSize;
    // Read the size of the incoming data
    if (Arduino.readBytes(&dataSize, sizeof(dataSize)) == sizeof(dataSize)) {
      Serial.print("Data size: ");
      Serial.println(dataSize);
      uint8_t* sensorData = new uint8_t[dataSize];
      // Read the actual data
      if (Arduino.readBytes(sensorData, dataSize) == dataSize) {
        // Convert sensorData to a String
        String sensorDataString;
        for (int i = 0; i < dataSize; i++) {
          sensorDataString += String(sensorData[i]);
          if (i < dataSize - 1) {
            sensorDataString += " "; // Add a space between values
          }
        }
        // Publish the sensor data
        Serial.print("Publishing: ");
        Serial.println(sensorDataString);
        client.publish("your/topic", sensorDataString.c_str(), sensorDataString.length());
        Serial.println("Published successfully.");
      }
      delete[] sensorData;
    }
 }
}

void reconnect() {
 Serial.println("Attempting MQTT connection...");
 while (!client.connected()) {
    if (client.connect("NodeMCU")) {
      Serial.println("MQTT connected");
    } else {
      Serial.print("MQTT connection failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
 }
}
