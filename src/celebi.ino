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
#include "pump.h"
#include "network_secrets.h"
#include "discord.h"
#include "persistance.h"

/* 
 * Pump variables
 */
Pump pump(6);
byte pumpOffAtNewMinutes;

/* 
 * RTC (Real time clock) variables
 */
RTCZero rtc;
bool dailyAlarmIsSet = false;
byte currentDay = 0;

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
    setupWifi();

    // Configure the RTC
    setupRTC();

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
    WiFiClient client = server.available();

    if (client) {
        communicateWithClient(client);

        if (client.connected()) {
            client.stop();
            Serial.println("remote client disconnected");
        }
    }

    // Check to see if any pumps are needing to be turned off.
    if (pump.isActive()) {
        if (rtc.getMinutes() == pumpOffAtNewMinutes) {
            pump.off();
        }
    }

    // Check if we need to set an alarm.
    if (pAlarmData.enabled) {
        setDailyPumpAlarm(pAlarmData.hour, pAlarmData.minutes);
    }

    // Check if we lost connection to the internet and try to reconnect if we did.
    if (status == WL_CONNECTION_LOST) {
        setupWifi();
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

/* Need wrapper function to pass function pointer to the alarm interup */
void turnPumpOn() { pump.on(); }

/* 
 * Set alarm
 */
void setPumpOnAlarm(byte hours, byte minutes, byte seconds) {
    Serial.println("alarm set");
    rtc.enableAlarm(rtc.MATCH_HHMMSS);
    rtc.setAlarmTime(hours, minutes, seconds);
    rtc.attachInterrupt(turnPumpOn);
    dailyAlarmIsSet = true;
}

/* 
 * Setup the wifi chip
 */
void setupWifi() {
    if (WiFi.status() == WL_NO_MODULE) {
        Serial.println("Communication with WiFi module failed!");
        // wifi module isn't responding.
        // don't contintue
        while (true);
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

    printWifiStatus();
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
    StaticJsonDocument<200> doc;
    String data;
    doc["pumpIsActive"] = pump.isActive();
    doc["softwareVersion"] = VERSION;
    doc["alarmHours"] = pAlarmData.hour;
    doc["alarmMinutes"] = pAlarmData.minutes;
    doc["alarmEnabled"] = pAlarmData.enabled;
    doc["alarmValid"] = pAlarmData.valid;
    doc["alarmId"] = pAlarmData.id;

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
                        pump.on();
                    } else {
                        pump.off();
                    }

                    ArduinoHttpServer::StreamHttpReply httpReply(client, request.getContentType());
                    httpReply.send("OK");
                } else if (endpoint == "/a") {
                    Serial.println(request.getBody());

                    StaticJsonDocument<128> data;
                    deserializeJson(data, request.getBody());
                    bool alarmWasChanged = false;

                    if (pAlarmData.hour != data["hours"]) {
                        pAlarmData.hour = data["hours"];
                        alarmWasChanged = true;
                    }
                    
                    if (pAlarmData.minutes != data["minutes"]) {
                        pAlarmData.minutes = data["minutes"];
                        alarmWasChanged = true;
                    }
                    
                    if (pAlarmData.id != data["id"]) {
                        alarmWasChanged = true;
                        pAlarmData.id = data["id"];
                    }

                    if (pAlarmData.enabled != data["enabled"]) {
                        alarmWasChanged = true;
                        pAlarmData.enabled = data["enabled"];
                    }

                    // Save parameter data to flash memory if needed.
                    saveAlarmParameterData(alarmWasChanged); 

                    ArduinoHttpServer::StreamHttpReply httpReply(client, request.getContentType());
                    httpReply.send("OK");
                }
            }
        }
    }
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
    Serial.print("Daily Alarm Enabled: ");
    if (!pAlarmData.enabled) {
        Serial.println("No");
        return;
    }
    Serial.println(pAlarmData.enabled);
    Serial.print("Daily Alarm Set: ");
    Serial.println(dailyAlarmIsSet);
    Serial.print("Current Alarm Time: ");

    byte newHours = pAlarmData.hour + 9;
    if (newHours > 23) {newHours -= 24;}
    Serial.print(newHours);
    Serial.print(":");

    if (pAlarmData.minutes < 10) {
        Serial.print(0);
    }
    Serial.println(pAlarmData.minutes);
}