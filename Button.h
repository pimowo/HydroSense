#ifndef BUTTON_H
#define BUTTON_H

#include <Arduino.h>
#include "SystemStatus.h"
#include "HomeAssistant.h"

struct ButtonState {
    unsigned long pressedTime = 0;
    unsigned long releasedTime = 0;
    bool lastState = HIGH;
};

extern ButtonState buttonState;

void handleButton();
void setupPin();

// Definicje stałych
const unsigned long DEBOUNCE_DELAY = 50;
const unsigned long LONG_PRESS_TIME = 1000;

#endif