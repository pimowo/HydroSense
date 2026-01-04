#ifndef MEASUREMENTS_H
#define MEASUREMENTS_H

#include <Arduino.h>

float getCurrentWaterLevel();
int measureDistance();
int calculateWaterLevel(int distance);
void updateWaterLevel();
void updateAlarmStates(float currentDistance);
void ultrasonicTask();

#endif // MEASUREMENTS_H
