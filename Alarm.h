#ifndef ALARM_H
#define ALARM_H

#include <Arduino.h>
#include "Pins.h"
#include "Config.h"
#include "SystemStatus.h"
#include "HomeAssistant.h"

// Deklaracje funkcji
void playShortWarningSound();
void playConfirmationSound();
void checkAlarmConditions();
void updateAlarmStates(float currentDistance);

// Stałe
const float TANK_EMPTY = 100.0;     // Poziom pustego zbiornika w cm
const float RESERVE_LEVEL = 80.0;   // Poziom rezerwy w cm
const float HYSTERESIS = 2.0;       // Histereza w cm

// Makro do debugowania
#ifdef DEBUG_MODE
  #define DEBUG_PRINT(x) Serial.println(x)
#else
  #define DEBUG_PRINT(x)
#endif

#endif