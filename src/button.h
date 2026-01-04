#ifndef BUTTON_H
#define BUTTON_H

#include <Arduino.h>

struct ButtonState {
    bool lastState;
    bool isInitialized = false;
    bool isLongPressHandled = false;
    unsigned long pressedTime = 0;
    unsigned long releasedTime = 0;
};

extern ButtonState buttonState;

#endif // BUTTON_H
