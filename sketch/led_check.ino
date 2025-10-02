#include <Adafruit_NeoPixel.h>

#define PIN            D2          // D2 
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
