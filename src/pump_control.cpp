#include "pump_control.h"
#include "globals.h"
#include "pins.h"
#include "measurements.h"

void sendPumpWorkTime() {
    if (status.pumpStartTime > 0) {
        unsigned long totalWorkTime = (millis() - status.pumpStartTime) / 1000UL;
        char timeStr[16];
        itoa(totalWorkTime, timeStr, 10);
        sensorPumpWorkTime.setValue(timeStr);
    }
}

void updatePump() {
    // Zabezpieczenie przed przepełnieniem licznika millis()
    if (millis() < status.pumpStartTime) status.pumpStartTime = millis();
    if (millis() < status.pumpDelayStartTime) status.pumpDelayStartTime = millis();

    bool waterPresent = (digitalRead(PIN_WATER_LEVEL) == LOW);
    sensorWater.setValue(waterPresent ? "ON" : "OFF");

    if (status.isServiceMode) {
        if (status.isPumpActive) {
            digitalWrite(POMPA_PIN, LOW);
            status.isPumpActive = false;
            sendPumpWorkTime();
            status.pumpStartTime = 0;
            sensorPump.setValue("OFF");
        }
        return;
    }

    if (status.isPumpActive && (millis() - status.pumpStartTime > (unsigned long)config.pump_work_time * 1000UL)) {
        digitalWrite(POMPA_PIN, LOW);
        status.isPumpActive = false;
        sendPumpWorkTime();
        status.pumpStartTime = 0;
        sensorPump.setValue("OFF");
        status.pumpSafetyLock = true;
        switchPumpAlarm.setState(true);
        DEBUG_PRINTF("ALARM: Pompa pracowała za długo - aktywowano blokadę bezpieczeństwa!");
        return;
    }

    if (status.pumpSafetyLock || status.waterAlarmActive) {
        if (status.isPumpActive) {
            digitalWrite(POMPA_PIN, LOW);
            status.isPumpActive = false;
            sendPumpWorkTime();
            status.pumpStartTime = 0;
            sensorPump.setValue("OFF");
        }
        return;
    }

    // Use last measured distance to avoid blocking ultrasonic measurement here
    float dist = currentDistance;
    if (status.isPumpActive && dist >= 0 && dist >= config.tank_empty) {
        digitalWrite(POMPA_PIN, LOW);
        status.isPumpActive = false;
        sendPumpWorkTime();
        status.pumpStartTime = 0;
        status.isPumpDelayActive = false;
        sensorPump.setValue("OFF");
        switchPumpAlarm.setState(true);
        DEBUG_PRINT(F("ALARM: Zatrzymano pompę - brak wody w zbiorniku!"));
        return;
    }

    if (!waterPresent && status.isPumpActive) {
        digitalWrite(POMPA_PIN, LOW);
        status.isPumpActive = false;
        sendPumpWorkTime();
        status.pumpStartTime = 0;
        status.isPumpDelayActive = false;
        sensorPump.setValue("OFF");
        return;
    }

    if (waterPresent && !status.isPumpActive && !status.isPumpDelayActive) {
        status.isPumpDelayActive = true;
        status.pumpDelayStartTime = millis();
        return;
    }

    if (status.isPumpDelayActive && !status.isPumpActive) {
        if (millis() - status.pumpDelayStartTime >= ((unsigned long)config.pump_delay * 1000UL)) {
            digitalWrite(POMPA_PIN, HIGH);
            status.isPumpActive = true;
            status.pumpStartTime = millis();
            status.isPumpDelayActive = false;
            sensorPump.setValue("ON");
        }
    }
}
// onPumpAlarmCommand handled in ha.cpp
