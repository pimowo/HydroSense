#include "network.h"
#include "globals.h"
#include <WiFiManager.h>
#include <EEPROM.h>
#include <ESP8266HTTPUpdateServer.h>
// Update API is provided by the ESP8266 core headers already included via other headers

// Konfiguracja strony i formularzy (przeniesione z main.cpp)
const char CONFIG_PAGE[] PROGMEM = R"rawliteral(
  <!DOCTYPE html>
  <html>
    <head>
        <meta charset='UTF-8'>
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>HydroSense</title>
        <style>body{font-family:Arial,sans-serif;background:#1a1a1a;color:#fff;}/*minified*/</style>
        <script>/* minimal client scripts omitted for brevity in module */</script>
    </head>
    <body>
        <h1>HydroSense</h1>
        %BUTTONS%
        %CONFIG_FORMS%
        %UPDATE_FORM%
        %FOOTER%
    </body>
  </html>
)rawliteral";

const char UPDATE_FORM[] PROGMEM = R"rawliteral(
<div class='section'>
    <h2>Aktualizacja firmware</h2>
    <form method='POST' action='/update' enctype='multipart/form-data'>
        <table class='config-table' style='margin-bottom: 15px;'>
            <tr><td colspan='2'><input type='file' name='update' accept='.bin'></td></tr>
        </table>
        <input type='submit' value='Aktualizuj firmware' class='btn btn-orange'>
    </form>
    <div id='update-progress' style='display:none'>
        <div class='progress'>
            <div id='progress-bar' class='progress-bar' role='progressbar' style='width: 0%'>0%</div>
        </div>
    </div>
</div>
)rawliteral";

const char PAGE_FOOTER[] PROGMEM = R"rawliteral(
<div class='footer'>
    <a href='https://github.com/pimowo/HydroSense' target='_blank'>Project by PMW</a>
</div>
)rawliteral";

String getConfigPage() {
    String html = FPSTR(CONFIG_PAGE);
    bool mqttConnected = client.connected();
    String mqttStatus = mqttConnected ? "Połączony" : "Rozłączony";
    String mqttStatusClass = mqttConnected ? "success" : "error";
    String soundStatus = config.soundEnabled ? "Włączony" : "Wyłączony";
    String soundStatusClass = config.soundEnabled ? "success" : "error";

    String buttons = F(
        "<div class='section'>"
        "<div class='buttons-container'>"
        "<button class='btn btn-blue' onclick='rebootDevice()'>Restart urządzenia</button>"
        "<button class='btn btn-red' onclick='factoryReset()'>Przywróć ustawienia fabryczne</button>"
        "</div>"
        "</div>"
    );

    html.replace("%MQTT_STATUS%", mqttStatus);
    html.replace("%MQTT_STATUS_CLASS%", mqttStatusClass);
    html.replace("%SOUND_STATUS%", soundStatus);
    html.replace("%SOUND_STATUS_CLASS%", soundStatusClass);
    html.replace("%SOFTWARE_VERSION%", SOFTWARE_VERSION);
    html.replace("%BUTTONS%", buttons);
    html.replace("%UPDATE_FORM%", FPSTR(UPDATE_FORM));
    html.replace("%FOOTER%", FPSTR(PAGE_FOOTER));

    // Build config forms compactly
    String configForms = F("<form method='POST' action='/save'>");
    configForms += F("<div class='section'><h2>Konfiguracja MQTT</h2><table class='config-table'>");
    configForms += F("<tr><td>Serwer</td><td><input type='text' name='mqtt_server' value='") + String(config.mqtt_server) + F("'></td></tr>");
    configForms += F("<tr><td>Port</td><td><input type='number' name='mqtt_port' value='") + String(config.mqtt_port) + F("'></td></tr>");
    configForms += F("</table></div>");
    configForms += F("<div class='section'><h2>Ustawienia zbiornika</h2><table class='config-table'>");
    configForms += "<tr><td>Odległość przy pustym [mm]</td><td><input type='number' name='tank_empty' value='" + String(config.tank_empty) + "'></td></tr>";
    configForms += "<tr><td>Odległość przy pełnym [mm]</td><td><input type='number' name='tank_full' value='" + String(config.tank_full) + "'></td></tr>";
    configForms += F("</table></div>");
    configForms += F("<div class='section'><input type='submit' value='Zapisz ustawienia' class='btn btn-blue'></div></form>");

    html.replace("%CONFIG_FORMS%", configForms);

#if DEBUG
    if (html.indexOf('%') != -1) {
        DEBUG_PRINT("Uwaga: Niektóre znaczniki nie zostały zastąpione!");
        int pos = html.indexOf('%');
        int start = pos > 20 ? pos - 20 : 0;
        DEBUG_PRINT(html.substring(start, pos + 20));
    }
#endif

    return html;
}

void handleRoot() {
    server.send(200, "text/html", getConfigPage());
}

bool connectMQTT() {   
    if (!mqtt.begin(config.mqtt_server, 1883, config.mqtt_user, config.mqtt_password)) {
        DEBUG_PRINT("\nBŁĄD POŁĄCZENIA MQTT!");
        return false;
    }
    DEBUG_PRINT("MQTT połączono pomyślnie!");
    return true;
}

void setupWiFi() {
    // Start non-blocking WiFi connection using saved credentials.
    WiFi.mode(WIFI_STA);
    WiFi.begin();
    timers.lastWiFiAttempt = millis();
    DEBUG_PRINT("Rozpoczęto asynchroniczne łączenie WiFi");
}

void handleSave() {
    if (server.method() != HTTP_POST) { server.send(405, "text/plain", "Method Not Allowed"); return; }

    bool needMqttReconnect = false;

    String oldServer = config.mqtt_server;
    int oldPort = config.mqtt_port;
    String oldUser = config.mqtt_user;
    String oldPassword = config.mqtt_password;

    strlcpy(config.mqtt_server, server.arg("mqtt_server").c_str(), sizeof(config.mqtt_server));
    config.mqtt_port = server.arg("mqtt_port").toInt();
    strlcpy(config.mqtt_user, server.arg("mqtt_user").c_str(), sizeof(config.mqtt_user));
    strlcpy(config.mqtt_password, server.arg("mqtt_password").c_str(), sizeof(config.mqtt_password));

    config.tank_full = server.arg("tank_full").toInt();
    config.tank_empty = server.arg("tank_empty").toInt();
    config.reserve_level = server.arg("reserve_level").toInt();
    config.tank_diameter = server.arg("tank_diameter").toInt();

    config.pump_delay = server.arg("pump_delay").toInt();
    config.pump_work_time = server.arg("pump_work_time").toInt();

    if (oldServer != config.mqtt_server || oldPort != config.mqtt_port || oldUser != config.mqtt_user || oldPassword != config.mqtt_password) {
        needMqttReconnect = true;
    }

    saveConfig();

    if (needMqttReconnect) {
        if (mqtt.isConnected()) mqtt.disconnect();
        connectMQTT();
    }

    server.send(204);
}

void handleDoUpdate() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        if (upload.filename == "") { String m = "update:error:No file selected"; webSocket.broadcastTXT(m); server.send(204); return; }
        if (!Update.begin(upload.contentLength)) { Update.printError(Serial); String m = "update:error:Update initialization failed"; webSocket.broadcastTXT(m); server.send(204); return; }
        { String m = "update:0"; webSocket.broadcastTXT(m); }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) { Update.printError(Serial); String m = "update:error:Write failed"; webSocket.broadcastTXT(m); return; }
        int progress = (upload.totalSize * 100) / upload.contentLength;
        String progressMsg = String("update:") + String(progress);
        webSocket.broadcastTXT(progressMsg);
    } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) { String m = "update:100"; webSocket.broadcastTXT(m); server.send(204); delay(1000); ESP.restart(); } else { Update.printError(Serial); String m = "update:error:Update failed"; webSocket.broadcastTXT(m); server.send(204); }
    }
}

void handleUpdateResult() {
    if (Update.hasError()) {
        server.send(200, "text/html", "<h1>Aktualizacja nie powiodła się</h1><a href='/'>Powrót</a>");
    } else {
        server.send(200, "text/html", "<h1>Aktualizacja zakończona powodzeniem</h1>Urządzenie zostanie zrestartowane...");
        delay(1000);
        ESP.restart();
    }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    if (type == WStype_CONNECTED) {
        Serial.printf("[%u] Connected\n", num);
    }
}

void setupWebServer() {
    server.on("/", handleRoot);
    server.on("/update", HTTP_POST, handleUpdateResult, handleDoUpdate);
    server.on("/save", handleSave);
    server.on("/reboot", HTTP_POST, [](){ server.send(200, "text/plain", "Restarting..."); delay(1000); ESP.restart(); });
    server.on("/factory-reset", HTTP_POST, [](){ server.send(200, "text/plain", "Resetting to factory defaults..."); delay(200); factoryReset(); });
    server.begin();
}

// Exponential backoff for WiFi reconnect attempts
void handleWiFiBackoff() {
    static unsigned long backoffDelay = 5000; // start 5s
    static int attempts = 0;
    const unsigned long MAX_DELAY = 300000; // 5 minutes

    if (WiFi.status() == WL_CONNECTED) {
        // reset backoff on success
        attempts = 0;
        backoffDelay = 5000;
        return;
    }

    unsigned long now = millis();
    if (now - timers.lastWiFiAttempt < backoffDelay) return;

    timers.lastWiFiAttempt = now;
    attempts++;
    DEBUG_PRINTF("WiFi reconnect attempt %d, delay %lu\n", attempts, backoffDelay);
    WiFi.begin();

    // calculate next delay (exponential, capped)
    unsigned long next = backoffDelay * 2UL;
    if (next > MAX_DELAY) next = MAX_DELAY;
    backoffDelay = next;
}
