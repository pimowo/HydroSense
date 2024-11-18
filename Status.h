// Status.h
#pragma once

struct SystemStatus {
    // Stan urządzenia
    bool isServiceEnabled = true;
    bool isPumpEnabled = true;
    bool isSoundEnabled = true;
    bool isAlarmActive = false;
    bool isPumpRunning = false;
    
    // Pomiary
    float currentWaterLevel = 0;
    float currentDistance = 0;
    unsigned long pumpStartTime = 0;
    
    // Alarmy
    bool isLowWaterAlarm = false;
    bool isHighWaterAlarm = false;
    bool isPumpAlarm = false;
    
    // Stan połączenia
    bool isWiFiConnected = false;
    bool isMQTTConnected = false;
};