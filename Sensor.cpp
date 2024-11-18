// Sensors.cpp
#include "Sensors.h"
#include <Arduino.h>

// Stałe dla czujnika ultradźwiękowego
const int TRIG_PIN = D6;
const int ECHO_PIN = D5;

float measureDistance() {
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    
    long duration = pulseIn(ECHO_PIN, HIGH);
    return duration * 0.034 / 2;
}