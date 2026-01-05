#include "config.h"
#ifdef ARDUINO
#include <EEPROM.h>
#endif

Config config;

#ifdef ARDUINO
// EEPROM layout helpers for config and network creds
const size_t CFG_SLOT_METADATA = sizeof(uint32_t);
const size_t CFG_SLOT_SIZE = CFG_SLOT_METADATA + sizeof(Config);
const int CFG_SLOTS = 2;
const size_t WIFI_SSID_MAX = 32;
const size_t WIFI_PASS_MAX = 64;
const size_t NETWORK_BASE = CFG_SLOT_SIZE * CFG_SLOTS;
#endif

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

bool loadNetworkCredentials(char* ssidOut, size_t ssidSize, char* passOut, size_t passSize) {
#ifdef ARDUINO
    if (!ssidOut || !passOut) return false;
    EEPROM.begin(NETWORK_BASE + WIFI_SSID_MAX + WIFI_PASS_MAX + 8);
    uint8_t bufSSID[WIFI_SSID_MAX];
    uint8_t bufPASS[WIFI_PASS_MAX];
    for (size_t i = 0; i < WIFI_SSID_MAX; ++i) bufSSID[i] = EEPROM.read(NETWORK_BASE + i);
    for (size_t i = 0; i < WIFI_PASS_MAX; ++i) bufPASS[i] = EEPROM.read(NETWORK_BASE + WIFI_SSID_MAX + i);
    uint8_t storedChecksum = EEPROM.read(NETWORK_BASE + WIFI_SSID_MAX + WIFI_PASS_MAX);
    // compute checksum
    uint8_t checksum = 0;
    for (size_t i = 0; i < WIFI_SSID_MAX; ++i) checksum ^= bufSSID[i];
    for (size_t i = 0; i < WIFI_PASS_MAX; ++i) checksum ^= bufPASS[i];
    EEPROM.end();
    if (checksum != storedChecksum) return false;
    // copy up to first NUL or full buffer
    size_t sLen = 0; while (sLen < WIFI_SSID_MAX && bufSSID[sLen]) ++sLen;
    size_t pLen = 0; while (pLen < WIFI_PASS_MAX && bufPASS[pLen]) ++pLen;
    size_t copyS = min(sLen, ssidSize-1);
    size_t copyP = min(pLen, passSize-1);
    memcpy(ssidOut, bufSSID, copyS); ssidOut[copyS]=0;
    memcpy(passOut, bufPASS, copyP); passOut[copyP]=0;
    return true;
#else
    (void)ssidOut; (void)ssidSize; (void)passOut; (void)passSize;
    return false;
#endif
}

void saveNetworkCredentials(const char* ssid, const char* pass) {
#ifdef ARDUINO
    EEPROM.begin(NETWORK_BASE + WIFI_SSID_MAX + WIFI_PASS_MAX + 8);
    uint8_t bufSSID[WIFI_SSID_MAX];
    uint8_t bufPASS[WIFI_PASS_MAX];
    memset(bufSSID, 0, WIFI_SSID_MAX);
    memset(bufPASS, 0, WIFI_PASS_MAX);
    if (ssid) strncpy((char*)bufSSID, ssid, WIFI_SSID_MAX-1);
    if (pass) strncpy((char*)bufPASS, pass, WIFI_PASS_MAX-1);
    for (size_t i = 0; i < WIFI_SSID_MAX; ++i) EEPROM.write(NETWORK_BASE + i, bufSSID[i]);
    for (size_t i = 0; i < WIFI_PASS_MAX; ++i) EEPROM.write(NETWORK_BASE + WIFI_SSID_MAX + i, bufPASS[i]);
    uint8_t checksum = 0;
    for (size_t i = 0; i < WIFI_SSID_MAX; ++i) checksum ^= bufSSID[i];
    for (size_t i = 0; i < WIFI_PASS_MAX; ++i) checksum ^= bufPASS[i];
    EEPROM.write(NETWORK_BASE + WIFI_SSID_MAX + WIFI_PASS_MAX, checksum);
    ESP.wdtFeed();
    EEPROM.commit();
    EEPROM.end();
#else
    (void)ssid; (void)pass;
#endif
}

bool loadConfig() {
#ifdef ARDUINO
    // Simple 2-slot wear-leveling: each slot contains a uint32_t seq + Config
    const size_t SLOT_METADATA = sizeof(uint32_t);
    const size_t SLOT_SIZE = SLOT_METADATA + sizeof(Config);
    const int SLOTS = 2;
    EEPROM.begin(SLOT_SIZE * SLOTS + 8);

    uint32_t bestSeq = 0;
    Config bestCfg;
    bool found = false;

    for (int s = 0; s < SLOTS; ++s) {
        size_t base = s * SLOT_SIZE;
        uint32_t seq = 0;
        uint8_t *seqP = (uint8_t*)&seq;
        for (size_t i = 0; i < SLOT_METADATA; ++i) seqP[i] = EEPROM.read(base + i);

        Config tempConfig;
        uint8_t *p = (uint8_t*)&tempConfig;
        for (size_t i = 0; i < sizeof(Config); i++) p[i] = EEPROM.read(base + SLOT_METADATA + i);

        char calculatedChecksum = calculateChecksum(tempConfig);
        if (calculatedChecksum == tempConfig.checksum) {
            if (!found || seq > bestSeq) {
                bestSeq = seq;
                memcpy(&bestCfg, &tempConfig, sizeof(Config));
                found = true;
            }
        }
    }

    EEPROM.end();

    if (found) {
        memcpy(&config, &bestCfg, sizeof(Config));
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
    // Write atomically to rotating slot (2-slot wear-leveling)
    const size_t SLOT_METADATA = sizeof(uint32_t);
    const size_t SLOT_SIZE = SLOT_METADATA + sizeof(Config);
    const int SLOTS = 2;
    EEPROM.begin(SLOT_SIZE * SLOTS + 8);

    // Read current seq values
    uint32_t seqs[SLOTS] = {0};
    for (int s = 0; s < SLOTS; ++s) {
        size_t base = s * SLOT_SIZE;
        uint32_t seq = 0;
        uint8_t *seqP = (uint8_t*)&seq;
        for (size_t i = 0; i < SLOT_METADATA; ++i) seqP[i] = EEPROM.read(base + i);
        seqs[s] = seq;
    }

    int target = (seqs[0] <= seqs[1]) ? 0 : 1; // write to the older slot (or slot 0 if equal)
    uint32_t nextSeq = (seqs[target] == 0xFFFFFFFF) ? 1 : seqs[target] + 1;

    config.checksum = calculateChecksum(config);
    size_t base = target * SLOT_SIZE;

    noInterrupts();
    // write seq
    uint8_t *seqP = (uint8_t*)&nextSeq;
    for (size_t i = 0; i < SLOT_METADATA; ++i) EEPROM.write(base + i, seqP[i]);
    // write config
    uint8_t *p = (uint8_t*)&config;
    for (size_t i = 0; i < sizeof(Config); ++i) EEPROM.write(base + SLOT_METADATA + i, p[i]);
    ESP.wdtFeed();
    EEPROM.commit();
    interrupts();
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
