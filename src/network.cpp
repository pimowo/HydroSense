#include "network.h"
#include "globals.h"
#include <WiFiManager.h>
#include <EEPROM.h>
#include <ESP8266HTTPUpdateServer.h>
// Update API is provided by the ESP8266 core headers already included via other headers

// Konfiguracja strony i formularzy (przeniesione z main.cpp)
const char CONFIG_PAGE[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="pl">
    <head>
        <meta charset="utf-8">
        <meta name="viewport" content="width=device-width,initial-scale=1">
        <title>HydroSense — Konfiguracja</title>
        <style>
            :root{--bg:#0f1115;--panel:#15181d;--muted:#9aa3b2;--accent:#4dd0e1;--accent-2:#7bd389;--danger:#ff6b6b;--glass:rgba(255,255,255,0.03)}
            *{box-sizing:border-box}
            html,body{height:100%;margin:0;font-family:Inter,Segoe UI,Helvetica,Arial,sans-serif;background:linear-gradient(180deg,var(--bg),#0b0c0f);color:#e6eef3}
            .wrap{max-width:1100px;margin:20px auto;padding:18px}
            header{display:flex;align-items:center;justify-content:space-between;gap:12px}
            h1{font-size:1.1rem;margin:0}
            .meta{color:var(--muted);font-size:0.85rem}
            .layout{display:grid;grid-template-columns:1fr 360px;gap:18px;margin-top:18px}
            @media(max-width:820px){.layout{grid-template-columns:1fr} .side{order:2}}
            .panel{background:linear-gradient(180deg,var(--panel),#0f1316);border-radius:12px;padding:16px;box-shadow:0 6px 18px rgba(0,0,0,0.6);backdrop-filter:blur(4px)}
            label{display:block;color:var(--muted);font-size:0.85rem;margin-bottom:6px}
            input[type=text],input[type=number],input[type=password],select{width:100%;padding:10px;border-radius:8px;border:1px solid rgba(255,255,255,0.04);background:var(--glass);color:#e6eef3}
            .row{display:flex;gap:10px}
            .btn{display:inline-block;padding:10px 14px;border-radius:10px;border:0;background:var(--accent);color:#041316;cursor:pointer}
            .btn.ghost{background:transparent;border:1px solid rgba(255,255,255,0.04);color:var(--muted)}
            .small{font-size:0.85rem;padding:8px 10px}
            .muted{color:var(--muted);font-size:0.85rem}
            .status{padding:8px;border-radius:8px;background:rgba(255,255,255,0.02);display:flex;align-items:center;gap:8px}
            .footer{margin-top:14px;color:var(--muted);font-size:0.8rem;text-align:center}
            .section-title{display:flex;justify-content:space-between;align-items:center;margin-bottom:10px}
            .wifi-list{margin:8px 0;padding:8px;border-radius:8px;background:rgba(255,255,255,0.02)}
            .field-grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}
            @media(max-width:480px){.field-grid{grid-template-columns:1fr}}
        </style>
        <script>
            function confirmAction(msg, path){ if(confirm(msg)){ fetch(path,{method:'POST'}).then(()=>location.reload()) } }
            function submitForm(){ document.querySelector('form').submit(); }
            // simple helper to toggle sections on mobile
            function toggle(id){const e=document.getElementById(id); if(e) e.style.display = (e.style.display==='none')?'block':'none'}
            function fetchNetworks(){
                const el = document.querySelector('.wifi-list');
                if(!el) return;
                el.innerHTML = '<div class="muted">Skanowanie... proszę czekać</div>';
                fetch('/scan_wifi').then(r=>r.json()).then(list=>{
                    if(!list || list.length===0){ el.innerHTML = '<div class="muted">Brak sieci</div>'; return; }
                    let html='';
                    list.sort((a,b)=>b.rssi-a.rssi);
                    list.forEach(item=>{
                        const lock = item.secure? '🔒':'🔓';
                        html += `<div style="padding:6px;border-bottom:1px solid rgba(255,255,255,0.02);display:flex;align-items:center;justify-content:space-between"><div><a href='#' onclick="document.querySelector('input[name=wifi_ssid]').value='${item.ssid}';return false">${item.ssid}</a><div class='muted' style='font-size:0.8rem'>RSSI: ${item.rssi} ${lock}</div></div><div><button class='btn ghost small' onclick="document.querySelector('input[name=wifi_ssid]').value='${item.ssid}';">Wybierz</button></div></div>`;
                    });
                    el.innerHTML = html;
                }).catch(err=>{ el.innerHTML = '<div class="muted">Błąd skanowania</div>'; });
            }
            // Form submit via fetch with client-side validation and feedback
            document.addEventListener('DOMContentLoaded', function(){
                const form = document.getElementById('config-form');
                if(!form) return;
                form.addEventListener('submit', function(ev){
                    ev.preventDefault();
                    const status = document.getElementById('status-msg');
                    status.innerHTML = '';
                    const data = new FormData(form);
                    const port = parseInt(data.get('mqtt_port')||0,10);
                    const tankEmpty = parseInt(data.get('tank_empty')||0,10);
                    const tankFull = parseInt(data.get('tank_full')||0,10);
                    // basic validation
                    if (!(port >=1 && port <= 65535)) { status.innerHTML = '<div class="muted" style="color:#ff8a8a">Nieprawidłowy port MQTT (1-65535)</div>'; return; }
                    if (!(tankEmpty > tankFull)) { status.innerHTML = '<div class="muted" style="color:#ff8a8a">"tank_empty" musi być większe niż "tank_full"</div>'; return; }
                    // send via fetch
                    status.innerHTML = '<div class="muted">Wysyłanie...</div>';
                    fetch('/save', { method:'POST', body: new URLSearchParams(data) }).then(r=>r.json()).then(obj=>{
                        if(obj && obj.status === 'ok'){
                            status.innerHTML = '<div style="color:#7bd389">'+(obj.message||'Zapisano')+'</div>';
                        } else {
                            status.innerHTML = '<div style="color:#ff8a8a">'+(obj?obj.message:'Błąd serwera')+'</div>';
                        }
                    }).catch(err=>{ status.innerHTML = '<div style="color:#ff8a8a">Błąd połączenia</div>'; });
                });
            });
        </script>
    </head>
    <body>
        <div class="wrap">
            <header>
                <div>
                    <h1>HydroSense</h1>
                    <div class="meta">Wersja: %SOFTWARE_VERSION% — konfiguracja urządzenia</div>
                </div>
                <div class="row">
                    <button class="btn small" onclick="location.reload()">Odśwież</button>
                    <button class="btn ghost small" onclick="confirmAction('Czy na pewno zrestartować urządzenie?','/reboot')">Restart</button>
                    <button class="btn ghost small" onclick="confirmAction('Przywrócić ustawienia fabryczne?','/factory-reset')" style="background:transparent;border:1px solid rgba(255,255,255,0.06);">Factory reset</button>
                </div>
            </header>

            <div class="layout">
                <main class="panel">
                    <div class="section-title"><strong>Sieć Wi‑Fi</strong><span class="muted">Ustawienia i status</span></div>
                    <div class="status"><div style="width:10px;height:10px;border-radius:50%;background:var(--accent)"></div><div>%MQTT_STATUS%</div><div style="margin-left:auto;color:var(--muted)">%MQTT_STATUS_CLASS%</div></div>

                    <div style="margin-top:12px">
                        <form id='config-form' method='POST' action='/save'>
                            <div style="margin-bottom:12px">
                                <label>SSID (opcjonalne)</label>
                                <input type='text' name='wifi_ssid' placeholder='Pozostaw puste aby użyć zapisanych danych'>
                            </div>
                            <div style="margin-bottom:12px">
                                <label>Hasło (opcjonalne)</label>
                                <input type='password' name='wifi_pass' placeholder='Hasło sieci Wi‑Fi'>
                            </div>

                            <div style="margin-top:8px" class="section-title"><strong>MQTT</strong><span class="muted">Ustawienia brokera</span></div>
                            <div class="field-grid">
                                <div>
                                    <label>Serwer</label>
                                    <input type='text' name='mqtt_server' value='%MQTT_SERVER%'>
                                </div>
                                <div>
                                    <label>Port</label>
                                    <input type='number' name='mqtt_port' value='%MQTT_PORT%'>
                                </div>
                                <div>
                                    <label>Użytkownik</label>
                                    <input type='text' name='mqtt_user' value='%MQTT_USER%'>
                                </div>
                                <div>
                                    <label>Hasło</label>
                                    <input type='password' name='mqtt_password' value=''>
                                </div>
                            </div>

                            <div style="margin-top:12px" class="section-title"><strong>Zbiornik</strong><span class="muted">Wymiary i progi</span></div>
                            <div class="field-grid">
                                <div>
                                    <label>Odległość przy pustym [mm]</label>
                                    <input type='number' name='tank_empty' value='%TANK_EMPTY%'>
                                </div>
                                <div>
                                    <label>Odległość przy pełnym [mm]</label>
                                    <input type='number' name='tank_full' value='%TANK_FULL%'>
                                </div>
                                <div>
                                    <label>Rezerwa [mm]</label>
                                    <input type='number' name='reserve_level' value='%RESERVE_LEVEL%'>
                                </div>
                                <div>
                                    <label>Średnica zbiornika [mm]</label>
                                    <input type='number' name='tank_diameter' value='%TANK_DIAMETER%'>
                                </div>
                            </div>

                            <div style="margin-top:16px;display:flex;gap:10px;align-items:center">
                                <button type='submit' class='btn'>Zapisz ustawienia</button>
                                <button type='button' class='btn ghost' onclick="toggle('wifi-networks')">Pokaż sieci Wi‑Fi</button>
                                <div style="margin-left:auto" class="muted">%SOFTWARE_VERSION%</div>
                            </div>
                            <div id='status-msg' style='margin-top:10px'></div>
                        </form>
                    </div>

                    <div id="wifi-networks" style="display:none;margin-top:10px" class="panel">
                        <div class="section-title"><strong>Dostępne sieci</strong><span class="muted">Kliknij, by wypełnić SSID</span></div>
                        <div class="wifi-list">%WIFI_LIST%</div>
                    </div>
                </main>

                <aside class="side panel">
                    <div class="section-title"><strong>Szybkie akcje</strong></div>
                    <div style="display:flex;flex-direction:column;gap:8px">
                        %BUTTONS%
                        %UPDATE_FORM%
                    </div>
                    <div class="footer">%FOOTER%</div>
                </aside>
            </div>
        </div>
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

    String buttons = F(
        "<div style='display:flex;flex-direction:column;gap:8px'>"
        "<button class='btn ghost small' onclick='confirmAction(\"Czy na pewno zrestartować urządzenie?\", \"/reboot\")'>Restart</button>"
        "<button class='btn ghost small' onclick='confirmAction(\"Przywrócić ustawienia fabryczne?\", \"/factory-reset\")'>Factory reset</button>"
        "</div>"
    );

    html.replace("%MQTT_STATUS%", mqttStatus);
    html.replace("%MQTT_STATUS_CLASS%", mqttStatusClass == "success" ? "Połączony" : "Rozłączony");
    html.replace("%SOFTWARE_VERSION%", SOFTWARE_VERSION);
    html.replace("%BUTTONS%", buttons);
    html.replace("%UPDATE_FORM%", FPSTR(UPDATE_FORM));
    html.replace("%FOOTER%", FPSTR(PAGE_FOOTER));

    // Fill dynamic fields
    html.replace("%MQTT_SERVER%", String(config.mqtt_server));
    html.replace("%MQTT_PORT%", String(config.mqtt_port));
    html.replace("%MQTT_USER%", String(config.mqtt_user));

    html.replace("%TANK_EMPTY%", String(config.tank_empty));
    html.replace("%TANK_FULL%", String(config.tank_full));
    html.replace("%RESERVE_LEVEL%", String(config.reserve_level));
    html.replace("%TANK_DIAMETER%", String(config.tank_diameter));

    // Placeholder for WiFi list – client can call /scan_wifi to populate
    html.replace("%WIFI_LIST%", "<div class='muted'>Kliknij 'Pokaż sieci Wi‑Fi', aby przeskanować sieci.</div>");

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

void handleScanWifi() {
    int n = WiFi.scanNetworks();
    String out = "[";
    for (int i = 0; i < n; ++i) {
        String ssid = WiFi.SSID(i);
        int rssi = WiFi.RSSI(i);
        bool secure = WiFi.encryptionType(i) != ENC_TYPE_NONE;
        // Escape quotes in SSID
        ssid.replace("\"", "\\\"");
        out += "{\"ssid\":\"" + ssid + "\",\"rssi\":" + String(rssi) + ",\"secure\":" + String(secure?1:0) + "}";
        if (i < n-1) out += ",";
    }
    out += "]";
    server.send(200, "application/json", out);
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

    String wifi_ssid = server.arg("wifi_ssid");
    String wifi_pass = server.arg("wifi_pass");

    if (wifi_ssid.length() > 0) {
        // Start connecting immediately with provided credentials (not persisted)
        WiFi.mode(WIFI_STA);
        WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());
        timers.lastWiFiAttempt = millis();
        DEBUG_PRINT("Rozpoczęto łączenie do podanej sieci WiFi");
    }

    if (needMqttReconnect) {
        if (mqtt.isConnected()) mqtt.disconnect();
        connectMQTT();
    }

    // Respond with JSON so the client can show a message without reloading
    String resp = "{\"status\":\"ok\",\"message\":\"Ustawienia zapisane\"}";
    server.send(200, "application/json", resp);
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
    server.on("/scan_wifi", HTTP_GET, handleScanWifi);
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
