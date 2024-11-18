#ifndef SYSTEM_STATUS_H
#define SYSTEM_STATUS_H

struct SystemStatus {
    bool isServiceMode = false;      // Tryb serwisowy
    bool soundEnabled = true;        // Stan dźwięku
    bool pumpActive = false;         // Stan pompy
    bool pumpSafetyLock = false;    // Blokada bezpieczeństwa pompy
    bool waterAlarmActive = false;   // Stan alarmu braku wody
    bool waterReserveActive = false; // Stan rezerwy wody
    bool isWiFiConnected = false;    // Status połączenia WiFi
    bool isMQTTConnected = false;    // Status połączenia MQTT
    unsigned long lastSoundAlert = 0; // Czas ostatniego alarmu dźwiękowego
};

extern SystemStatus systemStatus;

#endif