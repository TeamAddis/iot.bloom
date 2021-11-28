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
#include "network_secrets.h"

/* 
 * Pump variables
 */
bool pumpIsActive = false;
#define PUMP_PIN 6

/* 
 * RTC (Real time clock) variables
 */
RTCZero rtc;
const int GMT = 9;

/* 
 * Wifi variables
 */
char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;

int status = WL_IDLE_STATUS;
WiFiServer server(80);

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
    if (!setupWifi()) {while(true);}

    // Configure the RTC
    setupRTC(rtc);

    // Setup the pump pins.
    pinMode(PUMP_PIN, OUTPUT);
}

/* 
 * Function that will set an alarm for duration in the future measured in seconds.
 */
void setAutoOffAlarmSeconds(byte secondsFromNow) {
    byte seconds = rtc.getSeconds() + secondsFromNow;
    byte minutes = rtc.getMinutes();
    byte hours = rtc.getHours();
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
void setAutoOffAlarmMinutes(byte minutesFromNow) {
    byte hours = rtc.getHours();
    byte minutes = rtc.getMinutes() + minutesFromNow;
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
        setAutoOffAlarmSeconds(10);
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
    rtc.setAlarmTime(hours, minutes, seconds);
    rtc.enableAlarm(rtc.MATCH_HHMMSS);
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
void setupRTC(RTCZero rtc) {
    rtc.begin();
    
    unsigned long epoch = 0;
    int numberOfTries = 0, maxTries = 6;

    do {
        epoch = WiFi.getTime();
        numberOfTries++;
    } while ((epoch == 0) && (numberOfTries < maxTries));

    if (numberOfTries == maxTries) {
        Serial.println("NTP unreachable!!");
        while(1);
    } else {
        Serial.print("Time received: ");
        Serial.println(epoch);

        rtc.setEpoch(epoch);
        Serial.println();
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
            Serial.println(endpoint);
            if (method == ArduinoHttpServer::Method::Get) {
                if (endpoint == "/ps") {
                    sendStatusToClient(client);
                }
                
            } else if(method == ArduinoHttpServer::Method::Post) {
                if (endpoint == "/m") {
                    Serial.println(request.getBody());

                    DynamicJsonDocument data(24);
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

                    DynamicJsonDocument data(24);
                    deserializeJson(data, request.getBody());

                    int hours = data["hours"];
                    int minutes = data["minutes"];

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