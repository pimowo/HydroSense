// Alarm.cpp
#include "Alarm.h"
#include "Constants.h"
#include "Pins.h"

extern SystemStatus systemStatus;
extern ConfigManager configManager;

void playShortWarningSound() {
    if (configManager.config.soundEnabled) {
        tone(BUZZER_PIN, 2000, 100);
    }
}

void playConfirmationSound() {
    if (configManager.config.soundEnabled) {
        tone(BUZZER_PIN, 2000, 200);
    }
}

void checkAlarmConditions() {
    unsigned long currentTime = millis();
    if (currentTime - systemStatus.lastSoundAlert >= SOUND_ALERT_INTERVAL) {
        if (configManager.config.soundEnabled && 
            (systemStatus.pumpSafetyLock || systemStatus.isServiceMode)) {
            playShortWarningSound();
            systemStatus.lastSoundAlert = currentTime;
        }
    }
}

void updateAlarmStates(float currentDistance) {
    if (currentDistance >= TANK_EMPTY && !systemStatus.waterAlarmActive) {
        systemStatus.waterAlarmActive = true;
    }
    else if (currentDistance < (TANK_EMPTY - HYSTERESIS) && systemStatus.waterAlarmActive) {
        systemStatus.waterAlarmActive = false;
    }

    if (currentDistance >= RESERVE_LEVEL && !systemStatus.waterReserveActive) {
        systemStatus.waterReserveActive = true;
    }
    else if (currentDistance < (RESERVE_LEVEL - HYSTERESIS) && systemStatus.waterReserveActive) {
        systemStatus.waterReserveActive = false;
    }
}