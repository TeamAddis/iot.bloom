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
int pumpRunTimeInMinutes = 5;

/* 
 * RTC (Real time clock) variables
 */
RTCZero rtc;

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

    // start the WiFi OTA library with internal (flash) based storage
    // ArduinoOTA.begin(WiFi.localIP(), "Arduino", "password", InternalStorage);

    // Send message to discord confirming setup and connection to wifi
    sendMessageToDiscord("Celebi connected to wifi and ready to water the garden.");
}

/* 
 * Function that will set an alarm for duration in the future measured in seconds.
 */
void setAutoOffAlarmSeconds(int secondsFromNow) {
    int seconds = rtc.getSeconds() + secondsFromNow;
    int minutes = rtc.getMinutes();
    int hours = rtc.getHours();
    if (seconds >= 60) {
        seconds -= 60;
        minutes++;
        if (minutes >= 60) {
            minutes -= 60;
            if (hours == 24) {
                hours = 0;
            }
        }
    }
    setAlarm(hours, minutes, seconds);
}

/* 
 * Function that will set an alarm for duration in the future measured in minutes.
 */
void setAutoOffAlarmMinutes(int minutesFromNow) {
    int hours = rtc.getHours();
    int minutes = rtc.getMinutes() + minutesFromNow;
    if (minutes >= 60) {
        minutes -= 60;
        hours++;
        if (hours == 24) {
            hours = 0;
        }
    }
    setAlarm(hours, minutes, rtc.getSeconds());
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
    }
}

/* 
 * Toggle the submersable pump
 */
void togglePump() {
    if (pumpIsActive) {
        // Turn off the pump
        Serial.println("Turning Pump off");
        digitalWrite(PUMP_PIN, LOW);
        pumpIsActive = !pumpIsActive;
    } else {
        // Turn on the pump
        Serial.println("Turning Pump on");
        digitalWrite(PUMP_PIN, HIGH);
        pumpIsActive = !pumpIsActive;

        // Set alarm for turning off the pump
        setAutoOffAlarmMinutes(pumpRunTimeInMinutes);
    }
}

// Turn on the pump
void setPumpOn() {
    Serial.println("Turning Pump on");
    digitalWrite(PUMP_PIN, HIGH);
    pumpIsActive = true;
}

// Turn off the pump
void setPumpOff() {
    Serial.println("Turning Pump off");
    digitalWrite(PUMP_PIN, LOW);
    pumpIsActive = false;
}

/* 
 * Set alarm
 */
void setAlarm(byte hours, byte minutes, byte seconds) {

    Serial.println("alarm set");
    Serial.println(hours);
    Serial.println(minutes);
    Serial.println();

    rtc.enableAlarm(rtc.MATCH_HHMMSS);
    rtc.setAlarmTime(hours, minutes, seconds);
    rtc.attachInterrupt(togglePump);
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
void sendStatusToClient(WiFiClient client) {
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
void communicateWithClient(WiFiClient client) {
    if (client.connected()) {
        ArduinoHttpServer::StreamHttpRequest<1024> request(client);
        if (request.readRequest()) {
            ArduinoHttpServer::Method method( ArduinoHttpServer::Method::Invalid );
            method = request.getMethod();
            String endpoint = request.getResource().toString();

            if (method == ArduinoHttpServer::Method::Get) {
                if (endpoint == "/ps") {
                    sendStatusToClient(client);
                }
                
            } else if(method == ArduinoHttpServer::Method::Post) {
                if (endpoint == "/m") {
                    Serial.println(request.getBody());

                    StaticJsonDocument<200> data;
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

                    DynamicJsonDocument data(200);
                    deserializeJson(data, request.getBody());

                    byte hours = data["hours"];
                    byte minutes = data["minutes"];

                    setAlarm(hours, minutes, 0);

                    ArduinoHttpServer::StreamHttpReply httpReply(client, request.getContentType());
                    httpReply.send("OK");
                }
            }
        }
    }

    client.stop();
    Serial.println("client disconnected");
}

void sendMessageToDiscord(String message) {
    WiFiSSLClient client;
    HttpClient http_client = HttpClient(client, discord_url, 443);

    StaticJsonDocument<200> json;
    json["content"] = message;
    String data = "";
    serializeJson(json, data);

    Serial.println("[HTTP] Connecting to Discord...");
    Serial.println("[HTTP] Message: " + message);
    
    http_client.post(discordWebhook, "application/json", data);

    // read the status code and body of the response
    int statusCode = http_client.responseStatusCode();
    String response = http_client.responseBody();

    Serial.print("[HTTP] Status code: ");
    Serial.println(statusCode);
    Serial.print("[HTTP] Response: ");
    Serial.println(response);
}