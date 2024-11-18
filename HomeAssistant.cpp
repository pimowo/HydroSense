#include "HomeAssistant.h"
#include "Constants.h"
#include "SystemStatus.h"

// Funkcja konfigurująca Home Assistant
void setupHA() {
  // Konfiguracja urządzenia dla Home Assistant
  device.setName("HydroSense");           // Nazwa urządzenia
  device.setModel("HS ESP8266");          // Model urządzenia
  device.setManufacturer("PMW");          // Producent
  device.setSoftwareVersion("17.11.24");  // Wersja oprogramowania

  // Konfiguracja sensorów pomiarowych w HA
  sensorDistance.setName("Pomiar odległości");
  sensorDistance.setIcon("mdi:ruler");        // Ikona linijki
  sensorDistance.setUnitOfMeasurement("mm");  // Jednostka - milimetry

  sensorLevel.setName("Poziom wody");
  sensorLevel.setIcon("mdi:cup-water");   // Ikona poziomu wody
  sensorLevel.setUnitOfMeasurement("%");  // Jednostka - procenty

  sensorVolume.setName("Objętość wody");
  sensorVolume.setIcon("mdi:cup-water");   // Ikona wody
  sensorVolume.setUnitOfMeasurement("L");  // Jednostka - litry

  // Konfiguracja sensorów statusu w HA
  sensorPump.setName("Status pompy");
  sensorPump.setIcon("mdi:water-pump");  // Ikona pompy

  sensorWater.setName("Czujnik wody");
  sensorWater.setIcon("mdi:electric-switch");  // Ikona wody

  // Konfiguracja sensorów alarmowych w HA
  sensorAlarm.setName("Brak wody");
  sensorAlarm.setIcon("mdi:alarm-light");  // Ikona alarmu wody

  sensorReserve.setName("Rezerwa wody");
  sensorReserve.setIcon("mdi:alarm-light-outline");  // Ikona ostrzeżenia

  // Konfiguracja przełączników w HA
  switchService.setName("Serwis");
  switchService.setIcon("mdi:tools");               // Ikona narzędzi
  switchService.onCommand(onServiceSwitchCommand);  // Funkcja obsługi zmiany stanu
  switchService.setState(status.isServiceMode);     // Stan początkowy
  // Inicjalizacja stanu - domyślnie wyłączony
  status.isServiceMode = false;
  switchService.setState(false, true);  // force update przy starcie

  switchSound.setName("Dźwięk");
  switchSound.setIcon("mdi:volume-high");       // Ikona głośnika
  switchSound.onCommand(onSoundSwitchCommand);  // Funkcja obsługi zmiany stanu
  switchSound.setState(status.soundEnabled);    // Stan początkowy

  switchPumpAlarm.setName("Alarm pompy");
  switchPumpAlarm.setIcon("mdi:alert");           // Ikona alarmu
  switchPumpAlarm.onCommand(onPumpAlarmCommand);  // Funkcja obsługi zmiany stanu
}

// Wykonaj pierwszy pomiar i ustaw stany
void firstUpdateHA() {
  float initialDistance = measureDistance();

  // Ustaw początkowe stany na podstawie pomiaru
  status.waterAlarmActive = (initialDistance >= TANK_EMPTY);
  status.waterReserveActive = (initialDistance >= RESERVE_LEVEL);

  // Wymuś stan OFF na początku
  sensorAlarm.setValue("OFF");
  sensorReserve.setValue("OFF");
  switchSound.setState(false);  // Dodane - wymuś stan początkowy
  mqtt.loop();

  // Ustawienie końcowych stanów i wysyłka do HA
  sensorAlarm.setValue(status.waterAlarmActive ? "ON" : "OFF");
  sensorReserve.setValue(status.waterReserveActive ? "ON" : "OFF");
  switchSound.setState(status.soundEnabled);  // Dodane - ustaw aktualny stan dźwięku
  mqtt.loop();
}

/**
 * Funkcja obsługująca przełączanie trybu serwisowego z poziomu Home Assistant
 * 
 * @param state - nowy stan przełącznika (true = włączony, false = wyłączony)
 * @param sender - wskaźnik do obiektu przełącznika HA wywołującego funkcję
 * 
 * Działanie:
 * Przy włączaniu (state = true):
 * - Aktualizuje flagę trybu serwisowego
 * - Resetuje stan przycisku fizycznego
 * - Aktualizuje stan w Home Assistant
 * - Jeśli pompa pracowała:
 *   - Wyłącza pompę
 *   - Resetuje liczniki czasu pracy
 *   - Aktualizuje status w HA
 * 
 * Przy wyłączaniu (state = false):
 * - Wyłącza tryb serwisowy
 * - Aktualizuje stan w Home Assistant
 * - Umożliwia normalną pracę pompy według czujnika poziomu
 * - Resetuje stan opóźnienia pompy
 */
void onServiceSwitchCommand(bool state, HASwitch* sender) {
  playConfirmationSound();       // Sygnał potwierdzenia zmiany trybu
  status.isServiceMode = state;  // Ustawienie flagi trybu serwisowego
  buttonState.lastState = HIGH;  // Reset stanu przycisku

  // Aktualizacja stanu w Home Assistant
  switchService.setState(state);  // Synchronizacja stanu przełącznika

  if (state) {  // Włączanie trybu serwisowego
    if (status.isPumpActive) {
      digitalWrite(POMPA_PIN, LOW);  // Wyłączenie pompy
      status.isPumpActive = false;   // Reset flagi aktywności
      status.pumpStartTime = 0;      // Reset czasu startu
      sensorPump.setValue("OFF");    // Aktualizacja stanu w HA
    }
  } else {  // Wyłączanie trybu serwisowego
    // Reset stanu opóźnienia pompy aby umożliwić normalne uruchomienie
    status.isPumpDelayActive = false;
    status.pumpDelayStartTime = 0;
    // Normalny tryb pracy - pompa uruchomi się automatycznie
    // jeśli czujnik poziomu wykryje wodę
  }

  Serial.printf("Tryb serwisowy: %s (przez HA)\n",
                state ? "WŁĄCZONY" : "WYŁĄCZONY");
}

/**
 * Funkcja obsługująca przełączanie dźwięku alarmu z poziomu Home Assistant
 * 
 * @param state - nowy stan przełącznika (true = włączony, false = wyłączony)
 * @param sender - wskaźnik do obiektu przełącznika HA wywołującego funkcję
 * 
 * Działanie:
 * - Aktualizuje flagę stanu dźwięku
 * - Aktualizuje stan w Home Assistant
 */
void onSoundSwitchCommand(bool state, HASwitch* sender) {
  status.soundEnabled = state;  // Aktualizuj status lokalny
  config.soundEnabled = state;  // Aktualizuj konfigurację

  // Aktualizuj stan w Home Assistant
  switchSound.setState(state, true);  // force update

  // Zagraj dźwięk potwierdzenia tylko gdy włączamy dźwięk
  if (state) {
    playConfirmationSound();
  }

  Serial.printf("Zmieniono stan dźwięku na: %s\n", state ? "WŁĄCZONY" : "WYŁĄCZONY");
}