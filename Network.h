#ifndef NETWORK_H
#define NETWORK_H

#include <ESP8266WiFi.h>
#include "ConfigManager.h"
#include "SystemStatus.h"

class NetworkManager {
public:
    NetworkManager(ConfigManager& configManager, SystemStatus& systemStatus);
    bool setupWiFi();  // Tylko jedna wersja, zwracająca bool
    void checkWiFiConnection();
    bool isConnected() const;
    void setupAP();
    String getLocalIP() const;

private:
    ConfigManager& configManager;
    SystemStatus& systemStatus;
    static const unsigned long WIFI_CHECK_INTERVAL = 5000;  // 5 sekund
    unsigned long lastWiFiCheck;
    bool wifiInitiated;
};

#endif