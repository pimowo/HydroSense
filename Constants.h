// Constants.h
#ifndef CONSTANTS_H
#define CONSTANTS_H

// Time constants
const unsigned long LONG_PRESS_TIME = 1000;      // ms
const unsigned long WIFI_CHECK_INTERVAL = 30000; // ms
const unsigned long SOUND_ALERT_INTERVAL = 60000; // ms

// Water level measurements (mm)
const float TANK_EMPTY = 510.0;     // Distance when tank is empty
const float RESERVE_LEVEL = 450.0;  // Water reserve level
const float HYSTERESIS = 10.0;      // Level change hysteresis

#endif