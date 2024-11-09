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
#include <PubSubClient.h>

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
const char* MQTT_BROKER = "192.168.1.14";
const int MQTT_PORT = 1883;
const char* MQTT_USER = "hydrosense";
const char* MQTT_PASSWORD = "hydrosense";

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
    // Deklaracje zmiennych członkowskich
    WiFiClient m_wifiClient;
    PubSubClient mqtt;
    ESP8266WebServer m_webServer;
    Settings m_settings;
    WaterLevelSensor m_waterLevelSensor;
    unsigned long m_lastUpdateTime;
    static constexpr unsigned long UPDATE_INTERVAL = 1000; // ms
    static constexpr unsigned long MQTT_RETRY_INTERVAL = 5000; // ms
    String MQTT_CLIENT_ID;

public:
    HydroSenseApp() :
        mqtt(m_wifiClient),  // Inicjalizacja z m_wifiClient
        m_webServer(80),
        m_lastUpdateTime(0)
    {
        Serial.println("\n=== HydroSense - Inicjalizacja ===");
        
        // Załaduj ustawienia z EEPROM
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

    void run() {
        unsigned long currentTime = millis();
        
        if (currentTime - m_lastUpdateTime >= UPDATE_INTERVAL) {
            update();
            m_lastUpdateTime = currentTime;
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            mqtt.loop();
            ArduinoOTA.handle();
            m_webServer.handleClient();
        }
    }

private:
    void printSettings() {
        Serial.println("\nAktualne ustawienia:");
        Serial.printf("- Średnica zbiornika: %.1f mm\n", m_settings.getTankDiameter());
        Serial.printf("- Odległość gdy pełny: %.1f mm\n", m_settings.getFullDistance());
        Serial.printf("- Odległość gdy pusty: %.1f mm\n", m_settings.getEmptyDistance());
        Serial.printf("- Próg rezerwy: %.1f%%\n", m_settings.getReserveThreshold());
        Serial.printf("- Dźwięk włączony: %s\n", m_settings.isSoundEnabled() ? "Tak" : "Nie");
    }

    void welcomeBuzzer() {
        if (m_settings.isSoundEnabled()) {
            digitalWrite(PIN_BUZZER, HIGH);
            delay(100);
            digitalWrite(PIN_BUZZER, LOW);
            delay(100);
            digitalWrite(PIN_BUZZER, HIGH);
            delay(100);
            digitalWrite(PIN_BUZZER, LOW);
        }
    }

    void initializePins() {
        pinMode(PIN_TRIG, OUTPUT);
        pinMode(PIN_ECHO, INPUT);
        pinMode(PIN_SENSOR, INPUT_PULLUP);
        pinMode(PIN_PUMP, OUTPUT);
        pinMode(PIN_BUZZER, OUTPUT);
        pinMode(PIN_BUTTON, INPUT_PULLUP);
    }

    void initializeWiFi() {
        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        
        unsigned long startTime = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startTime < 30000) {
            delay(500);
            Serial.print(".");
        }
        Serial.println();
        
        Serial.print("WiFi status: ");
        Serial.println(WiFi.status() == WL_CONNECTED ? "Połączono" : "Brak połączenia");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
    }

    void initializeHomeAssistant() {
        MQTT_CLIENT_ID = "HydroSense_" + String(ESP.getChipId(), HEX);
        
        mqtt.setServer(MQTT_BROKER, MQTT_PORT);
        mqtt.setCallback([this](char* topic, byte* payload, unsigned int length) {
            handleMQTTMessage(topic, payload, length);
        });

        if (connectMQTT()) {
            Serial.println("Połączono z MQTT");
            publishHAConfig();
        }
    }

    void publishHAConfig() {
        // Unikalny identyfikator urządzenia
        String deviceId = "hydrosense_" + String(ESP.getChipId());
        
        // Konfiguracja urządzenia
        String device_config = "{\"identifiers\":[\"" + deviceId + "\"],"
                            "\"name\":\"HydroSense\","
                            "\"model\":\"HydroSense v1.0\","
                            "\"manufacturer\":\"DIY\","
                            "\"sw_version\":\"1.0.0\"}";

        // Sensor poziomu wody (mm)
        String config = "{\"name\":\"Water Level\","
                      "\"device_class\":\"distance\","
                      "\"state_topic\":\"hydrosense/" + deviceId + "/water_level\","
                      "\"unit_of_measurement\":\"mm\","
                      "\"value_template\":\"{{ value | float }}\","
                      "\"unique_id\":\"" + deviceId + "_water_level\","
                      "\"device\":" + device_config + "}";

        mqtt.publish("homeassistant/sensor/hydrosense/water_level/config", 
                    config.c_str(), 
                    true);

        // Sensor procentowy
        config = "{\"name\":\"Water Level Percentage\","
                "\"state_topic\":\"hydrosense/" + deviceId + "/water_level_percent\","
                "\"unit_of_measurement\":\"%\","
                "\"value_template\":\"{{ value | float }}\","
                "\"unique_id\":\"" + deviceId + "_water_level_percent\","
                "\"device\":" + device_config + "}";

        mqtt.publish("homeassistant/sensor/hydrosense/water_level_percent/config", 
                    config.c_str(), 
                    true);

        // Binary sensor rezerwy
        config = "{\"name\":\"Water Reserve Status\","
                "\"device_class\":\"problem\","
                "\"state_topic\":\"hydrosense/" + deviceId + "/reserve\","
                "\"payload_on\":\"ON\","
                "\"payload_off\":\"OFF\","
                "\"unique_id\":\"" + deviceId + "_reserve\","
                "\"device\":" + device_config + "}";

        mqtt.publish("homeassistant/binary_sensor/hydrosense/reserve/config", 
                    config.c_str(), 
                    true);

        Serial.println("Opublikowano konfigurację HA");
    }

    void handleMQTTMessage(char* topic, byte* payload, unsigned int length) {
        Serial.print("Otrzymano wiadomość MQTT [");
        Serial.print(topic);
        Serial.print("] ");
        for (int i = 0; i < length; i++) {
            Serial.print((char)payload[i]);
        }
        Serial.println();
    }

    bool connectMQTT() {
        if (!mqtt.connected()) {
            Serial.print("Łączenie z MQTT...");
            if (mqtt.connect(MQTT_CLIENT_ID.c_str(), MQTT_USER, MQTT_PASSWORD)) {
                Serial.println("połączono");
                return true;
            } else {
                Serial.print("błąd, rc=");
                Serial.println(mqtt.state());
                return false;
            }
        }
        return true;
    }

    void initializeWebServer() {
        // Strona główna
        m_webServer.on("/", HTTP_GET, [this]() {
            float waterLevel = m_waterLevelSensor.measureDistance();
            float percentage = calculateWaterPercentage(waterLevel);
            
            String html = "<html><head>";
            html += "<meta charset='utf-8'>";
            html += "<meta http-equiv='refresh' content='5'>";
            html += "<style>body{font-family: Arial, sans-serif;margin:40px;}</style>";
            html += "</head><body>";
            html += "<h1>HydroSense Status</h1>";
            html += "<p>Poziom wody: " + String(waterLevel, 1) + " mm</p>";
            html += "<p>Poziom wody: " + String(percentage, 1) + "%</p>";
            html += "<p>Stan rezerwy: " + String(m_settings.isInReserve() ? "ON" : "OFF") + "</p>";
            html += "<p>Stan pompy: " + String(digitalRead(PIN_PUMP) ? "Włączona" : "Wyłączona") + "</p>";
            html += "<p>Stan MQTT: " + String(mqtt.connected() ? "Połączony" : "Rozłączony") + "</p>";
            html += "<h2>Ustawienia</h2>";
            html += "<p>Średnica zbiornika: " + String(m_settings.getTankDiameter(), 2) + " mm</p>";
            html += "<p>Odległość gdy pełny: " + String(m_settings.getFullDistance(), 2) + " mm</p>";
            html += "<p>Odległość gdy pusty: " + String(m_settings.getEmptyDistance(), 2) + " mm</p>";
            html += "<p>Próg rezerwy: " + String(m_settings.getReserveThreshold(), 2) + " mm</p>";
            html += "<p>Histereza rezerwy: " + String(m_settings.getReserveHysteresis(), 2) + " mm</p>";
            html += "<p>Dźwięk: " + String(m_settings.isSoundEnabled() ? "Włączony" : "Wyłączony") + "</p>";
            html += "</body></html>";
                        
            m_webServer.send(200, "text/html", html);
            
            Serial.println("Strona WWW wyświetlona");
        });

        // API do pobierania danych w formacie JSON
        m_webServer.on("/api/status", HTTP_GET, [this]() {
            float waterLevel = m_waterLevelSensor.measureDistance();
            float percentage = calculateWaterPercentage(waterLevel);
            
            String json = "{";
            json += "\"water_level\":" + String(waterLevel, 1) + ",";
            json += "\"water_level_percent\":" + String(percentage, 1) + ",";
            json += "\"pump_state\":" + String(digitalRead(PIN_PUMP)) + ",";
            json += "\"mqtt_connected\":" + String(mqtt.connected()) + ",";
            json += "\"uptime\":" + String(millis() / 1000);
            json += "}";
            
            m_webServer.send(200, "application/json", json);
        });

        m_webServer.begin();
        Serial.println("Serwer WWW uruchomiony na porcie 80");
    }

    void initializeOTA() {
        ArduinoOTA.onStart([]() {
            Serial.println("Start aktualizacji OTA");
        });
        
        ArduinoOTA.onEnd([]() {
            Serial.println("\nKoniec aktualizacji");
        });
        
        ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
            Serial.printf("Postęp: %u%%\r", (progress / (total / 100)));
        });
        
        ArduinoOTA.begin();
        Serial.println("OTA gotowe");
    }

    void updateHomeAssistantSensors(float waterLevel) {
        if (mqtt.connected()) {
            String deviceId = "hydrosense_" + String(ESP.getChipId());
            
            // Poziom wody w mm
            mqtt.publish(("hydrosense/" + deviceId + "/water_level").c_str(), 
                        String(waterLevel, 1).c_str(), 
                        true);

            // Poziom wody w procentach
            float percentage = calculateWaterPercentage(waterLevel);
            mqtt.publish(("hydrosense/" + deviceId + "/water_level_percent").c_str(), 
                        String(percentage, 1).c_str(), 
                        true);

            // Stan rezerwy
            bool isInReserve = m_settings.checkReserveState(waterLevel);
            mqtt.publish(("hydrosense/" + deviceId + "/reserve").c_str(), 
                        isInReserve ? "ON" : "OFF", 
                        true);

            Serial.printf("Wysłano do HA - poziom: %.1f mm (%.1f%%), rezerwa: %s\n", 
                        waterLevel, percentage, isInReserve ? "ON" : "OFF");
        }
    }

    float calculateWaterPercentage(float waterLevel) {
        float fullLevel = m_settings.getFullDistance();
        float emptyLevel = m_settings.getEmptyDistance();
        float percentage = ((emptyLevel - waterLevel) / (emptyLevel - fullLevel)) * 100.0f;
        return constrain(percentage, 0.0f, 100.0f);
    }

    void update() {
        float waterLevel = m_waterLevelSensor.measureDistance();
        Serial.print("Zmierzony poziom wody: ");
        Serial.print(waterLevel);
        Serial.println(" mm");
        
        if (mqtt.connected()) {
            updateHomeAssistantSensors(waterLevel);
        } else {
            Serial.println("Próba ponownego połączenia z MQTT...");
            connectMQTT();
        }

        checkAlarmsAndNotifications();
    }

    void checkAlarmsAndNotifications() {
        float waterLevel = m_waterLevelSensor.measureDistance();
        
        if (waterLevel < m_settings.getReserveThreshold() && m_settings.isSoundEnabled()) {
            digitalWrite(PIN_BUZZER, HIGH);
        } else {
            digitalWrite(PIN_BUZZER, LOW);
        }
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
