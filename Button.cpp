// Button.cpp
#include "Button.h"

// Obsługa przycisku
/**
 * Funkcja obsługująca fizyczny przycisk na urządzeniu
 * 
 * Obsługuje dwa tryby naciśnięcia:
 * - Krótkie (< 1s): przełącza tryb serwisowy
 * - Długie (≥ 1s): kasuje blokadę bezpieczeństwa pompy
 */
// void handleButton() {
void handleButton() {
  static unsigned long lastDebounceTime = 0;
  static bool lastReading = HIGH;
  const unsigned long DEBOUNCE_DELAY = 50;  // 50ms debounce

  bool reading = digitalRead(PRZYCISK_PIN);

  // Jeśli odczyt się zmienił, zresetuj timer debounce
  if (reading != lastReading) {
    lastDebounceTime = millis();
  }

  // Kontynuuj tylko jeśli minął czas debounce
  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    // Jeśli stan się faktycznie zmienił po debounce
    if (reading != buttonState.lastState) {
      buttonState.lastState = reading;

      if (reading == LOW) {  // Przycisk naciśnięty
        buttonState.pressedTime = millis();
        buttonState.isLongPressHandled = false;  // Reset flagi długiego naciśnięcia
      } else {                                   // Przycisk zwolniony
        buttonState.releasedTime = millis();

        // Sprawdzenie czy to było krótkie naciśnięcie
        if (buttonState.releasedTime - buttonState.pressedTime < LONG_PRESS_TIME) {
          // Przełącz tryb serwisowy
          status.isServiceMode = !status.isServiceMode;
          playConfirmationSound();                             // Sygnał potwierdzenia zmiany trybu
          switchService.setState(status.isServiceMode, true);  // force update w HA

          // Log zmiany stanu
          Serial.printf("Tryb serwisowy: %s (przez przycisk)\n",
                        status.isServiceMode ? "WŁĄCZONY" : "WYŁĄCZONY");

          // Jeśli włączono tryb serwisowy podczas pracy pompy
          if (status.isServiceMode && status.isPumpActive) {
            digitalWrite(POMPA_PIN, LOW);  // Wyłącz pompę
            status.isPumpActive = false;   // Reset flagi aktywności
            status.pumpStartTime = 0;      // Reset czasu startu
            sensorPump.setValue("OFF");    // Aktualizacja w HA
          }
        }
      }
    }

    // Obsługa długiego naciśnięcia (reset blokady pompy)
    if (reading == LOW && !buttonState.isLongPressHandled) {
      if (millis() - buttonState.pressedTime >= LONG_PRESS_TIME) {
        ESP.wdtFeed();                          // Reset przy długim naciśnięciu
        status.pumpSafetyLock = false;          // Zdjęcie blokady pompy
        playConfirmationSound();                // Sygnał potwierdzenia zmiany trybu
        switchPumpAlarm.setState(false, true);  // force update w HA
        buttonState.isLongPressHandled = true;  // Oznacz jako obsłużone
        DEBUG_PRINT("Alarm pompy skasowany");
      }
    }
  }

  lastReading = reading;  // Zapisz ostatni odczyt dla następnego porównania
  yield();                // Oddaj sterowanie systemowi
}

// Konfiguracja kierunków pinów i stanów początkowych
void setupPin() {
  pinMode(PIN_ULTRASONIC_TRIG, OUTPUT);    // Wyjście - trigger czujnika ultradźwiękowego
  pinMode(PIN_ULTRASONIC_ECHO, INPUT);     // Wejście - echo czujnika ultradźwiękowego
  digitalWrite(PIN_ULTRASONIC_TRIG, LOW);  // Upewnij się że TRIG jest LOW na starcie

  pinMode(PIN_WATER_LEVEL, INPUT_PULLUP);  // Wejście z podciąganiem - czujnik poziomu
  pinMode(PRZYCISK_PIN, INPUT_PULLUP);     // Wejście z podciąganiem - przycisk
  pinMode(BUZZER_PIN, OUTPUT);             // Wyjście - buzzer
  digitalWrite(BUZZER_PIN, LOW);           // Wyłączenie buzzera
  pinMode(POMPA_PIN, OUTPUT);              // Wyjście - pompa
  digitalWrite(POMPA_PIN, LOW);            // Wyłączenie pompy
}