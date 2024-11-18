#include "Button.h"
#include "Pins.h"
#include <Arduino.h>
#include "GlobalDeclarations.h"

ButtonState buttonState;

void setupPin() {
    pinMode(PRZYCISK_PIN, INPUT_PULLUP);
    pinMode(POMPA_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(PIN_WATER_LEVEL, INPUT_PULLUP);
    pinMode(PIN_ULTRASONIC_TRIG, OUTPUT);
    pinMode(PIN_ULTRASONIC_ECHO, INPUT);
}

void handleButton() {
    static unsigned long lastDebounceTime = 0;
    static bool lastReading = HIGH;
    
    bool reading = digitalRead(PRZYCISK_PIN);

    if (reading != lastReading) {
        lastDebounceTime = millis();
    }

    if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
        if (reading != buttonState.lastState) {
            buttonState.lastState = reading;

            if (reading == LOW) {  // Przycisk naciśnięty
                buttonState.pressedTime = millis();
            } else {  // Przycisk zwolniony
                buttonState.releasedTime = millis();

                // Sprawdź czy to było krótkie naciśnięcie
                if (buttonState.releasedTime - buttonState.pressedTime < LONG_PRESS_TIME) {
                    // Przełącz tryb serwisowy
                    systemStatus.isServiceMode = !systemStatus.isServiceMode;
                    playConfirmationSound();
                    switchService.setState(systemStatus.isServiceMode, true);

                    Serial.printf("Tryb serwisowy: %s (przez przycisk)\n",
                                systemStatus.isServiceMode ? "WŁĄCZONY" : "WYŁĄCZONY");

                    if (!systemStatus.isServiceMode) {
                        digitalWrite(POMPA_PIN, LOW);  // Wyłącz pompę
                        systemStatus.pumpActive = false;
                        sensorPump.setValue("OFF");    // Aktualizacja w HA
                    }
                }
            }
        }
    }
    lastReading = reading;
}