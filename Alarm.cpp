// Alarm.cpp
#include "Alarm.h"

// Krótki sygnał dźwiękowy - pojedyncze piknięcie
void playShortWarningSound() {
  if (config.soundEnabled) {
    tone(BUZZER_PIN, 2000, 100);  // Krótkie piknięcie (2000Hz, 100ms)
  }
}

// Sygnał potwierdzenia
void playConfirmationSound() {
  if (config.soundEnabled) {
    tone(BUZZER_PIN, 2000, 200);  // Dłuższe piknięcie (2000Hz, 200ms)
  }
}

// Sprawdzenie warunków alarmowych
void checkAlarmConditions() {
  unsigned long currentTime = millis();

  // Sprawdź czy minęła minuta od ostatniego alarmu
  if (currentTime - status.lastSoundAlert >= 60000) {  // 60000ms = 1 minuta
    // Sprawdź czy dźwięk jest włączony i czy występuje alarm pompy lub tryb serwisowy
    if (config.soundEnabled && (status.pumpSafetyLock || status.isServiceMode)) {
      playShortWarningSound();
      status.lastSoundAlert = currentTime;

      // Debug info
      DEBUG_PRINT(F("Alarm dźwiękowy - przyczyna:"));
      if (status.pumpSafetyLock) DEBUG_PRINT(F("- Alarm pompy"));
      if (status.isServiceMode) DEBUG_PRINT(F("- Tryb serwisowy"));
    }
  }
}

void updateAlarmStates(float currentDistance) {
  // --- Obsługa alarmu krytycznie niskiego poziomu wody ---
  // Włącz alarm jeśli:
  // - odległość jest większa lub równa max (zbiornik pusty)
  // - alarm nie jest jeszcze aktywny
  if (currentDistance >= TANK_EMPTY && !status.waterAlarmActive) {
    status.waterAlarmActive = true;
    sensorAlarm.setValue("ON");
    DEBUG_PRINT("Brak wody ON");
  }
  // Wyłącz alarm jeśli:
  // - odległość spadła poniżej progu wyłączenia (z histerezą)
  // - alarm jest aktywny
  else if (currentDistance < (TANK_EMPTY - HYSTERESIS) && status.waterAlarmActive) {
    status.waterAlarmActive = false;
    sensorAlarm.setValue("OFF");
    DEBUG_PRINT("Brak wody OFF");
  }

  // --- Obsługa ostrzeżenia o rezerwie wody ---
  // Włącz ostrzeżenie o rezerwie jeśli:
  // - odległość osiągnęła próg rezerwy
  // - ostrzeżenie nie jest jeszcze aktywne
  if (currentDistance >= RESERVE_LEVEL && !status.waterReserveActive) {
    status.waterReserveActive = true;
    sensorReserve.setValue("ON");
    DEBUG_PRINT("Rezerwa ON");
  }
  // Wyłącz ostrzeżenie o rezerwie jeśli:
  // - odległość spadła poniżej progu rezerwy (z histerezą)
  // - ostrzeżenie jest aktywne
  else if (currentDistance < (RESERVE_LEVEL - HYSTERESIS) && status.waterReserveActive) {
    status.waterReserveActive = false;
    sensorReserve.setValue("OFF");
    DEBUG_PRINT("Rezerwa OFF");
  }
}