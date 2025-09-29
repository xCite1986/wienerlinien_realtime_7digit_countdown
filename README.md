# Wiener Linien Countdown – 4×7‑Segment NeoPixel Display (ESP8266)

ESP8266‑Projekt (Wemos D1 mini) für eine **4‑stellige 7‑Segment‑Anzeige** aus WS2812B/NeoPixeln.
Zeigt die Restzeit bis zur nächsten Abfahrt (Wiener Linien **OGD Realtime**), hat **REST‑API** zur LED‑Steuerung, **OTA**, **mDNS**, **LittleFS**‑Konfiguration, nicht‑blockierende Loop & Backoff.

> **Projektfoto / LED‑Pfad:** _Hier dein Bild einfügen_ (z. B. in `docs/` ablegen und unten verlinken).

---

## Features

- Countdown in `MM:SS`, Fallback auf blinkende Minuszeichen
- REST‑API
  - `GET /api/status`
  - `POST /api/config` – WLAN, API‑Key, RBL, Zeitzone, TLS‑Optionen
  - `POST /api/led` – Power, Helligkeit, Farbe
  - `POST /api/display` – Modus `countdown|off|minus`, Farbe/Helligkeit
  - `POST /api/fetch-now` – sofortige API‑Abfrage
  - `POST /api/factoryreset` – löscht `/config.json`
  - `GET /status-log` – Ringlog abrufen
- **Sicheres HTTPS** (CA‑Validierung oder Fingerprint‑Pinning; optional `insecure` zum Testen)
- **OTA‑Updates** (ArduinoOTA), **mDNS** (`http://wldisplay.local`)
- **LittleFS** JSON‑Konfiguration
- Nicht‑blockierendes Design mit **Exponential Backoff** bei API‑Fehlern

---

## Hardware

- **Controller:** Wemos D1 mini (ESP8266)
- **LEDs:** WS2812B/NeoPixel (56 LEDs → 4 Digits × 7 Segmente × 2 LEDs/Segment)
- **Widerstand:** 330–470 Ω in Serie im **Data‑Pin** (nahe am ersten LED)
- **Kondensator:** 1000 µF / ≥6.3 V zwischen **+5 V** und **GND** (nah am Strip)
- **Netzteil:** 5 V (mit Reserve; z. B. ≥1 A je nach Helligkeit)
- **Optional:** 3.3 V→5 V **Level‑Shifter** für den Datapin (bei langen Leitungen/Problemen)
- **Gemeinsame Masse ist Pflicht!**

🖼 **Schaltplan**: [`docs/esp8266_neopixel_schematic.svg`](docs/esp8266_neopixel_schematic.svg)

---

## 3D‑Druck

- 4‑stellige 7‑Segment‑Maske mit 2 LEDs pro Segment (insgesamt 56 LEDs).
- Schwarze Separatoren reduzieren Lichtübersprechen; Diffusor weiß/transparent.
- **Foto/LED‑Pfad:** _Hier ergänzen._

---

## Software

### Abhängigkeiten

- Arduino Core ESP8266
- Libraries:
  - `Adafruit NeoPixel`
  - `ArduinoJson`

Eine passende `platformio.ini`:

```ini
[env:d1_mini]
platform = espressif8266
board = d1_mini
framework = arduino
monitor_speed = 115200

lib_deps =
  adafruit/Adafruit NeoPixel@^1.12.3
  bblanchon/ArduinoJson@^7.0.4

build_flags =
  -D PIO_FRAMEWORK_ARDUINO_LWIP2_LOW_MEMORY
  -D BEARSSL_SSL_BASIC
```

### Flashen & erster Start

1. Kompilieren & flashen (PlatformIO).
2. Ohne gespeicherte WLAN‑Daten startet **AP‑Modus**:
   - SSID: `WienerLinienDisplaySetup`
   - Passwort: `12345678`
3. Per REST `/api/config` WLAN+API setzen.
4. Danach erreichbar unter `http://wldisplay.local/` (mDNS) oder via IP.
5. **OTA**: Hostname `wldisplay`.

---

## REST‑API (Kurz)

**Status**  
`GET /api/status`

**Konfiguration**  
`POST /api/config`
```json
{ "ssid":"MeinWLAN", "password":"geheim", "apiKey":"KEY", "rbl":"1234",
  "tz":"CET-1CEST,M3.5.0,M10.5.0/3", "httpsInsecure":false,
  "httpsFingerprint":"AA BB CC ... 99" }
```

**LED‑Steuerung**  
`POST /api/led`
```json
{ "power": true, "brightness": 30, "color": [255,128,0] }
```

**Display‑Modus**  
`POST /api/display`
```json
{ "mode":"countdown" }   // "off" | "minus"
```

**Sofortige Abfrage**  
`POST /api/fetch-now` → `{}`

**Werkseinstellungen**  
`POST /api/factoryreset` → `{}`

**Log**  
`GET /status-log`

---

## Segment‑Mapping

- **Digits:** 0..3 links→rechts
- **Segmente je Digit:** 0..6 (Segment 6 = Mittelstrich)
- **LEDs pro Segment:** 2  
Mapping in `digitSegmentMap` im Quellcode anpassen, falls dein Pfad abweicht.

---

## Sicherheit

- Standard: **TLS‑Validierung** gegen CA‑Store (BearSSL).
- Optional: **Fingerprint‑Pinning** (`httpsFingerprint`).
- Testweise: `httpsInsecure=true` (nicht empfohlen).
- Für Exposed‑Setups: Auth via Reverse‑Proxy/Basic‑Auth ergänzen.

---

## Troubleshooting

- **Flackern/keine Anzeige:** GND gemeinsam? 330–470 Ω Data‑Widerstand? 1000 µF Elko am Strip?
- **Timing instabil:** Leitung kürzen, ggf. Level‑Shifter einsetzen.
- **Falsche Zeit:** `/status-log` prüfen, Zeitzone/ISO‑Parsing ok?
- **Reboots:** Netzteil stärker, Helligkeit reduzieren.

---

## Lizenz

MIT

## Credits

- CAD/3D & Integration: **Alex**
- Libraries: Adafruit NeoPixel, ArduinoJson
- Daten: Wiener Linien OGD Realtime
