#ifndef HOME_ASSISTANT_H
#define HOME_ASSISTANT_H

#include <ArduinoHA.h>
#include "SystemStatus.h"

// Deklaracje funkcji
void setupHA();
void firstUpdateHA();
void onServiceSwitchCommand(bool state, HASwitch* sender);
void onSoundSwitchCommand(bool state, HASwitch* sender);

// Deklaracje zewnętrzne
extern HADevice device;
extern HAMqtt mqtt;
extern HASensor sensorDistance;
extern HASensor sensorLevel;
extern HASensor sensorVolume;
extern HASensor sensorPump;
extern HASensor sensorWater;
extern HASensor sensorAlarm;
extern HASensor sensorReserve;
extern HASwitch switchService;
extern HASwitch switchSound;

#endif