#ifndef NETWORK_H
#define NETWORK_H

#include <ESP8266WiFi.h>
#include "ConfigManager.h"
#include "SystemStatus.h"

class NetworkManager {
public:
    NetworkManager(ConfigManager& config, SystemStatus& status) : 
        configManager(config), 
        systemStatus(status),
        wifiInitiated(false) {}  // Add initialization

    bool setupWiFi();
    void setupAP();
    void checkWiFiConnection();
    bool isConnected() const;
    String getLocalIP() const;

private:
    ConfigManager& configManager;
    SystemStatus& systemStatus;
    bool wifiInitiated;  // Add missing member
    unsigned long lastWiFiCheck = 0;
};

#endif