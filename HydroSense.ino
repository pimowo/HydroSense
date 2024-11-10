/***************************************
 * System HydroSense
 * Wersja: 2.0
 * Autor: PMW
 * Data: 2024
 ***************************************/

#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <ArduinoHA.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>
#include <CRC32.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define BUILD_DATE TOSTRING(__DATE__)

#define STACK_PROTECTOR  512 // bytes

namespace HydroSense {

// Konfiguracja pinów
constexpr uint8_t PIN_TRIG = D6;
constexpr uint8_t PIN_ECHO = D7;
constexpr uint8_t PIN_SENSOR = D5;
constexpr uint8_t PIN_PUMP = D1;
constexpr uint8_t PIN_BUZZER = D2;
constexpr uint8_t PIN_BUTTON = D3;

// Stałe konfiguracyjne
constexpr unsigned long SENSOR_CHECK_INTERVAL = 1000; // ms
constexpr unsigned long MQTT_RETRY_INTERVAL = 5000;   // ms
constexpr float TANK_DIAMETER_MIN = 50.0f;           // mm
constexpr float TANK_DIAMETER_MAX = 250.0f;          // mm

// WiFi credentials
const char* WIFI_SSID = "pimowo";
const char* WIFI_PASSWORD = "ckH59LRZQzCDQFiUgj";

// Konfiguracja MQTT
const char* MQTT_BROKER = "192.168.1.14";
const uint16_t MQTT_PORT = 1883;
const char* MQTT_USER = "hydrosense";
const char* MQTT_PASSWORD = "hydrosense";
const char* MQTT_CLIENT_ID = "HydroSense_ESP8266"; // Dodajemy unikalny identyfikator klienta

class WaterLevelSensor {
public:
    WaterLevelSensor() : lastMeasurement(0), distance(0.0f) {}
    
    float measureDistance() {
        if (millis() - lastMeasurement < SENSOR_CHECK_INTERVAL) {
            return distance;
        }
        
        // Pomiar z użyciem średniej z 3 odczytów
        float measurements[3];
        for (int i = 0; i < 3; ++i) {
            digitalWrite(PIN_TRIG, LOW);
            delayMicroseconds(2);
            digitalWrite(PIN_TRIG, HIGH);
            delayMicroseconds(10);
            digitalWrite(PIN_TRIG, LOW);
            
            measurements[i] = pulseIn(PIN_ECHO, HIGH) * 0.343f / 2.0f;
            delayMicroseconds(50);
        }
        
        distance = (measurements[0] + measurements[1] + measurements[2]) / 3.0f;
        lastMeasurement = millis();
        return distance;
    }
    
private:
    unsigned long lastMeasurement;
    float distance;
};

class PumpController {
public:
    enum class State {
        IDLE,
        DELAYED_START,
        RUNNING,
        ERROR
    };
    
    PumpController() : 
        state(State::IDLE),
        startTime(0),
        pumpDuration(30000), // 30 sekund
        delayDuration(5000)  // 5 sekund
    {}
    
    void update() {
        unsigned long currentTime = millis();
        
        switch (state) {
            case State::DELAYED_START:
                if (currentTime - startTime >= delayDuration) {
                    startPump();
                }
                break;
                
            case State::RUNNING:
                if (currentTime - startTime >= pumpDuration) {
                    stopPump();
                }
                break;
                
            default:
                break;
        }
    }
    
    void requestStart() {
        if (state == State::IDLE) {
            state = State::DELAYED_START;
            startTime = millis();
        }
    }
    
    void stop() {
        stopPump();
    }
    
private:
    void startPump() {
        digitalWrite(PIN_PUMP, HIGH);
        state = State::RUNNING;
        startTime = millis();
    }
    
    void stopPump() {
        digitalWrite(PIN_PUMP, LOW);
        state = State::IDLE;
    }
    
    State state;
    unsigned long startTime;
    unsigned long pumpDuration;
    unsigned long delayDuration;
};

class Settings {
private:
    static const uint32_t SETTINGS_MAGIC = 0xABCD1234;
    uint32_t magic;
    float tankDiameter;     // mm
    float tankWidth;        // mm
    float tankHeight;       // mm
    float fullDistance;     // mm
    float emptyDistance;    // mm
    float reserveLevel;     // mm
    float reserveHysteresis;// mm
    bool soundEnabled;
    bool isInReserve;

public:
    Settings() {
        // Domyślne wartości dla zbiornika
        magic = SETTINGS_MAGIC;
        tankDiameter = 200.0f;     // Średnica zbiornika w mm
        tankWidth = 0.0f;          // Szerokość zbiornika w mm (0 = okrągły)
        tankHeight = 300.0f;       // Wysokość zbiornika w mm
        fullDistance = 50.0f;      // Odległość czujnika od lustra wody przy pełnym zbiorniku
        emptyDistance = 300.0f;    // Odległość czujnika od dna pustego zbiornika
        reserveLevel = 250.0f;     // Próg alarmu rezerwy
        reserveHysteresis = 20.0f; // Histereza dla stanu rezerwy
        soundEnabled = true;       // Domyślnie włączamy dźwięk
        isInReserve = false;       // Początkowy stan rezerwy
    }

    // Gettery
    float getTankDiameter() const { return tankDiameter; }
    float getTankWidth() const { return tankWidth; }
    float getTankHeight() const { return tankHeight; }
    float getFullDistance() const { return fullDistance; }
    float getEmptyDistance() const { return emptyDistance; }
    float getReserveLevel() const { return reserveLevel; }
    float getReserveThreshold() const { return reserveLevel; } // Alias dla zachowania kompatybilności
    float getReserveHysteresis() const { return reserveHysteresis; }
    bool isSoundEnabled() const { return soundEnabled; }
    bool isInReserve() const { return isInReserve; }
    
    // Settery
    void setTankDiameter(float value) { tankDiameter = value; save(); }
    void setTankWidth(float value) { tankWidth = value; save(); }
    void setTankHeight(float value) { tankHeight = value; save(); }
    void setFullDistance(float value) { fullDistance = value; save(); }
    void setEmptyDistance(float value) { emptyDistance = value; save(); }
    void setReserveLevel(float value) { reserveLevel = value; save(); }
    void setReserveHysteresis(float value) { reserveHysteresis = value; save(); }
    void setSoundEnabled(bool value) { soundEnabled = value; save(); }

    void save() {
        EEPROM.begin(512);
        magic = SETTINGS_MAGIC;
        EEPROM.put(0, magic);
        EEPROM.put(4, tankDiameter);
        EEPROM.put(8, tankWidth);
        EEPROM.put(12, tankHeight);
        EEPROM.put(16, fullDistance);
        EEPROM.put(20, emptyDistance);
        EEPROM.put(24, reserveLevel);
        EEPROM.put(28, reserveHysteresis);
        EEPROM.put(32, soundEnabled);
        EEPROM.commit();
        EEPROM.end();
    }

    void load() {
        EEPROM.begin(512);
        uint32_t magic;
        EEPROM.get(0, magic);
        
        if (magic != SETTINGS_MAGIC) {
            Serial.println("Wykryto niezainicjalizowany EEPROM - resetowanie do wartości domyślnych");
            reset();
            return;
        }
        
        EEPROM.get(4, tankDiameter);
        EEPROM.get(8, tankWidth);
        EEPROM.get(12, tankHeight);
        EEPROM.get(16, fullDistance);
        EEPROM.get(20, emptyDistance);
        EEPROM.get(24, reserveLevel);
        EEPROM.get(28, reserveHysteresis);
        EEPROM.get(32, soundEnabled);
        EEPROM.end();
    }

    void reset() {
        // Przywracanie wartości domyślnych
        magic = SETTINGS_MAGIC;
        tankDiameter = 200.0f;
        tankWidth = 0.0f;
        tankHeight = 300.0f;
        fullDistance = 50.0f;
        emptyDistance = 300.0f;
        reserveLevel = 250.0f;
        reserveHysteresis = 20.0f;
        soundEnabled = true;
        isInReserve = false;
        save();
    }

    bool checkReserveState(float currentDistance) {
        if (!isInReserve && currentDistance >= reserveLevel) {
            isInReserve = true;
        } else if (isInReserve && currentDistance <= (reserveLevel - reserveHysteresis)) {
            isInReserve = false;
        }
        return isInReserve;
    }
};

// Główna klasa aplikacji
class HydroSenseApp {
private:
    char mqtt_broker[40] = "";
    char mqtt_user[40] = "";
    char mqtt_password[40] = "";
    uint16_t mqtt_port = 1883;
    
    // WiFiManager
    WiFiManager wm;
    
    // Stałe czasowe
    static constexpr unsigned long WIFI_CHECK_INTERVAL = 30000;  // ms
    static constexpr unsigned long UPDATE_INTERVAL = 1000;       // ms
    
    // Komponenty sieciowe
    WiFiClient wifiClient;
    String deviceId;
    HADevice device;
    HAMqtt mqtt;
    ESP8266WebServer webServer;
    unsigned long lastWiFiCheck;
    unsigned long lastMqttRetry;
    unsigned long lastUpdateTime;

    // Sensory HA
    HASensor haWaterLevelSensor;
    HASensor haWaterLevelPercentSensor;
    HABinarySensor reserveSensor;

    // Inne komponenty
    Settings settings;
    WaterLevelSensor levelSensor;
    PumpController pumpController;

    // Przeniesienie metod pomocniczych do klasy
    String createSensorConfig(const char* id, const char* name, const char* unit) {
        String config = "{";
        config += "\"name\":\"" + String(name) + "\",";
        config += "\"device_class\":\"" + String(id) + "\",";
        config += "\"state_topic\":\"hydrosense/" + String(id) + "/state\",";
        config += "\"unit_of_measurement\":\"" + String(unit) + "\",";
        config += "\"unique_id\":\"hydrosense_" + deviceId + "_" + String(id) + "\",";
        config += "\"device\":{";
        config += "\"identifiers\":[\"hydrosense_" + deviceId + "\"],";
        config += "\"name\":\"HydroSense\",";
        config += "\"model\":\"HS ESP8266\",";
        config += "\"manufacturer\":\"PMW\"";
        config += "}}";
        return config;
    }

    float calculateWaterPercentage(float waterLevel) {
        float maxLevel = settings.getEmptyDistance() - settings.getFullDistance();
        if (maxLevel <= 0) return 0.0f;
        
        float currentLevel = settings.getEmptyDistance() - waterLevel;
        float percentage = (currentLevel / maxLevel) * 100.0f;
        return constrain(percentage, 0.0f, 100.0f);
    }

public:
    HydroSenseApp() :
        deviceId(String(ESP.getChipId(), HEX)),
        device(deviceId.c_str()),
        mqtt(wifiClient, device, 25),
        webServer(80),
        haWaterLevelSensor("water_level"),
        haWaterLevelPercentSensor("water_level_percent"),
        reserveSensor("reserve"),
        lastWiFiCheck(0),
        lastMqttRetry(0),
        lastUpdateTime(0)
    {
        delay(100); // Daj czas na inicjalizację sprzętu
        Serial.println("\n=== HydroSense - Inicjalizacja ===");
        
        // Konfiguracja urządzenia MQTT
        device.setName("HydroSense");
        device.setSoftwareVersion("2.0");
        device.setManufacturer("PMW");
        device.setModel("HS ESP8266");
        
        // Konfiguracja MQTT
        mqtt.setBufferSize(512);
        
        settings.load();
        delay(50);
        printSettings();
        
        initializePins();
        delay(50);
        welcomeBuzzer();
        
        initializeWiFi();
        delay(50);
        initializeHomeAssistant();
        delay(50);
        initializeWebServer();
        delay(50);
        initializeOTA();
        
        Serial.println("=== Inicjalizacja zakończona ===\n");
    }

    bool connectMQTT() {
        if (!WiFi.isConnected()) {
            Serial.println("Brak połączenia WiFi - nie można połączyć z MQTT");
            return false;
        }

        // Sprawdź czy mamy wszystkie wymagane parametry MQTT
        if (strlen(mqtt_broker) == 0) {
            Serial.println("Brak skonfigurowanego brokera MQTT");
            return false;
        }

        if (!mqtt.isConnected()) {
            Serial.printf("\nPróba połączenia z MQTT (WiFi Status: %d):\n", WiFi.status());
            Serial.printf("Broker: %s\n", mqtt_broker);
            Serial.printf("Port: %d\n", mqtt_port);
            Serial.printf("User: %s\n", mqtt_user);
            Serial.printf("Password: %s\n", mqtt_password);
            Serial.printf("Client ID: %s\n", deviceId.c_str());
            
            // Rozłącz jeśli było poprzednie połączenie
            mqtt.disconnect();
            delay(1000);
            
            // Próba połączenia z większą ilością debugowania
            bool beginResult = mqtt.begin(mqtt_broker, mqtt_port, mqtt_user, mqtt_password);
            Serial.printf("Begin result: %s\n", beginResult ? "TRUE" : "FALSE");
            
            if (beginResult) {
                delay(2000); // Czekamy na ustabilizowanie połączenia
                
                if (mqtt.isConnected()) {
                    Serial.println("MQTT Connected successfully!");
                    return true;
                } else {
                    Serial.println("MQTT begin() success but connection failed");
                }
            } else {
                Serial.println("MQTT begin() failed");
            }
            
            return false;
        }
        return true;
    }

    void resetAll() {
        Serial.println("Rozpoczynam pełny reset urządzenia...");
        
        // Potwierdzenie dźwiękowe rozpoczęcia resetu
        tone(PIN_BUZZER, 2000, 200);
        delay(300);
        
        // Reset WiFiManager
        wm.resetSettings();
        delay(100); // Daj czas na reset WiFiManager
        
        // Reset własnych ustawień
        settings.reset();
        delay(100); // Daj czas na reset ustawień
        
        // Sygnał zakończenia resetu
        tone(PIN_BUZZER, 1000, 200);
        delay(300);
        tone(PIN_BUZZER, 2000, 200);
        
        Serial.println("Reset zakończony - restartuję urządzenie...");
        delay(1000);
        ESP.restart();
    }

    void initializePins() {
        pinMode(PIN_TRIG, OUTPUT);
        pinMode(PIN_ECHO, INPUT);
        pinMode(PIN_BUTTON, INPUT_PULLUP);
        pinMode(PIN_BUZZER, OUTPUT);
        digitalWrite(PIN_BUZZER, LOW);
        
        // Sprawdzanie przycisku podczas startu
        int buttonHoldTime = 0;
        while (digitalRead(PIN_BUTTON) == LOW) { // Przycisk wciśnięty
            delay(100);
            buttonHoldTime += 100;
            
            // Po 1 sekundzie - pierwszy sygnał
            if (buttonHoldTime == 1000) {
                tone(PIN_BUZZER, 2000, 200);
            }
            
            // Po 3 sekundach - resetowanie
            if (buttonHoldTime >= 3000) {
                Serial.println("Wykryto długie przytrzymanie przycisku - rozpoczynam reset...");
                resetAll();
                return;
            }
        }
    }

    void welcomeBuzzer() {
        if (!settings.isSoundEnabled()) return;
        
        digitalWrite(PIN_BUZZER, HIGH);
        delay(100);
        digitalWrite(PIN_BUZZER, LOW);
    }

void initializeWiFi() {
    Serial.println("\nKonfiguracja WiFi...");
    
    // Parametry dla MQTT
    WiFiManagerParameter custom_mqtt_server("server", "MQTT Server", mqtt_broker, 40);
    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%d", mqtt_port);
    WiFiManagerParameter custom_mqtt_port("port", "MQTT Port", port_str, 6);
    WiFiManagerParameter custom_mqtt_user("user", "MQTT User", mqtt_user, 40);
    WiFiManagerParameter custom_mqtt_pass("pass", "MQTT Password", mqtt_password, 40);
    
    wm.setConfigPortalTimeout(180);
    wm.setConnectTimeout(30);
    wm.setDebugOutput(true);
    
    wm.addParameter(&custom_mqtt_server);
    wm.addParameter(&custom_mqtt_port);
    wm.addParameter(&custom_mqtt_user);
    wm.addParameter(&custom_mqtt_pass);
    
    bool configSuccess = false;
    for(int i = 0; i < 3; i++) {
        if(wm.autoConnect("HydroSense-Setup")) {
            configSuccess = true;
            break;
        }
        yield();
        delay(1000);
    }
    
    if(!configSuccess) {
        Serial.println("Błąd połączenia! Reset...");
        ESP.restart();
    }
    
    if(WiFi.status() == WL_CONNECTED) {
        Serial.println("Połączono z WiFi!");
        
        // Sprawdzamy czy parametry MQTT nie są puste
        const char* new_mqtt_broker = custom_mqtt_server.getValue();
        if(strlen(new_mqtt_broker) > 0) {
            strncpy(mqtt_broker, new_mqtt_broker, 39);
            mqtt_broker[39] = '\0';
        }
        
        const char* port_value = custom_mqtt_port.getValue();
        if(strlen(port_value) > 0) {
            mqtt_port = atoi(port_value);
        }
        
        const char* new_mqtt_user = custom_mqtt_user.getValue();
        if(strlen(new_mqtt_user) > 0) {
            strncpy(mqtt_user, new_mqtt_user, 39);
            mqtt_user[39] = '\0';
        }
        
        const char* new_mqtt_pass = custom_mqtt_pass.getValue();
        if(strlen(new_mqtt_pass) > 0) {
            strncpy(mqtt_password, new_mqtt_pass, 39);
            mqtt_password[39] = '\0';
        }
        
        Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("MQTT Server: %s\n", mqtt_broker);
        Serial.printf("MQTT Port: %d\n", mqtt_port);
        Serial.printf("MQTT User: %s\n", mqtt_user);
    }
}

    void initializeHomeAssistant() {
        haWaterLevelSensor.setName("Water Level");
        haWaterLevelSensor.setDeviceClass("distance");
        haWaterLevelSensor.setUnitOfMeasurement("mm");
        
        haWaterLevelPercentSensor.setName("Water Level Percentage");
        haWaterLevelPercentSensor.setUnitOfMeasurement("%");
        
        reserveSensor.setName("Water Reserve");
        reserveSensor.setDeviceClass("problem");
        
        delay(5000); // Daj WiFi czas na pełną stabilizację
        connectMQTT();
    }

    void initializeWebServer() {
        webServer.on("/", [this]() {
            handleRoot();
        });
        
        webServer.begin();
        Serial.println("Serwer WWW uruchomiony");
    }

    void initializeOTA() {
        ArduinoOTA.onStart([]() {
            Serial.println("Rozpoczęto aktualizację OTA");
        });
        
        ArduinoOTA.onEnd([]() {
            Serial.println("\nZakończono aktualizację OTA");
        });
        
        ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
            Serial.printf("Postęp: %u%%\r", (progress / (total / 100)));
        });
        
        ArduinoOTA.onError([](ota_error_t error) {
            Serial.printf("Błąd[%u]: ", error);
            if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
            else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
            else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
            else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
            else if (error == OTA_END_ERROR) Serial.println("End Failed");
        });
        
        ArduinoOTA.begin();
        Serial.println("OTA gotowe");
    }

    void run() {
        static unsigned long lastYield = 0;
        static unsigned long lastMqttReconnectAttempt = 0;
        static uint8_t mqttReconnectAttempts = 0;
        const unsigned long MQTT_RECONNECT_TIMEOUT = 60000; // 1 minuta między resetem licznika prób
        
        unsigned long currentTime = millis();
        
        // Sprawdzanie WiFi
        if (currentTime - lastWiFiCheck >= WIFI_CHECK_INTERVAL) {
            checkWiFiConnection();
            lastWiFiCheck = currentTime;
        }

        // Obsługa MQTT
        if (WiFi.isConnected()) {
            if (!mqtt.isConnected()) {
                // Reset licznika prób po timeout
                if (currentTime - lastMqttReconnectAttempt > MQTT_RECONNECT_TIMEOUT) {
                    mqttReconnectAttempts = 0;
                }
                
                // Próbuj ponownego połączenia z ograniczeniem
                if (mqttReconnectAttempts < 3 && 
                    currentTime - lastMqttRetry >= MQTT_RETRY_INTERVAL) {
                    if (connectMQTT()) {
                        mqttReconnectAttempts = 0;
                    } else {
                        mqttReconnectAttempts++;
                    }
                    lastMqttRetry = currentTime;
                    lastMqttReconnectAttempt = currentTime;
                }
            } else {
                mqtt.loop();
                ArduinoOTA.handle();
                webServer.handleClient();
            }
        }

        // Aktualizacja stanu
        if (currentTime - lastUpdateTime >= UPDATE_INTERVAL) {
            if (WiFi.isConnected() && mqtt.isConnected()) {
                update();
            }
            lastUpdateTime = currentTime;
        }

        yield();
    }

    void handleRoot() {
        float waterLevel = levelSensor.measureDistance();
        float percentage = calculateWaterPercentage(waterLevel);
        
        static char buffer[1024];
        snprintf(buffer, sizeof(buffer),
            "<html><body>"
            "<h1>HydroSense</h1>"
            "<p>Poziom wody: %.1f mm (%.1f%%)</p>"
            "<p>Stan rezerwy: %s</p>"
            "</body></html>",
            waterLevel, percentage,
            settings.checkReserveState(waterLevel) ? "TAK" : "NIE"
        );
        webServer.send(200, "text/html", buffer);
    }

    bool publishHAConfig() {
        if (!mqtt.isConnected()) {
            Serial.println("Próba publikacji bez połączenia MQTT!");
            return false;
        }
        
        // Dodajemy delay między publikacjami i sprawdzamy status każdej
        bool success = true;
        
        // Water Level sensor
        String config = createSensorConfig("water_level", "Water Level", "mm");
        Serial.println("Publikacja konfiguracji water_level...");
        if (!mqtt.publish("homeassistant/sensor/hydrosense/water_level/config", config.c_str(), true)) {
            Serial.println("Błąd publikacji water_level");
            success = false;
        }
        delay(500);
        
        // Water Level Percentage sensor
        config = createSensorConfig("water_level_percent", "Water Level Percentage", "%");
        Serial.println("Publikacja konfiguracji water_level_percent...");
        if (!mqtt.publish("homeassistant/sensor/hydrosense/water_level_percent/config", config.c_str(), true)) {
            Serial.println("Błąd publikacji water_level_percent");
            success = false;
        }
        delay(500);
        
        // Reserve sensor
        config = createReserveSensorConfig();
        Serial.println("Publikacja konfiguracji reserve...");
        if (mqtt.publish("homeassistant/binary_sensor/hydrosense/reserve/config", config.c_str(), true)) {
            Serial.println("Błąd publikacji reserve");
            success = false;
        }
        
        return success;
    }

    void update() {
        float waterLevel = levelSensor.measureDistance();
        updateHomeAssistantSensors(waterLevel);
        checkAlarmsAndNotifications();
    }

    void checkWiFiConnection() {
        if (WiFi.status() != WL_CONNECTED) {
            static uint8_t reconnectAttempts = 0;
            
            if (reconnectAttempts < 3) { // Limit prób ponownego połączenia
                Serial.println("Ponowne łączenie z WiFi...");
                
                WiFi.disconnect(true);
                delay(100);
                ESP.wdtFeed();
                
                WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
                
                uint8_t timeout = 0;
                while (WiFi.status() != WL_CONNECTED && timeout < 10) {
                    delay(300);
                    timeout++;
                    ESP.wdtFeed();
                }
                
                reconnectAttempts++;
            } else {
                // Reset licznika po pewnym czasie
                static unsigned long lastResetTime = 0;
                if (millis() - lastResetTime > 60000) { // 1 minuta
                    reconnectAttempts = 0;
                    lastResetTime = millis();
                }
            }
        }
    }

    void printSettings() {
        Serial.println("Aktualne ustawienia:");
        Serial.printf("- Średnica zbiornika: %.1f mm\n", settings.getTankDiameter());
        Serial.printf("- Wysokość pełna: %.1f mm\n", settings.getFullDistance());
        Serial.printf("- Wysokość pusta: %.1f mm\n", settings.getEmptyDistance());
        Serial.printf("- Próg rezerwy: %.1f mm\n", settings.getReserveThreshold());
        Serial.printf("- Dźwięk włączony: %s\n", settings.isSoundEnabled() ? "Tak" : "Nie");
    }

    String createReserveSensorConfig() {
        String config = "{";
        config += "\"name\":\"Water Reserve\",";
        config += "\"device_class\":\"problem\",";
        config += "\"state_topic\":\"hydrosense/reserve/state\",";
        config += "\"unique_id\":\"hydrosense_" + deviceId + "_reserve\",";
        config += "\"device\":{";
        config += "\"identifiers\":[\"hydrosense_" + deviceId + "\"],";
        config += "\"name\":\"HydroSense\",";
        config += "\"model\":\"HS ESP8266\",";
        config += "\"manufacturer\":\"PMW\"";
        config += "}}";
        return config;
    }

    void checkAlarmsAndNotifications() {
        float waterLevel = levelSensor.measureDistance();
        bool shouldAlarm = waterLevel >= settings.getReserveThreshold() && settings.isSoundEnabled();
        
        digitalWrite(PIN_BUZZER, shouldAlarm ? HIGH : LOW);
        
        if (shouldAlarm) {
            Serial.println("ALARM: Niski poziom wody!");
        }
    }

    void updateHomeAssistantSensors(float waterLevel) {
        if (mqtt.isConnected()) {
            String payload = String(waterLevel, 1);
            haWaterLevelSensor.setValue(payload.c_str());

            float percentage = calculateWaterPercentage(waterLevel);
            payload = String(percentage, 1);
            haWaterLevelPercentSensor.setValue(payload.c_str());

            bool isInReserve = settings.checkReserveState(waterLevel);
            reserveSensor.setState(isInReserve);

            Serial.println("Zaktualizowano dane w HA");
        }
    }
};

class WebServer {
private:
    ESP8266WebServer server;
    Settings& settings;
    
    // HTML templates
    static const char* HTML_HEAD;
    static const char* HTML_FOOT;
    
public:
    WebServer(Settings& settings) : server(80), settings(settings) {
        server.on("/", std::bind(&WebServer::handleRoot, this));
        server.on("/config", HTTP_GET, std::bind(&WebServer::handleConfigGet, this));
        server.on("/config", HTTP_POST, std::bind(&WebServer::handleConfigPost, this));
        server.on("/reset", std::bind(&WebServer::handleReset, this));
        server.begin();
    }
    
    void handle() {
        server.handleClient();
    }
    
private:
    void handleRoot() {
        String html = HTML_HEAD;
        html += "<h1>HydroSense</h1>";
        html += "<p><a href='/config'>Konfiguracja</a></p>";
        html += HTML_FOOT;
        server.send(200, "text/html", html);
    }
    
    void handleConfigGet() {
        String html = HTML_HEAD;
        html += "<h2>Konfiguracja HydroSense</h2>";
        html += "<form method='post'>";
        
        // WiFi & MQTT
        html += "<h3>WiFi i MQTT</h3>";
        html += "SSID: <input type='text' name='wifi_ssid' value='" + String(settings.getWiFiSSID()) + "'><br>";
        html += "Hasło WiFi: <input type='password' name='wifi_pass'><br>";
        html += "MQTT Server: <input type='text' name='mqtt_server' value='" + String(settings.getMqttServer()) + "'><br>";
        html += "MQTT Port: <input type='number' name='mqtt_port' value='" + String(settings.getMqttPort()) + "'><br>";
        html += "MQTT User: <input type='text' name='mqtt_user' value='" + String(settings.getMqttUser()) + "'><br>";
        html += "MQTT Pass: <input type='password' name='mqtt_pass'><br>";
        
        // Tank settings
        html += "<h3>Zbiornik</h3>";
        html += "Szerokość (mm): <input type='number' name='tank_width' value='" + String(settings.getTankWidth()) + "'><br>";
        html += "Wysokość (mm): <input type='number' name='tank_height' value='" + String(settings.getTankHeight()) + "'><br>";
        html += "Średnica (mm): <input type='number' name='tank_diameter' value='" + String(settings.getTankDiameter()) + "'><br>";
        html += "Poziom rezerwy (mm): <input type='number' name='reserve_level' value='" + String(settings.getReserveLevel()) + "'><br>";
        
        // Pump settings
        html += "<h3>Pompa</h3>";
        html += "Opóźnienie (s): <input type='number' name='pump_delay' value='" + String(settings.getPumpDelay()) + "'><br>";
        html += "Czas pracy (s): <input type='number' name='pump_work' value='" + String(settings.getPumpWork()) + "'><br>";
        
        // Other settings
        html += "<h3>Inne</h3>";
        html += "Dźwięk: <input type='checkbox' name='sound' " + String(settings.isSoundEnabled() ? "checked" : "") + "><br>";
        
        html += "<br><input type='submit' value='Zapisz'>";
        html += "</form>";
        
        html += "<br><form method='post' action='/reset'>";
        html += "<input type='submit' value='Reset do ustawień fabrycznych' onclick='return confirm(\"Czy na pewno chcesz zresetować wszystkie ustawienia?\")'>";
        html += "</form>";
        
        html += HTML_FOOT;
        server.send(200, "text/html", html);
    }
    
    void handleConfigPost() {
        // WiFi & MQTT
        if (server.hasArg("wifi_ssid") && server.hasArg("wifi_pass")) {
            settings.setWiFiCredentials(
                server.arg("wifi_ssid").c_str(),
                server.arg("wifi_pass").c_str()
            );
        }
        
        if (server.hasArg("mqtt_server") && server.hasArg("mqtt_port") &&
            server.hasArg("mqtt_user") && server.hasArg("mqtt_pass")) {
            settings.setMqttConfig(
                server.arg("mqtt_server").c_str(),
                server.arg("mqtt_user").c_str(),
                server.arg("mqtt_pass").c_str(),
                server.arg("mqtt_port").toInt()
            );
        }
        
        // Tank settings
        if (server.hasArg("tank_width") && server.hasArg("tank_height") && server.hasArg("tank_diameter")) {
            settings.setTankDimensions(
                server.arg("tank_width").toFloat(),
                server.arg("tank_height").toFloat(),
                server.arg("tank_diameter").toFloat()
            );
        }
        
        if (server.hasArg("reserve_level")) {
            settings.setReserveLevel(server.arg("reserve_level").toFloat());
        }
        
        // Pump settings
        if (server.hasArg("pump_delay") && server.hasArg("pump_work")) {
            settings.setPumpTiming(
                server.arg("pump_delay").toInt(),
                server.arg("pump_work").toInt()
            );
        }
        
        // Other settings
        settings.setSoundEnabled(server.hasArg("sound"));
        
        server.sendHeader("Location", "/config");
        server.send(303);
    }
    
    void handleReset() {
        settings.loadDefaults();
        server.sendHeader("Location", "/config");
        server.send(303);
    }
};

const char* WebServer::HTML_HEAD = R"html(
<!DOCTYPE html>
<html>
<head>
    <meta charset='utf-8'>
    <meta name='viewport' content='width=device-width, initial-scale=1'>
    <title>HydroSense</title>
    <style>
        body { font-family: Arial; margin: 20px; }
        input { margin: 5px 0; }
        h3 { margin-top: 20px; }
    </style>
</head>
<body>
)html";

const char* WebServer::HTML_FOOT = R"html(
</body>
</html>
)html";

} // namespace HydroSense

HydroSense::HydroSenseApp* app = nullptr;

void setup() {
    ESP.wdtDisable();
    ESP.wdtEnable(WDTO_8S);
    
    Serial.begin(115200);
    while (!Serial) {
        yield();
        delay(10);
    }
    
    app = new HydroSense::HydroSenseApp();
}

void loop() {
    ESP.wdtFeed();
    if (app) {
        app->run();
    }
    yield();
}
