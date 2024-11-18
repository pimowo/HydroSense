// ConfigManager.cpp
#include "ConfigManager.h"

const char* ConfigManager::CONFIG_FILE = "/config.json";

ConfigManager::ConfigManager() : mounted(false) {
}

bool ConfigManager::begin() {
    if (!LittleFS.begin()) {
        Serial.println(F("Failed to mount LittleFS"));
        return false;
    }
    mounted = true;
    
    if (!LittleFS.exists(CONFIG_FILE)) {
        Serial.println(F("No configuration file found"));
        setDefaultConfig();
        return saveConfig();
    }
    
    return loadConfig();
}

void ConfigManager::setDefaultConfig() {
    // Konfiguracja sieci
    networkConfig.wifi_ssid = "";
    networkConfig.wifi_password = "";
    networkConfig.mqtt_server = "";
    networkConfig.mqtt_user = "";
    networkConfig.mqtt_password = "";
    
    // Konfiguracja zbiornika
    tankConfig.full = 65;         // TANK_FULL
    tankConfig.empty = 510;       // TANK_EMPTY
    tankConfig.reserve_level = 100;// RESERVE_LEVEL
    tankConfig.hysteresis = 10;   // HYSTERESIS
    tankConfig.diameter = 315;    // TANK_DIAMETER
    
    // Konfiguracja pompy
    pumpConfig.delay = 30;        // PUMP_DELAY
    pumpConfig.work_time = 60;    // PUMP_WORK_TIME
    
    // Konfiguracja systemu
    systemConfig.ap_timeout = 300;// 5 minut
    systemConfig.hostname = "HydroSense";
}

bool ConfigManager::saveConfig() {
    if (!mounted) return false;

    DynamicJsonDocument doc(1024);
    
    // Sekcja network
    JsonObject network = doc.createNestedObject("network");
    network["wifi_ssid"] = networkConfig.wifi_ssid;
    network["wifi_password"] = networkConfig.wifi_password;
    network["mqtt_server"] = networkConfig.mqtt_server;
    network["mqtt_user"] = networkConfig.mqtt_user;
    network["mqtt_password"] = networkConfig.mqtt_password;
    
    // Sekcja tank
    JsonObject tank = doc.createNestedObject("tank");
    tank["full"] = tankConfig.full;
    tank["empty"] = tankConfig.empty;
    tank["reserve_level"] = tankConfig.reserve_level;
    tank["hysteresis"] = tankConfig.hysteresis;
    tank["diameter"] = tankConfig.diameter;
    
    // Sekcja pump
    JsonObject pump = doc.createNestedObject("pump");
    pump["delay"] = pumpConfig.delay;
    pump["work_time"] = pumpConfig.work_time;
    
    // Sekcja system
    JsonObject system = doc.createNestedObject("system");
    system["ap_timeout"] = systemConfig.ap_timeout;
    system["hostname"] = systemConfig.hostname;
    
    File configFile = LittleFS.open(CONFIG_FILE, "w");
    if (!configFile) {
        Serial.println(F("Failed to open config file for writing"));
        return false;
    }
    
    serializeJson(doc, configFile);
    configFile.close();
    return true;
}

bool ConfigManager::loadConfig() {
    if (!mounted) return false;

    File configFile = LittleFS.open(CONFIG_FILE, "r");
    if (!configFile) {
        Serial.println(F("Failed to open config file"));
        return false;
    }

    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, configFile);
    configFile.close();
    
    if (error) {
        Serial.println(F("Failed to parse config file"));
        return false;
    }
    
    // Wczytanie konfiguracji sieci
    if (doc.containsKey("network")) {
        JsonObject network = doc["network"];
        networkConfig.wifi_ssid = network["wifi_ssid"].as<String>();
        networkConfig.wifi_password = network["wifi_password"].as<String>();
        networkConfig.mqtt_server = network["mqtt_server"].as<String>();
        networkConfig.mqtt_user = network["mqtt_user"].as<String>();
        networkConfig.mqtt_password = network["mqtt_password"].as<String>();
    }
    
    // Wczytanie konfiguracji zbiornika
    if (doc.containsKey("tank")) {
        JsonObject tank = doc["tank"];
        tankConfig.full = tank["full"] | 65;
        tankConfig.empty = tank["empty"] | 510;
        tankConfig.reserve_level = tank["reserve_level"] | 100;
        tankConfig.hysteresis = tank["hysteresis"] | 10;
        tankConfig.diameter = tank["diameter"] | 315;
    }
    
    // Wczytanie konfiguracji pompy
    if (doc.containsKey("pump")) {
        JsonObject pump = doc["pump"];
        pumpConfig.delay = pump["delay"] | 30;
        pumpConfig.work_time = pump["work_time"] | 60;
    }
    
    // Wczytanie konfiguracji systemu
    if (doc.containsKey("system")) {
        JsonObject system = doc["system"];
        systemConfig.ap_timeout = system["ap_timeout"] | 300;
        systemConfig.hostname = system["hostname"] | "HydroSense";
    }
    
    return true;
}