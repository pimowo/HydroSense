// Network.cpp
#include "Network.h"
#include "Constants.h"

void NetworkManager::checkWiFiConnection() {
    if (WiFi.status() != WL_CONNECTED) {
        unsigned long currentMillis = millis();
        if (currentMillis - lastWiFiCheck >= WIFI_CHECK_INTERVAL) {
            Serial.print(".");
            lastWiFiCheck = millis();
            if (WiFi.status() == WL_DISCONNECTED) {
                WiFi.reconnect();
            }
        }
        systemStatus.isWiFiConnected = false;
    } else {
        systemStatus.isWiFiConnected = true;
    }
}

bool NetworkManager::isConnected() const {
    return WiFi.status() == WL_CONNECTED;
}

void NetworkManager::setupAP() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("HydroSense-Setup");
    Serial.print("AP IP address: ");
    Serial.println(WiFi.softAPIP());
}

String NetworkManager::getLocalIP() const {
    return WiFi.localIP().toString();
}