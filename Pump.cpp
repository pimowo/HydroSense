// Pump.cpp
#include "Pump.h"

// Kontrola pompy - funkcja zarządzająca pracą pompy i jej zabezpieczeniami
void updatePump() {
  // Odczyt stanu czujnika poziomu wody
  // LOW = brak wody - należy uzupełnić
  // HIGH = woda obecna - stan normalny
  bool waterPresent = (digitalRead(PIN_WATER_LEVEL) == LOW);
  sensorWater.setValue(waterPresent ? "ON" : "OFF");

  // Zabezpieczenie przed przepełnieniem licznika millis()
  unsigned long currentMillis = millis();  // Dodane: zapisanie aktualnego czasu

  if (currentMillis < status.pumpStartTime) {
    status.pumpStartTime = currentMillis;
  }

  if (currentMillis < status.pumpDelayStartTime) {
    status.pumpDelayStartTime = currentMillis;
  }

  // --- ZABEZPIECZENIE 1: Tryb serwisowy ---
  if (status.isServiceMode) {
    if (status.isPumpActive) {
      stopPump();
    }
    return;
  }

  // --- ZABEZPIECZENIE 2: Maksymalny czas pracy ---
  if (status.isPumpActive && (currentMillis - status.pumpStartTime > PUMP_WORK_TIME * 1000)) {
    stopPump();
    status.pumpSafetyLock = true;
    switchPumpAlarm.setState(true);
    DEBUG_PRINT(F("ALARM: Pompa pracowała za długo - aktywowano blokadę bezpieczeństwa!"));
    return;
  }

  // --- ZABEZPIECZENIE 3: Blokady bezpieczeństwa ---
  if (status.pumpSafetyLock || status.waterAlarmActive) {
    if (status.isPumpActive) {
      stopPump();
    }
    return;
  }

  // --- ZABEZPIECZENIE 4: Ochrona przed przepełnieniem ---
  if (!waterPresent && status.isPumpActive) {
    stopPump();
    status.isPumpDelayActive = false;
    switchPumpAlarm.setState(true, true);  // Wymuś aktualizację stanu na ON w HA
    return;
  }

  // --- LOGIKA WŁĄCZANIA POMPY ---
  if (waterPresent && !status.isPumpActive && !status.isPumpDelayActive) {
    status.isPumpDelayActive = true;
    status.pumpDelayStartTime = currentMillis;
    return;
  }

  // Po upływie opóźnienia, włącz pompę
  if (status.isPumpDelayActive && !status.isPumpActive) {
    if (currentMillis - status.pumpDelayStartTime >= (PUMP_DELAY * 1000)) {
      startPump();
    }
  }
}

// Ztrzymanie pompy
void stopPump() {
  digitalWrite(POMPA_PIN, LOW);
  status.isPumpActive = false;
  status.pumpStartTime = 0;
  sensorPump.setValue("OFF");
  DEBUG_PRINT(F("Pompa zatrzymana"));
}

// Uruchomienienie pompy
void startPump() {
  digitalWrite(POMPA_PIN, HIGH);
  status.isPumpActive = true;
  status.pumpStartTime = millis();
  status.isPumpDelayActive = false;
  sensorPump.setValue("ON");
  DEBUG_PRINT(F("Pompa uruchomiona"));
}

// Funkcja obsługuje zdarzenie resetu alarmu pompy
// Jest wywoływana gdy użytkownik zmieni stan przełącznika alarmu w interfejsie HA
//
// Parametry:
// - state: stan przełącznika (true = alarm aktywny, false = reset alarmu)
// - sender: wskaźnik do obiektu przełącznika w HA (niewykorzystywany)
void onPumpAlarmCommand(bool state, HASwitch* sender) {
  // Reset blokady bezpieczeństwa pompy następuje tylko gdy przełącznik
  // zostanie ustawiony na false (wyłączony)
  if (!state) {
    playConfirmationSound();          // Sygnał potwierdzenia zmiany trybu
    status.pumpSafetyLock = false;    // Wyłącz blokadę bezpieczeństwa pompy
    switchPumpAlarm.setState(false);  // Aktualizuj stan przełącznika w HA na OFF
  }
}