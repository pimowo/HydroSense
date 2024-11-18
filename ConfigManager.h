// ConfigManager.h
#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

struct Config {
    bool soundEnabled = true;
};

class ConfigManager {
public:
    Config config;
};

#endif