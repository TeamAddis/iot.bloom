#ifndef pump_h
#define pump_h

#include "Arduino.h"

#define PUMP_STATUS_ON "on"
#define PUMP_STATUS_OFF "off"

class Pump {
    public:
        Pump() { mPin = 6; }
        Pump(int pin);
        void on();
        void off();
        bool isActive() const { return mIsActive; }
        unsigned long getStartTime() const { return mStartTime; }
    private:
        int mPin;
        int mRuntimeInMinutes = 1;
        bool mIsActive = false;
        unsigned long mStartTime = 0;
};

#endif