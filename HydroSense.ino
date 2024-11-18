#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <ArduinoHA.h>
#include <ArduinoOTA.h>

#include "Constants.h"
#include "SystemStatus.h"
#include "ConfigManager.h"
#include "Network.h"
#include "HomeAssistant.h"
#include "Button.h"
#include "Alarm.h"

// Global instances
SystemStatus systemStatus;
ConfigManager configManager;
HADevice device("HydroSense");
WiFiClient client;
HAMqtt mqtt(client, device);
NetworkManager networkManager(configManager, systemStatus);
ESP8266WebServer webServer(80);
DNSServer dnsServer;

// Other global variables
ButtonState buttonState;

// Function declarations
void setupWiFi();
void setupAP();
void setupWebServer();
void switchToNormalMode();
void playConfirmationSound();
float measureDistance();