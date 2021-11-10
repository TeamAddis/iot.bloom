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

/* 
 * Pump variables
 */
bool activatePump = false;

/* 
 * RTC (Real time clock) variables
 */
const byte seconds = 0;
const byte minutes = 2;
const byte hours = 19;

RTCZero rtc;


/* 
 * Setup function
 * 
 * Main entry point of the software
 * 
 */
void setup() {
    Serial.begin(9600);

    // Configure the RTC
    rtc.begin();
    rtc.setTime(hours, minutes, seconds);

    // Set the alarm
    setAlarm(19, 5, 0);
    rtc.enableAlarm(rtc.MATCH_HHMMSS);
    rtc.attachInterrupt(togglePump);

    pinMode(LED_BUILTIN, OUTPUT);   // initialize digital pin LED_BUILTIN as an output.
}

/* 
 * Loop function
 * 
 * Main runtime loop of the software
 * 
 */
void loop() {
    if (activatePump) {
        digitalWrite(LED_BUILTIN, HIGH);   // turn the LED on (HIGH is the voltage level)
        Serial.println("Turned pump on");
    } else {
        digitalWrite(LED_BUILTIN, LOW);    // turn the LED off by making the voltage LOW
        Serial.println("Turned pump off");
    }
}

/* 
 * Toggle the submersable pump
 */
void togglePump() {
    activatePump = !activatePump;
}

/* 
 * Set alarm
 */
void setAlarm(byte hours, byte minutes, byte seconds) {
    rtc.setAlarmTime(hours, minutes, seconds);
    rtc.setAlarmTime(hours, minutes, seconds + 5);
}