#include "HomeAssistant.h"
#include "ConfigManager.h"
#include "SystemStatus.h"
#include "Network.h"
#include "Sensors.h"
#include "Alarm.h"
#include "Pins.h"

// Deklaracje zmiennych globalnych
ConfigManager configManager;
SystemStatus systemStatus;
NetworkManager networkManager(configManager, systemStatus);

void setup() {
    Serial.begin(115200);
    
    // Inicjalizacja pinów czujnika
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    
    // Inicjalizacja pozostałych komponentów
    networkManager.setupAP();
    // Reszta kodu setupu...
}

void loop() {
    // Twój kod pętli...
}