# HydroSense

HydroSense to inteligentny system monitorowania i kontroli zbiornika wody oparty na ESP8266, zintegrowany z Home Assistant. System umożliwia zdalne monitorowanie poziomu wody, automatyczne sterowanie pompą oraz obsługę alarmów.

## 🌟 Możliwości systemu

- 📊 Monitorowanie poziomu wody w czasie rzeczywistym
- 💧 Automatyczne sterowanie pompą z zabezpieczeniami
- 🚨 Wielopoziomowy system alarmowy
- 🌐 Pełna integracja z Home Assistant przez MQTT
- 🌐 Interfejs webowy do konfiguracji
- 📶 Praca w trybie AP (konfiguracja) lub Client (normalna praca)
- 💾 Przechowywanie konfiguracji w pamięci EEPROM

## 🛠️ Komponenty sprzętowe

### Wymagane

- ⚙️ ESP8266 (NodeMCU v3 lub kompatybilny)
- 🎛️ Czujnik ultradźwiękowy JSN-SR04T
- 🔌 Przekaźnik sterujący pompą
- 🔊 Brzęczyk do sygnalizacji alarmów

### Opcjonalne

- 🔘 Przycisk fizyczny do resetowania alarmów
- 💡 Diody LED do sygnalizacji stanu

## 🚀 Instalacja

1. Sklonuj repozytorium:
   
Potrzebne biblioteki Arduino:

ESP8266WiFi
ESP8266WebServer
ArduinoJson
ArduinoHA (Home Assistant)
LittleFS
W Arduino IDE:

Wybierz płytkę: "NodeMCU 1.0 (ESP-12E Module)"
Ustaw rozmiar Flash: "4MB (FS:1MB OTA:~1MB)"
Wybierz port szeregowy
Wgraj program do ESP8266
🏁 Pierwsze uruchomienie
Po pierwszym uruchomieniu, urządzenie utworzy sieć WiFi "HydroSense-Setup".
Połącz się z tą siecią.
Otwórz przeglądarkę i wpisz adres: http://192.168.4.1.
Skonfiguruj:
Połączenie WiFi
Parametry MQTT dla Home Assistant
Wymiary zbiornika
Ustawienia pompy
📂 Struktura projektu
HydroSense/
├── HydroSense.ino # Plik główny
├── Alarm.cpp/h # System alarmowy
├── Button.cpp/h # Obsługa przycisków
├── ConfigManager.cpp/h # Zarządzanie konfiguracją
├── HomeAssistant.cpp/h # Integracja z HA
├── Network.cpp/h # Obsługa sieci
├── Sensor.cpp/h # Obsługa czujników
└── WebServer.cpp/h # Serwer www

🏡 Integracja z Home Assistant
System udostępnia w Home Assistant:

🌊 Czujnik poziomu wody (%)
💧 Czujnik objętości wody (L)
🔌 Przełącznik pompy
🚨 Przełącznik resetowania alarmu
⚙️ Czujnik stanu pompy
📶 Czujnik połączenia WiFi
🔒 Funkcje bezpieczeństwa
🚱 Zabezpieczenie przed pracą pompy "na sucho"
⏱️ Monitorowanie czasu pracy pompy
⏲️ Automatyczne wyłączenie po przekroczeniu limitu czasu
🛠️ Wykrywanie awarii czujnika poziomu
🌐 Automatyczna rekonfiguracja WiFi przy utracie połączenia
⚙️ Konfiguracja
Wszystkie parametry można skonfigurować przez interfejs webowy:

📶 Parametry sieci (WiFi, MQTT)
📏 Wymiary zbiornika
🚨 Poziomy alarmowe
⏱️ Czasy pracy pompy
🛠️ Kalibracja czujnika
💡 Statusy LED
💡 Ciągłe światło: Normalna praca
💡 Wolne miganie: Tryb konfiguracji (AP)
💡 Szybkie miganie: Problem z połączeniem
💡 Podwójne mignięcie: Alarm aktywny
📜 Licencja
Ten projekt jest udostępniany na licencji MIT.

👤 Autor
pimowo

Uwaga: Ten projekt jest w trakcie rozwoju. Niektóre funkcje mogą ulec zmianie.
