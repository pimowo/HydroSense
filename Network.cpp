#include "Network.h"
#include "Constants.h"

NetworkManager::NetworkManager(ConfigManager& configManager, SystemStatus& systemStatus)
    : configManager(configManager), 
      systemStatus(systemStatus),
      lastWiFiCheck(0),
      wifiInitiated(false) {
}

bool NetworkManager::setupWiFi() {
    Serial.println(F("Rozpoczynanie konfiguracji WiFi..."));

    WiFi.mode(WIFI_STA);
    WiFi.begin(configManager.getNetworkConfig().wifi_ssid.c_str(),
               configManager.getNetworkConfig().wifi_password.c_str());

    // Czekaj maksymalnie 30 sekund na połączenie
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 30000) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        systemStatus.isWiFiConnected = true;
        Serial.print(F("Połączono z WiFi. IP: "));
        Serial.println(WiFi.localIP());
        return true;
    } else {
        systemStatus.isWiFiConnected = false;
        Serial.println(F("Nie udało się połączyć z WiFi"));
        return false;
    }
}

void NetworkManager::checkWiFiConnection() {
    ESP.wdtFeed();  // Reset watchdoga

    if (WiFi.status() != WL_CONNECTED) {
        if (millis() - lastWiFiCheck >= WIFI_CHECK_INTERVAL) {
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