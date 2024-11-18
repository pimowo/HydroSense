// Alarm.cpp
#include "Alarm.h"
#include "Constants.h"
#include "Pins.h"
#include "ConfigManager.h"  // Dodane

extern SystemStatus systemStatus;
extern ConfigManager configManager;

void playShortWarningSound() {
    if (configManager.config.soundEnabled) {
        tone(BUZZER_PIN, 2000, 100);
    }
}