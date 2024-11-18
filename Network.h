// Network.h
#ifndef NETWORK_H
#define NETWORK_H

#include <ESP8266WiFi.h>
#include "ConfigManager.h"
#include "SystemStatus.h"

class NetworkManager {
    ConfigManager& configManager;
    SystemStatus& systemStatus;
    unsigned long lastWiFiCheck;
    bool wifiInitiated;

public:
    NetworkManager(ConfigManager& config, SystemStatus& status);
    void setupAP();  // Dodana deklaracja
    void checkWiFiConnection();
    bool isConnected() const;
    String getLocalIP() const;
};

#endif