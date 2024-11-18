// --- Biblioteki

#include <Arduino.h>      // Podstawowa biblioteka Arduino zawierająca funkcje rdzenia
#include <ArduinoHA.h>    // Integracja z Home Assistant przez protokół MQTT
//#include <ArduinoOTA.h>   // Aktualizacja oprogramowania przez sieć WiFi (Over-The-Air)
#include <ESP8266WiFi.h>  // Biblioteka WiFi dedykowana dla układu ESP8266
#include <ESP8266WebServer.h>
//#include <DNSServer.h>
//#include <LittleFS.h>
//#include <ArduinoJson.h>

// Własne pliki nagłówkowe
#include "SystemStatus.h"
#include "Constants.h"
#include "Pins.h"
#include "Config.h"
#include "Button.h"
#include "HomeAssistant.h"
#include "Network.h"

#include "ConfigManager.h"
#include "Pump.h"
#include "Sensor.h"
#include "Status.h"
#include "WebServer.h"

// Global instances
SystemStatus systemStatus;
Config config;
HADevice device("HydroSense");
WiFiClient client;
HAMqtt mqtt(client, device);
ConfigManager configManager;
NetworkManager networkManager(configManager, systemStatus);
WebServerManager webServerManager(configManager);

ESP8266WebServer webServer(80);

// --- Definicje stałych i zmiennych globalnych

// Stałe dla trybu AP
const char* AP_SSID = "HydroSense";
const char* AP_PASSWORD = "hydrosense";
const byte DNS_PORT = 53;

// Konfiguracja pinów ESP8266
#define PIN_ULTRASONIC_TRIG D6  // Pin TRIG czujnika ultradźwiękowego
#define PIN_ULTRASONIC_ECHO D7  // Pin ECHO czujnika ultradźwiękowego

#define PIN_WATER_LEVEL D5  // Pin czujnika poziomu wody w akwarium
#define POMPA_PIN D1        // Pin sterowania pompą
#define BUZZER_PIN D2       // Pin buzzera do alarmów dźwiękowych
#define PRZYCISK_PIN D3     // Pin przycisku do kasowania alarmów

// Stałe czasowe (wszystkie wartości w milisekundach)
const unsigned long ULTRASONIC_TIMEOUT = 50;       // Timeout pomiaru czujnika ultradźwiękowego
const unsigned long MEASUREMENT_INTERVAL = 15000;  // Interwał między pomiarami
const unsigned long WIFI_CHECK_INTERVAL = 5000;    // Interwał sprawdzania połączenia WiFi
const unsigned long WATCHDOG_TIMEOUT = 8000;       // Timeout dla watchdoga
const unsigned long PUMP_MAX_WORK_TIME = 300000;   // Maksymalny czas pracy pompy (5 minut)
const unsigned long PUMP_DELAY_TIME = 60000;       // Opóźnienie ponownego załączenia pompy (1 minuta)
const unsigned long SENSOR_READ_INTERVAL = 5000;   // Częstotliwość odczytu czujnika
const unsigned long MQTT_RETRY_INTERVAL = 5000;    // Interwał prób połączenia MQTT
const unsigned long WIFI_RETRY_INTERVAL = 10000;   // Interwał prób połączenia WiFi
const unsigned long BUTTON_DEBOUNCE_TIME = 50;     // Czas debouncingu przycisku
const unsigned long LONG_PRESS_TIME = 1000;        // Czas długiego naciśnięcia przycisku
const unsigned long SOUND_ALERT_INTERVAL = 60000;  // Interwał między sygnałami dźwiękowymi

// Definicja debugowania - ustaw 1 aby włączyć, 0 aby wyłączyć
#define DEBUG 0

#if DEBUG
#define DEBUG_PRINT(x) Serial.println(x)
#define DEBUG_PRINTF(format, ...) Serial.printf(format, __VA_ARGS__)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTF(format, ...)
#endif

// Stałe konfiguracyjne zbiornika
// Wszystkie odległości w milimetrach od czujnika do powierzchni wody
// Mniejsza odległość = wyższy poziom wody
const int TANK_FULL = 65;          // Odległość gdy zbiornik jest pełny (mm)
const int TANK_EMPTY = 510;        // Odległość gdy zbiornik jest pusty (mm)
const int RESERVE_LEVEL = 450;     // Poziom rezerwy wody (mm)
const int HYSTERESIS = 10;         // Histereza przy zmianach poziomu (mm)
const int TANK_DIAMETER = 150;     // Średnica zbiornika (mm)
const int SENSOR_AVG_SAMPLES = 5;  // Liczba próbek do uśrednienia pomiaru
const int PUMP_DELAY = 5;          // Opóźnienie uruchomienia pompy (sekundy)
const int PUMP_WORK_TIME = 60;     // Czas pracy pompy

float aktualnaOdleglosc = 0;  // Aktualny dystans
float lastFilteredDistance = 0;
const float EMA_ALPHA = 0.2;       // Współczynnik filtru EMA (0.0-1.0)
const int MEASUREMENT_DELAY = 30;  // Opóźnienie między pomiarami w ms
const int VALID_MARGIN = 20;       // Margines błędu pomiaru (mm)

unsigned long ostatniCzasDebounce = 0;  // Ostatni czas zmiany stanu przycisku

// Obiekty do komunikacji
HADevice device("HydroSense");  // Definicja urządzenia dla Home Assistant

// Sensory pomiarowe
HASensor sensorDistance("water_level");       // Odległość od lustra wody (w mm)
HASensor sensorLevel("water_level_percent");  // Poziom wody w zbiorniku (w procentach)
HASensor sensorVolume("water_volume");        // Objętość wody (w litrach)

// Sensory statusu
HASensor sensorPump("pump");    // Status pracy pompy (ON/OFF)
HASensor sensorWater("water");  // Status czujnika poziomu w akwarium (ON=niski/OFF=ok)

// Sensory alarmowe
HASensor sensorAlarm("water_alarm");      // Alarm braku wody w zbiorniku dolewki
HASensor sensorReserve("water_reserve");  // Alarm rezerwy w zbiorniku dolewki

// Przełączniki
HASwitch switchPumpAlarm("pump_alarm");  // Przełącznik resetowania blokady pompy
HASwitch switchService("service_mode");  // Przełącznik trybu serwisowego
HASwitch switchSound("sound_switch");    // Przełącznik dźwięku alarmu

float currentDistance = 0;
float volume = 0;
unsigned long pumpStartTime = 0;
float waterLevelBeforePump = 0;

// Funkcje pomocnicze
bool isIp(String str) {
  for (int i = 0; i < str.length(); i++) {
    int c = str.charAt(i);
    if (c != '.' && (c < '0' || c > '9')) {
      return false;
    }
  }
  return true;
}

String toStringIp(IPAddress ip) {
  String res = "";
  for (int i = 0; i < 3; i++) {
    res += String((ip >> (8 * i)) & 0xFF) + ".";
  }
  res += String(((ip >> 8 * 3)) & 0xFF);
  return res;
}

webServer.on("/restart", HTTP_POST, []() {
  webServer.send(200, "text/plain", "Restarting...");
  delay(1000);
  ESP.restart();
});

void switchToNormalMode() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(configManager.getNetworkConfig().wifi_ssid.c_str(),
             configManager.getNetworkConfig().wifi_password.c_str());
  // Zatrzymaj serwery AP
  webServer.stop();
  dnsServer.stop();

  setupHA();
  firstUpdateHA();
}

bool connectMQTT() {
  if (!systemStatus.isWiFiConnected) {
    Serial.println(F("Brak połączenia WiFi - nie można połączyć z MQTT"));
    return false;
  }

  const ConfigManager::NetworkConfig& networkConfig = configManager.getNetworkConfig();

  if (networkConfig.mqtt_server.length() == 0) {
    Serial.println(F("Brak skonfigurowanego serwera MQTT"));
    return false;
  }

  Serial.println(F("Łączenie z MQTT..."));

  if (networkConfig.mqtt_user.length() > 0) {
    mqtt.begin(networkConfig.mqtt_server.c_str(),
               networkConfig.mqtt_user.c_str(),
               networkConfig.mqtt_password.c_str());
  } else {
    mqtt.begin(networkConfig.mqtt_server.c_str());
  }

  // Czekaj maksymalnie 10 sekund na połączenie
  unsigned long startTime = millis();
  while (!mqtt.isConnected() && millis() - startTime < 10000) {
    mqtt.loop();
    delay(100);
  }

  systemStatus.isMQTTConnected = mqtt.isConnected();

  if (systemStatus.isMQTTConnected) {
    Serial.println(F("Połączono z MQTT"));
    return true;
  } else {
    Serial.println(F("Nie udało się połączyć z MQTT"));
    return false;
  }
}

// Melodia powitalna
void welcomeMelody() {
  tone(BUZZER_PIN, 1397, 100);  // F6
  delay(150);
  tone(BUZZER_PIN, 1568, 100);  // G6
  delay(150);
  tone(BUZZER_PIN, 1760, 150);  // A6
  delay(200);
}

// --- Setup
void setup() {
  ESP.wdtEnable(WATCHDOG_TIMEOUT);  // Aktywacja watchdoga
  Serial.begin(115200);             // Inicjalizacja portu szeregowego
  setupPin();

 if (!networkManager.setupWiFi()) {
        networkManager.setupAP();
    }
    webServerManager.setup();

  // Inicjalizacja managera konfiguracji
  if (!configManager.begin()) {
    Serial.println(F("Błąd inicjalizacji konfiguracji"));
    playShortWarningSound();
  }

  // Próba połączenia z WiFi
  if (!setupWiFi()) {
    // Jeśli nie udało się połączyć, przejdź w tryb AP
    setupAP();
    setupWebServer();
    playShortWarningSound();
  } else {
    // Normalna inicjalizacja dla trybu połączonego
    if (connectMQTT()) {
      setupHA();
      firstUpdateHA();
      playConfirmationSound();
    } else {
      playShortWarningSound();
    }
  }

  // Konfiguracja OTA (Over-The-Air) dla aktualizacji oprogramowania
  ArduinoOTA.setHostname("HydroSense");  // Ustaw nazwę urządzenia
  ArduinoOTA.setPassword("hydrosense");  // Ustaw hasło dla OTA
  ArduinoOTA.begin();                    // Uruchom OTA

  DEBUG_PRINT("Setup zakończony pomyślnie!");

  if (status.soundEnabled) {
    welcomeMelody();
  }
}

void loop() {
  unsigned long currentMillis = millis();
  static unsigned long lastMQTTRetry = 0;
  static unsigned long lastMeasurement = 0;
  static unsigned long lastStatsUpdate = 0;

  dnsServer.processNextRequest();
  webServer.handleClient();

    networkManager.checkWiFiConnection();
    webServerManager.handleClient();

  // KRYTYCZNE OPERACJE SYSTEMOWE

  // Zabezpieczenie przed zawieszeniem systemu
  ESP.wdtFeed();  // Resetowanie licznika watchdog
  yield();        // Obsługa krytycznych zadań systemowych ESP8266

  // System aktualizacji bezprzewodowej
  ArduinoOTA.handle();  // Nasłuchiwanie żądań aktualizacji OTA

  // ZARZĄDZANIE ŁĄCZNOŚCIĄ

  if (WiFi.getMode() == WIFI_AP) {
    // Handle AP mode
    dnsServer.processNextRequest();
    webServer.handleClient();
  } else {
    // Normal operation mode
    if (!mqtt.isConnected()) {
      if (!connectMQTT()) {
        playShortWarningSound();
      }
    }
    mqtt.loop();

    if (status.isServiceEnabled) {
      handleButton();
      updateWaterLevel();
      checkAlarmConditions();
      updatePump();
    }
  }

  // Monitoring poziomu wody
  if (currentMillis - lastMeasurement >= MEASUREMENT_INTERVAL) {
    updateWaterLevel();  // Pomiar i aktualizacja stanu wody
    lastMeasurement = currentMillis;
  }
}