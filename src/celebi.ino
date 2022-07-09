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
byte pumpOnStartMinutes;

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

    // Init the data storage Ids if needed
    initAlarmIds();

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
    // This is to ensure we never have pumps on more than 1 minute
    // as a safety measure to prevent damage to the pump in the case
    // we run the resivor dry.
    if (pump.isActive()) {
        if ((rtc.getMinutes() - pumpOnStartMinutes > 1) || (rtc.getMinutes() - pumpOnStartMinutes < 0)) {
            turnPumpOff();
        } 
    }

    // Check if we have any enabled alarms
    checkAlarms();

    // Check if we lost connection to the internet and try to reconnect if we did.
    if (status == WL_CONNECTION_LOST) {
        setupWifi();
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
    sendMessageToDiscord("Turning Pump on.");
}
void turnPumpOff() {
    pump.off();
    sendMessageToDiscord("Turning Pump off.");
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
    StaticJsonDocument<16> doc;
    String data;
    doc["softwareVersion"] = VERSION;

    serializeJsonPretty(doc, data);

    Serial.println(data);
    Serial.println();

    ArduinoHttpServer::StreamHttpReply httpReply(client, "application/json");
    httpReply.send(data);
}

void sendPumpStatusToClient(WiFiClient &client) {
    StaticJsonDocument<16> doc;
    String data;
    doc["pumpIsActive"] = pump.isActive();

    serializeJsonPretty(doc, data);

    Serial.println(data);
    Serial.println();

    ArduinoHttpServer::StreamHttpReply httpReply(client, "application/json");
    httpReply.send(data);
}

void sendAlarmStatusToClient(WiFiClient &client) {
    StaticJsonDocument<512> doc;
    String data;
    JsonArray alarms = doc.createNestedArray("alarms");
    addAlarmsToJSONArray(alarms);

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
                    sendPumpStatusToClient(client);
                } else if (endpoint == "/ss") {
                    sendStatusToClient(client);
                } else if (endpoint == "/as") {
                    sendAlarmStatusToClient(client);
                }
                printAlarmStatus();
            } else if(method == ArduinoHttpServer::Method::Post) {
                if (endpoint == "/m") {
                    Serial.println(request.getBody());

                    StaticJsonDocument<32> data;
                    deserializeJson(data, request.getBody());

                    if (data["isOn"]) {
                        turnPumpOn();
                    } else {
                        turnPumpOff();
                    }

                    ArduinoHttpServer::StreamHttpReply httpReply(client, request.getContentType());
                    httpReply.send("OK");
                } else if (endpoint == "/a") {
                    Serial.println("remote client requests set alarm:");
                    Serial.println(request.getBody());

                    StaticJsonDocument<128> data;
                    deserializeJson(data, request.getBody());
                    bool alarmWasChanged = false;

                    byte id = data["id"];
                    switch (id) {
                        case 0:
                            pAlarms.timer0.hour = data["hours"];
                            pAlarms.timer0.minutes = data["minutes"];
                            pAlarms.timer0.enabled = data["enabled"];
                            pAlarms.timer0.valid = true;
                            Serial.println("updating alarm 0");
                            break;
                        case 1:
                            pAlarms.timer1.hour = data["hours"];
                            pAlarms.timer1.minutes = data["minutes"];
                            pAlarms.timer1.enabled = data["enabled"];
                            pAlarms.timer1.valid = true;
                            Serial.println("updating alarm 1");
                            break;
                        case 2:
                            pAlarms.timer2.hour = data["hours"];
                            pAlarms.timer2.minutes = data["minutes"];
                            pAlarms.timer2.enabled = data["enabled"];
                            pAlarms.timer2.valid = true;
                            break;
                        case 3:
                            pAlarms.timer3.hour = data["hours"];
                            pAlarms.timer3.minutes = data["minutes"];
                            pAlarms.timer3.enabled = data["enabled"];
                            pAlarms.timer3.valid = true;
                            break;
                    }
                    
                    alarmWasChanged = true;
                    
                    saveAlarms(alarmWasChanged);

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