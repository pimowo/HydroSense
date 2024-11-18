#ifndef BUTTON_H
#define BUTTON_H

#include <Arduino.h>  // Add this for HIGH definition
#include "Constants.h"
#include "SystemStatus.h"

const unsigned long DEBOUNCE_DELAY = 50;  // Add debounce constant

struct ButtonState {
    int lastState = HIGH;
    unsigned long pressedTime = 0;
    unsigned long releasedTime = 0;  // Add missing member
    bool isLongDetected = false;
};

extern ButtonState buttonState;
extern void playConfirmationSound();  // Add missing declaration

void setupButton();
void handleButton();

#endif