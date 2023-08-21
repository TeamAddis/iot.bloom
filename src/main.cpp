/* 
 * Celebi Smart watering system
 *
 * Parts:
 * Arduino MKR WIFI 1010
 * 3v-5v DC submersable pump
 * 
 */

/* 
 * Includes
 */
#include <RTCZero.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include <ArduinoBearSSL.h>
#include <ArduinoECCX08.h>
#include <ArduinoMqttClient.h>
#include <WiFiNINA.h>
#include "pump.h"
#include "network_secrets.h"
#include "aws_secrets.h"
#include "mqtt_topics.h"
#include "persistance.h"

/* 
 * Declare functions
 */
void printWifiStatus();
void printAlarmStatus();
void connectWifi();
void connectMQTT();
void setupRTC();
void sendStatusToClient(WiFiClient &client);
void sendPumpStatusToClient(WiFiClient &client);
void sendAlarmStatusToClient(WiFiClient &client);
void checkAlarms();
void checkAlarmAgainstTime(p_alarmData &alarm);
void turnPumpOn();
void turnPumpOff();
void onMessageReceived(int messageSize);
unsigned long getTime();


/*
 * Secret data
*/
const char ssid[] = SECRET_SSID;        // your network SSID (name)
const char pass[] = SECRET_PASS;        // your network password (use for WPA, or use as key for WEP)
const char broker[] = SECRET_BROKER;    // MQTT broker ip address
const char* certificate = SECRET_CERTIFICATE; // AWS certificate

/* 
 * Pump variables
 */
Pump pump(6);
byte pumpOnStartMinutes;

/* 
 * RTC (Real time clock) variables
 */
RTCZero rtc;
bool rtcIsConfigured = false;
bool dailyAlarmIsSet = false;
byte currentDay = 0;

/* 
 * Wifi
 */
WiFiClient client;            // Used for the TCP socket connection
BearSSLClient sslClient(client); // Used for SSL/TLS connection, integrates with ECC508
MqttClient mqttClient(sslClient);
const int brokerPort = 8883;

/* 
 * Software Version
 */
const int VERSION = 1;

/* 
 * Setup function
 * 
 * Main entry point of the software
 * 
 */
void setup() {
    Serial.begin(115200);
    while (!Serial);

    // Setup ECCX08
    if (!ECCX08.begin()) {
        Serial.println("No ECCX08 present!");
        while (1);
    }

    // set a callback to get the current time
    // used to validate the servers certificate
    ArduinoBearSSL.onGetTime(getTime);

    // set the ECCX08 slot to use for the private key
    // and the accompanying public certificate for it
    sslClient.setEccSlot(0, certificate);

    // set the message received callback for the mqtt client
    mqttClient.onMessage(onMessageReceived);

    // setup the wifi module
    if (WiFi.status() == WL_NO_MODULE) {
        Serial.println("Communication with WiFi module failed!");
        // wifi module isn't responding.
        // don't contintue
        while (true);
    }
}

/* 
 * Loop function
 * 
 * Main runtime loop of the software
 * 
 */
void loop() {
    // check if the wifi module is connected to the network
    if (WiFi.status() != WL_CONNECTED) {
        connectWifi();
    }

    if (!mqttClient.connected()) {
    // MQTT client is disconnected, connect
    connectMQTT();
    }

    // poll for new MQTT messages and send keep alive
    mqttClient.poll();
}

unsigned long getTime() {
  // get the current time from the WiFi module  
  return WiFi.getTime();
}

/* 
 * Connect to the wifi network
 */
void connectWifi() {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
    Serial.println(" ");

    while (WiFi.begin(ssid, pass) != WL_CONNECTED) {
        // failed to connect to the network
        // try again in 5 seconds
        delay(5000);
        Serial.println("Failed to connect to the network. Retrying...");
    }
    Serial.println();
    Serial.println("Connected to the network.");
    Serial.println();
}

/*
 * Connect to the MQTT broker
*/
void connectMQTT() {
  Serial.print("Attempting to MQTT broker: ");
  Serial.print(broker);
  Serial.println(" ");

  while (!mqttClient.connect(broker, brokerPort)) {
    // failed, retry
    Serial.print("MQTT connection failed! Error code = ");
    Serial.println(mqttClient.connectError());
    delay(5000);
  }
  Serial.println();

  Serial.println("You're connected to the MQTT broker");
  Serial.println();

  // subscribe to topics
  mqttClient.subscribe(MANUAL_PUMP_TOPIC);
}

/*
 * Receive message from MQTT Broker
*/
void onMessageReceived(int messageSize) {
    // we received a message, store the topic and payload for later use.
    String topic = mqttClient.messageTopic();
    
    // Create a JSON document based on the message contents
    StaticJsonDocument<200> jsonDocument;
    String jsonString = mqttClient.readString();
    deserializeJson(jsonDocument, jsonString);

    // we received a message, print out the topic and contents
    Serial.print("Received a message with topic '");
    Serial.print(topic);
    Serial.print("', length ");
    Serial.print(messageSize);
    Serial.println(" bytes:");

   
    // Print the message
    Serial.println(jsonString);
    Serial.println();

    Serial.println();

    // Check the topic and perform the appropriate action
    if (topic == MANUAL_PUMP_TOPIC) {
        if (jsonDocument["message"] == "on") {
            turnPumpOn();
        } else if (jsonDocument["message"] == "off") {
            turnPumpOff();
        }
    }
}

void checkAlarms() {
    if (areActiveAlarms()) {
        // We have 4 different alarms available in FlashStorage
        // pAlarms.timer0
        // pAlarms.timer1
        // pAlarms.timer2
        // pAlarms.timer3
        //
        // Check for enabled alarms, and if enabled compare it to the current time.
        checkAlarmAgainstTime(pAlarms.timer0);
        checkAlarmAgainstTime(pAlarms.timer1);
        checkAlarmAgainstTime(pAlarms.timer2);
        checkAlarmAgainstTime(pAlarms.timer3);
    }
}

/* 
    check for matching alarms to the current time
    match format is HH:MM:SS
    but SS will always be 00 since we are not setting the seconds.
 */
void checkAlarmAgainstTime(p_alarmData &alarm) {
    if (alarm.enabled) {
        byte currentHours = rtc.getHours();
        byte currentMinutes = rtc.getMinutes();
        byte currentSeconds = rtc.getSeconds();

        if ((currentHours == alarm.hour) && (currentMinutes == alarm.minutes) && (currentSeconds == 0)) {
            // We have a match and should turn the pump on.
            turnPumpOn();
        }
    }
}

/* Need wrapper function to pass function pointer to the alarm interup */
void turnPumpOn() {
    pump.on();
    pumpOnStartMinutes = rtc.getMinutes();
}
void turnPumpOff() {
    pump.off();
}

/* 
 * Setup the RTC
 */
void setupRTC() {
    rtc.begin();
    
    unsigned long epoch = 0;
    int numberOfTries = 0, maxTries = 6;

    do {
        epoch = WiFi.getTime();
        numberOfTries++;
    } while ((epoch == 0) && (numberOfTries < maxTries));

    if (numberOfTries == maxTries) {
        Serial.println("NTP unreachable!!");
    } else {
        Serial.print("Epoch received: ");
        Serial.println(epoch);

        rtc.setEpoch(epoch);

        Serial.print("Time received: ");
        Serial.print(rtc.getHours());
        Serial.print(":");
        Serial.print(rtc.getMinutes());
        Serial.println();  // need this to flush io buffer to display text 
    }
}


/* 
* Send the connected client the server status
 */
void sendStatusToClient(WiFiClient &client) {
    StaticJsonDocument<16> doc;
    String data;
    doc["softwareVersion"] = VERSION;

    serializeJsonPretty(doc, data);

    Serial.println(data);
    Serial.println();

}

void sendPumpStatusToClient(WiFiClient &client) {
    StaticJsonDocument<16> doc;
    String data;
    doc["pumpIsActive"] = pump.isActive();

    serializeJsonPretty(doc, data);

    Serial.println(data);
    Serial.println();

}

void sendAlarmStatusToClient(WiFiClient &client) {
    StaticJsonDocument<512> doc;
    String data;
    JsonArray alarms = doc.createNestedArray("alarms");
    addAlarmsToJSONArray(alarms);

    serializeJsonPretty(doc, data);

    Serial.println(data);
    Serial.println();

}

void printWifiStatus() {
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your board's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
}

void printAlarmStatus() {
    Serial.println("Current Alarm Status");
    
    Serial.println("Alarm 0");
    Serial.print("Is Valid: ");
    Serial.println(pAlarms.timer0.valid);
    Serial.print("Is Enabled: ");
    Serial.println(pAlarms.timer0.enabled);
    Serial.print("Alarm Set: ");
    Serial.print(pAlarms.timer0.hour);
    Serial.print(":");
    Serial.println(pAlarms.timer0.minutes);

    Serial.println("Alarm 1");
    Serial.print("Is Valid: ");
    Serial.println(pAlarms.timer1.valid);
    Serial.print("Is Enabled: ");
    Serial.println(pAlarms.timer1.enabled);
    Serial.print("Alarm Set: ");
    Serial.print(pAlarms.timer1.hour);
    Serial.print(":");
    Serial.println(pAlarms.timer1.minutes);

    Serial.println("Alarm 2");
    Serial.print("Is Valid: ");
    Serial.println(pAlarms.timer2.valid);
    Serial.print("Is Enabled: ");
    Serial.println(pAlarms.timer2.enabled);
    Serial.print("Alarm Set: ");
    Serial.print(pAlarms.timer2.hour);
    Serial.print(":");
    Serial.println(pAlarms.timer2.minutes);

    Serial.println("Alarm 3");
    Serial.print("Is Valid: ");
    Serial.println(pAlarms.timer3.valid);
    Serial.print("Is Enabled: ");
    Serial.println(pAlarms.timer3.enabled);
    Serial.print("Alarm Set: ");
    Serial.print(pAlarms.timer3.hour);
    Serial.print(":");
    Serial.println(pAlarms.timer3.minutes);
}