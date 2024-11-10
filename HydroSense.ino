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
#include <ArduinoHA.h>
#include <WiFiManager.h>

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define BUILD_DATE TOSTRING(__DATE__)

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
};

// Główna klasa aplikacji
class HydroSenseApp {
private:
    // Zmienne dla konfiguracji MQTT
    char mqtt_broker[40];
    char mqtt_user[40];
    char mqtt_password[40];
    uint16_t mqtt_port;
    
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
        // Ustaw domyślne wartości MQTT
        strcpy(mqtt_broker, "192.168.1.14");
        strcpy(mqtt_user, "hydrosense");
        strcpy(mqtt_password, "hydrosense");
        mqtt_port = 1883;
        
        Serial.println("\n=== HydroSense - Inicjalizacja ===");
        
        m_settings.load();
        printSettings();
        
        initializePins();
        welcomeBuzzer();
        
        initializeWiFi();
        initializeHomeAssistant();
        initializeWebServer();
        initializeOTA();
        
        Serial.println("=== Inicjalizacja zakończona ===\n");
    }

    bool connectMQTT() {
        if (!WiFi.isConnected()) {
            Serial.println("Brak połączenia WiFi - nie można połączyć z MQTT");
            return false;
        }

        if (!m_mqtt.isConnected()) {
            Serial.printf("Łączenie z MQTT (%s:%d)...", mqtt_broker, mqtt_port);
            
            m_mqtt.disconnect();
            delay(100);
            
            if (m_mqtt.begin(mqtt_broker, mqtt_port, mqtt_user, mqtt_password)) {
                Serial.println("Połączono!");
                delay(500);
                
                Serial.printf("Stan połączenia MQTT po begin(): %s\n", 
                            m_mqtt.isConnected() ? "Połączony" : "Rozłączony");
                
                if (m_mqtt.publish("hydrosense/test", "test")) {
                    Serial.println("Test publikacji udany");
                    
                    if (publishHAConfig()) {
                        Serial.println("Konfiguracja HA opublikowana pomyślnie");
                        return true;
                    }
                }
                Serial.println("Błąd publikacji");
                return false;
            }
            Serial.println("Błąd połączenia!");
            return false;
        }
        return true;
    }

    void initializePins() {
        pinMode(PIN_TRIG, OUTPUT);
        pinMode(PIN_ECHO, INPUT);
        pinMode(PIN_BUZZER, OUTPUT);
        digitalWrite(PIN_BUZZER, LOW);

        // Sprawdź czy przycisk jest wciśnięty przy starcie
        if(digitalRead(PIN_BUTTON) == LOW) {
            Serial.println("Przycisk wciśnięty - reset ustawień WiFi");
            wm.resetSettings();
            ESP.restart();
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
        WiFiManagerParameter custom_mqtt_port("port", "MQTT Port", String(mqtt_port).c_str(), 6);
        WiFiManagerParameter custom_mqtt_user("user", "MQTT User", mqtt_user, 40);
        WiFiManagerParameter custom_mqtt_pass("pass", "MQTT Password", mqtt_password, 40);
        
        // Dodaj parametry do WiFiManager
        wm.addParameter(&custom_mqtt_server);
        wm.addParameter(&custom_mqtt_port);
        wm.addParameter(&custom_mqtt_user);
        wm.addParameter(&custom_mqtt_pass);
        
        // Skonfiguruj zachowanie WiFiManager
        wm.setConfigPortalTimeout(180);
        wm.setConnectTimeout(30);
        wm.setDebugOutput(true);
        
        // Własny nagłówek portalu
        wm.setTitle("HydroSense Setup");
        
        // Próba automatycznego połączenia
        if(!wm.autoConnect("HydroSense-Setup")) {
            Serial.println("Błąd połączenia! Reset...");
            ESP.restart();
        }
        
        // Jeśli połączono, zapisz parametry MQTT
        if(WiFi.status() == WL_CONNECTED) {
            Serial.println("Połączono z WiFi!");
            
            // Zapisz parametry MQTT - użyj strncpy zamiast strcpy dla bezpieczeństwa
            strncpy(mqtt_broker, custom_mqtt_server.getValue(), 39);
            mqtt_broker[39] = '\0';  // Upewnij się że string jest zakończony
            
            mqtt_port = atoi(custom_mqtt_port.getValue());
            
            strncpy(mqtt_user, custom_mqtt_user.getValue(), 39);
            mqtt_user[39] = '\0';
            
            strncpy(mqtt_password, custom_mqtt_pass.getValue(), 39);
            mqtt_password[39] = '\0';
            
            Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
            Serial.printf("MQTT Server: %s\n", mqtt_broker);
            Serial.printf("MQTT Port: %d\n", mqtt_port);
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
        String html = "<html><body>";
        html += "<h1>HydroSense</h1>";
        
        float waterLevel = m_levelSensor.measureDistance();
        float percentage = calculateWaterPercentage(waterLevel);
        
        html += "<p>Poziom wody: " + String(waterLevel, 1) + " mm (" + String(percentage, 1) + "%)</p>";
        html += "<p>Stan rezerwy: " + String(m_settings.checkReserveState(waterLevel) ? "TAK" : "NIE") + "</p>";
        
        html += "</body></html>";
        
        m_webServer.send(200, "text/html", html);
    }

    bool publishHAConfig() {
        if (!m_mqtt.isConnected()) {
            Serial.println("Próba publikacji bez połączenia MQTT!");
            return false;
        }
        
        String config;
        
        // Debugowanie połączenia
        Serial.println("Status MQTT przed publikacją: " + String(m_mqtt.isConnected() ? "Połączony" : "Rozłączony"));
        
        // Konfiguracja czujnika poziomu wody
        config = createSensorConfig("water_level", "Water Level", "mm");
        Serial.println("Próba publikacji water_level config...");
        Serial.println("Temat: homeassistant/sensor/hydrosense/water_level/config");
        Serial.println("Zawartość: " + config);
        
        if (!m_mqtt.publish("homeassistant/sensor/hydrosense/water_level/config", config.c_str())) {
            Serial.println("Błąd publikacji konfiguracji water_level");
            return false;
        }
        Serial.println("Opublikowano water_level config");
        delay(100);  // Zwiększamy opóźnienie
        
        // Konfiguracja czujnika procentowego
        config = createSensorConfig("water_level_percent", "Water Level Percentage", "%");
        Serial.println("Próba publikacji water_level_percent config...");
        if (!m_mqtt.publish("homeassistant/sensor/hydrosense/water_level_percent/config", config.c_str())) {
            Serial.println("Błąd publikacji konfiguracji water_level_percent");
            return false;
        }
        Serial.println("Opublikowano water_level_percent config");
        delay(100);
        
        // Konfiguracja czujnika rezerwy
        config = createReserveSensorConfig();
        Serial.println("Próba publikacji reserve config...");
        if (!m_mqtt.publish("homeassistant/binary_sensor/hydrosense/reserve/config", config.c_str())) {
            Serial.println("Błąd publikacji konfiguracji reserve");
            return false;
        }
        Serial.println("Opublikowano reserve config");
        
        return true;
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
};

} // namespace HydroSense

// Główna pętla programu
void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);
    
    static HydroSense::HydroSenseApp app;
    
    while (true) {
        app.run();
        yield();
    }
}

void loop() {
  // Pusty loop - wszystko dzieje się w klasie HydroSenseApp
}
