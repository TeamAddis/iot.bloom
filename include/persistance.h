#include "Arduino.h"
#include <FlashStorage.h>

/* 
 * Setup parameter storage in flash
 *
 *  The "valid" variable is set to "true" once the structure is
 *  filled with actual data for the first time.
 */
struct Alarm {
    bool valid = false;       // Whether the alarm is valid
    bool enabled = false;     // Whether the alarm is enabled
    byte hour;                // Hour of the alarm (24-hour format)
    byte minute;              // Minute of the alarm

    // Constructor
    Alarm(bool v = false, bool e = false, byte h = 0, byte m = 0)
        : valid(v), enabled(e), hour(h), minute(m) {}
};

typedef struct {
    Alarm alarms[4];          // Support up to 4 alarms
    unsigned long maxPumpRuntime; // in milliseconds
} AlarmStorage;

FlashStorage(alarmStorage, AlarmStorage);
AlarmStorage alarmData = alarmStorage.read();




// Save the alarm information that we receive from remote client
//
// Only use when you need to save the values in alarmHours and alarmMinutes
// Return the change flag in the new state (false if we have completed the save)
// inline void saveAlarmParameterData(bool alarmWasChanged) {
//     if (alarmWasChanged) {
//         if (pAlarmData.valid == false) { pAlarmData.valid = true; }
//         parameter_store.write(pAlarmData);
//     } else {
//         Serial.println("Alarm time is same in Flash, no need to perform write.");
//     }
// }
void saveAlarms() {
    alarmStorage.write(alarmData);
    Serial.println("Alarms saved to flash.");
}

void loadAlarms() {
    alarmData = alarmStorage.read();
    Serial.println("Alarms loaded from flash.");
}

void setDefaultAlarms() {
    bool allInvalid = true;

    // Check if all alarms are invalid
    for (int i = 0; i < 4; i++) {
        if (alarmData.alarms[i].valid) {
            allInvalid = false;
            break;
        }
    }

    // If all alarms are invalid, set default values
    if (allInvalid) {
        Serial.println("No valid alarms found. Setting default alarms...");

        alarmData.alarms[0] = { true, true, 6, 0 };    // Default alarm 1: 6:00 AM, enabled
        alarmData.alarms[1] = { true, true, 17, 0 };   // Default alarm 2: 5:00 PM, enabled
        alarmData.alarms[2] = { true, false, 0, 0 }; 
        alarmData.alarms[3] = { true, false, 0, 0 }; 

        saveAlarms(); // Save the default alarms to flash

        // Debug: Print the default alarms
        for (int i = 0; i < 4; i++) {
            Serial.print("Default Alarm ");
            Serial.print(i);
            Serial.print(": hour=");
            Serial.print(alarmData.alarms[i].hour);
            Serial.print(", minute=");
            Serial.print(alarmData.alarms[i].minute);
            Serial.print(", enabled=");
            Serial.print(alarmData.alarms[i].enabled);
            Serial.print(", valid=");
            Serial.println(alarmData.alarms[i].valid);
        }
    } else {
        Serial.println("Valid alarms found. Skipping default alarm setup.");
    }
}