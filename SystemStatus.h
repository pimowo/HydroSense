#ifndef SYSTEM_STATUS_H
#define SYSTEM_STATUS_H

struct SystemStatus {
    bool isWiFiConnected = false;
    bool isMQTTConnected = false;
    bool isServiceEnabled = true;
    bool soundEnabled = true;
};

extern SystemStatus systemStatus;

#endif