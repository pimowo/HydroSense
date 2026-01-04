#ifndef HA_H
#define HA_H

#include <Arduino.h>
#include <ArduinoHA.h>

void setupHA();
void onPumpAlarmCommand(bool state, HASwitch* sender);
void onSoundSwitchCommand(bool state, HASwitch* sender);
void onServiceSwitchCommand(bool state, HASwitch* sender);

#endif // HA_H
