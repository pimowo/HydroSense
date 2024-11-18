// Button.h
#ifndef BUTTON_H
#define BUTTON_H

#include <Arduino.h>  // Add this for HIGH, LOW definitions
#include "Constants.h"
#include "SystemStatus.h"

// Add these constants if not in Constants.h
#ifndef DEBOUNCE_DELAY
#define DEBOUNCE_DELAY 50  // ms
#endif

struct ButtonState {
    int lastState = HIGH;
    unsigned long pressedTime = 0;
    unsigned long releasedTime = 0;  // Added missing member
    bool isLongDetected = false;
};

extern ButtonState buttonState;
extern void playConfirmationSound();  // Forward declaration

void setupButton();
void handleButton();

#endif