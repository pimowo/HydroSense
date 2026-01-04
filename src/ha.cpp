#include "ha.h"
#include "globals.h"
#include "pins.h"

// Definicje sensorów i przełączników używanych w projekcie
HASensor sensorDistance("water_level");
HASensor sensorLevel("water_level_percent");
HASensor sensorVolume("water_volume");
HASensor sensorPumpWorkTime("pump_work_time");

HASensor sensorPump("pump");
HASensor sensorWater("water");

HASensor sensorAlarm("water_alarm");
HASensor sensorReserve("water_reserve");

HASwitch switchPumpAlarm("pump_alarm");
HASwitch switchService("service_mode");
HASwitch switchSound("sound_switch");

void onPumpAlarmCommand(bool state, HASwitch* sender) {
    if (!state) {
        playConfirmationSound();
        status.pumpSafetyLock = false;
        switchPumpAlarm.setState(false);
    }
}

void onSoundSwitchCommand(bool state, HASwitch* sender) {
    status.soundEnabled = state;
    config.soundEnabled = state;
    saveConfig();
    switchSound.setState(state, true);
    if (state) playConfirmationSound();
}

void onServiceSwitchCommand(bool state, HASwitch* sender) {
    playConfirmationSound();
    status.isServiceMode = state;
    buttonState.lastState = HIGH;
    switchService.setState(state);

    if (state) {
        if (status.isPumpActive) {
            digitalWrite(POMPA_PIN, LOW);
            status.isPumpActive = false;
            status.pumpStartTime = 0;
            sensorPump.setValue("OFF");
        }
    } else {
        status.isPumpDelayActive = false;
        status.pumpDelayStartTime = 0;
    }
}

void setupHA() {
    device.setName("HydroSense");
    device.setModel("HS ESP8266");
    device.setManufacturer("PMW");
    device.setSoftwareVersion(SOFTWARE_VERSION);

    sensorDistance.setName("Pomiar odległości");
    sensorDistance.setIcon("mdi:ruler");
    sensorDistance.setUnitOfMeasurement("mm");

    sensorLevel.setName("Poziom wody");
    sensorLevel.setIcon("mdi:cup-water");
    sensorLevel.setUnitOfMeasurement("%");

    sensorVolume.setName("Objętość wody");
    sensorVolume.setIcon("mdi:cup-water");
    sensorVolume.setUnitOfMeasurement("L");

    sensorPumpWorkTime.setName("Czas pracy pompy");
    sensorPumpWorkTime.setIcon("mdi:timer-outline");
    sensorPumpWorkTime.setUnitOfMeasurement("s");

    sensorPump.setName("Status pompy");
    sensorPump.setIcon("mdi:water-pump");

    sensorWater.setName("Czujnik wody");
    sensorWater.setIcon("mdi:electric-switch");

    sensorAlarm.setName("Brak wody");
    sensorAlarm.setIcon("mdi:alarm-light");

    sensorReserve.setName("Rezerwa wody");
    sensorReserve.setIcon("mdi:alarm-light-outline");

    switchService.setName("Serwis");
    switchService.setIcon("mdi:account-wrench-outline");
    switchService.onCommand(onServiceSwitchCommand);
    status.isServiceMode = false;
    switchService.setState(false, true);

    switchSound.setName("Dźwięk");
    switchSound.setIcon("mdi:volume-high");
    switchSound.onCommand(onSoundSwitchCommand);
    switchSound.setState(status.soundEnabled);

    switchPumpAlarm.setName("Alarm pompy");
    switchPumpAlarm.setIcon("mdi:alert");
    switchPumpAlarm.onCommand(onPumpAlarmCommand);
}
