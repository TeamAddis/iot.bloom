/* 
 * Bloom IoT garden
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
void connectWifi();
void connectMQTT();
void setupRTC();
void turnPumpOn();
void turnPumpOff();
void onMessageReceived(int messageSize);
unsigned long getTime();
void sendPumpStatus();
void sendMCUStatus();
void checkAlarms();
void sendAlarmStatus();


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
// Maximum runtime in milliseconds (60 seconds)
unsigned long maxPumpRuntime = 60000; // default 60 seconds

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
    // Wait for Serial connection only if in development/debug mode
    #ifdef DEBUG
    while (!Serial) {
        // Wait for Serial Monitor to be opened
    }
    Serial.println("Debug mode: Serial connection established.");
    #else
    Serial.println("Production mode: Skipping Serial connection wait.");
    #endif

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
        // reset and try again
        delay(1000);
        NVIC_SystemReset();
    }

    // Connect to WiFi
    connectWifi();

    // Setup RTC
    setupRTC();

    // Initialize FlashStorage and load alarms
    loadAlarms();

    // Set default alarms if all are invalid
    setDefaultAlarms();

    // Load maxPumpRuntime from flash, or set default if not set
    if (alarmData.maxPumpRuntime == 0 || alarmData.maxPumpRuntime > 60 * 1000UL) { // max 1 min
        alarmData.maxPumpRuntime = 60000; // 60 seconds default
        saveAlarms();
    }
    maxPumpRuntime = alarmData.maxPumpRuntime;

        Serial.print("Size of AlarmStorage: ");
        Serial.println(sizeof(AlarmStorage));
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

    // Check if the pump has exceeded its maximum runtime
    if (pump.isActive() && (millis() - pump.getStartTime() > maxPumpRuntime)) {
        Serial.println("Pump exceeded maximum runtime. Shutting off for safety.");
        pump.off();
        sendPumpStatus();
    }

    // Check alarms
    checkAlarms();


}

void checkAlarms() {
    if (!rtcIsConfigured) return;

    byte currentHour = rtc.getHours();
    byte currentMinute = rtc.getMinutes();

    for (int i = 0; i < 4; i++) {
        Alarm& alarm = alarmData.alarms[i];
        if (alarm.enabled && alarm.valid &&
            alarm.hour == currentHour && alarm.minute == currentMinute) {
            Serial.print("Alarm triggered! Turning pump on for alarm ");
            Serial.println(i);
            turnPumpOn();
            delay(60000); // Prevent multiple activations within the same minute
        }
    }
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

    // Subscribe to topics
    mqttClient.subscribe(PUMP_CONTROL_TOPIC); 
    mqttClient.subscribe(PUMP_STATUS_REQUEST_TOPIC); 
    mqttClient.subscribe(MCU_STATUS_REQUEST_TOPIC);
    mqttClient.subscribe(MCU_ALARM_SET_TOPIC);
    mqttClient.subscribe(MCU_ALARM_STATUS_REQUEST_TOPIC);
    mqttClient.subscribe(MCU_PUMP_RUNTIME_SET_TOPIC);

    Serial.println("Subscribed to MQTT topics:");
    Serial.println(PUMP_CONTROL_TOPIC);
    Serial.println(PUMP_STATUS_REQUEST_TOPIC);
    Serial.println(MCU_STATUS_REQUEST_TOPIC);
    Serial.println(MCU_ALARM_SET_TOPIC);
    Serial.println(MCU_ALARM_STATUS_REQUEST_TOPIC);
    Serial.println(MCU_PUMP_RUNTIME_SET_TOPIC);
}

/*
 * Receive message from MQTT Broker
*/
void onMessageReceived(int messageSize) {
    // we received a message, store the topic and payload for later use.
    String topic = mqttClient.messageTopic();
    
    // Create a JSON document based on the message contents
    StaticJsonDocument<400> jsonDocument;
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
    if (topic == PUMP_CONTROL_TOPIC) {
        if (jsonDocument["message"] == PUMP_STATUS_ON) {
            turnPumpOn();
            sendPumpStatus();
        } else if (jsonDocument["message"] == PUMP_STATUS_OFF) {
            turnPumpOff();
            sendPumpStatus();
        }
    } else if (topic == PUMP_STATUS_REQUEST_TOPIC) {
        sendPumpStatus();
        
    } else if (topic == MCU_STATUS_REQUEST_TOPIC) {
        sendMCUStatus();
    } else if (topic == MCU_ALARM_SET_TOPIC) {
        if (jsonDocument.containsKey("alarms")) {
            JsonArray alarmsArray = jsonDocument["alarms"].as<JsonArray>();
            int i = 0;
            for (JsonObject alarm : alarmsArray) {
                if (i >= 4) break; // Ensure we don't exceed the number of alarms
    
                alarmData.alarms[i].hour = alarm["hour"] | 0; // Default to 0 if not provided
                alarmData.alarms[i].minute = alarm["minute"] | 0; // Default to 0 if not provided
                alarmData.alarms[i].enabled = alarm["enabled"] | false; // Default to false if not provided
                alarmData.alarms[i].valid = alarm["valid"] | false; // Default to false if not provided
    
                i++;
            }
        }
        saveAlarms(); // Persist the updated alarms
        sendAlarmStatus();
        Serial.println("Alarms updated via MQTT.");
    } else if (topic == MCU_ALARM_STATUS_REQUEST_TOPIC) {
        sendAlarmStatus();
    } else if (topic == MCU_PUMP_RUNTIME_SET_TOPIC) {
        if (jsonDocument.containsKey("maxPumpRuntime")) {
            unsigned long newRuntime = jsonDocument["maxPumpRuntime"];
            if (newRuntime > 0 && newRuntime <= 60000) { // 1 min max
                alarmData.maxPumpRuntime = newRuntime;
                maxPumpRuntime = newRuntime;
                saveAlarms();
                Serial.print("Updated maxPumpRuntime via MQTT: ");
                Serial.println(maxPumpRuntime);
            } else {
                Serial.println("Rejected maxPumpRuntime: Out of allowed range (1-60000 ms)");
            }
        }
    }
}

void sendAlarmStatus() {
    StaticJsonDocument<400> responseJson;

    JsonArray alarms = responseJson.createNestedArray("alarms");
    for (int i = 0; i < 4; i++) {
        JsonObject alarm = alarms.createNestedObject();
        alarm["hour"] = alarmData.alarms[i].hour;
        alarm["minute"] = alarmData.alarms[i].minute;
        alarm["enabled"] = alarmData.alarms[i].enabled;
        alarm["valid"] = alarmData.alarms[i].valid;
    }

    String responseString;
    serializeJson(responseJson, responseString);

    mqttClient.beginMessage(ALARM_STATUS_TOPIC);
    mqttClient.print(responseString);
    mqttClient.endMessage();

    Serial.print("Alarm status sent: ");
    Serial.println(responseString);
}

void sendPumpStatus() {
    StaticJsonDocument<200> responseJson;
    responseJson["status"] = pump.isActive() ? PUMP_STATUS_ON : PUMP_STATUS_OFF;
    String responseString;
    serializeJson(responseJson, responseString);

    mqttClient.beginMessage(PUMP_STATUS_TOPIC);
    mqttClient.print(responseString);
    mqttClient.endMessage();

    Serial.print("Pump status response sent: ");
    Serial.println(responseString);
}

void sendMCUStatus() {
    StaticJsonDocument<200> responseJson;
    responseJson["version"] = VERSION;
    String responseString;
    serializeJson(responseJson, responseString);

    mqttClient.beginMessage(MCU_STATUS_TOPIC);
    mqttClient.print(responseString);
    mqttClient.endMessage();

    Serial.print("MCU status response sent: ");
    Serial.println(responseString);
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
    rtc.begin(); // Initialize the RTC

    unsigned long epoch = 0;
    int numberOfTries = 0, maxTries = 6;

    // Attempt to get the current epoch time from the WiFi module
    do {
        epoch = WiFi.getTime();
        numberOfTries++;
        if (epoch == 0) {
            Serial.println("Failed to get time from WiFi module. Retrying...");
            delay(5000); // Wait before retrying
        }
    } while ((epoch == 0) && (numberOfTries < maxTries));

    if (epoch == 0) {
        // If we failed to get the time after maxTries, print an error
        Serial.println("NTP unreachable! RTC not configured.");
        rtcIsConfigured = false;
    } else {
        // Successfully received the epoch time
        Serial.print("Epoch received (UTC+0): ");
        Serial.println(epoch);

        // Adjust the epoch time to JST (UTC+9)
        const unsigned long SECONDS_IN_AN_HOUR = 3600;
        epoch += 9 * SECONDS_IN_AN_HOUR;

        rtc.setEpoch(epoch); // Set the RTC with the received epoch time
        rtcIsConfigured = true;

        // Print the configured time for verification
        Serial.print("RTC Time set to: ");
        Serial.print(rtc.getHours());
        Serial.print(":");
        Serial.print(rtc.getMinutes());
        Serial.print(":");
        Serial.print(rtc.getSeconds());
        Serial.println();
    }
}