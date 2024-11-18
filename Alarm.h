// Alarm.h
#ifndef ALARM_H
#define ALARM_H

#include "Constants.h"
#include "SystemStatus.h"
#include "HomeAssistant.h"

void checkAlarmConditions();
void updateAlarmStates(float currentDistance);

#endif