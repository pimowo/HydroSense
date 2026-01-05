#ifndef NETWORK_H
#define NETWORK_H

#include <Arduino.h>
#include <WebSocketsServer.h>

void setupWiFi();
bool connectMQTT();
void setupWebServer();
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);
String getConfigPage();
void handleRoot();
void handleSave();
void handleDoUpdate();
void handleUpdateResult();
void handleWiFiBackoff();

#endif // NETWORK_H
