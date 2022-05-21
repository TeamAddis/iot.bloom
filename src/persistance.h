#include "Arduino.h"
#include <FlashStorage.h>

/* 
 * Setup parameter storage in flash
 *
 *  The "valid" variable is set to "true" once the structure is
 *  filled with actual data for the first time.
 */
typedef struct {
    byte id = 0;
    bool valid = false;
    bool enabled = false;
    byte hour;
    byte minutes;
} p_alarmData;

FlashStorage(parameter_store, p_alarmData);
p_alarmData pAlarmData = parameter_store.read();

// Save the alarm information that we receive from remote client
//
// Only use when you need to save the values in alarmHours and alarmMinutes
// Return the change flag in the new state (false if we have completed the save)
void saveAlarmParameterData(bool alarmWasChanged) {
    if (alarmWasChanged) {
        if (pAlarmData.valid == false) { pAlarmData.valid = true; }
        parameter_store.write(pAlarmData);
    } else {
        Serial.println("Alarm time is same in Flash, no need to perform write.");
    }
}