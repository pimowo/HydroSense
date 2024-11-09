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

// MQTT broker details
// const char* MQTT_BROKER = "192.168.1.14";
// const int MQTT_PORT = 1883;
// const char* MQTT_USER = "hydrosense";
// const char* MQTT_PASSWORD = "hydrosense";

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
        // wartości domyślne są już zainicjalizowane przy deklaracji zmiennych
    }
};

// Główna klasa aplikacji
class HydroSenseApp {
private:
    // Stałe
    static constexpr unsigned long UPDATE_INTERVAL = 1000; // ms
    static constexpr unsigned long MQTT_RETRY_INTERVAL = 5000; // ms
    
    // Stałe MQTT
    const char* MQTT_BROKER = "twoj_broker";
    const char* MQTT_USER = "twoj_uzytkownik";
    const char* MQTT_PASSWORD = "twoje_haslo";

    // Piny
    static constexpr uint8_t PIN_TRIGGER = D1;  // Pin trigger sensora ultradźwiękowego
    static constexpr uint8_t PIN_ECHO = D2;     // Pin echo sensora ultradźwiękowego
    static constexpr uint8_t PIN_BUZZER = D5;   // Pin buzzera
    static constexpr uint8_t PIN_PUMP = D6;     // Pin pompy

    // Komponenty sieciowe
    WiFiClient m_wifiClient;
    HADevice m_device;
    HAMqtt m_mqtt;
    ESP8266WebServer m_webServer;

    // Sensory HA
    HASensor m_haWaterLevelSensor;
    HASensor m_haWaterLevelPercentSensor;
    HABinarySensor m_reserveSensor;

    // Inne komponenty
    Settings m_settings;
    WaterLevelSensor m_levelSensor;
    PumpController m_pumpController;
    String m_deviceId;
    unsigned long m_lastUpdateTime;

public:
    HydroSenseApp() :
        m_deviceId(String(ESP.getChipId(), HEX)),
        m_device(m_deviceId.c_str()),
        m_mqtt(m_wifiClient, m_device),
        m_webServer(80),
        m_haWaterLevelSensor("water_level"),
        m_haWaterLevelPercentSensor("water_level_percent"),
        m_reserveSensor("reserve"),
        m_lastUpdateTime(0)
    {
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

    void initializePins() {
        pinMode(PIN_TRIGGER, OUTPUT);
        pinMode(PIN_ECHO, INPUT);
        pinMode(PIN_BUZZER, OUTPUT);
        pinMode(PIN_PUMP, OUTPUT);
        digitalWrite(PIN_BUZZER, LOW);
        digitalWrite(PIN_PUMP, LOW);
        Serial.println("Piny zainicjalizowane");
    }

    void welcomeBuzzer() {
        digitalWrite(PIN_BUZZER, HIGH);
        delay(100);
        digitalWrite(PIN_BUZZER, LOW);
        Serial.println("Sygnał powitalny");
    }

    void initializeWiFi() {
        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        
        Serial.print("Łączenie z WiFi");
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            Serial.print(".");
            attempts++;
        }
        Serial.println();
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.print("Połączono z WiFi, IP: ");
            Serial.println(WiFi.localIP());
        } else {
            Serial.println("Nie udało się połączyć z WiFi!");
        }
    }

    void initializeHomeAssistant() {
        // Konfiguracja urządzenia
        m_device.setName("HydroSense");
        m_device.setSoftwareVersion("1.0.0");
        m_device.setManufacturer("DIY");
        m_device.setModel("HydroSense v1.0");

        // Konfiguracja sensorów
        m_haWaterLevelSensor.setName("Water Level");
        m_haWaterLevelSensor.setDeviceClass("distance");
        m_haWaterLevelSensor.setUnitOfMeasurement("mm");

        m_haWaterLevelPercentSensor.setName("Water Level Percentage");
        m_haWaterLevelPercentSensor.setIcon("mdi:water-percent");
        m_haWaterLevelPercentSensor.setUnitOfMeasurement("%");

        m_reserveSensor.setName("Water Reserve");
        m_reserveSensor.setDeviceClass("problem");

        // Połączenie z MQTT
        connectMQTT();
    }

    void initializeOTA() {
        ArduinoOTA.setHostname("HydroSense");
        ArduinoOTA.setPassword("admin");
        
        ArduinoOTA.onStart([]() {
            Serial.println("Start aktualizacji OTA");
        });
        
        ArduinoOTA.onEnd([]() {
            Serial.println("\nKoniec aktualizacji OTA");
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

    void initializeWebServer() {
        m_webServer.on("/", [this]() {
            float waterLevel = m_levelSensor.measureDistance();
            String html = "<html><body>";
            html += "<h1>HydroSense</h1>";
            html += "<p>Poziom wody: " + String(waterLevel) + " mm</p>";
            html += "<p>Stan MQTT: " + String(m_mqtt.isConnected() ? "Połączony" : "Rozłączony") + "</p>";
            html += "</body></html>";
            m_webServer.send(200, "text/html", html);
        });

        m_webServer.on("/api/status", [this]() {
            float waterLevel = m_levelSensor.measureDistance();
            String json = "{";
            json += "\"water_level\":" + String(waterLevel) + ",";
            json += "\"water_level_percent\":" + String(calculateWaterPercentage(waterLevel)) + ",";
            json += "\"mqtt_connected\":" + String(m_mqtt.isConnected()) + ",";
            json += "\"reserve\":" + String(m_settings.checkReserveState(waterLevel));
            json += "}";
            m_webServer.send(200, "application/json", json);
        });

        m_webServer.begin();
        Serial.println("Serwer HTTP uruchomiony");
    }

    void printSettings() {
        Serial.println("Aktualne ustawienia:");
        Serial.printf("- Średnica zbiornika: %.1f mm\n", m_settings.getTankDiameter());
        Serial.printf("- Wysokość pełna: %.1f mm\n", m_settings.getFullDistance());
        Serial.printf("- Wysokość pusta: %.1f mm\n", m_settings.getEmptyDistance());
        Serial.printf("- Próg rezerwy: %.1f mm\n", m_settings.getReserveThreshold());
        Serial.printf("- Dźwięk włączony: %s\n", m_settings.isSoundEnabled() ? "Tak" : "Nie");
    }

    String createSensorConfig(const char* id, const char* name, const char* unit) {
        String deviceId = String(ESP.getChipId(), HEX);
        String config = "{";
        config += "\"name\":\"" + String(name) + "\",";
        config += "\"device_class\":\"" + String(id) + "\",";
        config += "\"state_topic\":\"hydrosense/" + String(id) + "/state\",";
        config += "\"unit_of_measurement\":\"" + String(unit) + "\",";
        config += "\"value_template\":\"{{ value_json.value }}\",";
        config += "\"unique_id\":\"hydrosense_" + deviceId + "_" + String(id) + "\",";
        config += "\"device\":{";
        config += "\"identifiers\":[\"hydrosense_" + deviceId + "\"],";
        config += "\"name\":\"HydroSense\",";
        config += "\"model\":\"HydroSense v1.0\",";
        config += "\"manufacturer\":\"DIY\"";
        config += "}}";
        return config;
    }

    void publishHAConfig() {
        if (m_mqtt.isConnected()) {
            String waterLevelTopic = "homeassistant/sensor/hydrosense/water_level/config";
            String config = createSensorConfig("water_level", "Water Level", "mm");
            m_mqtt.publish(waterLevelTopic.c_str(), config.c_str(), true);
            
            // Konfiguracja pozostałych sensorów...
        }
    }

    bool connectMQTT() {
        if (!m_mqtt.isConnected()) {
            if (m_mqtt.begin(MQTT_BROKER, MQTT_USER, MQTT_PASSWORD)) {
                Serial.println("Połączono z brokerem MQTT");
                return true;
            } else {
                Serial.println("Błąd połączenia z MQTT");
                return false;
            }
        }
        return true;
    }

    void updateHomeAssistantSensors(float waterLevel) {
        if (m_mqtt.isConnected()) {
            m_haWaterLevelSensor.setValue(String(waterLevel, 1).c_str());

            float percentage = calculateWaterPercentage(waterLevel);
            m_haWaterLevelPercentSensor.setValue(String(percentage, 1).c_str());

            bool isInReserve = m_settings.checkReserveState(waterLevel);
            m_reserveSensor.setState(isInReserve);
        }
    }

    void checkAlarmsAndNotifications() {
        float waterLevel = m_levelSensor.measureDistance();
        bool shouldAlarm = waterLevel < m_settings.getReserveThreshold() && m_settings.isSoundEnabled();
        
        digitalWrite(PIN_BUZZER, shouldAlarm ? HIGH : LOW);
        
        if (shouldAlarm) {
            Serial.println("ALARM: Niski poziom wody!");
        }
    }

    void update() {
        float waterLevel = m_levelSensor.measureDistance();
        Serial.print("Zmierzony poziom wody: ");
        Serial.print(waterLevel);
        Serial.println(" mm");
        
        if (m_mqtt.isConnected()) {
            updateHomeAssistantSensors(waterLevel);
        }

        m_pumpController.update();
        checkAlarmsAndNotifications();
    }

    void run() {
        unsigned long currentTime = millis();
        
        if (currentTime - m_lastUpdateTime >= UPDATE_INTERVAL) {
            update();
            m_lastUpdateTime = currentTime;
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            m_mqtt.loop();
            ArduinoOTA.handle();
            m_webServer.handleClient();
        }
    }

private:
    float calculateWaterPercentage(float waterLevel) {
        // Implementacja obliczania procentów...
        return 0.0f; // Tymczasowo
    }
};

} // namespace HydroSense

// Główna pętla programu
void setup() {
  Serial.begin(115200);
  HydroSense::HydroSenseApp app;
  
  while (true) {
    app.run();
    yield();
  }
}

void loop() {
  // Pusty loop - wszystko dzieje się w klasie HydroSenseApp
}
