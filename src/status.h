#ifndef STATUS_H
#define STATUS_H

#include <Arduino.h>

struct Status {
    bool soundEnabled;
    bool waterAlarmActive;
    bool waterReserveActive;
    bool isPumpActive;
    bool isPumpDelayActive;
    bool pumpSafetyLock;
    bool isServiceMode;
    float waterLevelBeforePump;
    unsigned long pumpStartTime;
    unsigned long pumpDelayStartTime;
    unsigned long lastSoundAlert;
    unsigned long lastSuccessfulMeasurement;
};

extern Status status;

#endif // STATUS_H
