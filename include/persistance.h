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

typedef struct {
    p_alarmData timer0;
    p_alarmData timer1;
    p_alarmData timer2;
    p_alarmData timer3;
    bool wasInit = false;
} p_timers;

FlashStorage(alarm_storage, p_timers);
p_timers pAlarms = alarm_storage.read();

bool areActiveAlarms() {
    return (pAlarms.timer0.enabled || pAlarms.timer1.enabled ||
        pAlarms.timer2.enabled || pAlarms.timer3.enabled);
}

void addAlarmsToJSONArray(JsonArray &alarms) {
    if (pAlarms.timer0.valid) {
        JsonObject alarm = alarms.createNestedObject();
        alarm["hours"] = pAlarms.timer0.hour;
        alarm["minutes"] = pAlarms.timer0.minutes;
        alarm["enabled"] = pAlarms.timer0.enabled;
        alarm["id"] = pAlarms.timer0.id;
    } 
    if (pAlarms.timer1.valid) {
        JsonObject alarm = alarms.createNestedObject();
        alarm["hours"] = pAlarms.timer1.hour;
        alarm["minutes"] = pAlarms.timer1.minutes;
        alarm["enabled"] = pAlarms.timer1.enabled;
        alarm["id"] = pAlarms.timer1.id;
    }
    if (pAlarms.timer2.valid) {
        JsonObject alarm = alarms.createNestedObject();
        alarm["hours"] = pAlarms.timer2.hour;
        alarm["minutes"] = pAlarms.timer2.minutes;
        alarm["enabled"] = pAlarms.timer2.enabled;
        alarm["id"] = pAlarms.timer2.id;
    }
    if (pAlarms.timer3.valid) {
        JsonObject alarm = alarms.createNestedObject();
        alarm["hours"] = pAlarms.timer3.hour;
        alarm["minutes"] = pAlarms.timer3.minutes;
        alarm["enabled"] = pAlarms.timer3.enabled;
        alarm["id"] = pAlarms.timer3.id;
    }
}

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
void saveAlarms(bool dataWasChanged) {
   if (dataWasChanged) {
       alarm_storage.write(pAlarms);
   } else {
       Serial.println("Alarm times is same in Flash, no need to perform write.");
   }
}

void initAlarmIds() {
    if (pAlarms.wasInit) {return;}
    pAlarms.timer0.id = 0;
    pAlarms.timer1.id = 1;
    pAlarms.timer2.id = 2;
    pAlarms.timer3.id = 3;
    pAlarms.wasInit = true;
    saveAlarms(true);
}