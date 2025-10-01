# 🚏 Wiener Linien Countdown Display

Ein ESP8266-basiertes Projekt, das die **Abfahrtszeiten der Wiener Linien** von der offiziellen OGD-API abruft und auf einem **NeoPixel 7-Segment-Display (4-stellig mit Doppelpunkt)** darstellt.  
Dazu gibt es ein **Webinterface** mit Setup, Live-Status, LED-Steuerung und einer **OpenStreetMap-Karte** der Haltestelle.

---

## ✨ Features

- **Countdown (MM:SS)** zur nächsten Abfahrt in Farbe (Rot / Gelb / Grün, je nach Zeit).
- **NeoPixel 7-Segment-Display** (4-stellig, Doppelpunkt optional, insgesamt 58 LEDs).
- **Webinterface**:
  - WLAN & API-Key Setup
  - LED-Einstellungen (Helligkeit, Farben, Schwellwerte)
  - Standby- und Power-Schalter
  - Live-Countdown & Log der API-Antwort
  - OpenStreetMap-Karte der Haltestelle
  - LittleFS-Formatierung
- **REST-API Endpunkte** (siehe unten).
- **OTA Updates** und **mDNS (`http://wldisplay.local`)**.
- **LittleFS** für Konfiguration.

---

## 🛠️ Hardware

- **Wemos D1 Mini (ESP8266)** oder kompatibel.
- **NeoPixel-Strip** mit **58 LEDs** (4×14 Segmente + 2 Doppelpunkt-LEDs).
- 5V Netzteil (mind. 2A empfohlen).
- Optional: 3D-Druck-Gehäuse für 7-Segment-Zahlen.

### LED-Layout


- Je Ziffer: 7 Segmente × 2 LEDs = 14 LEDs
- Doppelpunkt: 2 LEDs (Index 56/57)

---

## 🔌 Schaltung

- **D4 (GPIO2)** → Data-In des NeoPixel-Strips  
- **5V** → VCC Strip  
- **GND** → GND Strip  
- ggf. 330 Ω Widerstand in der Datenleitung  
- ggf. 1000 µF Elko parallel an 5V/GND

---

## 📦 Software Setup

### 1. Arduino IDE vorbereiten
- [Arduino IDE](https://www.arduino.cc/en/software) installieren
- ESP8266 Board-Paket hinzufügen (`http://arduino.esp8266.com/stable/package_esp8266com_index.json`)
- Bibliotheken installieren:
  - `Adafruit NeoPixel`
  - `ArduinoJson`
  - `ESP8266WiFi`, `ESP8266WebServer`, `ArduinoOTA`, `ESP8266mDNS`
  - `LittleFS`

### 2. Sketch flashen
- Code aus diesem Repository öffnen
- Board: **Wemos D1 mini**
- Upload & anschließend **LittleFS Filesystem** flashen (Menü: "ESP8266 LittleFS Data Upload")

---

## 🌐 Webinterface

Nach dem Start:

- Im Setup-Modus (wenn keine WLAN-Daten):  
  `WienerLinienDisplaySetup` WLAN (Passwort `12345678`) verbinden  
  → Webinterface: [http://192.168.4.1/](http://192.168.4.1/)

- Im WLAN:  
  Zugriff über **IP** oder [http://wldisplay.local](http://wldisplay.local)

Screens im Webinterface:
- **Setup**: SSID, Passwort, RBL (Haltestellen-ID), API-Key
- **Live-Anzeige**: Countdown (MM:SS), Log der API
- **Karte**: Haltestelle in OpenStreetMap
- **LED & Farben**: Power, Helligkeit, Farb-Schwellen
- **Extras**: LittleFS formatieren, Status, OTA

---

## 🔗 API Endpunkte

Die Firmware bietet eine REST-API:

| Endpoint          | Methode | Beschreibung                          |
|-------------------|---------|--------------------------------------|
| `/api/status`     | GET     | JSON Status (IP, RSSI, Countdown etc.) |
| `/api/config`     | POST    | WLAN + API-Config speichern          |
| `/api/led`        | POST    | LED-Einstellungen (Helligkeit, Farben) |
| `/api/display`    | POST    | Anzeige-Modus (countdown, off, minus, standby) |
| `/api/standby`    | POST    | Standby ein/aus                      |
| `/api/fetch-now`  | POST    | Sofort API neu abfragen              |
| `/api/fs-format`  | POST    | LittleFS formatieren                 |
| `/api/last-payload` | GET   | Letzte API-Rohantwort                 |

---

## 🧪 LED-Diagnose

Test-Sketch, um die LEDs zu prüfen:

```cpp
#include <Adafruit_NeoPixel.h>

#define PIN            D4          // D4 = GPIO2 auf dem Wemos D1 Pro/Mini
#define NUMPIXELS      58          // Anzahl der NeoPixel

Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

void setup() {
  pixels.begin(); // Initialisierung der NeoPixel
  pixels.clear(); // Alle Pixel aus
  pixels.show();
}

void loop() {
  // Test 1: Alle nacheinander rot, grün, blau durchgehen
  for (uint16_t i=0; i<NUMPIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(255,0,0)); // Rot
    pixels.show();
    delay(50);
  }
  delay(300);
  for (uint16_t i=0; i<NUMPIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(0,255,0)); // Grün
    pixels.show();
    delay(50);
  }
  delay(300);
  for (uint16_t i=0; i<NUMPIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(0,0,255)); // Blau
    pixels.show();
    delay(50);
  }
  delay(300);

  // Test 2: Alle gleichzeitig weiß
  for (uint16_t i=0; i<NUMPIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(255,255,255));
  }
  pixels.show();
  delay(1000);

  // Ausmachen
  pixels.clear();
  pixels.show();
  delay(1000);
}

