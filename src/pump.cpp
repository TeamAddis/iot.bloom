#include "Arduino.h"
#include "pump.h"

Pump::Pump(int pin) {
    pinMode(pin, OUTPUT);
    mPin = pin;
}

void Pump::on() {
    Serial.println("Turning Pump on.");
    digitalWrite(mPin, HIGH);
    mIsActive = true;
    mStartTime = millis();
}

void Pump::off() {
    Serial.println("Turning Pump off.");
    digitalWrite(mPin, LOW);
    mIsActive = false;
    mStartTime = 0;
}