// HomeAssistant.cpp
#include "HomeAssistant.h"
#include "Constants.h"
#include "Pins.h"

SystemStatus systemStatus;  // Deklaracja zmiennej globalnej

void setupHA() {
    switchService.setState(systemStatus.isServiceMode);
}

void firstUpdateHA() {
    float initialDistance = measureDistance();
    systemStatus.waterAlarmActive = (initialDistance >= TANK_EMPTY);
    systemStatus.waterReserveActive = (initialDistance >= RESERVE_LEVEL);
}

void onServiceSwitchCommand(bool state, HASwitch* s) {
    systemStatus.isServiceMode = state;
    if (!state) {
        digitalWrite(POMPA_PIN, LOW);
    }
}

void onSoundSwitchCommand(bool state, HASwitch* s) {
    systemStatus.soundEnabled = state;
}