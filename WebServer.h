#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <ESP8266WebServer.h>
#include "ConfigManager.h"

class WebServerManager {
public:
    WebServerManager(ConfigManager& configManager);
    void setup();
    void handleClient();
    
private:
    ESP8266WebServer server;
    ConfigManager& configManager;
    
    void handleRoot();
    void handleGetConfig();
    void handleSaveConfig();
    void handleWiFiScan();
    bool handleCaptivePortal();
    bool isIp(String str);
    String toStringIp(IPAddress ip);
    void sendHeaders();
};

#endif