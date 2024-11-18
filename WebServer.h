// WebServer.h
#pragma once
#include <ESP8266WebServer.h>

void setupWebServer();
void handleRoot();
void handleGetConfig();
void handleSaveConfig();
void handleWiFiScan();
bool handleCaptivePortal();