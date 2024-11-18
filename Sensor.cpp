// Sensor.cpp
#include "Sensor.h"

// Pomiar odległości
// Funkcja wykonuje pomiar odległości za pomocą czujnika ultradźwiękowego HC-SR04
// Zwraca:
//   - zmierzoną odległość w milimetrach (mediana z kilku pomiarów)
//   - (-1) w przypadku błędu lub przekroczenia czasu odpowiedzi
int measureDistance() {
  int measurements[SENSOR_AVG_SAMPLES];
  int validCount = 0;

  // Rozszerzony zakres akceptowalnych pomiarów o margines
  const int MIN_VALID_DISTANCE = TANK_FULL - VALID_MARGIN;   // 45mm
  const int MAX_VALID_DISTANCE = TANK_EMPTY + VALID_MARGIN;  // 530mm

  for (int i = 0; i < SENSOR_AVG_SAMPLES; i++) {
    measurements[i] = -1;  // Domyślna wartość błędu

    // Reset trigger
    digitalWrite(PIN_ULTRASONIC_TRIG, LOW);
    delayMicroseconds(2);

    // Wysłanie impulsu 10µs
    digitalWrite(PIN_ULTRASONIC_TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(PIN_ULTRASONIC_TRIG, LOW);

    // Pomiar czasu odpowiedzi z timeoutem
    unsigned long startTime = micros();
    unsigned long timeout = startTime + 25000;  // 25ms timeout

    // Czekaj na początek echa
    bool validEcho = true;
    while (digitalRead(PIN_ULTRASONIC_ECHO) == LOW) {
      if (micros() > timeout) {
        DEBUG_PRINT(F("Echo start timeout"));
        validEcho = false;
        break;
      }
    }

    if (validEcho) {
      startTime = micros();

      // Czekaj na koniec echa
      while (digitalRead(PIN_ULTRASONIC_ECHO) == HIGH) {
        if (micros() > timeout) {
          DEBUG_PRINT(F("Echo end timeout"));
          validEcho = false;
          break;
        }
      }

      if (validEcho) {
        unsigned long duration = micros() - startTime;
        int distance = (duration * 343) / 2000;  // Prędkość dźwięku 343 m/s

        // Walidacja odległości z uwzględnieniem marginesu
        if (distance >= MIN_VALID_DISTANCE && distance <= MAX_VALID_DISTANCE) {
          measurements[i] = distance;
          validCount++;
        } else {
          DEBUG_PRINT(F("Distance out of range: "));
          DEBUG_PRINT(distance);
        }
      }
    }

    delay(MEASUREMENT_DELAY);
  }

  // Sprawdź czy mamy wystarczająco dużo poprawnych pomiarów
  if (validCount < (SENSOR_AVG_SAMPLES / 2)) {
    DEBUG_PRINT(F("Too few valid measurements: "));
    DEBUG_PRINT(validCount);
    return -1;
  }

  // Przygotuj tablicę na poprawne pomiary
  int validMeasurements[validCount];
  int validIndex = 0;

  // Kopiuj tylko poprawne pomiary
  for (int i = 0; i < SENSOR_AVG_SAMPLES; i++) {
    if (measurements[i] != -1) {
      validMeasurements[validIndex++] = measurements[i];
    }
  }

  // Sortowanie
  for (int i = 0; i < validCount - 1; i++) {
    for (int j = 0; j < validCount - i - 1; j++) {
      if (validMeasurements[j] > validMeasurements[j + 1]) {
        int temp = validMeasurements[j];
        validMeasurements[j] = validMeasurements[j + 1];
        validMeasurements[j + 1] = temp;
      }
    }
  }

  // Oblicz medianę
  float medianValue;
  if (validCount % 2 == 0) {
    medianValue = (validMeasurements[(validCount - 1) / 2] + validMeasurements[validCount / 2]) / 2.0;
  } else {
    medianValue = validMeasurements[validCount / 2];
  }

  // Zastosuj filtr EMA
  if (lastFilteredDistance == 0) {
    lastFilteredDistance = medianValue;
  }

  // Zastosuj filtr EMA z ograniczeniem maksymalnej zmiany
  float maxChange = 10.0;  // maksymalna zmiana między pomiarami w mm
  float currentChange = medianValue - lastFilteredDistance;
  if (abs(currentChange) > maxChange) {
    medianValue = lastFilteredDistance + (currentChange > 0 ? maxChange : -maxChange);
  }

  lastFilteredDistance = (EMA_ALPHA * medianValue) + ((1 - EMA_ALPHA) * lastFilteredDistance);

  // Końcowe ograniczenie do rzeczywistego zakresu zbiornika
  if (lastFilteredDistance < TANK_FULL) lastFilteredDistance = TANK_FULL;
  if (lastFilteredDistance > TANK_EMPTY) lastFilteredDistance = TANK_EMPTY;

  return (int)lastFilteredDistance;
}

// Funkcja obliczająca poziom wody w zbiorniku dolewki w procentach
//
// @param distance - zmierzona odległość od czujnika do lustra wody w mm
// @return int - poziom wody w procentach (0-100%)
// Wzór: ((EMPTY - distance) / (EMPTY - FULL)) * 100
int calculateWaterLevel(int distance) {
  // Ograniczenie wartości do zakresu pomiarowego
  if (distance < TANK_FULL) distance = TANK_FULL;    // Nie mniej niż przy pełnym
  if (distance > TANK_EMPTY) distance = TANK_EMPTY;  // Nie więcej niż przy pustym

  // Obliczenie procentowe poziomu wody
  float percentage = (float)(TANK_EMPTY - distance) /   // Różnica: pusty - aktualny
                     (float)(TANK_EMPTY - TANK_FULL) *  // Różnica: pusty - pełny
                     100.0;                             // Przeliczenie na procenty

  return (int)percentage;  // Zwrot wartości całkowitej
}

// Pobranie aktualnego poziomu wody
/**
 * @brief Wykonuje pomiar odległości za pomocą czujnika ultradźwiękowego i oblicza aktualny poziom wody.
 * 
 * Funkcja najpierw wykonuje pomiar odległości za pomocą czujnika ultradźwiękowego HC-SR04,
 * zapisuje wynik pomiaru do zmiennej, a następnie oblicza poziom wody na podstawie tej wartości.
 * 
 * @return Poziom wody w jednostkach używanych przez funkcję calculateWaterLevel.
 *         Zwraca wartość obliczoną przez funkcję calculateWaterLevel, która przelicza odległość na poziom wody.
 */
float getCurrentWaterLevel() {
  int distance = measureDistance();                  // Pobranie wyniku pomiaru
  float waterLevel = calculateWaterLevel(distance);  // Obliczenie poziomu wody na podstawie wyniku pomiaru
  return waterLevel;                                 // Zwrócenie obliczonego poziomu wody
}

//
void updateWaterLevel() {
  // Zapisz poprzednią objętość
  float previousVolume = volume;

  currentDistance = measureDistance();
  if (currentDistance < 0) return;  // błąd pomiaru

  // Aktualizacja stanów alarmowych
  updateAlarmStates(currentDistance);

  // Obliczenie objętości
  float waterHeight = TANK_EMPTY - currentDistance;
  waterHeight = constrain(waterHeight, 0, TANK_EMPTY - TANK_FULL);

  // Obliczenie objętości w litrach (wszystko w mm)
  float radius = TANK_DIAMETER / 2.0;
  volume = PI * (radius * radius) * waterHeight / 1000000.0;  // mm³ na litry

  // Aktualizacja sensorów pomiarowych
  sensorDistance.setValue(String((int)currentDistance).c_str());
  sensorLevel.setValue(String(calculateWaterLevel(currentDistance)).c_str());

  char valueStr[10];
  dtostrf(volume, 1, 1, valueStr);
  sensorVolume.setValue(valueStr);

  // Debug info tylko gdy wartości się zmieniły (conajmniej 5mm)
  static float lastReportedDistance = 0;
  if (abs(currentDistance - lastReportedDistance) > 5) {
    Serial.printf("Poziom: %.1f mm, Obj: %.1f L\n", currentDistance, volume);
    lastReportedDistance = currentDistance;
  }
}