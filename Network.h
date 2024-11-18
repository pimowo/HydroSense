#ifndef NETWORK_H
#define NETWORK_H

#include "SystemStatus.h"
#include "Config.h"

class NetworkManager {
public:
    NetworkManager(ConfigManager& configManager, SystemStatus& status);
    bool setupWiFi();
    void checkWiFiConnection();
    void setupAP();

private:
    ConfigManager& configManager;
    SystemStatus& status;
};

#endif