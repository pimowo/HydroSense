
// ** BIBLIOTEKI **
#include <Arduino.h>
#include <ArduinoHA.h>
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>
#include <ESP8266HTTPUpdateServer.h>

#include "pins.h"
#include "config.h"
#include "status.h"
#include "button.h"
#include "timers.h"
#include "globals.h"
#include "measurements.h"
#include "ha.h"
#include "pump_control.h"
#include "network.h"



const char* SOFTWARE_VERSION = "26.11.24";

// USTAWIENIA CZASOWE
const unsigned long ULTRASONIC_TIMEOUT = 50;
const unsigned long MEASUREMENT_INTERVAL = 60000;
const unsigned long WATCHDOG_TIMEOUT = 8000;
const unsigned long LONG_PRESS_TIME = 1000;
const unsigned long MQTT_LOOP_INTERVAL = 100;
const unsigned long OTA_CHECK_INTERVAL = 1000;
const unsigned long MQTT_RETRY_INTERVAL = 10000;
const unsigned long WIFI_RETRY_INTERVAL = 10000;
const unsigned long MILLIS_OVERFLOW_THRESHOLD = 4294967295U - 60000;

// globalne instancje `config`, `status`, `buttonState` i `timers`
// są zadeklarowane w osobnych plikach źródłowych (config.cpp, status.cpp, button.cpp, timers.cpp)

// Prototypy funkcji używanych wcześniej w pliku
// (pomiarów zostały przeniesione do measurements.cpp)
void onServiceSwitchCommand(bool state, HASwitch* sender);
void onSoundSwitchCommand(bool state, HASwitch* sender);

// ** FILTROWANIE I POMIARY **

// Parametry czujnika ultradźwiękowego i obliczeń
const int HYSTERESIS = 10;  // Histereza przy zmianach poziomu (mm)
const int SENSOR_MIN_RANGE = 20;    // Minimalny zakres czujnika (mm)
const int SENSOR_MAX_RANGE = 1020;  // Maksymalny zakres czujnika (mm)
const float EMA_ALPHA = 0.2f;       // Współczynnik wygładzania dla średniej wykładniczej (0-1)
const int SENSOR_AVG_SAMPLES = 3;   // Liczba próbek do uśrednienia pomiaru

float lastFilteredDistance = 0;     // Dla filtra EMA (Exponential Moving Average)
float lastReportedDistance = 0;     // Ostatnia zgłoszona wartość odległości
unsigned long lastMeasurement = 0;  // Ostatni czas pomiaru
float currentDistance = 0;          // Bieżąca odległość od powierzchni wody (mm)
float volume = 0;                   // Objętość wody w akwarium (l)
unsigned long pumpStartTime = 0;    // Czas rozpoczęcia pracy pompy
float waterLevelBeforePump = 0;     // Poziom wody przed uruchomieniem pompy

// ** INSTANCJE URZĄDZEŃ I USŁUG **

// Wi-Fi, MQTT i Home Assistant
WiFiClient client;              // Klient połączenia WiFi
HADevice device("HydroSense");  // Definicja urządzenia dla Home Assistant
HAMqtt mqtt(client, device);    // Klient MQTT dla Home Assistant

// Serwer HTTP i WebSockets
ESP8266WebServer server(80);     // Tworzenie instancji serwera HTTP na porcie 80
WebSocketsServer webSocket(81);  // Tworzenie instancji serwera WebSockets na porcie 81

// Czujniki i przełączniki dla Home Assistant

// Sensory i przełączniki są zadeklarowane w `globals.h` i zdefiniowane w `ha.cpp`

// ** FUNKCJE I METODY SYSTEMOWE **

// Reset do ustawień fabrycznych
void factoryReset() {    
    WiFi.disconnect(true);  // true = kasuj zapisane ustawienia
    WiFi.mode(WIFI_OFF);   
    delay(100);
    
    WiFiManager wm;
    wm.resetSettings();
    ESP.eraseConfig();
    
    setDefaultConfig();
    saveConfig();
    
    delay(100);
    ESP.reset();
}

// Reset urządzenia
void rebootDevice() {
    ESP.restart();
}

// Przepełnienie licznika millis()
void handleMillisOverflow() {
    unsigned long currentMillis = millis();
    
    // Sprawdź przepełnienie dla wszystkich timerów
    if (currentMillis < status.pumpStartTime) status.pumpStartTime = 0;
    if (currentMillis < status.pumpDelayStartTime) status.pumpDelayStartTime = 0;
    if (currentMillis < status.lastSoundAlert) status.lastSoundAlert = 0;
    if (currentMillis < status.lastSuccessfulMeasurement) status.lastSuccessfulMeasurement = 0;
    if (currentMillis < lastMeasurement) lastMeasurement = 0;
    
    // Jeśli zbliża się przepełnienie, zresetuj wszystkie timery
    if (currentMillis > MILLIS_OVERFLOW_THRESHOLD) {
        status.pumpStartTime = 0;
        status.pumpDelayStartTime = 0;
        status.lastSoundAlert = 0;
        status.lastSuccessfulMeasurement = 0;
        lastMeasurement = 0;
        
        DEBUG_PRINT(F("Reset timerów - zbliża się przepełnienie millis()"));
    }
}

// Funkcje związane z konfiguracją (setDefaultConfig, loadConfig, saveConfig,
// calculateChecksum) zostały przeniesione do `config.cpp`.

// ** FUNKCJE DŹWIĘKOWE **

// Odtwórz krótki dźwięk ostrzegawczy
void playShortWarningSound() {
    if (config.soundEnabled) {
        tone(BUZZER_PIN, 2000, 100); // Krótkie piknięcie (2000Hz, 100ms)
    }
}

// Odtwórz dźwięk potwierdzenia
void playConfirmationSound() {
    if (config.soundEnabled) {
        tone(BUZZER_PIN, 2000, 200); // Dłuższe piknięcie (2000Hz, 200ms)
    }
}

// ** FUNKCJE ALARMÓW I STEROWANIA POMPĄ **

// Sprawdź warunki alarmowe
void checkAlarmConditions() {
    unsigned long currentTime = millis();

    // Sprawdź czy minęła minuta od ostatniego alarmu
    if (currentTime - status.lastSoundAlert >= 60000) { // 60000ms = 1 minuta
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

// Alarm handling moved to measurements.cpp

// Pump control moved to pump_control.cpp

// ** FUNKCJE WI-FI I MQTT **

// Reset ustawień Wi-Fi
void resetWiFiSettings() {
    DEBUG_PRINT(F("Rozpoczynam kasowanie ustawień WiFi..."));
    
    // Najpierw rozłącz WiFi i wyczyść wszystkie zapisane ustawienia
    WiFi.disconnect(false, true);  // false = nie wyłączaj WiFi, true = kasuj zapisane ustawienia
    
    // Upewnij się, że WiFi jest w trybie stacji
    WiFi.mode(WIFI_STA);
    
    // Reset przez WiFiManager
    WiFiManager wm;
    wm.resetSettings();
    
    DEBUG_PRINT(F("Ustawienia WiFi zostały skasowane"));
    delay(100);
}

// Network functions moved to network.cpp

// Home Assistant setup moved to ha.cpp

// ** FUNKCJE ZWIĄZANE Z PINAMI **

// Konfiguracja pinów wejścia/wyjścia
void setupPin() {
    pinMode(PIN_ULTRASONIC_TRIG, OUTPUT);  // Wyjście - trigger czujnika ultradźwiękowego
    pinMode(PIN_ULTRASONIC_ECHO, INPUT);  // Wejście - echo czujnika ultradźwiękowego
    digitalWrite(PIN_ULTRASONIC_TRIG, LOW);  // Upewnij się że TRIG jest LOW na starcie
    
    pinMode(PIN_WATER_LEVEL, INPUT_PULLUP);  // Wejście z podciąganiem - czujnik poziomu
    pinMode(PRZYCISK_PIN, INPUT_PULLUP);  // Wejście z podciąganiem - przycisk
    pinMode(BUZZER_PIN, OUTPUT);  // Wyjście - buzzer
    digitalWrite(BUZZER_PIN, LOW);  // Wyłączenie buzzera
    pinMode(POMPA_PIN, OUTPUT);  // Wyjście - pompa
    digitalWrite(POMPA_PIN, LOW);  // Wyłączenie pompy
}

// Odtwarzaj melodię powitalną
void welcomeMelody() {
    tone(BUZZER_PIN, 1397, 100);  // F6
    delay(150);
    tone(BUZZER_PIN, 1568, 100);  // G6
    delay(150);
    tone(BUZZER_PIN, 1760, 150);  // A6
    delay(200);
}

// Wyślij pierwszą aktualizację stanu do Home Assistant
void firstUpdateHA() {
    float initialDistance = measureDistance();
    
    // Ustaw początkowe stany na podstawie pomiaru
    status.waterAlarmActive = (initialDistance >= config.tank_empty);
    status.waterReserveActive = (initialDistance >= config.reserve_level);
    
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

    sensorPumpWorkTime.setValue("0");
    mqtt.loop();
}

// ** FUNKCJE ZWIĄZANE Z PRZYCISKIEM **

// Obsługa przycisku
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
            } else {  // Przycisk zwolniony
                buttonState.releasedTime = millis();
                
                // Sprawdzenie czy to było krótkie naciśnięcie
                if (buttonState.releasedTime - buttonState.pressedTime < LONG_PRESS_TIME) {
                    // Przełącz tryb serwisowy
                    status.isServiceMode = !status.isServiceMode;
                    playConfirmationSound();  // Sygnał potwierdzenia zmiany trybu
                    switchService.setState(status.isServiceMode, true);  // force update w HA
                    
                    // Log zmiany stanu
                    DEBUG_PRINTF("Tryb serwisowy: %s (przez przycisk)\n", status.isServiceMode ? "WŁĄCZONY" : "WYŁĄCZONY");
                    
                    // Jeśli włączono tryb serwisowy podczas pracy pompy
                    if (status.isServiceMode && status.isPumpActive) {
                        digitalWrite(POMPA_PIN, LOW);  // Wyłącz pompę
                        status.isPumpActive = false;  // Reset flagi aktywności
                        status.pumpStartTime = 0;  // Reset czasu startu
                        sensorPump.setValue("OFF");  // Aktualizacja w HA
                    }
                }
            }
        }
        
        // Obsługa długiego naciśnięcia (reset blokady pompy)
        if (reading == LOW && !buttonState.isLongPressHandled) {
            if (millis() - buttonState.pressedTime >= LONG_PRESS_TIME) {
                ESP.wdtFeed();  // Reset przy długim naciśnięciu
                status.pumpSafetyLock = false;  // Zdjęcie blokady pompy
                playConfirmationSound();  // Sygnał potwierdzenia zmiany trybu
                switchPumpAlarm.setState(false, true);  // force update w HA
                buttonState.isLongPressHandled = true;  // Oznacz jako obsłużone
                DEBUG_PRINT("Alarm pompy skasowany");
            }
        }
    }
    
    lastReading = reading;  // Zapisz ostatni odczyt dla następnego porównania
    yield();  // Oddaj sterowanie systemowi
}

// HA switch handlers implemented in ha.cpp

// Network handlers and web server moved to network.cpp
// ** FUNKCJE POMOCNICZE **

// Funkcje obliczeniowe dla poziomu wody i pomiaru

// Pomiary i obliczenia zostały przeniesione do `measurements.cpp`.

// ** Funkcja setup - inicjalizacja urządzeń i konfiguracja **

void setup() {
    ESP.wdtEnable(WATCHDOG_TIMEOUT);  // Aktywacja watchdoga
    Serial.begin(115200);  // Inicjalizacja portu szeregowego
    DEBUG_PRINTF("\nHydroSense start...");  // Komunikat startowy
    
    // Wczytaj konfigurację na początku
    if (!loadConfig()) {
        DEBUG_PRINTF("Błąd wczytywania konfiguracji - używam ustawień domyślnych");
        setDefaultConfig();
        saveConfig();  // Zapisz domyślną konfigurację do EEPROM
    }
    
    setupPin();  // Ustawienia GPIO
    setupWiFi();  // Nawiązanie połączenia WiFi
    setupWebServer();  // Serwer www    
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);

    // Próba połączenia MQTT
    DEBUG_PRINT("Rozpoczynam połączenie MQTT...");
    connectMQTT();

    setupHA();  // Konfiguracja Home Assistant
   
    // Wczytaj konfigurację z EEPROM
    if (loadConfig()) {        
        status.soundEnabled = config.soundEnabled;  // Synchronizuj stan dźwięku z wczytanej konfiguracji
    }
    
    firstUpdateHA();  // Wyślij pierwsze odczyty do Home Assistant
    status.lastSoundAlert = millis();
    
    // Konfiguracja OTA
    ArduinoOTA.setHostname("HydroSense");  // Ustaw nazwę urządzenia
    ArduinoOTA.setPassword("hydrosense");  // Ustaw hasło dla OTA
    ArduinoOTA.begin();  // Uruchom OTA    
    
    DEBUG_PRINT("Setup zakończony pomyślnie!");

    // Powitanie
    if (status.soundEnabled) {  // Gdy jest włączony dzwięk
        welcomeMelody();  //  to odegraj muzyczkę, że program poprawnie wystartował
    }  
        
    // Ustawienia fabryczne    
    // Czekaj 2 sekundy na wciśnięcie przycisku
    // unsigned long startTime = millis();
    // while(millis() - startTime < 2000) {
    //     if(digitalRead(PRZYCISK_PIN) == LOW) {
    //         playConfirmationSound();
    //         factoryReset();
    //     }
    // }
}

// ** Funkcja loop - główny cykl pracy urządzenia **

void loop() {
    unsigned long currentMillis = millis();  // Pobierz bieżącą wartość millis()

    // KRYTYCZNE OPERACJE CZASOWE
    handleMillisOverflow();  // Obsługa przepełnienia millis()
    ultrasonicTask(); // progresja stanu pomiaru ultradźwiękowego (nieblokująca)
    updatePump();   // Aktualizacja stanu pompy
    ESP.wdtFeed();  // Reset watchdog timer ESP
    yield();        // Umożliwienie przetwarzania innych zadań

    // BEZPOŚREDNIA INTERAKCJA
    handleButton();          // Obsługa naciśnięcia przycisku
    checkAlarmConditions();  // Sprawdzenie warunków alarmowych
    server.handleClient();   // Obsługa serwera WWW
    webSocket.loop();

    // POMIARY I AKTUALIZACJE
    if (currentMillis - timers.lastMeasurement >= MEASUREMENT_INTERVAL) {
        updateWaterLevel();                      // Aktualizacja poziomu wody
        timers.lastMeasurement = currentMillis;  // Aktualizacja znacznika czasu ostatniego pomiaru
    }

    // KOMUNIKACJA
    if (currentMillis - timers.lastMQTTLoop >= MQTT_LOOP_INTERVAL) {
        mqtt.loop();  // Obsługa pętli MQTT
        timers.lastMQTTLoop = currentMillis;  // Aktualizacja znacznika czasu ostatniej pętli MQTT
    }

    if (currentMillis - timers.lastOTACheck >= OTA_CHECK_INTERVAL) {
        ArduinoOTA.handle();                  // Obsługa aktualizacji OTA
        timers.lastOTACheck = currentMillis;  // Aktualizacja znacznika czasu ostatniego sprawdzenia OTA
    }

    // ZARZĄDZANIE POŁĄCZENIEM (z backoffem)
    handleWiFiBackoff();

    if (!mqtt.isConnected() && 
        (currentMillis - timers.lastMQTTRetry >= MQTT_RETRY_INTERVAL)) {
        timers.lastMQTTRetry = currentMillis;                          // Aktualizacja znacznika czasu ostatniej próby połączenia MQTT
        DEBUG_PRINT(F("Brak połączenia MQTT - próba połączenia..."));  // Wydrukuj komunikat debugowania
        if (!mqtt.begin(config.mqtt_server, 1883, config.mqtt_user, config.mqtt_password)) {
            DEBUG_PRINT(F("MQTT połączono ponownie!"));                // Wydrukuj komunikat debugowania
        }
    }
}