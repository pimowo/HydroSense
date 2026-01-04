#ifndef TIMERS_H
#define TIMERS_H

#include <Arduino.h>

struct Timers {
    unsigned long lastMQTTRetry;
    unsigned long lastMeasurement;
    unsigned long lastOTACheck;
    unsigned long lastMQTTLoop;
    unsigned long lastWiFiAttempt;
    Timers() : lastMQTTRetry(0), lastMeasurement(0), lastOTACheck(0), lastMQTTLoop(0) {}
};

extern Timers timers;

#endif // TIMERS_H
