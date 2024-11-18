// WebServer.cpp
#include "WebServer.h"

void setupWebServer() {
  // Strona główna
  webServer.on("/", HTTP_GET, handleRoot);

  // Endpointy API
  webServer.on("/api/config", HTTP_GET, handleGetConfig);
  webServer.on("/api/config", HTTP_POST, handleSaveConfig);
  webServer.on("/api/wifi/scan", HTTP_GET, handleWiFiScan);

  // Obsługa nieznanych ścieżek
  webServer.onNotFound([]() {
    if (!handleCaptivePortal()) {
      webServer.send(404, "text/plain", "Not Found");
    }
  });

  webServer.begin();
}

bool handleCaptivePortal() {
  if (!isIp(webServer.hostHeader())) {
    Serial.println(F("Redirect to captive portal"));
    webServer.sendHeader("Location", String("http://") + toStringIp(WiFi.softAPIP()), true);
    webServer.send(302, "text/plain", "");
    webServer.client().stop();
    return true;
  }
  return false;
}

void handleRoot() {
  String html = F("<!DOCTYPE html>"
                  "<html>"
                  "<head>"
                  "<meta charset='UTF-8'>"
                  "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                  "<title>HydroSense Configuration</title>"
                  "<style>"
                  "body { font-family: Arial, sans-serif; margin: 0; padding: 20px; background: #f0f0f0; }"
                  ".container { max-width: 800px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }"
                  ".section { margin-bottom: 20px; padding: 15px; border: 1px solid #ddd; border-radius: 4px; }"
                  ".section h2 { margin-top: 0; color: #333; }"
                  "input, select { width: calc(100% - 20px); padding: 8px; margin: 5px 0; border: 1px solid #ddd; border-radius: 4px; }"
                  "label { display: block; margin-top: 10px; color: #666; }"
                  "button { background-color: #4CAF50; color: white; padding: 10px 15px; border: none; border-radius: 4px; cursor: pointer; margin-right: 10px; }"
                  "button:hover { background-color: #45a049; }"
                  ".button-group { margin-top: 20px; }"
                  ".status { margin-top: 10px; padding: 10px; border-radius: 4px; }"
                  ".success { background-color: #dff0d8; color: #3c763d; }"
                  ".error { background-color: #f2dede; color: #a94442; }"
                  "</style>"
                  "</head>"
                  "<body>"
                  "<div class='container'>"
                  "<h1>HydroSense Configuration</h1>"

                  "<div class='section' id='network-section'>"
                  "<h2>Network Settings</h2>"
                  "<label>WiFi SSID:</label>"
                  "<input type='text' id='wifi_ssid'>"
                  "<button onclick='scanWiFi()'>Scan Networks</button>"
                  "<select id='wifi_networks' style='display:none' onchange='selectNetwork()'></select>"
                  "<label>WiFi Password:</label>"
                  "<input type='password' id='wifi_password'>"
                  "<label>MQTT Server:</label>"
                  "<input type='text' id='mqtt_server'>"
                  "<label>MQTT User:</label>"
                  "<input type='text' id='mqtt_user'>"
                  "<label>MQTT Password:</label>"
                  "<input type='password' id='mqtt_password'>"
                  "</div>"

                  "<div class='section' id='tank-section'>"
                  "<h2>Tank Settings</h2>"
                  "<label>Tank Full Level (mm):</label>"
                  "<input type='number' id='tank_full'>"
                  "<label>Tank Empty Level (mm):</label>"
                  "<input type='number' id='tank_empty'>"
                  "<label>Reserve Level (mm):</label>"
                  "<input type='number' id='reserve_level'>"
                  "<label>Hysteresis (mm):</label>"
                  "<input type='number' id='hysteresis'>"
                  "<label>Tank Diameter (mm):</label>"
                  "<input type='number' id='tank_diameter'>"
                  "</div>"

                  "<div class='section' id='pump-section'>"
                  "<h2>Pump Settings</h2>"
                  "<label>Pump Delay (seconds):</label>"
                  "<input type='number' id='pump_delay'>"
                  "<label>Pump Work Time (seconds):</label>"
                  "<input type='number' id='pump_work_time'>"
                  "</div>"

                  "<div class='button-group'>"
                  "<button onclick='saveConfig()'>Save Configuration</button>"
                  "<button onclick='loadConfig()'>Reload Configuration</button>"
                  "<button onclick='restartDevice()' style='background-color: #d9534f;'>Restart Device</button>"
                  "</div>"

                  "<div id='status' class='status' style='display:none;'></div>"
                  "</div>"

                  "<script>"
                  "function showStatus(message, isError = false) {"
                  "    const status = document.getElementById('status');"
                  "    status.className = 'status ' + (isError ? 'error' : 'success');"
                  "    status.textContent = message;"
                  "    status.style.display = 'block';"
                  "    setTimeout(() => status.style.display = 'none', 3000);"
                  "}"

                  "function loadConfig() {"
                  "    fetch('/api/config')"
                  "        .then(response => response.json())"
                  "        .then(data => {"
                  "            if (data.network) {"
                  "                document.getElementById('wifi_ssid').value = data.network.wifi_ssid || '';"
                  "                document.getElementById('mqtt_server').value = data.network.mqtt_server || '';"
                  "                document.getElementById('mqtt_user').value = data.network.mqtt_user || '';"
                  "            }"
                  "            if (data.tank) {"
                  "                document.getElementById('tank_full').value = data.tank.full || '';"
                  "                document.getElementById('tank_empty').value = data.tank.empty || '';"
                  "                document.getElementById('reserve_level').value = data.tank.reserve_level || '';"
                  "                document.getElementById('hysteresis').value = data.tank.hysteresis || '';"
                  "                document.getElementById('tank_diameter').value = data.tank.diameter || '';"
                  "            }"
                  "            if (data.pump) {"
                  "                document.getElementById('pump_delay').value = data.pump.delay || '';"
                  "                document.getElementById('pump_work_time').value = data.pump.work_time || '';"
                  "            }"
                  "            showStatus('Configuration loaded');"
                  "        })"
                  "        .catch(error => showStatus('Error loading configuration: ' + error, true));"
                  "}"

                  "function saveConfig() {"
                  "    const config = {"
                  "        network: {"
                  "            wifi_ssid: document.getElementById('wifi_ssid').value,"
                  "            wifi_password: document.getElementById('wifi_password').value,"
                  "            mqtt_server: document.getElementById('mqtt_server').value,"
                  "            mqtt_user: document.getElementById('mqtt_user').value,"
                  "            mqtt_password: document.getElementById('mqtt_password').value"
                  "        },"
                  "        tank: {"
                  "            full: parseInt(document.getElementById('tank_full').value),"
                  "            empty: parseInt(document.getElementById('tank_empty').value),"
                  "            reserve_level: parseInt(document.getElementById('reserve_level').value),"
                  "            hysteresis: parseInt(document.getElementById('hysteresis').value),"
                  "            diameter: parseInt(document.getElementById('tank_diameter').value)"
                  "        },"
                  "        pump: {"
                  "            delay: parseInt(document.getElementById('pump_delay').value),"
                  "            work_time: parseInt(document.getElementById('pump_work_time').value)"
                  "        }"
                  "    };"

                  "    fetch('/api/config', {"
                  "        method: 'POST',"
                  "        headers: {'Content-Type': 'application/json'},"
                  "        body: JSON.stringify(config)"
                  "    })"
                  "    .then(response => response.text())"
                  "    .then(result => showStatus('Configuration saved'))"
                  "    .catch(error => showStatus('Error saving configuration: ' + error, true));"
                  "}"

                  "function scanWiFi() {"
                  "    const button = event.target;"
                  "    const select = document.getElementById('wifi_networks');"
                  "    button.disabled = true;"
                  "    button.textContent = 'Scanning...';"
                  "    select.style.display = 'none';"

                  "    fetch('/api/wifi/scan')"
                  "        .then(response => response.json())"
                  "        .then(data => {"
                  "            select.innerHTML = '';"
                  "            select.appendChild(new Option('Select network...', ''));"
                  "            data.networks.sort((a, b) => b.rssi - a.rssi)"
                  "                .forEach(network => {"
                  "                    const option = new Option(network.ssid + ' (' + network.rssi + 'dBm)', network.ssid);"
                  "                    select.appendChild(option);"
                  "                });"
                  "            select.style.display = 'block';"
                  "            button.disabled = false;"
                  "            button.textContent = 'Scan Networks';"
                  "        })"
                  "        .catch(error => {"
                  "            showStatus('Error scanning networks: ' + error, true);"
                  "            button.disabled = false;"
                  "            button.textContent = 'Scan Networks';"
                  "        });"
                  "}"

                  "function selectNetwork() {"
                  "    const select = document.getElementById('wifi_networks');"
                  "    const ssidInput = document.getElementById('wifi_ssid');"
                  "    ssidInput.value = select.value;"
                  "}"

                  "function restartDevice() {"
                  "    if (confirm('Are you sure you want to restart the device?')) {"
                  "        showStatus('Restarting device...');"
                  "        fetch('/restart', { method: 'POST' })"
                  "            .then(() => showStatus('Device is restarting...'));"
                  "    }"
                  "}"

                  "// Load configuration when page loads"
                  "document.addEventListener('DOMContentLoaded', loadConfig);"
                  "</script>"
                  "</body>"
                  "</html>");

  webServer.send(200, "text/html", html);
}

void handleGetConfig() {
  DynamicJsonDocument doc(1024);

  const ConfigManager::NetworkConfig& networkConfig = configManager.getNetworkConfig();
  const ConfigManager::TankConfig& tankConfig = configManager.getTankConfig();
  const ConfigManager::PumpConfig& pumpConfig = configManager.getPumpConfig();

  JsonObject network = doc.createNestedObject("network");
  network["wifi_ssid"] = networkConfig.wifi_ssid;
  network["mqtt_server"] = networkConfig.mqtt_server;
  network["mqtt_user"] = networkConfig.mqtt_user;

  JsonObject tank = doc.createNestedObject("tank");
  tank["full"] = tankConfig.full;
  tank["empty"] = tankConfig.empty;
  tank["reserve_level"] = tankConfig.reserve_level;
  tank["hysteresis"] = tankConfig.hysteresis;
  tank["diameter"] = tankConfig.diameter;

  JsonObject pump = doc.createNestedObject("pump");
  pump["delay"] = pumpConfig.delay;
  pump["work_time"] = pumpConfig.work_time;

  String response;
  serializeJson(doc, response);
  webServer.send(200, "application/json", response);
}

void handleSaveConfig() {
  if (!webServer.hasArg("plain")) {
    webServer.send(400, "text/plain", "Brak danych");
    return;
  }

  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, webServer.arg("plain"));

  if (error) {
    webServer.send(400, "text/plain", "Nieprawidłowy format JSON");
    return;
  }

  bool configChanged = false;
  bool wifiChanged = false;

  ConfigManager::NetworkConfig& networkConfig = configManager.getNetworkConfig();
  ConfigManager::TankConfig& tankConfig = configManager.getTankConfig();
  ConfigManager::PumpConfig& pumpConfig = configManager.getPumpConfig();

  // Walidacja i aktualizacja konfiguracji sieci
  if (doc.containsKey("network")) {
    JsonObject network = doc["network"];

    if (network.containsKey("wifi_ssid")) {
      String newSSID = network["wifi_ssid"].as<String>();
      if (newSSID.length() > 0 && newSSID.length() <= 32) {
        networkConfig.wifi_ssid = newSSID;
        wifiChanged = true;
        configChanged = true;
      }
    }

    if (network.containsKey("wifi_password")) {
      String newPass = network["wifi_password"].as<String>();
      if (newPass.length() <= 64) {  // Pusty string jest dozwolony
        networkConfig.wifi_password = newPass;
        wifiChanged = true;
        configChanged = true;
      }
    }

    if (network.containsKey("mqtt_server")) {
      String newServer = network["mqtt_server"].as<String>();
      if (newServer.length() <= 64) {
        networkConfig.mqtt_server = newServer;
        configChanged = true;
      }
    }
  }

  // Walidacja i aktualizacja konfiguracji zbiornika
  if (doc.containsKey("tank")) {
    JsonObject tank = doc["tank"];

    if (tank.containsKey("full") && tank["full"].is<int>()) {
      int newFull = tank["full"].as<int>();
      if (newFull > 0 && newFull < 5000) {  // Maksymalna wysokość 5m
        tankConfig.full = newFull;
        configChanged = true;
      }
    }

    // Podobnie dla pozostałych parametrów zbiornika...
  }

  // Walidacja i aktualizacja konfiguracji pompy
  if (doc.containsKey("pump")) {
    JsonObject pump = doc["pump"];

    if (pump.containsKey("delay") && pump["delay"].is<int>()) {
      int newDelay = pump["delay"].as<int>();
      if (newDelay >= 0 && newDelay <= 3600) {  // Maksymalnie 1 godzina
        pumpConfig.delay = newDelay;
        configChanged = true;
      }
    }

    // Podobnie dla pozostałych parametrów pompy...
  }

  if (configChanged) {
    if (configManager.saveConfig()) {
      webServer.send(200, "text/plain", "Konfiguracja zapisana");
      if (wifiChanged) {
        delay(1000);  // Poczekaj na wysłanie odpowiedzi
        switchToNormalMode();
      }
    } else {
      webServer.send(500, "text/plain", "Błąd zapisu konfiguracji");
    }
  } else {
    webServer.send(400, "text/plain", "Brak zmian w konfiguracji");
  }
}