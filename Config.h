#ifndef CONFIG_H
#define CONFIG_H

struct Config {
    bool soundEnabled = true;  // Domyślnie dźwięk włączony
    // Dodaj inne ustawienia konfiguracyjne tutaj
};

extern Config config;

// struct SystemStatus {
//   // Stan urządzenia
//   bool isServiceEnabled = true;
//   bool isPumpEnabled = true;
//   bool isSoundEnabled = true;
//   bool isAlarmActive = false;
//   bool isPumpRunning = false;

//   // Pomiary i liczniki
//   float currentWaterLevel = 0;
//   float currentDistance = 0;
//   unsigned long pumpStartTime = 0;
//   unsigned long lastMeasurementTime = 0;
//   unsigned long lastUpdateTime = 0;

//   // Alarmy
//   bool isLowWaterAlarm = false;
//   bool isHighWaterAlarm = false;
//   bool isPumpAlarm = false;

//   // Stan połączenia
//   bool isWiFiConnected = false;
//   bool isMQTTConnected = false;
// };

// SystemStatus systemStatus;

// // eeprom
// struct Config {
//   uint8_t version;    // Wersja konfiguracji
//   bool soundEnabled;  // Status dźwięku (włączony/wyłączony)
//   char checksum;      // Suma kontrolna
// };
// Config config;

// // Struktura dla obsługi przycisku
// struct ButtonState {
//   bool lastState;                   // Poprzedni stan przycisku
//   unsigned long pressedTime = 0;    // Czas wciśnięcia przycisku
//   unsigned long releasedTime = 0;   // Czas puszczenia przycisku
//   bool isLongPressHandled = false;  // Flaga obsłużonego długiego naciśnięcia
//   bool isInitialized = false;
// };
// ButtonState buttonState;

// // Struktura dla dźwięków alarmowych
// struct AlarmTone {
//   uint16_t frequency;      // Częstotliwość dźwięku
//   uint16_t duration;       // Czas trwania
//   uint8_t repeats;         // Liczba powtórzeń
//   uint16_t pauseDuration;  // Przerwa między powtórzeniami
// };

// struct Status {
//   bool soundEnabled;
//   bool waterAlarmActive;
//   bool waterReserveActive;
//   bool isPumpActive;
//   bool isPumpDelayActive;
//   bool pumpSafetyLock;
//   bool isServiceMode;
//   unsigned long pumpStartTime;
//   unsigned long pumpDelayStartTime;
//   unsigned long lastSoundAlert;
// };
// Status status;

#endif