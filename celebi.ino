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
#include <WiFiNINA.h>
#include "network_secrets.h"

/* 
 * Pump variables
 */
bool pumpIsActive = false;
// #define PUMP_PIN D6

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

const int GMT = 9;

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

    pinMode(LED_BUILTIN, OUTPUT);

    // Set the test alarm
    testAlarm(2);
}

void testAlarm(byte secondsFromNow) {
    byte newSeconds = rtc.getSeconds() + secondsFromNow;
    setAlarm(rtc.getHours(), rtc.getMinutes(), newSeconds);
}

/* 
 * Loop function
 * 
 * Main runtime loop of the software
 * 
 */
void loop() {
    
}

/* 
 * Toggle the submersable pump
 */
void togglePump() {
    if (pumpIsActive) {
        // Turn off the pump
        Serial.println("Turning Pump off");
        digitalWrite(LED_BUILTIN, LOW);
        pumpIsActive = !pumpIsActive;
    } else {
        // Turn on the pump
        Serial.println("Turning Pump on");
        digitalWrite(LED_BUILTIN, HIGH);
        pumpIsActive = !pumpIsActive;

        // Set alarm for turning off the pump
        testAlarm(10);
    }
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

    return true;
}
