// Network.cpp
#include "Network.h"

bool setupWiFi() {
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

void setupAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  // Konfiguracja DNS dla captive portal
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  Serial.print(F("AP Started. IP: "));
  Serial.println(WiFi.softAPIP());
}

void handleWiFiScan() {
  DynamicJsonDocument doc(1024);
  JsonArray networks = doc.createNestedArray("networks");

  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; ++i) {
    JsonObject network = networks.createNestedObject();
    network["ssid"] = WiFi.SSID(i);
    network["rssi"] = WiFi.RSSI(i);
    network["encryption"] = WiFi.encryptionType(i);
  }

  String response;
  serializeJson(doc, response);
  webServer.send(200, "application/json", response);
}

