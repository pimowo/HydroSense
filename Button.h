// Button.h
#ifndef BUTTON_H
#define BUTTON_H

#include "Constants.h"
#include "SystemStatus.h"

struct ButtonState {
    int lastState = HIGH;
    unsigned long pressedTime = 0;
    bool isLongDetected = false;
};

extern ButtonState buttonState;

void setupButton();
void handleButton();

#endif