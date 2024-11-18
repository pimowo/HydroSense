// Config.h
#pragma once

struct Config {
    // Konfiguracja sieci
    String wifi_ssid;
    String wifi_password;
    String mqtt_server;
    String mqtt_user;
    String mqtt_password;

    // Konfiguracja zbiornika
    int tank_full;
    int tank_empty;
    int reserve_level;
    int hysteresis;
    int tank_diameter;

    // Konfiguracja pompy
    int pump_delay;
    int pump_work_time;
};