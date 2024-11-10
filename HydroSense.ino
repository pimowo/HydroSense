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

// Definicje makr
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
    uint32_t m_magic;

    // WiFi settings
    char m_wifiSSID[32];
    char m_wifiPassword[64];

    // MQTT settings
    char m_mqttServer[40];
    uint16_t m_mqttPort;
    char m_mqttUser[40];
    char m_mqttPassword[40];

    // Tank dimensions
    float m_tankDiameter;     // mm
    float m_tankWidth;        // mm
    float m_tankHeight;       // mm
    float m_fullDistance;     // mm
    float m_emptyDistance;    // mm
    float m_reserveLevel;     // mm
    float m_reserveHysteresis;// mm

    // Pump timing
    uint32_t m_pumpDelay;    // seconds
    uint32_t m_pumpWork;     // seconds

    // States
    bool m_soundEnabled = true;
    bool m_reserveState;     // Zmieniliśmy nazwę zmiennej z isInReserve na m_reserveState

public:
    Settings() {
        loadDefaults();
    }

    void loadDefaults() {
        m_magic = SETTINGS_MAGIC;
        
        // WiFi defaults
        memset(m_wifiSSID, 0, sizeof(m_wifiSSID));
        memset(m_wifiPassword, 0, sizeof(m_wifiPassword));

        // MQTT defaults
        memset(m_mqttServer, 0, sizeof(m_mqttServer));
        m_mqttPort = 1883;
        memset(m_mqttUser, 0, sizeof(m_mqttUser));
        memset(m_mqttPassword, 0, sizeof(m_mqttPassword));

        // Tank defaults
        m_tankDiameter = 200.0f;
        m_tankWidth = 0.0f;
        m_tankHeight = 300.0f;
        m_fullDistance = 50.0f;
        m_emptyDistance = 300.0f;
        m_reserveLevel = 250.0f;
        m_reserveHysteresis = 20.0f;

        // Pump defaults
        m_pumpDelay = 300;    // 5 minut
        m_pumpWork = 60;      // 1 minuta

        // State defaults
        m_soundEnabled = true;
        m_reserveState = false;

        save();
    }

    // WiFi getters and setters
    const char* getWiFiSSID() const { return m_wifiSSID; }
    void setWiFiCredentials(const char* ssid, const char* password) {
        strncpy(m_wifiSSID, ssid, sizeof(m_wifiSSID) - 1);
        strncpy(m_wifiPassword, password, sizeof(m_wifiPassword) - 1);
        m_wifiSSID[sizeof(m_wifiSSID) - 1] = '\0';
        m_wifiPassword[sizeof(m_wifiPassword) - 1] = '\0';
        save();
    }

    // MQTT getters and setters
    const char* getMqttServer() const { return m_mqttServer; }
    uint16_t getMqttPort() const { return m_mqttPort; }
    const char* getMqttUser() const { return m_mqttUser; }
    const char* getMqttPassword() const { return m_mqttPassword; }
    
    void setMqttConfig(const char* server, uint16_t port, const char* user, const char* password) {
        strncpy(m_mqttServer, server, sizeof(m_mqttServer) - 1);
        m_mqttPort = port;
        strncpy(m_mqttUser, user, sizeof(m_mqttUser) - 1);
        strncpy(m_mqttPassword, password, sizeof(m_mqttPassword) - 1);
        m_mqttServer[sizeof(m_mqttServer) - 1] = '\0';
        m_mqttUser[sizeof(m_mqttUser) - 1] = '\0';
        m_mqttPassword[sizeof(m_mqttPassword) - 1] = '\0';
        save();
    }

    void setReserveLevel(float level) {
        m_reserveLevel = level;
        save();
    }

    // Tank dimension getters and setters
    float getTankDiameter() const { return m_tankDiameter; }
    float getTankWidth() const { return m_tankWidth; }
    float getTankHeight() const { return m_tankHeight; }
    float getFullDistance() const { return m_fullDistance; }
    float getEmptyDistance() const { return m_emptyDistance; }
    float getReserveLevel() const { return m_reserveLevel; }
    float getReserveHysteresis() const { return m_reserveHysteresis; }

    void setTankDimensions(float diameter, float width, float height, float fullDist, float emptyDist, float reserveLevel) {
        m_tankDiameter = diameter;
        m_tankWidth = width;
        m_tankHeight = height;
        m_fullDistance = fullDist;
        m_emptyDistance = emptyDist;
        m_reserveLevel = reserveLevel;
        save();
    }

    // Pump timing getters and setters
    uint32_t getPumpDelay() const { return m_pumpDelay; }
    uint32_t getPumpWork() const { return m_pumpWork; }
    
    void setPumpTiming(uint32_t delay, uint32_t work) {
        m_pumpDelay = delay;
        m_pumpWork = work;
        save();
    }

    // State getters and setters
    bool isSoundEnabled() const { return m_soundEnabled; }
    void setSoundEnabled(bool enabled) {
        m_soundEnabled = enabled;
        save();
    }

    bool isInReserve() const { return m_reserveState; }

    bool checkReserveState(float currentDistance) {
        if (!m_reserveState && currentDistance >= m_reserveLevel) {
            m_reserveState = true;
        } else if (m_reserveState && currentDistance <= (m_reserveLevel - m_reserveHysteresis)) {
            m_reserveState = false;
        }
        return m_reserveState;
    }

    void save() {
        EEPROM.begin(512);
        uint16_t addr = 0;

        Serial.println("Zapisywanie ustawień do EEPROM");

        EEPROM.put(addr, m_magic); addr += sizeof(m_magic);
        EEPROM.put(addr, m_wifiSSID); addr += sizeof(m_wifiSSID);
        EEPROM.put(addr, m_wifiPassword); addr += sizeof(m_wifiPassword);
        EEPROM.put(addr, m_mqttServer); addr += sizeof(m_mqttServer);
        EEPROM.put(addr, m_mqttPort); addr += sizeof(m_mqttPort);
        EEPROM.put(addr, m_mqttUser); addr += sizeof(m_mqttUser);
        EEPROM.put(addr, m_mqttPassword); addr += sizeof(m_mqttPassword);
        EEPROM.put(addr, m_tankDiameter); addr += sizeof(m_tankDiameter);
        EEPROM.put(addr, m_tankWidth); addr += sizeof(m_tankWidth);
        EEPROM.put(addr, m_tankHeight); addr += sizeof(m_tankHeight);
        EEPROM.put(addr, m_fullDistance); addr += sizeof(m_fullDistance);
        EEPROM.put(addr, m_emptyDistance); addr += sizeof(m_emptyDistance);
        EEPROM.put(addr, m_reserveLevel); addr += sizeof(m_reserveLevel);
        EEPROM.put(addr, m_reserveHysteresis); addr += sizeof(m_reserveHysteresis);
        EEPROM.put(addr, m_pumpDelay); addr += sizeof(m_pumpDelay);
        EEPROM.put(addr, m_pumpWork); addr += sizeof(m_pumpWork);
        EEPROM.put(addr, m_soundEnabled); addr += sizeof(m_soundEnabled);
        EEPROM.put(addr, m_reserveState); addr += sizeof(m_reserveState);

        EEPROM.commit();
        EEPROM.end();

        Serial.println("Ustawienia zapisane");
        printSettings();

    }

    void load() {
        EEPROM.begin(512);
        uint16_t addr = 0;

        Serial.println("Ładowanie ustawień z EEPROM");

        EEPROM.get(addr, m_magic); addr += sizeof(m_magic);

        if (m_magic != SETTINGS_MAGIC) {
            Serial.println("Wykryto niezainicjalizowany EEPROM - ładowanie wartości domyślnych");
            EEPROM.end();
            loadDefaults();
            return;
        }

        EEPROM.get(addr, m_wifiSSID); addr += sizeof(m_wifiSSID);
        EEPROM.get(addr, m_wifiPassword); addr += sizeof(m_wifiPassword);
        EEPROM.get(addr, m_mqttServer); addr += sizeof(m_mqttServer);
        EEPROM.get(addr, m_mqttPort); addr += sizeof(m_mqttPort);
        EEPROM.get(addr, m_mqttUser); addr += sizeof(m_mqttUser);
        EEPROM.get(addr, m_mqttPassword); addr += sizeof(m_mqttPassword);
        EEPROM.get(addr, m_tankDiameter); addr += sizeof(m_tankDiameter);
        EEPROM.get(addr, m_tankWidth); addr += sizeof(m_tankWidth);
        EEPROM.get(addr, m_tankHeight); addr += sizeof(m_tankHeight);
        EEPROM.get(addr, m_fullDistance); addr += sizeof(m_fullDistance);
        EEPROM.get(addr, m_emptyDistance); addr += sizeof(m_emptyDistance);
        EEPROM.get(addr, m_reserveLevel); addr += sizeof(m_reserveLevel);
        EEPROM.get(addr, m_reserveHysteresis); addr += sizeof(m_reserveHysteresis);
        EEPROM.get(addr, m_pumpDelay); addr += sizeof(m_pumpDelay);
        EEPROM.get(addr, m_pumpWork); addr += sizeof(m_pumpWork);
        EEPROM.get(addr, m_soundEnabled); addr += sizeof(m_soundEnabled);
        EEPROM.get(addr, m_reserveState); addr += sizeof(m_reserveState);

        EEPROM.end();

        Serial.println("Ustawienia załadowane");
        printSettings();

    }

    void printSettings() {
        Serial.printf("m_wifiSSID: %s\n", m_wifiSSID);
        Serial.printf("m_wifiPassword: %s\n", m_wifiPassword);
        Serial.printf("m_mqttServer: %s\n", m_mqttServer);
        Serial.printf("m_mqttPort: %d\n", m_mqttPort);
        Serial.printf("m_mqttUser: %s\n", m_mqttUser);
        Serial.printf("m_mqttPassword: %s\n", m_mqttPassword);
        Serial.printf("m_tankDiameter: %.1f mm\n", m_tankDiameter);
        Serial.printf("m_tankWidth: %.1f mm\n", m_tankWidth);
        Serial.printf("m_tankHeight: %.1f mm\n", m_tankHeight);
        Serial.printf("m_fullDistance: %.1f mm\n", m_fullDistance);
        Serial.printf("m_emptyDistance: %.1f mm\n", m_emptyDistance);
        Serial.printf("m_reserveLevel: %.1f mm\n", m_reserveLevel);
        Serial.printf("m_reserveHysteresis: %.1f mm\n", m_reserveHysteresis);
        Serial.printf("m_pumpDelay: %d s\n", m_pumpDelay);
        Serial.printf("m_pumpWork: %d s\n", m_pumpWork);
        Serial.printf("m_soundEnabled: %d\n", m_soundEnabled);
        Serial.printf("m_reserveState: %d\n", m_reserveState);
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

    void handleButton() {
        static unsigned long pressStartTime = 0;
        static bool wasPressed = false;
        static bool serviceMode = false;
        static bool longPressActionExecuted = false;
        
        bool isPressed = (digitalRead(PIN_BUTTON) == LOW);
        
        if (isPressed && !wasPressed) {
            pressStartTime = millis();
            wasPressed = true;
            longPressActionExecuted = false;
        }
        else if (isPressed && wasPressed) {
            if (!longPressActionExecuted && (millis() - pressStartTime >= 1000)) {
                Serial.println("Wykonuję kasowanie alarmu...");
                digitalWrite(PIN_BUZZER, LOW);
                // Sygnał dźwiękowy: dwa krótkie piknięcia
                tone(PIN_BUZZER, 2000, 100);
                delay(150);
                tone(PIN_BUZZER, 2000, 100);
                longPressActionExecuted = true;
            }
        }
        else if (!isPressed && wasPressed) {
            unsigned long pressDuration = millis() - pressStartTime;
            wasPressed = false;
            
            if (!longPressActionExecuted && pressDuration < 1000) {
                serviceMode = !serviceMode;
                if (serviceMode) {
                    Serial.println("Włączam tryb serwisowy");
                    tone(PIN_BUZZER, 2000, 100);
                } else {
                    Serial.println("Wyłączam tryb serwisowy");
                    tone(PIN_BUZZER, 1000, 100);
                }
            }
        }
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
        
        // Sygnał dźwiękowy potwierdzający reset
        tone(PIN_BUZZER, 2000, 500);
        delay(600);
        tone(PIN_BUZZER, 1000, 500);
        delay(600);
        
        // Reset WiFiManager
        wm.resetSettings();
        delay(100); // Daj czas na reset WiFiManager
        
        // Reset własnych ustawień
        settings.loadDefaults();
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
        // Najpierw inicjalizujemy Serial jeśli nie jest zainicjalizowany
        if (!Serial) {
            Serial.begin(115200);
            delay(100); // Daj czas na inicjalizację
        }

        Serial.println("Inicjalizacja pinów...");
        
        // Inicjalizacja pinów
        pinMode(PIN_TRIG, OUTPUT);
        pinMode(PIN_ECHO, INPUT);
        pinMode(PIN_BUTTON, INPUT_PULLUP);
        pinMode(PIN_SENSOR, INPUT_PULLUP);
        pinMode(PIN_BUZZER, OUTPUT);
        digitalWrite(PIN_BUZZER, LOW);
        
        // Dodajemy delay aby upewnić się, że piny są stabilne
        delay(50);
        
        // Sprawdzenie stanu przycisku podczas startu
        if (digitalRead(PIN_SENSOR) == LOW) {
            Serial.println("Wykryto wciśnięty przycisk podczas startu!");
            // Seria krótkich sygnałów ostrzegawczych
            for(int i = 0; i < 3; i++) {
                tone(PIN_BUZZER, 2000, 100);
                delay(150);
            }
            Serial.println("Rozpoczynam reset...");
            delay(500);
            resetAll();
            return;
        }

        Serial.println("Inicjalizacja zakończona");
    }

    void welcomeBuzzer() {
        if (!settings.isSoundEnabled()) return;        
            // Prosta melodia powitalna
            tone(PIN_BUZZER, 1397, 100);  // F6
            delay(150);
            tone(PIN_BUZZER, 1568, 100);  // G6
            delay(150);
            tone(PIN_BUZZER, 1760, 150);  // A6
            delay(200);
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
        
        // Obsługa przycisku
        handleButton();

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
        Serial.printf("- Próg rezerwy: %.1f mm\n", settings.getReserveLevel());
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
        bool shouldAlarm = waterLevel >= settings.getReserveLevel() && settings.isSoundEnabled();
        
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
    
    void begin() {
        // implementacja metody begin
    }

    void handle() {
        server.handleClient();
    }
    
private:
    ESP8266WebServer server;
    Settings& settings;

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
                (uint16_t)server.arg("mqtt_port").toInt(),  // Dodanie jawnej konwersji na uint16_t
                server.arg("mqtt_user").c_str(),
                server.arg("mqtt_password").c_str()
            );
        }
        
        // Tank settings
        if (server.hasArg("tank_width") && server.hasArg("tank_height") && server.hasArg("tank_diameter")) {
            settings.setTankDimensions(
                server.arg("tank_diameter").toFloat(),
                server.arg("tank_width").toFloat(),
                server.arg("tank_height").toFloat(),
                server.arg("full_distance").toFloat(),
                server.arg("empty_distance").toFloat(),
                server.arg("reserve_level").toFloat()
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

const char* WebServer::HTML_FOOT = R"html(</body></html>)html";

} // namespace HydroSense

// Deklaracja zmiennych globalnych
HydroSense::HydroSenseApp* app = nullptr;
HydroSense::WebServer* webServer = nullptr;
HydroSense::Settings settings; // Deklaracja globalna

void startWebServer() {
    static HydroSense::WebServer webServerInstance(settings); // Przekaż argument 'settings'
    webServer = &webServerInstance;
    webServer->begin();
    Serial.println("Serwer WWW uruchomiony");
}

void setup() {
    Serial.begin(115200);
    Serial.println("");
    Serial.println("=== HydroSense - Inicjalizacja ===");

    // Inicjalizacja serwera WWW
    Serial.println("Inicjalizacja serwera WWW");
    startWebServer();
}

void loop() {
    ESP.wdtFeed();
    if (app) {
        app->run();
    }
    if (webServer) {
        webServer->handle();
    }
    yield();
}
