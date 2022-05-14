#ifndef Pump_h
#define Pump_h

#include "Arduino.h"

class Pump {
    public:
        Pump() { mPin = 6; }
        Pump(int pin);
        void on();
        void off();
        bool isActive() const { return mIsActive; }
    private:
        int mPin;
        int mRuntimeInMinutes = 1;
        bool mIsActive = false;
};

#endif