#include "config.h"
#ifdef ARDUINO
#include <EEPROM.h>
#endif

Config config;

void setDefaultConfig() {
    config.soundEnabled = true;
    strlcpy(config.mqtt_server, "", sizeof(config.mqtt_server));
    config.mqtt_port = 1883;
    strlcpy(config.mqtt_user, "", sizeof(config.mqtt_user));
    strlcpy(config.mqtt_password, "", sizeof(config.mqtt_password));
    config.tank_full = 50;
    config.tank_empty = 1050;
    config.reserve_level = 550;
    config.tank_diameter = 100;
    config.pump_delay = 5;
    config.pump_work_time = 30;
    config.checksum = calculateChecksum(config);
    saveConfig();
}

bool loadConfig() {
#ifdef ARDUINO
    EEPROM.begin(sizeof(Config) + 1);
    Config tempConfig;
    uint8_t *p = (uint8_t*)&tempConfig;
    for (size_t i = 0; i < sizeof(Config); i++) {
        p[i] = EEPROM.read(i);
    }
    EEPROM.end();
    char calculatedChecksum = calculateChecksum(tempConfig);
    if (calculatedChecksum == tempConfig.checksum) {
        memcpy(&config, &tempConfig, sizeof(Config));
        return true;
    } else {
        setDefaultConfig();
        return false;
    }
#else
    // Native build: no EEPROM available. Use defaults.
    setDefaultConfig();
    return true;
#endif
}

void saveConfig() {
#ifdef ARDUINO
    EEPROM.begin(sizeof(Config) + 1);
    config.checksum = calculateChecksum(config);
    uint8_t *p = (uint8_t*)&config;
    for (size_t i = 0; i < sizeof(Config); i++) {
        EEPROM.write(i, p[i]);
    }
    EEPROM.commit();
    EEPROM.end();
#else
    // Native build: no EEPROM. Do nothing.
#endif
}

char calculateChecksum(const Config& cfg) {
    const uint8_t* p = (const uint8_t*)&cfg;
    char checksum = 0;
    for (size_t i = 0; i < offsetof(Config, checksum); i++) {
        checksum ^= p[i];
    }
    return checksum;
}
