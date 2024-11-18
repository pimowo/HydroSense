#ifndef CONSTANTS_H
#define CONSTANTS_H

// Time constants
const unsigned long LONG_PRESS_TIME = 1000;  // ms

// Tank measurements (in mm)
const float TANK_EMPTY = 510.0;      // Odległość gdy zbiornik jest pusty
const float RESERVE_LEVEL = 450.0;   // Poziom rezerwy wody
const float HYSTERESIS = 10.0;       // Histereza przy zmianach poziomu

#endif