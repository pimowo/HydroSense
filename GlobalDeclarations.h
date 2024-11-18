#ifndef GLOBAL_DECLARATIONS_H
#define GLOBAL_DECLARATIONS_H

#include <Arduino.h>
#include "SystemStatus.h"
#include "HomeAssistant.h"

// Pin definitions
const int BUZZER_PIN = 5;  // Adjust pin number as needed

// Debug macro
#ifdef DEBUG_MODE
  #define DEBUG_PRINT(x) Serial.println(x)
#else
  #define DEBUG_PRINT(x)
#endif

// External declarations
extern SystemStatus systemStatus;
extern HASwitch switchService;
extern HASensor sensorPump;

void playConfirmationSound();
float measureDistance();

#endif