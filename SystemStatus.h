#ifndef SYSTEM_STATUS_H
#define SYSTEM_STATUS_H

struct SystemStatus {
    bool isServiceMode = false;
    bool soundEnabled = true;
    bool pumpActive = false;
    bool pumpSafetyLock = false;
    bool waterAlarmActive = false;
    bool waterReserveActive = false;
    bool isWiFiConnected = false;
    bool isMQTTConnected = false;
    unsigned long lastSoundAlert = 0;
};

extern SystemStatus systemStatus; // Declaration of the global instance

#endif