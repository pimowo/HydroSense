/***************************************
 *      Konfiguracja systemowa         *
 ***************************************/

#include <ESP8266WiFi.h>  // Biblioteka do obsługi Wi-Fi dla ESP8266
#include <EEPROM.h>  // Biblioteka do obsługi pamięci EEPROM
#include <ArduinoHA.h>  // Biblioteka do integracji z Home Assistant
#include <ArduinoOTA.h>  // Biblioteka do obsługi aktualizacji OTA (Over-The-Air)
#include <ESP8266WebServer.h>  // Biblioteka do obsługi serwera HTTP na ESP8266
#include <CRC32.h>  // Biblioteka CRC32

/***************************************
 *    Definicje i stałe systemowe      *
 ***************************************/
 
// Definicje pinów
#define TRIG_PIN D6 // Pin "TRIG" czujnika odległości
#define ECHO_PIN D7 // Pin "ECHO" czujnika odległości

#define SENSOR_PIN D5 // Czujnik poziomu wody
#define PUMP_PIN D1 // Pompa
#define BUZZER_PIN D2 // Buzzer
#define BUTTON_PIN D3 // Przycisk kasowania alarmu

// Ustawienia WiFi i MQTT
const char* ssid = "pimowo";  // Nazwa sieci Wi-Fi, do której ma się połączyć urządzenie
const char* password = "ckH59LRZQzCDQFiUgj";  // Hasło do sieci Wi-Fi
const char* mqtt_server = "192.168.1.14";  // Adres IP serwera MQTT, z którym ma się komunikować urządzenie
const char* mqtt_user = "hydrosense";  // Nazwa użytkownika do autoryzacji na serwerze MQTT
const char* mqtt_password = "hydrosense";  // Hasło użytkownika do autoryzacji na serwerze MQTT

// Zmienne kontrolne dla Wi-Fi
bool wifiConnected = false;  // Flaga informująca o statusie połączenia Wi-Fi (połączony/niepołączony)
unsigned long lastWifiCheckTime = 0;  // Czas ostatniego sprawdzenia połączenia Wi-Fi
unsigned long wifiCheckInterval = 15000;  // Interwał sprawdzania Wi-Fi w milisekundach
unsigned long mqttReconnectInterval = 5000;  // Interwał ponownego łączenia z MQTT w milisekundach

// Adresy EEPROM
#define EEPROM_PUMP_TIME 0  // Bajt 0 - czas pracy pompy (int)
#define EEPROM_DELAY_TIME 2  // Bajt 2 - czas opóźnienia (int)
#define EEPROM_BUZZER_STATUS 4  // Bajt 4 - stan buzzera (bool)
#define EEPROM_TANK_DIAMETER 5  // Bajt 5 - średnica zbiornika (float, 4 bajty)
#define EEPROM_DISTANCE_FULL 9  // Bajt 9 - odległość pełnego zbiornika (float, 4 bajty)
#define EEPROM_DISTANCE_EMPTY 13  // Bajt 13 - odległość pustego zbiornika (float, 4 bajty)
#define EEPROM_RESERVE_THRESHOLD 17  // Bajt 17 - próg zbiornika rezerwy (float, 4 bajty)

#define EEPROM_INITIALIZED_FLAG 100  // Adres bajtu, w którym przechowywana będzie flaga inicjalizacji EEPROM
#define INITIALIZED_FLAG_VALUE 0xAA  // Wartość, która wskazuje, że EEPROM został już zainicjalizowany

struct Settings {
  int pumpTime;  // Czas pracy pompy (w sekundach lub milisekundach, w zależności od implementacji)
  int delayTime;  // Opóźnienie między operacjami pompy (np. czas potrzebny na pełne opróżnienie lub napełnienie)
  float tankDiameter;  // Średnica zbiornika (w metrach lub centymetrach), używana do obliczeń objętości
  float distanceFull;  // Odległość od czujnika do poziomu pełnego zbiornika
  float distanceEmpty;  // Odległość od czujnika do poziomu pustego zbiornika
  float reserveThreshold; // Próg rezerwowy – minimalny poziom wody, przy którym następuje ostrzeżenie lub wyłączenie pompy
  bool buzzerEnabled;  // Flaga aktywująca sygnał dźwiękowy (true, jeśli buzzer jest włączony, false, jeśli wyłączony)
  uint32_t checksum;  // Suma kontrolna
};

Settings settings;

// Zmienne kontrolne
float tankDiameter = 100;  // Średnica zbiornika w mm
float distanceFull = 50;  // Odległość dla pełnego zbiornika w mm
float distanceEmpty = 1000;  // Odległość dla pustego zbiornika w mm
float currentWaterLevelPercent = 0;  // Bieżący poziom wody w procentach
float currentWaterVolumeLiters = 0;  // Bieżąca objętość wody w litrach
float lastWaterLevelPercent = -1;   // Ostatni procent, do porównania zmian
float lastDistance = -1;  // Przechowuje poprzednią wartość odległości
unsigned long previousPumpTime = 0;  // Czas ostatniego uruchomienia pompy
unsigned long startDelayTime = 0;  // Czas rozpoczęcia opóźnienia
unsigned long lastBeepTime = 0;  // Czas ostatniego dźwięku sygnalizacyjnego
int pumpTime = 30;  // Czas pracy pompy [s]
int delayTime = 5;  // Czas opóźnienia [s]
bool sensorState = false;  // Stan czujnika (aktywny/nieaktywny)
bool alarmActive = false;  // Flaga aktywacji alarmu
bool buzzerEnabled = true;  // Flaga włączania sygnalizatora dźwiękowego
bool serviceMode = false;  // Flaga trybu serwisowego
bool welcomeComplete = false;  // Flaga informująca, czy powitanie zostało zakończone
bool delayInProgress = false;  // Flaga informująca, czy opóźnienie jest w toku
bool firstRun = true;  // Flaga pierwszego uruchomienia

unsigned long lastMqttAttemptTime = 0;  // Czas ostatniej próby połączenia z MQTT

// SR04
bool waterEmptyState = false;
const int hysteresis = 10;  // Histereza 10 mm

// Tworzenie obiektu klienta Wi-Fi, który będzie używany do komunikacji
WiFiClient espClient;
// Inicjalizacja urządzenia Home Assistant o nazwie "HydroSense"
HADevice device("HydroSense");
// Inicjalizacja obiektu MQTT z wykorzystaniem klienta Wi-Fi i urządzenia
HAMqtt haMqtt(espClient, device);
// Czujniki
HASensor waterSensor("water");  // Czujnik wody - informuje Home Assistant o aktualnym stanie czujnika wody (włączony/wyłączony).
HASensor pumpSensor("pump");  // Sterowanie pompą - ten obiekt umożliwia włączanie i wyłączanie pompy za pomocą Home Assistant.
HASensor waterLevelPercent("water_level_percent");  // Procent zapełnienia zbiornika - informuje Home Assistant o aktualnym poziomie wody w zbiorniku w procentach.
HASensor waterVolumeLiters("water_volume_liters");  // Ilość wody w litrach - informuje Home Assistant o ilości wody w zbiorniku w litrach.
HASensor reserveSensor("reserve");  // Czujnik rezerwy
HASensor pomiarSensor("pomiar");  // Czujnik odległości - aktualny odczyt w mm
HASensor waterEmptySensor("water_empty");  // Sensor "Brak wody" do HA
// Przełączniki
HASwitch buzzerSwitch("buzzer");  // Sterowanie buzzerem - umożliwia włączanie i wyłączanie buzzera przez Home Assistant.
HASwitch alarmSwitch("alarm_switch");  // Wyświetlanie i kasowanie alarmu - ten przełącznik pozwala na wyświetlenie stanu alarmu oraz jego ręczne kasowanie.
HASwitch serviceSwitch("service_mode");  // Tryb serwis - przełącznik pozwalający na włączenie lub wyłączenie trybu serwisowego (ignorowanie niektórych funkcji).
// Ustawienia liczbowe
HANumber pumpTimeNumber("pump_time", HANumber::PrecisionP0);  // Czas pracy pompy - pozwala ustawić czas pracy pompy, z dokładnością do pełnych sekund.
HANumber delayTimeNumber("delay_time", HANumber::PrecisionP0);  // Czas opóźnienia - umożliwia ustawienie opóźnienia przed włączeniem pompy, z dokładnością do pełnych sekund.
HANumber tankDiameterHA("tank_diameter", HANumber::PrecisionP0);  // Średnica zbiornika - umożliwia ustawienie średnicy zbiornika w Home Assistant.
HANumber distanceFullHA("distance_full", HANumber::PrecisionP0);  // Odległość pełnego zbiornika - ustawienie odległości czujnika od powierzchni wody, gdy zbiornik jest pełny.
HANumber distanceEmptyHA("distance_empty", HANumber::PrecisionP0);  // Odległość pustego zbiornika - ustawienie odległości czujnika od dna zbiornika, gdy zbiornik jest pusty.
HANumber reserveThresholdHA("reserve_threshold", HANumber::PrecisionP0);  // Ustawienie progu rezerwy

// Zmienne czasowe
unsigned long previousTime = 0;  //  Czas ostatniego odczytu
unsigned long lastDebugOutput = 0;
unsigned long interval = 5000;  //  Interwał zmiany (5000 ms = 5 s) - można zmienić na 15 sekund
float distanceReadings[10];
int readingIndex = 0;  //  Indeks aktualnego odczytu w tablicy
int totalReadings = 0;  //  Całkowita liczba odczytów zapisanych w tablicy

// Rezerwa
float reserveThreshold = 200;  // Domyślna wartość progu rezerwy w mm
bool reserveActive = false;  // Flaga stanu rezerwy

// --- Funkcja pomocnicza do debugowania
ESP8266WebServer server(80); // Tworzymy serwer na porcie 80
String debugMessages = ""; // Zmienna do przechowywania komunikatów debugowania

/***************************************
 *         Deklaracje funkcji          *
 ***************************************/

// Wyświetlanie komunikatów debugowania
void debugPrint(String message) {
  Serial.println(message);
  
  // Logika czyszczenia debugMessages
  if (debugMessages.length() + message.length() > 5000) {
    debugMessages = debugMessages.substring(debugMessages.length() - 4000); // Przytnij do 4000 znaków
  }

  debugMessages += message + "<br>";
}

// void debugPrint(String message) {
//   static String debugBuffer = ""; // Bufor do przechowywania wiadomości

//   if (debugBuffer.length() + message.length() > 1000) { // Jeśli bufor jest zapełniony
//     Serial.print(debugBuffer); // Wypisz cały bufor
//     debugBuffer = ""; // Wyzeruj bufor
//   }

//   debugBuffer += message + "\n";        // Dodaj wiadomość do bufora
// }

// Strona główna serwera WWW dla debugowania
void handleRoot() {
  // Zaktualizuj zawartość strony HTML
  String html = "<html><head><meta http-equiv='refresh' content='3'></head><body>";
  html += "<h1>HydroSense</h1>";
  html += "<pre>" + debugMessages + "</pre>"; // Dodanie <pre> dla lepszego formatowania komunikatów
  html += "</body></html>";

  server.send(200, "text/html", html); // Wysyłamy stronę do przeglądarki
}

// Czyszczenie starych wiadomości debugowania
void clearDebugMessages() {
  if (debugMessages.length() > 5000) {
    debugMessages = debugMessages.substring(debugMessages.length() - 1000);
  }
}

// --- Konfiguracja WiFi i ponowna próba połączenia z MQTT
void checkWifiConnection() {
  static unsigned long lastWifiAttemptTime = 0;
  static int wifiAttemptCounter = 0; // Licznik prób połączenia

  if (WiFi.status() != WL_CONNECTED && millis() - lastWifiAttemptTime > wifiCheckInterval) {
    WiFi.begin(ssid, password);  // Próbuj połączyć się z Wi-Fi
    lastWifiAttemptTime = millis();
    wifiAttemptCounter++;
    debugPrint("Proba polaczenia z Wi-Fi...");

    if (wifiAttemptCounter >= 5) {
      wifiCheckInterval *= 2;  // Podwój interwał po kilku nieudanych próbach
    }
  }

  if (WiFi.status() == WL_CONNECTED && !wifiConnected) {
    wifiConnected = true;
    wifiAttemptCounter = 0;
    wifiCheckInterval = 15000;  // Zresetuj do domyślnego interwału
    debugPrint("Polaczono z Wi-Fi");
  } else if (WiFi.status() != WL_CONNECTED && wifiConnected) {
    wifiConnected = false;
    debugPrint("Rozlaczono z Wi-Fi");
  }
}

// --- Funkcja resetująca watchdog
void resetWatchdog() {
  ESP.wdtFeed(); // Odświeżenie watchdog timer'a, aby zapobiec resetowi systemu
}

// --- Funkcja powitalna z buzzerem
void welcomeBuzzer() {
  if (welcomeComplete) return;

  pinMode(BUZZER_PIN, OUTPUT);
  debugPrint("Rozpoczynamy powitanie buzzerem...");

  for (int i = 0; i < 3; i++) {
    digitalWrite(BUZZER_PIN, HIGH);  // Włącz buzzer
    delay(150);                      // Utrzymaj przez 300 ms
    digitalWrite(BUZZER_PIN, LOW);   // Wyłącz buzzer
    delay(150);                      // Przerwa 300 ms
  }

  welcomeComplete = true; // Ustaw flagę, że powitanie zostało zakończone
  debugPrint("Powitanie buzzerem zakonczone");
}

// --- Definicja funkcji onAlarmSwitchCommand
void onAlarmSwitchCommand(bool state, HASwitch* sender) {
  if (!state && alarmActive) {
    clearAlarm();
  }
}

// --- Funkcja do pomiaru odległości z SR04
int measureDistance() {
  int readings[] = {0, 0, 0};
  for (int i = 0; i < 3; ++i) {
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    readings[i] = pulseIn(ECHO_PIN, HIGH) * 0.343 / 2;
    delay(10);  // Krótsze opóźnienie
  }
  return (readings[0] + readings[1] + readings[2]) / 3; // Średnia z trzech pomiarów
}

// --- Funkcja do obliczenia objętości wody w zbiorniku
void calculateWaterVolume(float distance) {
  static float tankArea = PI * pow(tankDiameter / 2.0 / 1000.0, 2); // Oblicz powierzchnię podstawy zbiornika tylko raz
  static float heightRange = distanceEmpty - distanceFull; // Przechowaj zakres wysokości tylko raz

  if (distance > distanceEmpty) {
    currentWaterLevelPercent = 0;
    currentWaterVolumeLiters = 0;
  } else if (distance < distanceFull) {
    currentWaterLevelPercent = 100;
    currentWaterVolumeLiters = tankArea * heightRange;
  } else {
    float level = (distanceEmpty - distance);                      // Obliczenie wysokości na podstawie odległości
    currentWaterLevelPercent = (level / heightRange) * 100;
    currentWaterVolumeLiters = tankArea * level;
  }

  currentWaterLevelPercent = roundf(currentWaterLevelPercent * 10) / 10;
  currentWaterVolumeLiters = roundf(currentWaterVolumeLiters * 1000) / 1000;

  debugPrint("Objetosc wody: " + String(currentWaterVolumeLiters) + " l");
  debugPrint("Poziom wody: " + String(currentWaterLevelPercent) + "%");
}

// --- Funkcja do obliczenia średniej kroczącej
float calculateMovingAverage(float newDistance) {
  if (newDistance < 1 || newDistance > distanceEmpty) {
    debugPrint("Odrzucono bledny odczyt: " + String(newDistance) + " mm");
    return (totalReadings > 0) ? calculateMovingAverage(0) : 0;
  }

  totalReadings = min(totalReadings + 1, 10);
  distanceReadings[readingIndex] = newDistance;
  readingIndex = (readingIndex + 1) % 10;

  float sum = 0;
  for (int i = 0; i < totalReadings; i++) {
    sum += distanceReadings[i];
  }

  float average = sum / totalReadings;
  debugPrint("Srednia kroczaca: " + String(average) + " mm");
  return average;
}

// Brak wody
void checkWaterLevel() {
  float currentDistance = measureDistance();
  
  // Włącz stan "Brak wody" poniżej progu `distanceEmpty`
  if (currentDistance > distanceEmpty && !waterEmptyState) {
    waterEmptyState = true;
    waterEmptySensor.setValue("ON");
    debugPrint("Stan brak wody: ON");

    // Uruchom alarm i zablokuj pompę
    alarmActive = true;
    pumpSensor.setValue("OFF");  // Wyłącz pompę w HA
    stopPump();
  }
  // Wyłącz stan "Brak wody", gdy poziom wody przekroczy `distanceEmpty + hysteresis`
  else if (currentDistance <= (distanceEmpty - hysteresis) && waterEmptyState) {
    waterEmptyState = false;
    waterEmptySensor.setValue("OFF");
    debugPrint("Stan brak wody: OFF");

    // Wyłącz alarm
    alarmActive = false;
  }
}

// --- Funkcja aktualizująca dane w Home Assistant
void updateHomeAssistant() {
  if (abs(currentWaterLevelPercent - lastWaterLevelPercent) > 1 || firstRun) {
    char percentBuffer[10];
    dtostrf(currentWaterLevelPercent, 5, 1, percentBuffer);
    waterLevelPercent.setValue(percentBuffer);

    char volumeBuffer[10];
    dtostrf(currentWaterVolumeLiters, 5, 3, volumeBuffer);
    waterVolumeLiters.setValue(volumeBuffer);

    lastWaterLevelPercent = currentWaterLevelPercent;
    firstRun = false;  // Wyłącz flaga po pierwszym przesłaniu
  }

  // Odczytaj i uśrednij odległość
  float rawDistance = measureDistance();
  int averagedDistance = static_cast<int>(round(calculateMovingAverage(rawDistance)));  // Użycie średniej kroczącej

  // Wysyłanie do HA tylko przy zmianie odległości
  if (averagedDistance != lastDistance) {
    pomiarSensor.setValue(String(averagedDistance).c_str());  // Aktualizuj wartość odczytu w mm
    lastDistance = averagedDistance;  // Aktualizacja poprzedniego pomiaru
    debugPrint(String("Pomiar odleglosci: ") + String(averagedDistance));
  }

  bool newReserveActive = averagedDistance >= (reserveThreshold + 10);

  if (newReserveActive != reserveActive) {
    reserveActive = newReserveActive;
    reserveSensor.setValue(reserveActive ? "ON" : "OFF");
    debugPrint("Zaktualizowano stan rezerwy: " + String(reserveActive ? "ON" : "OFF"));
  }

  resetWatchdog();
}

// --- Funkcje zmieniające ustawienia zbiornika
void onTankDiameterChange(HANumeric value, HANumber* sender) {
  float newTankDiameter = value.toFloat();
  
  // Sprawdzanie, czy średnica zbiornika mieści się w przedziale
  if (newTankDiameter >= 50 && newTankDiameter <= 250 && newTankDiameter != tankDiameter) {
    tankDiameter = newTankDiameter;
    EEPROM.put(EEPROM_TANK_DIAMETER, tankDiameter); // Zapis do EEPROM
    EEPROM.commit(); // Zapisz zmiany do EEPROM
    tankDiameterHA.setState(tankDiameter);
    debugPrint("Zaktualizowano srednice zbiornika: " + String(tankDiameter) + "mm");
  } else {
    debugPrint("Niepoprawna wartosc srednicy zbiornika lub brak zmiany");
  }
}

void onDistanceFullChange(HANumeric value, HANumber* sender) {
  float newDistanceFull = value.toFloat();
  
  // Sprawdzanie, czy odległość pełnego zbiornika mieści się w przedziale
  if (newDistanceFull > 0 && newDistanceFull < distanceEmpty && newDistanceFull != distanceFull) {
    distanceFull = newDistanceFull;
    EEPROM.put(EEPROM_DISTANCE_FULL, distanceFull); // Zapis do EEPROM
    EEPROM.commit(); // Zapisz zmiany do EEPROM
    distanceFullHA.setState(distanceFull);
    debugPrint("Zaktualizowano odległosc pelnego zbiornika: " + String(distanceFull) + " mm");
  } else {
    debugPrint("Niepoprawna wartosc odleglosci pelnego zbiornika lub brak zmiany");
  }

  configureReserveSlider();  // Ponownie skonfiguruj suwak rezerwy
}

void onDistanceEmptyChange(HANumeric value, HANumber* sender) {
  float newDistanceEmpty = value.toFloat();
  
  // Sprawdzanie, czy odległość pustego zbiornika mieści się w przedziale
  if (newDistanceEmpty > distanceFull && newDistanceEmpty != distanceEmpty) {
    distanceEmpty = newDistanceEmpty;
    EEPROM.put(EEPROM_DISTANCE_EMPTY, distanceEmpty); // Zapis do EEPROM
    EEPROM.commit(); // Zapisz zmiany do EEPROM
    distanceEmptyHA.setState(distanceEmpty);
    debugPrint("Zaktualizowano odległosc pustego zbiornika: " + String(distanceEmpty) + " mm");
  } else {
    debugPrint("Niepoprawna wartosc odległosci pustego zbiornika lub brak zmiany.");
  }

  configureReserveSlider();  // Ponownie skonfiguruj suwak rezerwy
}

// Zapisuje ustawienia do EEPROM

void saveSettings() {
  settings.pumpTime = pumpTime;
  settings.delayTime = delayTime;
  settings.tankDiameter = tankDiameter;
  settings.distanceFull = distanceFull;
  settings.distanceEmpty = distanceEmpty;
  settings.reserveThreshold = reserveThreshold;
  settings.buzzerEnabled = buzzerEnabled;

  // Oblicz sumę kontrolną
  CRC32 crc;
  crc.update((uint8_t*)&settings, sizeof(settings) - sizeof(settings.checksum));
  settings.checksum = crc.finalize();

  // Zapis całej struktury
  EEPROM.put(0, settings);
  EEPROM.commit();
  debugPrint("Zapisano ustawienia do EEPROM");
}

bool loadSettings() {
  EEPROM.get(0, settings);

  // Walidacja sumy kontrolnej
  CRC32 crc;
  crc.update((uint8_t*)&settings, sizeof(settings) - sizeof(settings.checksum));
  if (settings.checksum != crc.finalize()) {
    debugPrint("Nieprawidłowa suma kontrolna!");
    debugPrint("Ladowanie wartosci domyslnych");
    return false;
  }

  // Przypisanie wartości do zmiennych globalnych
  pumpTime = settings.pumpTime;
  delayTime = settings.delayTime;
  tankDiameter = settings.tankDiameter;
  distanceFull = settings.distanceFull;
  distanceEmpty = settings.distanceEmpty;
  reserveThreshold = settings.reserveThreshold;
  buzzerEnabled = settings.buzzerEnabled;

  // Wyświetlanie wczytanych wartości w celu debugowania
  debugPrint("=======================================");
  debugPrint("              Wczytane dane EEPROM");
  debugPrint("       Czas opoznienia : " + String(delayTime) + " s");
  debugPrint("      Czas pracy pompy : " + String(pumpTime) + " s");
  debugPrint("    Srednica zbiornika : " + String((int)tankDiameter) + " mm");
  debugPrint("        Pelny zbiornik : " + String((int)distanceFull) + " mm");
  debugPrint("        Pusty zbiornik : " + String((int)distanceEmpty) + " mm");
  debugPrint("          Prog rezerwy : " + String((int)reserveThreshold) + " mm");
  debugPrint("                Dzwiek : " + String(buzzerEnabled ? "TAK" : "NIE"));
  debugPrint("=======================================");

  return true;
}

// Inicjalizacja pamięci EEPROM
void initializeEEPROM() {
  if (EEPROM.read(EEPROM_INITIALIZED_FLAG) != INITIALIZED_FLAG_VALUE) {
    debugPrint("Inicjalizacja EEPROM z wartościami domyślnymi");
    
    // Ustawienia domyślne
    int pumpTime = 30;  // Czas pracy pompy (w sekundach lub milisekundach, w zależności od implementacji)
    int delayTime = 50;  // Opóźnienie między operacjami pompy (np. czas potrzebny na pełne opróżnienie lub napełnienie)
    float tankDiameter = 150;  // Średnica zbiornika (w metrach lub centymetrach), używana do obliczeń objętości
    float distanceFull = 50;  // Odległość od czujnika do poziomu pełnego zbiornika
    float distanceEmpty = 500;  // Odległość od czujnika do poziomu pustego zbiornika
    float reserveThreshold = 400;  // Próg rezerwowy – minimalny poziom wody, przy którym następuje ostrzeżenie lub wyłączenie pompy
    bool buzzerEnabled = true;  // Flaga aktywująca sygnał dźwiękowy (true, jeśli buzzer jest włączony, false, jeśli wyłączony)

    saveSettings();  // Zapisz ustawienia domyślne

    // Zapis flagi inicjalizacji
    EEPROM.write(EEPROM_INITIALIZED_FLAG, INITIALIZED_FLAG_VALUE);
    EEPROM.commit();

    debugPrint("EEPROM zainicjalizowany");
  } else {
    debugPrint("EEPROM juz zainicjalizowany");
  }
}

// Zapisuje domyślne wartości do EEPROM
// void saveDefaults() {
//   bool settingsChanged = false;

//   // Zapis floatów
//   float eepromTankDiameter;
//   EEPROM.get(EEPROM_TANK_DIAMETER, eepromTankDiameter);
//   if (eepromTankDiameter != tankDiameter) {
//     EEPROM.put(EEPROM_TANK_DIAMETER, tankDiameter);
//     settingsChanged = true;
//   }

//   float eepromDistanceFull;
//   EEPROM.get(EEPROM_DISTANCE_FULL, eepromDistanceFull);
//   if (eepromDistanceFull != distanceFull) {
//     EEPROM.put(EEPROM_DISTANCE_FULL, distanceFull);
//     settingsChanged = true;
//   }

//   float eepromDistanceEmpty;
//   EEPROM.get(EEPROM_DISTANCE_EMPTY, eepromDistanceEmpty);
//   if (eepromDistanceEmpty != distanceEmpty) {
//     EEPROM.put(EEPROM_DISTANCE_EMPTY, distanceEmpty);
//     settingsChanged = true;
//   }

//   // Zapis int (pumpTime, delayTime)
//   EEPROM.write(EEPROM_PUMP_TIME, (pumpTime >> 8) & 0xFF);  // Wyższy bajt
//   EEPROM.write(EEPROM_PUMP_TIME + 1, pumpTime & 0xFF);     // Niższy bajt
//   EEPROM.write(EEPROM_DELAY_TIME, (delayTime >> 8) & 0xFF);  // Wyższy bajt
//   EEPROM.write(EEPROM_DELAY_TIME + 1, delayTime & 0xFF);     // Niższy bajt

//   // Zapis bool (1 bajt)
//   EEPROM.put(EEPROM_BUZZER_STATUS, buzzerEnabled);

//   if (settingsChanged) {
//     EEPROM.commit();
//     debugPrint("Zapisano wszystkie wartości do EEPROM.");
//   }
// }

// Funkcja, która rejestruje wszystkie encje w Home Assistant po połączeniu z MQTT
void onMqttConnected() {
  pumpSensor.setValue("OFF");
  waterSensor.setValue("OFF");
  reserveSensor.setValue(reserveActive ? "ON" : "OFF");
  waterLevelPercent.setValue(String(currentWaterLevelPercent, 1).c_str());
  waterVolumeLiters.setValue(String(currentWaterVolumeLiters, 3).c_str());

  pumpTimeNumber.setState(pumpTime);
  delayTimeNumber.setState(delayTime);
  tankDiameterHA.setState(tankDiameter);
  distanceFullHA.setState(distanceFull);
  distanceEmptyHA.setState(distanceEmpty);
  reserveThresholdHA.setState(reserveThreshold);
  buzzerSwitch.setState(buzzerEnabled);
  alarmSwitch.setState(alarmActive);
  serviceSwitch.setState(serviceMode);
}

// --- Zmiana czasu pracy pompy
void onPumpTimeCommand(HANumeric value, HANumber* sender) {
  int newPumpTime = value.toInt8();  // Używaj toInt8 dla liczb całkowitych
  //debugPrint("Otrzymana wartosc pumpTime z HA: " + String(newPumpTime));

  if (newPumpTime > 0 && newPumpTime <= 120 && newPumpTime != pumpTime) {
    pumpTime = newPumpTime;
    EEPROM.write(EEPROM_PUMP_TIME, (pumpTime >> 8) & 0xFF);  // Wyższy bajt
    EEPROM.write(EEPROM_PUMP_TIME + 1, pumpTime & 0xFF);     // Niższy bajt
    EEPROM.commit();
    pumpTimeNumber.setState(pumpTime);
    debugPrint("Zaktualizowano czas pracy pompy: " + String(pumpTime) + " s");
  } else {
    debugPrint("Niepoprawna wartosc czasu pracy pompy lub brak zmiany");
  }
}

// --- Zmiana czasu opóźnienia
void onDelayTimeCommand(HANumeric value, HANumber* sender) {
  int newDelayTime = value.toInt8();  // Używamy metody toInt8, aby przekonwertować wartość na liczbę całkowitą
  debugPrint("Otrzymana wartosc delayTime z HA: " + String(newDelayTime));  // Wypisujemy otrzymaną wartość czasu opóźnienia

  // Sprawdzamy, czy nowa wartość opóźnienia jest większa od 0, mniejsza lub równa 30 
  // oraz czy różni się od aktualnej wartości opóźnienia
  if (newDelayTime > 0 && newDelayTime <= 30 && newDelayTime != delayTime) {
    delayTime = newDelayTime;  // Aktualizujemy wartość czasu opóźnienia

    // Zapisujemy nowy czas opóźnienia w EEPROM, rozdzielając go na dwa bajty
    EEPROM.write(EEPROM_DELAY_TIME, (delayTime >> 8) & 0xFF);  // Wyższy bajt
    EEPROM.write(EEPROM_DELAY_TIME + 1, delayTime & 0xFF);     // Niższy bajt
    EEPROM.commit();  // Zapisujemy zmiany w pamięci EEPROM

    delayTimeNumber.setState(delayTime);  // Aktualizujemy stan czasu opóźnienia w Home Assistant
    debugPrint("Zaktualizowano czas opoznienia: " + String(delayTime) + " s");  // Wypisujemy informację o zaktualizowanym czasie opóźnienia
  } else {
    debugPrint("Niepoprawna wartosc czasu opoznienia lub brak zmiany");  // Wypisujemy informację, jeśli wartość jest niepoprawna lub nie ma zmiany
  }
}

// --- Zmiana stanu buzzera
void onBuzzerCommand(bool state, HASwitch* sender) {
  buzzerEnabled = state;                                              // Zmieniamy stan buzzera na podstawie komendy z Home Assistant
  EEPROM.put(EEPROM_BUZZER_STATUS, buzzerEnabled);                    // Zapisujemy stan buzzera do pamięci EEPROM
  EEPROM.commit();                                                    // Upewniamy się, że zmiany zostały zapisane w pamięci EEPROM
  buzzerSwitch.setState(buzzerEnabled);                               // Aktualizujemy stan przełącznika buzzera w Home Assistant
  debugPrint(buzzerEnabled ? "Buzzer wlaczony" : "Buzzer wylaczony"); // Wypisujemy informację o stanie buzzera
}

// --- Obsługa trybu serwis
void onServiceModeCommand(bool state, HASwitch* sender) {
  serviceMode = state;                                                        // Ustawiamy tryb serwisowy zgodnie z otrzymanym stanem
  debugPrint(serviceMode ? "Tryb serwis wlaczony" : "Tryb serwis wylaczony"); // Wypisujemy informację o włączeniu lub wyłączeniu trybu serwisowego

  // Ustaw stan przełącznika serwisowego w Home Assistant zgodnie z rzeczywistym stanem
  serviceSwitch.setState(serviceMode);

  // Jeśli tryb serwisowy jest włączony, natychmiast zatrzymaj pompę
  if (serviceMode) {
    stopPump();                                                               // Wywołujemy funkcję zatrzymującą pompę
  }
}

// --- Kasowanie alarmu
void clearAlarm() {
  if (alarmActive) {
    alarmActive = false;
    alarmSwitch.setState(false);
    debugPrint("Alarm skasowany");

    if (sensorState && !serviceMode) {
      startDelayTime = millis();
      delayInProgress = true;
      debugPrint("Uruchomienie opoznienia po skasowaniu alarmu");
    }
  } else {
    debugPrint("Brak aktywnego alarmu do skasowania");
  }
}

// --- Sprawdzanie stanu czujnika wody
void checkSensor() {
  if (serviceMode) {
    return;  // W trybie serwisowym pomijamy działanie czujnika
  }

  // Odczyt aktualnego stanu czujnika - LOW oznacza aktywność
  bool currentSensorState = (digitalRead(SENSOR_PIN) == LOW);
  if (sensorState != currentSensorState) {
    sensorState = currentSensorState;
    waterSensor.setValue(sensorState ? "ON" : "OFF");
    debugPrint("Stan czujnika zmieniony: " + String(sensorState ? "ON" : "OFF"));
    
    if (sensorState && !alarmActive) {
      startDelayTime = millis();
      delayInProgress = true;
    }
    
    if (!sensorState && digitalRead(PUMP_PIN) == HIGH) {
      stopPump();
    }
  }

  // Dodanie logiki rezerwy z histerezą
  float currentDistance = measureDistance();
  bool reserveActiveNew = currentDistance >= (reserveThreshold + 10); // Włącz rezerwę gdy odległość >= próg + histereza

  if (reserveActiveNew != reserveActive && currentDistance < reserveThreshold) {
    reserveActive = reserveActiveNew;
    reserveSensor.setValue(reserveActive ? "ON" : "OFF");
    debugPrint("Zaktualizowano stan rezerwy: " + String(reserveActive ? "ON" : "OFF"));
  }
}

// --- Obsługa opóźnienia przed włączeniem pompy
void delayBeforePumpNonBlocking() {
  if (!serviceMode && delayInProgress && millis() - startDelayTime >= delayTime * 1000UL) {
    delayInProgress = false;
    startPump();
  }
}

// --- Uruchomienie pompy
void startPump() {
  // Sprawdzenie, czy aktywowany jest tryb serwisowy
  if (serviceMode) {
    return;  // W trybie serwis pompa się nie uruchomi
  }
  // Ustawienie pinu pompy na HIGH, co uruchamia pompę
  digitalWrite(PUMP_PIN, HIGH);
  // Zapisanie bieżącego czasu jako czas uruchomienia pompy
  previousPumpTime = millis();
  // Ustawienie stanu przełącznika pompy na aktywny
  pumpSensor.setValue("ON");
  // Wydrukowanie komunikatu debugowego informującego o uruchomieniu pompy
  debugPrint("Pompa uruchomiona");
}

// --- Zatrzymanie pompy
void stopPump() {
  // Ustawienie pinu pompy na LOW, co zatrzymuje pompę
  digitalWrite(PUMP_PIN, LOW);
  // Ustawienie stanu przełącznika pompy na nieaktywny
  pumpSensor.setValue("OFF");
  // Zmiana stanu zmiennej informującej o trwającym opóźnieniu na false
  delayInProgress = false;
  // Resetowanie licznika czasu pracy pompy
  previousPumpTime = 0;  
  // Wydrukowanie komunikatu debugowego informującego o zatrzymaniu pompy
  debugPrint("Pompa zatrzymana");
}

// --- Sprawdzanie stanu alarmu
void checkAlarm() {
  // Sprawdzenie, czy tryb serwisowy nie jest aktywny
  if (!serviceMode && digitalRead(PUMP_PIN) == HIGH && (millis() - previousPumpTime) / 1000 >= pumpTime) {
    // Zatrzymanie pompy
    stopPump();
    // Aktywacja alarmu
    alarmActive = true;
    // Ustawienie stanu przełącznika alarmowego na aktywny
    alarmSwitch.setState(true);
    // Wydrukowanie komunikatu debugowego
    debugPrint("Alarm - pompa dzialala zbyt dlugo");
  }
}

// --- Obsługa buzzera podczas alarmu i trybu serwis
void handleBuzzer() {
  static unsigned long buzzerStartTime = 0;  // Przechowuje czas, kiedy buzzer został włączony
  static bool buzzerActive = false;  // Flaga wskazująca, czy buzzer jest aktualnie aktywny

  // Jeśli buzzer jest wyłączony (buzzerEnabled == false), zakończ wykonywanie funkcji
  if (!buzzerEnabled) return;

  // Sprawdza, czy alarm jest aktywny lub tryb serwisowy jest włączony oraz czy minęła minuta od ostatniego beepnięcia
  if ((alarmActive || serviceMode) && millis() - lastBeepTime >= 60000) {
    buzzerActive = true;  // Ustaw buzzer jako aktywny
    digitalWrite(BUZZER_PIN, HIGH);  // Włącz buzzer
    buzzerStartTime = millis();  // Zapisz czas rozpoczęcia działania buzzera
    lastBeepTime = millis();  // Zaktualizuj czas ostatniego beepnięcia
  }

  // Jeśli buzzer jest aktywny i minęło 100 ms od jego włączenia
  if (buzzerActive && millis() - buzzerStartTime >= 100) {
    digitalWrite(BUZZER_PIN, LOW);  // Wyłącz buzzer
    buzzerActive = false;  // Ustaw buzzer jako nieaktywny
  }
}

// --- Sprawdzanie przycisku do kasowania alarmu
void checkButton() {
  static unsigned long lastButtonPress = 0;  // Przechowuje czas ostatniego naciśnięcia przycisku

  // Sprawdza, czy przycisk jest naciśnięty (stan niski) i czy od ostatniego naciśnięcia minęło więcej niż 300 ms
  if (digitalRead(BUTTON_PIN) == LOW && millis() - lastButtonPress > 300) {
    clearAlarm();  // Kasuje alarm
    lastButtonPress = millis();  // Aktualizuje czas ostatniego naciśnięcia przycisku
  }
}

// --- Rezerwa
void onReserveThresholdChange(HANumeric value, HANumber* sender) {
  float newReserveThreshold = value.toFloat();

  if (newReserveThreshold > distanceFull && newReserveThreshold < distanceEmpty && newReserveThreshold != reserveThreshold) {
    reserveThreshold = newReserveThreshold;
    EEPROM.put(EEPROM_RESERVE_THRESHOLD, reserveThreshold);  // Zapis do EEPROM
    EEPROM.commit();  // Zapisz zmiany
    reserveThresholdHA.setState(reserveThreshold);
    debugPrint("Zaktualizowano prog rezerwy: " + String(reserveThreshold) + " mm");
  } else {
    debugPrint("Niepoprawna wartosc progu rezerwy lub brak zmiany");
  }
}

void configureReserveSlider() { 
  reserveThresholdHA.setMin(distanceFull);  // Ustaw minimalną wartość na odległość pełnego zbiornika
  reserveThresholdHA.setMax(distanceEmpty);  // Ustaw maksymalną wartość na odległość pustego zbiornika
  reserveThresholdHA.setStep(1);
  reserveThresholdHA.setMode(HANumber::ModeSlider);
}   

void configureHASensors() {
  resetWatchdog();
  // Inicjalizacja urządzenia
  device.setName("HydroSense");
  device.setModel("HS ESP8266");
  device.setManufacturer("PMW");
  device.setSoftwareVersion("9.11.24");
  // Ustawienia dla czujnika wody
  waterSensor.setName("Czujnik wody");
  waterSensor.setIcon("mdi:water");  // Ikona kropli wody
  // Ustawienia dla czujnika rezerwy
  reserveSensor.setName("Rezerwa");
  reserveSensor.setIcon("mdi:alert-outline");  // Ikona ostrzeżenia dla rezerwy
  // Ustawienia dla procentowego poziomu wody w zbiorniku
  waterLevelPercent.setName("Zapełnienie zbiornika");
  waterLevelPercent.setIcon("mdi:water-percent");  // Ikona z procentem wody
  waterLevelPercent.setUnitOfMeasurement("%");  // Jednostka: procent
  // Ustawienia dla objętości wody w zbiorniku
  waterVolumeLiters.setName("Ilość wody");
  waterVolumeLiters.setIcon("mdi:cup-water");  // Ikona kubka z wodą
  waterVolumeLiters.setUnitOfMeasurement("l");  // Jednostka: litry
  // Ustawienia dla sensora odległości
  pomiarSensor.setName("Pomiar Odległości");
  pomiarSensor.setIcon("mdi:ruler");  // Ikona linijki
  pomiarSensor.setUnitOfMeasurement("mm");  // Jednostka: mm
  // Ustawienia dla pompy
  pumpSensor.setName("Pompa");
  pumpSensor.setIcon("mdi:water-pump");  // Ikona pompy wodnej
  // Ustawienia dla przełącznika buzzer (dźwięk)
  buzzerSwitch.setName("Dzwięk");
  buzzerSwitch.setIcon("mdi:bell");  // Ikona dzwonka dla buzzera
  // Ustawienia dla przełącznika alarmu
  alarmSwitch.setName("Alarm");
  alarmSwitch.setIcon("mdi:alert");  // Ikona alarmu
  // Ustawienia dla przełącznika trybu serwisowego
  serviceSwitch.setName("Serwis");
  serviceSwitch.setIcon("mdi:tools");  // Ikona narzędzi dla trybu serwisowego
  // Konfiguracja czasu pracy pompy dla Home Assistant
  pumpTimeNumber.setName("Czas pracy pompy");
  pumpTimeNumber.setIcon("mdi:clock");  // Ikona zegara
  pumpTimeNumber.setUnitOfMeasurement("s");  // Jednostka: s
  pumpTimeNumber.setMin(1);  // Minimalna wartość
  pumpTimeNumber.setMax(120);  // Maksymalna wartość
  pumpTimeNumber.setStep(1);  // Krok zmiany wartości
  pumpTimeNumber.setMode(HANumber::ModeSlider);  // Tryb suwaka
  // Konfiguracja czasu opóźnienia dla Home Assistant
  delayTimeNumber.setName("Czas opóźnienia");
  delayTimeNumber.setIcon("mdi:clock");  // Ikona zegara
  delayTimeNumber.setUnitOfMeasurement("s");  // Jednostka: s
  delayTimeNumber.setMin(1);
  delayTimeNumber.setMax(15);
  delayTimeNumber.setStep(1);
  delayTimeNumber.setMode(HANumber::ModeSlider);
  // Konfiguracja średnicy zbiornika dla Home Assistant
  tankDiameterHA.setName("Zbiornik średnica");
  tankDiameterHA.setIcon("mdi:ruler");  // Ikona linijki dla średnicy
  tankDiameterHA.setUnitOfMeasurement("mm");  // Jednostka: mm
  tankDiameterHA.setMin(50);
  tankDiameterHA.setMax(200);
  tankDiameterHA.setStep(1);
  tankDiameterHA.setMode(HANumber::ModeSlider);
  // Konfiguracja odległości pełnego zbiornika dla Home Assistant
  distanceFullHA.setName("Zbiornik pełny");
  distanceFullHA.setIcon("mdi:ruler");  // Ikona linijki
  distanceFullHA.setUnitOfMeasurement("mm");  // Jednostka: mm
  distanceFullHA.setMin(10);
  distanceFullHA.setMax(100);
  distanceFullHA.setStep(1);
  distanceFullHA.setMode(HANumber::ModeSlider);
  // Konfiguracja odległości pustego zbiornika dla Home Assistant
  distanceEmptyHA.setName("Zbiornik pusty");
  distanceEmptyHA.setIcon("mdi:ruler");  // Ikona linijki
  distanceEmptyHA.setUnitOfMeasurement("mm");  // Jednostka: mm
  distanceEmptyHA.setMin(500);
  distanceEmptyHA.setMax(750);
  distanceEmptyHA.setStep(1);
  distanceEmptyHA.setMode(HANumber::ModeSlider);
  // Konfiguracja progu rezerwy dla Home Assistant
  reserveThresholdHA.setName("Zbiornik rezerwa");
  reserveThresholdHA.setIcon("mdi:ruler");  // Ikona wody z minusem dla progu rezerwy
  reserveThresholdHA.setUnitOfMeasurement("mm");  // Jednostka: mm
  reserveSensor.setValue(reserveActive ? "ON" : "OFF");
  // Brak wody
  waterEmptySensor.setName("Brak wody");
  waterEmptySensor.setIcon("mdi:water-off");  // Ikona wskazująca brak wody
  //Przełączniki
  buzzerSwitch.setName("Dzwięk");
  buzzerSwitch.setIcon("mdi:bell");
  alarmSwitch.setName("Alarm");
  alarmSwitch.setIcon("mdi:alert");
  serviceSwitch.setName("Serwis");
  serviceSwitch.setIcon("mdi:tools");
}

// --- 
void reconnectMQTT() {
  static int mqttAttemptCounter = 0; // Licznik prób połączenia z MQTT

  // Sprawdź, czy Wi-Fi jest połączone oraz czy MQTT nie jest połączone
  if (WiFi.status() == WL_CONNECTED && !haMqtt.isConnected() && millis() - lastMqttAttemptTime >= mqttReconnectInterval) {
    debugPrint("Proba nawiazania polaczenia z serwerem MQTT...");

    // Spróbuj połączyć się z MQTT
    bool connected = haMqtt.begin(mqtt_server, mqtt_user, mqtt_password);

    if (connected) {
      mqttAttemptCounter = 0;
      mqttReconnectInterval = 5000;  // Przywróć domyślny interwał
      debugPrint("Polaczono z MQTT!");  // Komunikat po pomyślnym połączeniu
      lastMqttAttemptTime = millis(); // Zaktualizuj czas po pomyślnym połączeniu
    } else {
      mqttAttemptCounter++;
      debugPrint("Nie udalo sie polaczyc z MQTT");

      if (mqttAttemptCounter >= 3) {
        mqttReconnectInterval *= 2;  // Zwiększ interwał po 3 nieudanych próbach
      }
    }
  } else if (haMqtt.isConnected()) {
    static bool mqttAlreadyConnected = false;

    // Wypisuj komunikat tylko raz, jeśli połączenie zostało nawiązane
    if (!mqttAlreadyConnected) {
      debugPrint("Juz polaczono z MQTT!");  // Komunikat, gdy połączenie jest aktywne
      mqttAlreadyConnected = true;  // Zmień stan na połączony
    }
  } else {
    // Resetuj flagę, jeśli połączenie jest zerwane
    static bool mqttAlreadyConnected = false;
    mqttAlreadyConnected = false;
  }

  // Zaktualizuj czas ostatniej próby
  lastMqttAttemptTime = millis();
}

// ---
void manageConnections() {
  static unsigned long lastAttempt = 0;
  if (millis() - lastAttempt > 15000) { // Próbuj co 15 sekund
    checkWifiConnection();
    if (wifiConnected) reconnectMQTT();
    lastAttempt = millis();
  }
}

// Funkcja pomocnicza do początkowej synchronizacji MQTT z Home Assistant
void synchronizeInitialStateWithHomeAssistant() {
  pumpTimeNumber.setState(pumpTime);
  delayTimeNumber.setState(delayTime);
  tankDiameterHA.setState(tankDiameter);
  distanceFullHA.setState(distanceFull);
  distanceEmptyHA.setState(distanceEmpty);
  reserveThresholdHA.setState(reserveThreshold);
  buzzerSwitch.setState(buzzerEnabled);
  alarmSwitch.setState(alarmActive);
  serviceSwitch.setState(serviceMode);
}

// --- SETUP ---
void setup() {
  Serial.begin(115200);
  ESP.wdtEnable(2000);  // Ustaw watchdog na 2s
  EEPROM.begin(512);

  // Inicjalizacja ustawień z EEPROM
  if (!loadSettings()) {
    initializeEEPROM();  // Inicjalizuj EEPROM domyślnymi wartościami, jeśli dane są niepoprawne
    saveSettings();      // Wymuś zapis, aby suma kontrolna była poprawna
  }
  
  // Konfiguracja połączenia WiFi i MQTT
  checkWifiConnection();
  if (wifiConnected) reconnectMQTT();

  // Jeśli buzzer jest włączony, powitanie dźwiękiem
  if (buzzerEnabled) {
    welcomeBuzzer();
  }

  // Konfiguracja czujników Home Assistant
  configureHASensors(); 
  synchronizeInitialStateWithHomeAssistant();  // Funkcja synchronizacji początkowej

  // Ustawienie początkowych stanów w Home Assistant
  pumpTimeNumber.setState(pumpTime);  // Ustaw początkowy stan czasu pracy pompy
  delayTimeNumber.setState(delayTime);  // Ustaw początkowy stan czasu opóźnienia
  haMqtt.loop();  // Synchronizacja danych z Home Assistant
  
  // Konfiguracja suwaka rezerwy
  configureReserveSlider();

  // Obsługa zmian ustawień w Home Assistant
  pumpTimeNumber.onCommand(onPumpTimeCommand);
  delayTimeNumber.onCommand(onDelayTimeCommand);
  buzzerSwitch.onCommand(onBuzzerCommand);
  alarmSwitch.onCommand(onAlarmSwitchCommand);
  serviceSwitch.onCommand(onServiceModeCommand);
  tankDiameterHA.onCommand(onTankDiameterChange);
  distanceFullHA.onCommand(onDistanceFullChange);
  distanceEmptyHA.onCommand(onDistanceEmptyChange);
  reserveThresholdHA.onCommand(onReserveThresholdChange);

  // Działania przy ponownym połączeniu z MQTT
  haMqtt.onConnected([]() {
    pumpTimeNumber.setState(pumpTime);  // Ustaw czas pracy pompy
    delayTimeNumber.setState(delayTime);  // Ustaw czas opóźnienia
    buzzerSwitch.setState(buzzerEnabled);  // Ustaw stan buzzera
    alarmSwitch.onCommand(onAlarmSwitchCommand);
    serviceSwitch.onCommand(onServiceModeCommand);
    tankDiameterHA.setState(tankDiameter);  // Ustaw średnicę zbiornika
    distanceFullHA.setState(distanceFull);  // Ustaw odległość dla pełnego zbiornika
    distanceEmptyHA.setState(distanceEmpty);  // Ustaw odległość dla pustego zbiornika
    reserveThresholdHA.setState(reserveThreshold);  // Ustaw próg rezerwy
  });

  // Obsługa zmiany czasu pracy pompy
  pumpTimeNumber.onCommand([](HANumeric number, HANumber* sender) {
    pumpTime = static_cast<int>(number.toInt8());
    saveSettings();  // Zapisz zmiany do EEPROM
    pumpTimeNumber.setState(pumpTime);
    debugPrint("Zmieniono czas pracy pompy na: " + String(pumpTime));
  });

  // Obsługa zmiany czasu opóźnienia
  delayTimeNumber.onCommand([](HANumeric number, HANumber* sender) {
    delayTime = static_cast<int>(number.toInt8());
    saveSettings();  // Zapisz zmiany do EEPROM
    delayTimeNumber.setState(delayTime);
    debugPrint("Zmieniono czas opoznienia na: " + String(delayTime));
  });

  // Obsługa zmiany średnicy zbiornika
  tankDiameterHA.onCommand([](HANumeric number, HANumber* sender) {
    tankDiameter = number.toFloat();
    saveSettings();  // Zapisz zmiany do EEPROM
    tankDiameterHA.setState(tankDiameter);
    debugPrint("Zmieniono srednice zbiornika na: " + String(tankDiameter));
  });

  // Obsługa zmiany odległości pełnego zbiornika
  distanceFullHA.onCommand([](HANumeric number, HANumber* sender) {
    distanceFull = number.toFloat();
    saveSettings();  // Zapisz zmiany do EEPROM
    distanceFullHA.setState(distanceFull);
    debugPrint("Zmieniono odleglosc pelnego zbiornika na: " + String(distanceFull));
  });

  // Obsługa zmiany odległości pustego zbiornika
  distanceEmptyHA.onCommand([](HANumeric number, HANumber* sender) {
    distanceEmpty = number.toFloat();
    saveSettings();  // Zapisz zmiany do EEPROM
    distanceEmptyHA.setState(distanceEmpty);
    debugPrint("Zmieniono odleglosc pustego zbiornika na: " + String(distanceEmpty));
  });

  // Obsługa zmiany progu rezerwy
  reserveThresholdHA.onCommand([](HANumeric number, HANumber* sender) {
    reserveThreshold = number.toFloat();
    saveSettings();  // Zapisz zmiany do EEPROM
    reserveThresholdHA.setState(reserveThreshold);
    debugPrint("Zmieniono prog rezerwy na: " + String(reserveThreshold) + " mm");
  });

  // Obsługa zmiany stanu buzzera
  buzzerSwitch.onCommand([](bool state, HASwitch* sender) {
    buzzerEnabled = state;
    saveSettings();  // Zapisz zmiany do EEPROM
    buzzerSwitch.setState(buzzerEnabled);
    debugPrint(buzzerEnabled ? "Buzzer wlaczony" : "Buzzer wylaczony");
  });

  // Konfiguracja i uruchomienie serwera HTTP
  server.on("/", handleRoot);
  server.begin();
  debugPrint("Serwer WWW uruchomiony");

  // Konfiguracja OTA dla aktualizacji oprogramowania
  ArduinoOTA.setHostname("HydroSense");  // Ustaw nazwę urządzenia
  ArduinoOTA.setPassword("hydrosense");  // Ustaw hasło dla OTA
  ArduinoOTA.begin();  // Uruchom OTA
}
 
// --- Główna pętla programu
void loop() {
  resetWatchdog(); // Resetowanie Watchdog
  manageConnections(); // Wywołaj zarządzanie połączeniami

  unsigned long currentTime = millis(); // Pobierz bieżący czas w milisekundach

  if (currentTime - previousTime >= interval) {
    previousTime = currentTime; // Aktualizacja czasu ostatniego sprawdzenia
    float rawDistance = measureDistance(); // Zmierz odległość i dodaj nową wartość do tablicy odczytów
    distanceReadings[readingIndex] = rawDistance;
    readingIndex = (readingIndex + 1) % 10; // Przejdź do następnego indeksu cyklicznie
    float averageDistance = calculateMovingAverage(rawDistance); // Oblicz średnią kroczącą odległości zamiast mediany
    calculateWaterVolume(averageDistance); // Oblicz procentowy poziom wody i objętość na podstawie średniej
    updateHomeAssistant(); // Wyślij aktualizacje do Home Assistant
  }

  // Regularne zadania
  checkSensor();  // Sprawdź stan czujnika
  checkWaterLevel();  // Wywołanie funkcji sprawdzającej stan "Brak wody"
  delayBeforePumpNonBlocking();  // Obsługa opóźnienia przed uruchomieniem pompy bez blokowania
  checkAlarm();  // Sprawdź, czy alarm powinien być włączony lub wyłączony
  handleBuzzer();  // Obsługa buzzera na podstawie stanu alarmu
  checkButton();  // Sprawdź, czy przycisk został naciśnięty

  // Możemy dodawać komunikaty debugowania
  // if (Serial.available()) {
  //   String message = Serial.readStringUntil('\n'); // Odczytujemy komunikat z portu szeregowego
  //   debugMessages += message + "<br>"; // Dodajemy komunikat do zmiennej
  //   Serial.println("Odebrano: " + message);
  // }

  if (currentTime - lastDebugOutput > 1000) {  // Wypisz debugowanie co sekundę
    debugPrint("");                       // Spowoduje wypisanie bufora na Serial
    lastDebugOutput = millis();
  }

  server.handleClient(); // Obsługa serwera WWW
  ArduinoOTA.handle(); // Obsługa OTA (aktualizacji oprogramowania przez sieć)
}