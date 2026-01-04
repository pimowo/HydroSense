#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

struct Config {
    uint8_t version;
    bool soundEnabled;
    char mqtt_server[40];
    uint16_t mqtt_port;
    char mqtt_user[32];
    char mqtt_password[32];
    int tank_full;
    int tank_empty;
    int reserve_level;
    int tank_diameter;
    int pump_delay;
    int pump_work_time;
    char checksum;
};

extern Config config;

void setDefaultConfig();
bool loadConfig();
void saveConfig();
char calculateChecksum(const Config& cfg);

#endif // CONFIG_H
