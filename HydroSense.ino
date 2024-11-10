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
    WaterLevelSensor() : m_lastMeasurement(0), m_distance(0.0f) {}
    
    float measureDistance() {
        if (millis() - m_lastMeasurement < SENSOR_CHECK_INTERVAL) {
            return m_distance;
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
        
        m_distance = (measurements[0] + measurements[1] + measurements[2]) / 3.0f;
        m_lastMeasurement = millis();
        return m_distance;
    }
    
private:
    unsigned long m_lastMeasurement;
    float m_distance;
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
        m_state(State::IDLE),
        m_startTime(0),
        m_pumpDuration(30000), // 30 sekund
        m_delayDuration(5000)  // 5 sekund
    {}
    
    void update() {
        unsigned long currentTime = millis();
        
        switch (m_state) {
            case State::DELAYED_START:
                if (currentTime - m_startTime >= m_delayDuration) {
                    startPump();
                }
                break;
                
            case State::RUNNING:
                if (currentTime - m_startTime >= m_pumpDuration) {
                    stopPump();
                }
                break;
                
            default:
                break;
        }
    }
    
    void requestStart() {
        if (m_state == State::IDLE) {
            m_state = State::DELAYED_START;
            m_startTime = millis();
        }
    }
    
    void stop() {
        stopPump();
    }
    
private:
    void startPump() {
        digitalWrite(PIN_PUMP, HIGH);
        m_state = State::RUNNING;
        m_startTime = millis();
    }
    
    void stopPump() {
        digitalWrite(PIN_PUMP, LOW);
        m_state = State::IDLE;
    }
    
    State m_state;
    unsigned long m_startTime;
    unsigned long m_pumpDuration;
    unsigned long m_delayDuration;
};

class Settings {
private:
    float m_tankDiameter = 315.0f;     // mm
    float m_fullDistance = 50.0f;      // mm
    float m_emptyDistance = 300.0f;    // mm
    float m_reserveThreshold = 250.0f; // mm - próg rezerwy
    float m_reserveHysteresis = 20.0f; // mm - histereza
    bool m_soundEnabled = true;
    bool m_isInReserve = false;        // stan rezerwy

public:
    // Gettery
    float getTankDiameter() const { return m_tankDiameter; }
    float getFullDistance() const { return m_fullDistance; }
    float getEmptyDistance() const { return m_emptyDistance; }
    float getReserveThreshold() const { return m_reserveThreshold; }
    float getReserveHysteresis() const { return m_reserveHysteresis; }
    bool isSoundEnabled() const { return m_soundEnabled; }
    bool isInReserve() const { return m_isInReserve; }
    
    // Settery
    void setTankDiameter(float value) { m_tankDiameter = value; save(); }
    void setFullDistance(float value) { m_fullDistance = value; save(); }
    void setEmptyDistance(float value) { m_emptyDistance = value; save(); }
    void setReserveThreshold(float value) { m_reserveThreshold = value; save(); }
    void setReserveHysteresis(float value) { m_reserveHysteresis = value; save(); }
    void setSoundEnabled(bool value) { m_soundEnabled = value; save(); }
    
    void load() {
        EEPROM.begin(512);
        EEPROM.get(0, m_tankDiameter);
        EEPROM.get(4, m_fullDistance);
        EEPROM.get(8, m_emptyDistance);
        EEPROM.get(12, m_reserveThreshold);
        EEPROM.get(16, m_reserveHysteresis);
        EEPROM.get(20, m_soundEnabled);
        EEPROM.end();
    }

    void save() {
        EEPROM.begin(512);
        EEPROM.put(0, m_tankDiameter);
        EEPROM.put(4, m_fullDistance);
        EEPROM.put(8, m_emptyDistance);
        EEPROM.put(12, m_reserveThreshold);
        EEPROM.put(16, m_reserveHysteresis);
        EEPROM.put(20, m_soundEnabled);
        EEPROM.commit();
        EEPROM.end();
    }
    
    // Sprawdzenie stanu rezerwy z histerezą
    bool checkReserveState(float currentDistance) {
        if (!m_isInReserve && currentDistance >= m_reserveThreshold) {
            m_isInReserve = true;
        } else if (m_isInReserve && currentDistance <= (m_reserveThreshold - m_reserveHysteresis)) {
            m_isInReserve = false;
        }
        return m_isInReserve;
    }

    // Konstruktor z wartościami domyślnymi
    Settings() {
        // Domyślne wartości dla zbiornika
        m_tankDiameter = 315.0f;     // Średnica zbiornika w mm
        m_fullDistance = 50.0f;      // Odległość czujnika od lustra wody przy pełnym zbiorniku
        m_emptyDistance = 300.0f;    // Odległość czujnika od dna pustego zbiornika
        m_reserveThreshold = 250.0f; // Próg alarmu rezerwy
        m_reserveHysteresis = 20.0f; // Histereza dla stanu rezerwy
        m_soundEnabled = true;       // Domyślnie włączamy dźwięk
        m_isInReserve = false;       // Początkowy stan rezerwy
        
        // Zapisujemy wartości domyślne do EEPROM
        save();
        
        // Weryfikacja zapisu
        load();
        
        // Log wartości po inicjalizacji
        Serial.println("Zainicjalizowano domyślne wartości:");
        Serial.printf("- Średnica zbiornika: %.1f mm\n", m_tankDiameter);
        Serial.printf("- Wysokość pełna: %.1f mm\n", m_fullDistance);
        Serial.printf("- Wysokość pusta: %.1f mm\n", m_emptyDistance);
        Serial.printf("- Próg rezerwy: %.1f mm\n", m_reserveThreshold);
        Serial.printf("- Histereza rezerwy: %.1f mm\n", m_reserveHysteresis);
        Serial.printf("- Dźwięk włączony: %s\n", m_soundEnabled ? "Tak" : "Nie");
    }

    void reset() {
        // Przywróć wartości domyślne
        m_tankDiameter = 315.0f;     // Średnica zbiornika w mm
        m_fullDistance = 50.0f;      // Odległość czujnika od lustra wody przy pełnym zbiorniku
        m_emptyDistance = 300.0f;    // Odległość czujnika od dna pustego zbiornika
        m_reserveThreshold = 250.0f; // Próg alarmu rezerwy
        m_reserveHysteresis = 20.0f; // Histereza dla stanu rezerwy
        m_soundEnabled = true;       // Domyślnie włączamy dźwięk
        m_isInReserve = false;       // Stan rezerwy
        
        // Zapisz wartości domyślne do EEPROM
        save();
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
    WiFiClient m_wifiClient;
    String m_deviceId;
    HADevice m_device;
    HAMqtt m_mqtt;
    ESP8266WebServer m_webServer;
    unsigned long m_lastWiFiCheck;
    unsigned long m_lastMqttRetry;
    unsigned long m_lastUpdateTime;

    // Sensory HA
    HASensor m_haWaterLevelSensor;
    HASensor m_haWaterLevelPercentSensor;
    HABinarySensor m_reserveSensor;

    // Inne komponenty
    Settings m_settings;
    WaterLevelSensor m_levelSensor;
    PumpController m_pumpController;

    // Przeniesienie metod pomocniczych do klasy
    String createSensorConfig(const char* id, const char* name, const char* unit) {
        String config = "{";
        config += "\"name\":\"" + String(name) + "\",";
        config += "\"device_class\":\"" + String(id) + "\",";
        config += "\"state_topic\":\"hydrosense/" + String(id) + "/state\",";
        config += "\"unit_of_measurement\":\"" + String(unit) + "\",";
        config += "\"unique_id\":\"hydrosense_" + m_deviceId + "_" + String(id) + "\",";
        config += "\"device\":{";
        config += "\"identifiers\":[\"hydrosense_" + m_deviceId + "\"],";
        config += "\"name\":\"HydroSense\",";
        config += "\"model\":\"HS ESP8266\",";
        config += "\"manufacturer\":\"PMW\"";
        config += "}}";
        return config;
    }

    float calculateWaterPercentage(float waterLevel) {
        float maxLevel = m_settings.getEmptyDistance() - m_settings.getFullDistance();
        if (maxLevel <= 0) return 0.0f;
        
        float currentLevel = m_settings.getEmptyDistance() - waterLevel;
        float percentage = (currentLevel / maxLevel) * 100.0f;
        return constrain(percentage, 0.0f, 100.0f);
    }

public:
    HydroSenseApp() :
        m_deviceId(String(ESP.getChipId(), HEX)),
        m_device(m_deviceId.c_str()),
        m_mqtt(m_wifiClient, m_device, 25),
        m_webServer(80),
        m_haWaterLevelSensor("water_level"),
        m_haWaterLevelPercentSensor("water_level_percent"),
        m_reserveSensor("reserve"),
        m_lastWiFiCheck(0),
        m_lastMqttRetry(0),
        m_lastUpdateTime(0)
    {
        delay(100); // Daj czas na inicjalizację sprzętu
        Serial.println("\n=== HydroSense - Inicjalizacja ===");
        
        // Konfiguracja urządzenia MQTT
        m_device.setName("HydroSense");
        m_device.setSoftwareVersion("2.0");
        m_device.setManufacturer("PMW");
        m_device.setModel("HS ESP8266");
        
        // Konfiguracja MQTT
        m_mqtt.setBufferSize(512);
        
        m_settings.load();
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

        if (!m_mqtt.isConnected()) {
            Serial.printf("\nPróba połączenia z MQTT:\n");
            Serial.printf("Broker: %s\n", mqtt_broker);
            Serial.printf("Port: %d\n", mqtt_port);
            Serial.printf("User: %s\n", mqtt_user);
            Serial.printf("Password: %s\n", mqtt_password);
            
            m_mqtt.disconnect();
            delay(1000); // Zwiększamy opóźnienie przed ponownym połączeniem
            
            // Dodajemy więcej parametrów połączenia
            HADevice device(m_deviceId.c_str());
            device.setName("HydroSense");
            device.setSoftwareVersion("2.0");
            device.setManufacturer("PMW");
            device.setModel("HS ESP8266");
            
            if (m_mqtt.begin(mqtt_broker, mqtt_port, mqtt_user, mqtt_password)) {
                Serial.println("Połączenie MQTT zainicjowane");
                delay(2000); // Dajemy więcej czasu na stabilizację połączenia
                
                if (m_mqtt.isConnected()) {
                    Serial.println("MQTT Connected - próba publikacji konfiguracji");
                    if (publishHAConfig()) {
                        Serial.println("Konfiguracja HA opublikowana pomyślnie");
                        return true;
                    } else {
                        Serial.println("Błąd publikacji konfiguracji HA");
                    }
                } else {
                    Serial.println("MQTT nie jest połączone po begin()");
                }
            } else {
                Serial.println("Błąd inicjalizacji MQTT");
            }
            
            Serial.println("Połączenie MQTT nie powiodło się");
            return false;
        }
        return true;
    }

    void resetAll() {
        // Reset WiFiManager
        wm.resetSettings();
        
        // Reset własnych ustawień
        m_settings.reset();
        
        // Czyszczenie EEPROM
        for (int i = 0; i < 512; i++) {
            EEPROM.write(i, 0);
        }
        EEPROM.commit();
        
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
                // Sygnał potwierdzający reset
                tone(PIN_BUZZER, 1000, 200);
                delay(300);
                tone(PIN_BUZZER, 2000, 200);
                
                Serial.println("Reset ustawień...");
                
                // Reset WiFiManager
                wm.resetSettings();
                
                // Reset własnych ustawień
                m_settings.reset();
                
                delay(1000);
                Serial.println("Restart urządzenia...");
                ESP.restart();
            }
        }
    }

    void welcomeBuzzer() {
        if (!m_settings.isSoundEnabled()) return;
        
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
        } else {
            // Jeśli broker jest pusty, używamy domyślnej wartości
            strncpy(mqtt_broker, "192.168.1.14", 39);
        }
        
        const char* port_value = custom_mqtt_port.getValue();
        if(strlen(port_value) > 0) {
            mqtt_port = atoi(port_value);
        }
        
        const char* new_mqtt_user = custom_mqtt_user.getValue();
        if(strlen(new_mqtt_user) > 0) {
            strncpy(mqtt_user, new_mqtt_user, 39);
            mqtt_user[39] = '\0';
        } else {
            strncpy(mqtt_user, "hydrosense", 39);
        }
        
        const char* new_mqtt_pass = custom_mqtt_pass.getValue();
        if(strlen(new_mqtt_pass) > 0) {
            strncpy(mqtt_password, new_mqtt_pass, 39);
            mqtt_password[39] = '\0';
        } else {
            strncpy(mqtt_password, "hydrosense", 39);
        }
        
        Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("MQTT Server: %s\n", mqtt_broker);
        Serial.printf("MQTT Port: %d\n", mqtt_port);
        Serial.printf("MQTT User: %s\n", mqtt_user);
    }
}

    void initializeHomeAssistant() {
        m_haWaterLevelSensor.setName("Water Level");
        m_haWaterLevelSensor.setDeviceClass("distance");
        m_haWaterLevelSensor.setUnitOfMeasurement("mm");
        
        m_haWaterLevelPercentSensor.setName("Water Level Percentage");
        m_haWaterLevelPercentSensor.setUnitOfMeasurement("%");
        
        m_reserveSensor.setName("Water Reserve");
        m_reserveSensor.setDeviceClass("problem");
        
        connectMQTT();
    }

    void initializeWebServer() {
        m_webServer.on("/", [this]() {
            handleRoot();
        });
        
        m_webServer.begin();
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
        if (currentTime - m_lastWiFiCheck >= WIFI_CHECK_INTERVAL) {
            checkWiFiConnection();
            m_lastWiFiCheck = currentTime;
        }

        // Obsługa MQTT
        if (WiFi.isConnected()) {
            if (!m_mqtt.isConnected()) {
                // Reset licznika prób po timeout
                if (currentTime - lastMqttReconnectAttempt > MQTT_RECONNECT_TIMEOUT) {
                    mqttReconnectAttempts = 0;
                }
                
                // Próbuj ponownego połączenia z ograniczeniem
                if (mqttReconnectAttempts < 3 && 
                    currentTime - m_lastMqttRetry >= MQTT_RETRY_INTERVAL) {
                    if (connectMQTT()) {
                        mqttReconnectAttempts = 0;
                    } else {
                        mqttReconnectAttempts++;
                    }
                    m_lastMqttRetry = currentTime;
                    lastMqttReconnectAttempt = currentTime;
                }
            } else {
                m_mqtt.loop();
                ArduinoOTA.handle();
                m_webServer.handleClient();
            }
        }

        // Aktualizacja stanu
        if (currentTime - m_lastUpdateTime >= UPDATE_INTERVAL) {
            if (WiFi.isConnected() && m_mqtt.isConnected()) {
                update();
            }
            m_lastUpdateTime = currentTime;
        }

        yield();
    }

    void handleRoot() {
        float waterLevel = m_levelSensor.measureDistance();
        float percentage = calculateWaterPercentage(waterLevel);
        
        static char buffer[1024];
        snprintf(buffer, sizeof(buffer),
            "<html><body>"
            "<h1>HydroSense</h1>"
            "<p>Poziom wody: %.1f mm (%.1f%%)</p>"
            "<p>Stan rezerwy: %s</p>"
            "</body></html>",
            waterLevel, percentage,
            m_settings.checkReserveState(waterLevel) ? "TAK" : "NIE"
        );
        m_webServer.send(200, "text/html", buffer);
    }

    bool publishHAConfig() {
        if (!m_mqtt.isConnected()) {
            Serial.println("Próba publikacji bez połączenia MQTT!");
            return false;
        }
        
        // Dodajemy delay między publikacjami i sprawdzamy status każdej
        bool success = true;
        
        // Water Level sensor
        String config = createSensorConfig("water_level", "Water Level", "mm");
        Serial.println("Publikacja konfiguracji water_level...");
        if (!m_mqtt.publish("homeassistant/sensor/hydrosense/water_level/config", config.c_str(), true)) {
            Serial.println("Błąd publikacji water_level");
            success = false;
        }
        delay(500);
        
        // Water Level Percentage sensor
        config = createSensorConfig("water_level_percent", "Water Level Percentage", "%");
        Serial.println("Publikacja konfiguracji water_level_percent...");
        if (!m_mqtt.publish("homeassistant/sensor/hydrosense/water_level_percent/config", config.c_str(), true)) {
            Serial.println("Błąd publikacji water_level_percent");
            success = false;
        }
        delay(500);
        
        // Reserve sensor
        config = createReserveSensorConfig();
        Serial.println("Publikacja konfiguracji reserve...");
        if (!m_mqtt.publish("homeassistant/binary_sensor/hydrosense/reserve/config", config.c_str(), true)) {
            Serial.println("Błąd publikacji reserve");
            success = false;
        }
        
        return success;
    }

    void update() {
        float waterLevel = m_levelSensor.measureDistance();
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
        Serial.printf("- Średnica zbiornika: %.1f mm\n", m_settings.getTankDiameter());
        Serial.printf("- Wysokość pełna: %.1f mm\n", m_settings.getFullDistance());
        Serial.printf("- Wysokość pusta: %.1f mm\n", m_settings.getEmptyDistance());
        Serial.printf("- Próg rezerwy: %.1f mm\n", m_settings.getReserveThreshold());
        Serial.printf("- Dźwięk włączony: %s\n", m_settings.isSoundEnabled() ? "Tak" : "Nie");
    }

    String createReserveSensorConfig() {
        String config = "{";
        config += "\"name\":\"Water Reserve\",";
        config += "\"device_class\":\"problem\",";
        config += "\"state_topic\":\"hydrosense/reserve/state\",";
        config += "\"unique_id\":\"hydrosense_" + m_deviceId + "_reserve\",";
        config += "\"device\":{";
        config += "\"identifiers\":[\"hydrosense_" + m_deviceId + "\"],";
        config += "\"name\":\"HydroSense\",";
        config += "\"model\":\"HS ESP8266\",";
        config += "\"manufacturer\":\"PMW\"";
        config += "}}";
        return config;
    }

    void checkAlarmsAndNotifications() {
        float waterLevel = m_levelSensor.measureDistance();
        bool shouldAlarm = waterLevel >= m_settings.getReserveThreshold() && m_settings.isSoundEnabled();
        
        digitalWrite(PIN_BUZZER, shouldAlarm ? HIGH : LOW);
        
        if (shouldAlarm) {
            Serial.println("ALARM: Niski poziom wody!");
        }
    }

    void updateHomeAssistantSensors(float waterLevel) {
        if (m_mqtt.isConnected()) {
            String payload = String(waterLevel, 1);
            m_haWaterLevelSensor.setValue(payload.c_str());

            float percentage = calculateWaterPercentage(waterLevel);
            payload = String(percentage, 1);
            m_haWaterLevelPercentSensor.setValue(payload.c_str());

            bool isInReserve = m_settings.checkReserveState(waterLevel);
            m_reserveSensor.setState(isInReserve);

            Serial.println("Zaktualizowano dane w HA");
        }
    }
};

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
