#include "measurements.h"
#include "globals.h"
#include "pins.h"

// Non-blocking ultrasonic measurement state machine
enum USState { US_IDLE, US_TRIG, US_WAIT_HIGH, US_WAIT_LOW, US_DELAY, US_DONE };
static USState us_state = US_IDLE;
static int us_samples[8];
static int us_sampleIndex = 0;
static unsigned long us_triggerMicros = 0;
static unsigned long us_echoStartMicros = 0;
static unsigned long us_timeoutMicros = 0;
static unsigned long us_nextSampleMillis = 0;
static bool us_resultReady = false;
static int us_resultDistance = -1;

static void startTrigger() {
    digitalWrite(PIN_ULTRASONIC_TRIG, LOW);
    // start pulse
    digitalWrite(PIN_ULTRASONIC_TRIG, HIGH);
    us_triggerMicros = micros();
    us_timeoutMicros = us_triggerMicros + 25000UL; // 25ms timeout
    us_state = US_TRIG;
}

void ultrasonicTask() {
    unsigned long nowMicros = micros();
    unsigned long nowMillis = millis();

    switch (us_state) {
        case US_IDLE:
            // nothing
            break;
        case US_TRIG:
            // maintain ~10us trigger
            if (nowMicros - us_triggerMicros >= 10UL) {
                digitalWrite(PIN_ULTRASONIC_TRIG, LOW);
                us_state = US_WAIT_HIGH;
                us_timeoutMicros = micros() + 25000UL;
            }
            break;
        case US_WAIT_HIGH:
            if (digitalRead(PIN_ULTRASONIC_ECHO) == HIGH) {
                us_echoStartMicros = micros();
                us_state = US_WAIT_LOW;
                us_timeoutMicros = us_echoStartMicros + 25000UL;
            } else if (micros() > us_timeoutMicros) {
                // timeout waiting for high
                us_samples[us_sampleIndex++] = -1;
                us_nextSampleMillis = nowMillis + ULTRASONIC_TIMEOUT;
                us_state = (us_sampleIndex < SENSOR_AVG_SAMPLES) ? US_DELAY : US_DONE;
            }
            break;
        case US_WAIT_LOW:
            if (digitalRead(PIN_ULTRASONIC_ECHO) == LOW) {
                unsigned long duration = micros() - us_echoStartMicros;
                int distance = (duration * 343) / 2000; // mm
                us_samples[us_sampleIndex++] = distance;
                us_nextSampleMillis = nowMillis + ULTRASONIC_TIMEOUT;
                us_state = (us_sampleIndex < SENSOR_AVG_SAMPLES) ? US_DELAY : US_DONE;
            } else if (micros() > us_timeoutMicros) {
                // timeout waiting for low
                us_samples[us_sampleIndex++] = -1;
                us_nextSampleMillis = nowMillis + ULTRASONIC_TIMEOUT;
                us_state = (us_sampleIndex < SENSOR_AVG_SAMPLES) ? US_DELAY : US_DONE;
            }
            break;
        case US_DELAY:
            if (nowMillis >= us_nextSampleMillis) {
                startTrigger();
            }
            break;
        case US_DONE:
            // compute median of valid samples
            {
                int validCount = 0;
                for (int i = 0; i < us_sampleIndex; ++i) if (us_samples[i] != -1) validCount++;
                if (validCount < (SENSOR_AVG_SAMPLES / 2)) {
                    us_resultDistance = -1;
                } else {
                    int tmp[validCount];
                    int idx = 0;
                    for (int i = 0; i < us_sampleIndex; ++i) if (us_samples[i] != -1) tmp[idx++] = us_samples[i];
                    // simple sort
                    for (int i = 0; i < validCount-1; ++i) for (int j = 0; j < validCount-i-1; ++j) if (tmp[j] > tmp[j+1]) { int t = tmp[j]; tmp[j]=tmp[j+1]; tmp[j+1]=t; }
                    int median = (validCount % 2 == 0) ? ((tmp[validCount/2 -1] + tmp[validCount/2]) / 2) : tmp[validCount/2];
                    us_resultDistance = median;
                }
                us_resultReady = true;
            }
            us_state = US_IDLE;
            break;
    }
}

// Zwróć bieżący poziom wody w zbiorniku
float getCurrentWaterLevel() {
    int distance = measureDistance();
    return (float)calculateWaterLevel(distance);
}

// Mierzy odległość od czujnika do powierzchni wody
int measureDistance() {
    // legacy synchronous call removed — prefer non-blocking ultrasonicTask()
    // Provide immediate cached value
    return (int)lastFilteredDistance;
}

// Oblicz poziom wody na podstawie zmierzonej odległości
int calculateWaterLevel(int distance) {
    if (distance < config.tank_full) distance = config.tank_full;
    if (distance > config.tank_empty) distance = config.tank_empty;

    float percentage = (float)(config.tank_empty - distance) /
                       (float)(config.tank_empty - config.tank_full) * 100.0f;
    return (int)percentage;
}

// Aktualizuj stany alarmowe
void updateAlarmStates(float currentDistance) {
    if (currentDistance >= config.tank_empty && !status.waterAlarmActive) {
        status.waterAlarmActive = true;
        sensorAlarm.setValue("ON");
    } else if (currentDistance < (config.tank_empty - HYSTERESIS) && status.waterAlarmActive) {
        status.waterAlarmActive = false;
        sensorAlarm.setValue("OFF");
    }

    if (currentDistance >= config.reserve_level && !status.waterReserveActive) {
        status.waterReserveActive = true;
        sensorReserve.setValue("ON");
    } else if (currentDistance < (config.reserve_level - HYSTERESIS) && status.waterReserveActive) {
        status.waterReserveActive = false;
        sensorReserve.setValue("OFF");
    }
}

// Aktualizuj poziom wody i wyślij dane do Home Assistant
void updateWaterLevel() {
    // Non-blocking: if ultrasonic measurement not started, start it and return.
    if (us_state == US_IDLE && !us_resultReady) {
        us_sampleIndex = 0;
        us_resultReady = false;
        startTrigger();
        return;
    }

    // If measurement not yet ready, skip processing this cycle
    if (!us_resultReady) return;

    // Use measurement result
    if (us_resultDistance < 0) return;
    currentDistance = us_resultDistance;
    us_resultReady = false;

    updateAlarmStates(currentDistance);

    float waterHeight = config.tank_empty - currentDistance;
    waterHeight = constrain(waterHeight, 0, config.tank_empty - config.tank_full);

    float radius = config.tank_diameter / 2.0f;
    volume = PI * (radius * radius) * waterHeight / 1000000.0f; // mm^3 -> L

    char buf[16];
    snprintf(buf, sizeof(buf), "%d", (int)currentDistance);
    sensorDistance.setValue(buf);

    snprintf(buf, sizeof(buf), "%d", calculateWaterLevel(currentDistance));
    sensorLevel.setValue(buf);

    char valueStr[16];
    dtostrf(volume, 1, 1, valueStr);
    sensorVolume.setValue(valueStr);

    static float lastReportedDistance = 0;
    if (abs(currentDistance - lastReportedDistance) > 5) {
        DEBUG_PRINTF("Poziom: %.1f mm, Obj: %.1f L\n", currentDistance, volume);
        lastReportedDistance = currentDistance;
    }

    timers.lastMeasurement = millis();
}
