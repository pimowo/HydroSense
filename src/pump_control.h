#ifndef PUMP_CONTROL_H
#define PUMP_CONTROL_H

#include <Arduino.h>

class HASwitch;

void updatePump();
void onPumpAlarmCommand(bool state, HASwitch* sender);

#endif // PUMP_CONTROL_H
