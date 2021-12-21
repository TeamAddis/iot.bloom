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
#include <WiFiNINA.h>
#include <ArduinoHttpServer.h>
#include <ArduinoHttpClient.h>
//#include <ArduinoOTA.h>
#include "network_secrets.h"

/* 
 * Pump variables
 */
bool pumpIsActive = false;
#define PUMP_PIN 6
int pumpRunTimeInMinutes = 1;
byte pumpOffAtNewMinutes;

/* 
 * RTC (Real time clock) variables
 */
RTCZero rtc;
bool dailyAlarmIsSet = false;
bool dailyAlarmEnabled = false;
byte alarmHours, alarmMinutes, currentDay = 0;

/* 
 * Wifi variables
 */
char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;

int status = WL_IDLE_STATUS;
WiFiServer server(80);

/*
 *  Discord variables
 */
const char discord_url[] = "discordapp.com";
const String discordWebhook = SECRET_DISCORD_WEBHOOK;
WiFiSSLClient sslClient;
HttpClient http_client = HttpClient(sslClient, discord_url, 443);

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

    // Setup the wifi module.
    while (!setupWifi());

    // Configure the RTC
    while (!setupRTC());

    // Setup the pump pins.
    pinMode(PUMP_PIN, OUTPUT);

    // Configure the Discord connection
    // http_client.setHttpResponseTimeout(1000);

    // start the WiFi OTA library with internal (flash) based storage
    // ArduinoOTA.begin(WiFi.localIP(), "Arduino", "password", InternalStorage);

    // Send message to discord confirming setup and connection to wifi
    sendMessageToDiscord("Celebi connected to wifi and ready to water the garden.");
}

/* 
 * Loop function
 * 
 * Main runtime loop of the software
 * 
 */
void loop() {
    // check for WiFi OTA updates
    //ArduinoOTA.poll();

    WiFiClient client = server.available();
    // WiFiSSLClient client = server.available();

    if (client) {
        communicateWithClient(client);

        if (client.connected()) {
            client.stop();
            Serial.println("remote client disconnected");
        }
    }

    // Check to see if any pumps are needing to be turned off.
    if (pumpIsActive) {
        if (rtc.getMinutes() == pumpOffAtNewMinutes) {
            setPumpOff();
        }
    }

    // Check if we need to set an alarm.
    if (dailyAlarmEnabled) {
        setDailyPumpAlarm(alarmHours, alarmMinutes);
    }
}

void setDailyPumpAlarm(byte hours, byte minutes) {
    if (!dailyAlarmIsSet) {
        currentDay = rtc.getDay();
        setPumpOnAlarm(hours, minutes, 0);
    } else {
        if (currentDay != rtc.getDay()) {
            dailyAlarmIsSet = false;
        }
    }
    
}

// Turn on the pump
void setPumpOn() {
    Serial.println("Turning Pump on");
    digitalWrite(PUMP_PIN, HIGH);
    pumpIsActive = true;
    pumpOffAtNewMinutes = rtc.getMinutes() + pumpRunTimeInMinutes;
    if (pumpOffAtNewMinutes > 59) {pumpOffAtNewMinutes-=60;}
    sendMessageToDiscord("Pump has turned on.");
}

// Turn off the pump
void setPumpOff() {
    Serial.println("Turning Pump off");
    digitalWrite(PUMP_PIN, LOW);
    pumpIsActive = false;
    sendMessageToDiscord("Pump has turned off.");
}

/* 
 * Set alarm
 */
void setPumpOnAlarm(byte hours, byte minutes, byte seconds) {
    Serial.println("alarm set");

    rtc.enableAlarm(rtc.MATCH_HHMMSS);
    rtc.setAlarmTime(hours, minutes, seconds);
    rtc.attachInterrupt(setPumpOn);
    dailyAlarmIsSet = true;
}

/* 
 * Setup the wifi chip
 */
bool setupWifi() {
    if (WiFi.status() == WL_NO_MODULE) {
        Serial.println("Communication with WiFi module failed!");
        return false;
    }

    // Check the firmware version
    String fv = WiFi.firmwareVersion();
    if (fv < WIFI_FIRMWARE_LATEST_VERSION) {
        Serial.println("Please upgrade the firmware");
    }

    // attempt to connect to WiFi network:
    while (status != WL_CONNECTED) {
        Serial.print("Attempting to connect to Network named: ");
        Serial.println(ssid);                   // print the network name (SSID);

        status = WiFi.begin(ssid, pass);
        
        // wait 10 seconds for connection:
        delay(10000);
    }

    server.begin();

    return true;
}

/* 
 * Setup the RTC
 */
bool setupRTC() {
    rtc.begin();
    
    
    unsigned long epoch = 0;
    int numberOfTries = 0, maxTries = 6;

    do {
        epoch = WiFi.getTime();
        numberOfTries++;
    } while ((epoch == 0) && (numberOfTries < maxTries));

    if (numberOfTries == maxTries) {
        Serial.println("NTP unreachable!!");
        return false;
    } else {
        Serial.print("Epoch received: ");
        Serial.println(epoch);

        rtc.setEpoch(epoch);

        Serial.print("Time received: ");
        Serial.print(rtc.getHours());
        Serial.print(":");
        Serial.print(rtc.getMinutes());
        Serial.println();  // need this to flush io buffer to display text 
        return true;
    }
}

/* 
* Send the connected client the server status
 */
void sendStatusToClient(WiFiClient &client) {
    StaticJsonDocument<200> doc;
    String data;
    doc["pumpIsActive"] = pumpIsActive;
    doc["softwareVersion"] = VERSION;

    serializeJsonPretty(doc, data);

    Serial.println(data);
    Serial.println();

    ArduinoHttpServer::StreamHttpReply httpReply(client, "application/json");
    httpReply.send(data);
}

/* 
 * Read from a connected client
 */
void communicateWithClient(WiFiClient &client) {
    if (client.connected()) {
        ArduinoHttpServer::StreamHttpRequest<1024> request(client);
        if (request.readRequest()) {
            ArduinoHttpServer::Method method( ArduinoHttpServer::Method::Invalid );
            method = request.getMethod();
            String endpoint = request.getResource().toString();

            if (method == ArduinoHttpServer::Method::Get) {
                if (endpoint == "/ps") {
                    sendStatusToClient(client);
                    printAlarmStatus();
                }
                
            } else if(method == ArduinoHttpServer::Method::Post) {
                if (endpoint == "/m") {
                    Serial.println(request.getBody());

                    StaticJsonDocument<32> data;
                    deserializeJson(data, request.getBody());

                    if (data["isOn"]) {
                        setPumpOn();
                    } else {
                        setPumpOff();
                    }

                    ArduinoHttpServer::StreamHttpReply httpReply(client, request.getContentType());
                    httpReply.send("OK");
                } else if (endpoint == "/a") {
                    Serial.println(request.getBody());

                    StaticJsonDocument<64> data;
                    deserializeJson(data, request.getBody());

                    alarmHours = data["hours"];
                    alarmMinutes = data["minutes"];

                    dailyAlarmEnabled = true;

                    ArduinoHttpServer::StreamHttpReply httpReply(client, request.getContentType());
                    httpReply.send("OK");
                }
            }
        }
    }
}

void sendMessageToDiscord(const String& message) {
    if (message == NULL) {
        Serial.println("Message cannot be NULL.");
        return;
    }

    int messageLength = message.length();
    if (messageLength > 0) {

        Serial.println("[HTTP] Connecting to Discord...");
        Serial.println("[HTTP] Message: " + message);
        // Serial.println("[HTTP] Content: " + data);
        
        http_client.post(discordWebhook, "application/json", "{\"content\":\"" + message + "\"}");

        // read the status code and body of the response
        if (http_client.connected()) {
            int statusCode = http_client.responseStatusCode();
            String response = http_client.responseBody();

            Serial.print("[HTTP] Status code: ");
            Serial.println(statusCode);
            Serial.print("[HTTP] Response: ");
            Serial.println(response);

            http_client.stop();
            Serial.println("Disconnecting client from Discord.");
        }

    } else {return;} 
}

void printAlarmStatus() {
    Serial.println("Current Alarm Status");
    Serial.print("Daily Alarm Enabled: ");
    if (!dailyAlarmEnabled) {
        Serial.println("No");
        return;
    }
    Serial.println(dailyAlarmEnabled);
    Serial.print("Daily Alarm Set: ");
    Serial.println(dailyAlarmIsSet);
    Serial.print("Current Alarm Time: ");

    byte newHours = alarmHours + 9;
    if (newHours > 23) {newHours -= 24;}
    Serial.print(newHours);
    Serial.print(":");

    if (alarmMinutes < 10) {
        Serial.print(0);
    }
    Serial.println(alarmMinutes);
}