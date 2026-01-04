#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <ArduinoHA.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>

#include "config.h"
#include "status.h"
#include "button.h"
#include "timers.h"

extern WiFiClient client;
extern HADevice device;
extern HAMqtt mqtt;
extern ESP8266WebServer server;
extern WebSocketsServer webSocket;

extern HASensor sensorDistance;
extern HASensor sensorLevel;
extern HASensor sensorVolume;
extern HASensor sensorPumpWorkTime;
extern HASensor sensorPump;
extern HASensor sensorWater;
extern HASensor sensorAlarm;
extern HASensor sensorReserve;

extern HASwitch switchPumpAlarm;
extern HASwitch switchService;
extern HASwitch switchSound;

// HA module defines sensors/switches
// (they are defined in ha.cpp)

// Shared constants used by measurement module
extern const int HYSTERESIS;
extern const int SENSOR_MIN_RANGE;
extern const int SENSOR_MAX_RANGE;
extern const float EMA_ALPHA;
extern const int SENSOR_AVG_SAMPLES;
extern const unsigned long ULTRASONIC_TIMEOUT;

// Shared runtime state used by measurements
extern float lastFilteredDistance;
extern float lastReportedDistance;
extern unsigned long lastMeasurement;
extern float currentDistance;
extern float volume;

// Debug macros
#ifndef DEBUG
#define DEBUG 0
#endif

#if DEBUG
	#define DEBUG_PRINT(x) Serial.println(x)
	#define DEBUG_PRINTF(format, ...) Serial.printf(format, __VA_ARGS__)
#else
	#define DEBUG_PRINT(x)
	#define DEBUG_PRINTF(format, ...)
#endif

// Globalne stałe/funcje dostępne w programie
extern const char* SOFTWARE_VERSION;
void playShortWarningSound();
void playConfirmationSound();
void factoryReset();

#endif // GLOBALS_H
