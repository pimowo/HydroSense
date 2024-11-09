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
    // Stałe czasowe
    static constexpr unsigned long UPDATE_INTERVAL = 1000;        // ms
    static constexpr unsigned long MQTT_RETRY_INTERVAL = 5000;    // ms
    static constexpr unsigned long WIFI_CHECK_INTERVAL = 30000;   // ms
    
    // Stałe MQTT
    const char* MQTT_BROKER = "192.168.1.14";
    const uint16_t MQTT_PORT = 1883;
    const char* MQTT_USER = "hydrosense";
    const char* MQTT_PASSWORD = "hydrosense";

    // Komponenty sieciowe
    WiFiClient m_wifiClient;
    String m_deviceId;
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
    
    // Zmienne czasowe
    unsigned long m_lastUpdateTime;
    unsigned long m_lastWiFiCheck;
    unsigned long m_lastMqttRetry;

public:
    HydroSenseApp() :
        m_deviceId(String(ESP.getChipId(), HEX)),
        m_device(m_deviceId.c_str()),
        m_mqtt(m_wifiClient, m_device, 25),
        m_webServer(80),
        m_haWaterLevelSensor("water_level"),
        m_haWaterLevelPercentSensor("water_level_percent"),
        m_reserveSensor("reserve"),
        m_lastUpdateTime(0),
        m_lastWiFiCheck(0),
        m_lastMqttRetry(0)
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
        pinMode(PIN_TRIG, OUTPUT);
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
        WiFi.setSleepMode(WIFI_NONE_SLEEP);
        WiFi.setOutputPower(20.5); // maksymalna moc nadawania
        delay(100);
        
        Serial.print("\nŁączenie z WiFi");
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(250);
            Serial.print(".");
            yield();
            attempts++;
            
            if (attempts % 4 == 0) {
                Serial.println();
                Serial.printf("Próba %d/20, RSSI: %d dBm\n", attempts, WiFi.RSSI());
            }
        }
        Serial.println();
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("Połączono z WiFi, IP: %s, RSSI: %d dBm\n", 
                         WiFi.localIP().toString().c_str(), 
                         WiFi.RSSI());
        } else {
            Serial.println("Nie udało się połączyć z WiFi!");
        }
    }

    void initializeHomeAssistant() {
        m_device.setName("HydroSense");
        m_device.setSoftwareVersion("09.11.24");
        m_device.setManufacturer("PMW");
        m_device.setModel("HS ESP8266");

        m_haWaterLevelSensor.setName("Water Level");
        m_haWaterLevelSensor.setDeviceClass("distance");
        m_haWaterLevelSensor.setUnitOfMeasurement("mm");

        m_haWaterLevelPercentSensor.setName("Water Level Percentage");
        m_haWaterLevelPercentSensor.setIcon("mdi:water-percent");
        m_haWaterLevelPercentSensor.setUnitOfMeasurement("%");

        m_reserveSensor.setName("Water Reserve");
        m_reserveSensor.setDeviceClass("problem");

        if (connectMQTT()) {
            publishHAConfig();
        }
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
            switch (error) {
                case OTA_AUTH_ERROR: Serial.println("Auth Failed"); break;
                case OTA_BEGIN_ERROR: Serial.println("Begin Failed"); break;
                case OTA_CONNECT_ERROR: Serial.println("Connect Failed"); break;
                case OTA_RECEIVE_ERROR: Serial.println("Receive Failed"); break;
                case OTA_END_ERROR: Serial.println("End Failed"); break;
            }
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

        m_webServer.begin();
        Serial.println("Serwer HTTP uruchomiony");
    }

    bool connectMQTT() {
        if (!m_mqtt.isConnected()) {
            Serial.print("Łączenie z MQTT...");
            if (m_mqtt.begin(MQTT_BROKER, MQTT_PORT, MQTT_USER, MQTT_PASSWORD)) {
                Serial.println("OK");
                publishHAConfig();
                return true;
            } else {
                Serial.println("BŁĄD");
                return false;
            }
        }
        return true;
    }

    void publishHAConfig() {
        if (m_mqtt.isConnected()) {
            String config;
            String topic;

            // Konfiguracja water_level
            config = createSensorConfig("water_level", "Water Level", "mm");
            topic = "homeassistant/sensor/hydrosense/water_level/config";
            m_mqtt.publish(topic.c_str(), config.c_str(), true);

            // Konfiguracja water_level_percent
            config = createSensorConfig("water_level_percent", "Water Level Percentage", "%");
            topic = "homeassistant/sensor/hydrosense/water_level_percent/config";
            m_mqtt.publish(topic.c_str(), config.c_str(), true);

            // Konfiguracja reserve
            topic = "homeassistant/binary_sensor/hydrosense/reserve/config";
            config = createReserveSensorConfig();
            m_mqtt.publish(topic.c_str(), config.c_str(), true);

            Serial.println("Opublikowano konfigurację HA");
        }
    }

    void checkWiFiConnection() {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi rozłączone, próba ponownego połączenia...");
            WiFi.disconnect();
            delay(100);
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
            
            int attempts = 0;
            while (WiFi.status() != WL_CONNECTED && attempts < 10) {
                delay(100);
                yield();
                attempts++;
            }
            
            if (WiFi.status() == WL_CONNECTED) {
                Serial.printf("Ponownie połączono z WiFi, IP: %s\n", 
                            WiFi.localIP().toString().c_str());
            }
        }
    }

    void updateHomeAssistantSensors(float waterLevel) {
        if (m_mqtt.isConnected()) {
            String payload;
            
            // Aktualizacja water_level
            payload = "{\"value\":" + String(waterLevel, 1) + "}";
            m_mqtt.publish("hydrosense/water_level/state", payload.c_str());

            // Aktualizacja water_level_percent
            float percentage = calculateWaterPercentage(waterLevel);
            payload = "{\"value\":" + String(percentage, 1) + "}";
            m_mqtt.publish("hydrosense/water_level_percent/state", payload.c_str());

            // Aktualizacja reserve
            bool isInReserve = m_settings.checkReserveState(waterLevel);
            m_mqtt.publish("hydrosense/reserve/state", isInReserve ? "ON" : "OFF");
        }
    }

    void update() {
        float waterLevel = m_levelSensor.measureDistance();
        
        if (m_mqtt.isConnected()) {
            updateHomeAssistantSensors(waterLevel);
        }

        m_pumpController.update();
        checkAlarmsAndNotifications();
    }

    void run() {
        unsigned long currentTime = millis();
        
        // Sprawdzanie WiFi
        if (currentTime - m_lastWiFiCheck >= WIFI_CHECK_INTERVAL) {
            checkWiFiConnection();
            m_lastWiFiCheck = currentTime;
        }

        // Jeśli WiFi jest połączone
        if (WiFi.status() == WL_CONNECTED) {
            // Próba połączenia MQTT jeśli rozłączone
            if (!m_mqtt.isConnected() && 
                (currentTime - m_lastMqttRetry >= MQTT_RETRY_INTERVAL)) {
                connectMQTT();
                m_lastMqttRetry = currentTime;
            }
            
            m_mqtt.loop();
            ArduinoOTA.handle();
            m_webServer.handleClient();
        }

        // Regularna aktualizacja
        if (currentTime - m_lastUpdateTime >= UPDATE_INTERVAL) {
            update();
            m_lastUpdateTime = currentTime;
        }

        yield(); // Zapobieganie watchdog reset
    }

private:
    float calculateWaterPercentage(float waterLevel) {
        float maxLevel = m_settings.getEmptyDistance() - m_settings.getFullDistance();
        if (maxLevel <= 0) return 0.0f;
        
        float currentLevel = m_settings.getEmptyDistance() - waterLevel;
        float percentage = (currentLevel / maxLevel) * 100.0f;
        return constrain(percentage, 0.0f, 100.0f);
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
        config += "\"model\":\"HS ESP8266\",";           // Zmiana modelu
        config += "\"manufacturer\":\"PMW\"";            // Zmiana producenta
        config += "}}";
        return config;
    }
};

} // namespace HydroSense

// Główna pętla programu
void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);  // czekaj na port szeregowy
    
    static HydroSense::HydroSenseApp app;
    
    while (true) {
        app.run();
        yield();
    }
}

void loop() {
  // Pusty loop - wszystko dzieje się w klasie HydroSenseApp
}
