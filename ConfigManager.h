// ConfigManager.h
#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

class ConfigManager {
public:
    // Struktura przechowująca konfigurację sieciową
    struct NetworkConfig {
        String wifi_ssid;
        String wifi_password;
        String mqtt_server;
        String mqtt_user;
        String mqtt_password;
    };

    // Struktura przechowująca konfigurację zbiornika
    struct TankConfig {
        int full;           // TANK_FULL
        int empty;          // TANK_EMPTY
        int reserve_level;  // RESERVE_LEVEL
        int hysteresis;     // HYSTERESIS
        int diameter;       // TANK_DIAMETER
    };

    // Struktura przechowująca konfigurację pompy
    struct PumpConfig {
        int delay;          // PUMP_DELAY
        int work_time;      // PUMP_WORK_TIME
    };

    // Struktura przechowująca konfigurację systemu
    struct SystemConfig {
        int ap_timeout;     // Timeout dla trybu AP (w sekundach)
        String hostname;    // Nazwa hosta
    };

    ConfigManager();
    
    bool begin();  // Inicjalizacja systemu plików i wczytanie konfiguracji
    bool saveConfig();  // Zapisanie konfiguracji
    bool loadConfig();  // Wczytanie konfiguracji
    void setDefaultConfig();  // Ustawienie domyślnej konfiguracji
    
    // Gettery dla konfiguracji
    NetworkConfig& getNetworkConfig() { return networkConfig; }
    TankConfig& getTankConfig() { return tankConfig; }
    PumpConfig& getPumpConfig() { return pumpConfig; }
    SystemConfig& getSystemConfig() { return systemConfig; }

private:
    NetworkConfig networkConfig;
    TankConfig tankConfig;
    PumpConfig pumpConfig;
    SystemConfig systemConfig;
    
    static const char* CONFIG_FILE;  // = "/config.json"
    
    bool mounted;  // Stan montowania systemu plików
};

#endif // CONFIG_MANAGER_H