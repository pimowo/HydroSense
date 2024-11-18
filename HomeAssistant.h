#ifndef HOME_ASSISTANT_H
#define HOME_ASSISTANT_H

#include <ArduinoHA.h>
#include "SystemStatus.h"
#include "Constants.h"
#include "Pins.h"

void setupHA();
void firstUpdateHA();
void updateHA();

extern HASwitch switchService;
extern HASwitch switchPumpAlarm;

#endif