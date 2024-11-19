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

- ⚙️ ESP8266 (Wemos D1 MINI lub kompatybilny)
- 🎛️ Czujnik ultradźwiękowy JSN-SR04T
- 🔌 Przekaźnik sterujący pompą
- 🔊 Brzęczyk do sygnalizacji alarmów

### Opcjonalne

- 🔘 Przycisk fizyczny do resetowania alarmów i przęłączania trybu "Serwis"
- 💡 Diody LED do sygnalizacji stanu

## 🚀 Instalacja

1. Sklonuj repozytorium:

   ```bash
   git clone https://github.com/pimowo/HydroSense.git
   ```

2. Potrzebne biblioteki Arduino:

   - Arduino
   - ArduinoHA (Home Assistant)
   - ArduinoOTA
   - ESP8266WiFi
   - EEPROM
   - WiFiManager

3. W Arduino IDE:

   - Wybierz płytkę: "Wemod D1 MINI"
   - Wybierz port szeregowy
   - Wgraj program do ESP8266

## 🏁 Pierwsze uruchomienie

1. Po pierwszym uruchomieniu, urządzenie utworzy AP:
  - SSID: HydroSense
  - Hasło: hydrosense
2. Połącz się z tą siecią
3. Otwórz przeglądarkę i wpisz adres: http://192.168.4.1
4. Skonfiguruj:
   - Połączenie WiFi
5. 

## 🏡 Integracja z Home Assistant

System udostępnia w Home Assistant:

- 🌊 Czujnik 1 Poziomu wody (%)
- 💧 Czujnik 2 Objętość wody (L)
- 🔌 Czujnik 3 Praca pompy (ON/OFF)
- 📶 Czujnik 4 Pomiar odległości (mm)
- 🚨 Przełącznik 1 Resetowanie alarmu

## 🔒 Funkcje bezpieczeństwa

- 🚱 Zabezpieczenie przed pracą pompy "na sucho"
- ⏱️ Monitorowanie czasu pracy pompy
- ⏲️ Automatyczne wyłączenie po przekroczeniu limitu czasu
- 🛠️ Wykrywanie awarii czujnika poziomu
- 🌐 Automatyczna rekonfiguracja WiFi przy utracie połączenia

## ⚙️ Konfiguracja

Wszystkie parametry można skonfigurować przez interfejs webowy:

- 📶 Parametry sieci (WiFi, MQTT)
- 📏 Wymiary zbiornika
- 🚨 Poziomy alarmowe
- ⏱️ Czas opóźnienia włączenia pompy
- ⏱️ Czasy pracy pompy
- 🛠️ Kalibracja czujnika

## 💡 Statusy LED

- 💡 Ciągłe światło: Normalna praca
- 💡 Wolne miganie: Tryb konfiguracji (AP)
- 💡 Szybkie miganie: Problem z połączeniem
- 💡 Podwójne mignięcie: Alarm aktywny

## 📜 Licencja

Ten projekt jest udostępniany na licencji MIT.

## 👤 Autor

pimowo
pimowo@gmail.com

**Uwaga**: Ten projekt jest w trakcie rozwoju. Niektóre funkcje mogą ulec zmianie.
```
