#ifndef HOME_ASSISTANT_H
#define HOME_ASSISTANT_H

#include <ArduinoHA.h>
#include "SystemStatus.h"
#include "Constants.h"

extern HADevice device;
extern HAMqtt mqtt;

// Sensor declarations
extern HASensor sensorDistance;
extern HASensor sensorLevel;
extern HASensor sensorVolume;
extern HASensor sensorPump;
extern HASensor sensorWater;
extern HASensor sensorAlarm;
extern HASensor sensorReserve;

// Switch declarations
extern HASwitch switchService;
extern HASwitch switchSound;
extern HASwitch switchPumpAlarm;

void setupHA();
void firstUpdateHA();
void updateHA();
void onServiceSwitchCommand(bool state, HASwitch* s);
void onSoundSwitchCommand(bool state, HASwitch* s);
void onPumpAlarmCommand(bool state, HASwitch* s);

#endif