/*
  NeoPixel Test – ESP8266 (D4), NEO_GRB + 800 kHz
  - Helligkeits-Ramp
  - Solid Colors (R,G,B,Weiss)
  - ColorWipe vor/zurück
  - Theater-Chase
  - Rainbow
  - Optional: 7-Segment-Layout-Test (4 Digits × 7 Segmente × 2 LEDs)
*/

#include <Adafruit_NeoPixel.h>

// ------------ Hardware ------------
#define LED_PIN       D4

// Falls du dein 4×7-Segment mit je 2 LEDs pro Segment nutzt:
#define NUM_DIGITS    4
#define NUM_SEGMENTS  7
#define LEDS_PER_SEG  2
#define NUM_LEDS      (NUM_DIGITS * NUM_SEGMENTS * LEDS_PER_SEG)

// Wenn du stattdessen eine andere Pixelzahl testen willst, kommentiere die
// vier Defines oben aus und nimm hier z. B.
// #define NUM_LEDS    60

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// ---------- (Optional) 7-Segment-Mapping ----------
const uint8_t digitSegmentMap[4][14] PROGMEM = {
  { 0,1,  2,3,  4,5,  6,7,  8,9, 10,11, 12,13 },
  {14,15, 16,17, 18,19, 20,21, 22,23, 24,25, 26,27 },
  {28,29, 30,31, 32,33, 34,35, 36,37, 38,39, 40,41 },
  {42,43, 44,45, 46,47, 48,49, 50,51, 52,53, 54,55 }
};

const uint8_t digitPatterns[10][7] PROGMEM = {
  {1,1,1,1,1,1,0}, {0,1,1,0,0,0,0}, {1,1,0,1,1,0,1}, {1,1,1,1,0,0,1},
  {0,1,1,0,0,1,1}, {1,0,1,1,0,1,1}, {1,0,1,1,1,1,1}, {1,1,1,0,0,0,0},
  {1,1,1,1,1,1,1}, {1,1,1,1,0,1,1}
};

// ------------- Helpers -------------
uint32_t Color(uint8_t r,uint8_t g,uint8_t b){ return strip.Color(r,g,b); }
void clearShow(){ strip.clear(); strip.show(); }

void colorWipe(uint32_t c, uint16_t wait=20){
  for (uint16_t i=0; i<strip.numPixels(); i++){
    strip.setPixelColor(i, c);
    strip.show();
    delay(wait);
  }
}
void colorWipeBack(uint32_t c, uint16_t wait=20){
  for (int i=strip.numPixels()-1; i>=0; i--){
    strip.setPixelColor(i, c);
    strip.show();
    delay(wait);
  }
}

void theaterChase(uint32_t c, uint8_t cycles=10, uint16_t wait=50){
  for (uint8_t j=0; j<cycles; j++){
    for (uint8_t q=0; q<3; q++){
      strip.clear();
      for (uint16_t i=q; i<strip.numPixels(); i+=3) strip.setPixelColor(i, c);
      strip.show();
      delay(wait);
    }
  }
}

uint32_t wheel(byte pos){
  if (pos < 85)  return strip.Color(pos * 3, 255 - pos * 3, 0);
  if (pos < 170){ pos -= 85; return strip.Color(255 - pos * 3, 0, pos * 3); }
  pos -= 170;    return strip.Color(0, pos * 3, 255 - pos * 3);
}

void rainbow(uint16_t cycles=2, uint16_t wait=10){
  for (uint16_t j=0; j<256*cycles; j++){
    for (uint16_t i=0; i<strip.numPixels(); i++){
      strip.setPixelColor(i, wheel((i + j) & 255));
    }
    strip.show();
    delay(wait);
  }
}

void brightnessRamp(uint8_t fromB=5, uint8_t toB=120, uint16_t hold=300){
  strip.setBrightness(fromB);
  strip.fill(Color(255,255,255)); strip.show(); delay(hold);
  for(uint8_t b=fromB; b<=toB; b++){ strip.setBrightness(b); strip.show(); delay(10); }
  for(int b=toB; b>=fromB; b--){ strip.setBrightness(b); strip.show(); delay(5); }
  clearShow();
}

// ----- (Optional) 7-Segment-Tests -----
void lightSegment(uint8_t digit, uint8_t seg, uint32_t c){
  for(uint8_t l=0; l<LEDS_PER_SEG; l++){
    uint8_t idx = pgm_read_byte(&digitSegmentMap[digit][seg*LEDS_PER_SEG + l]);
    strip.setPixelColor(idx, c);
  }
}
void showDigit(uint8_t digit, uint8_t val, uint32_t c){
  for(uint8_t s=0; s<7; s++){
    bool on = pgm_read_byte(&digitPatterns[val][s]);
    if(on) lightSegment(digit, s, c);
  }
}
void segmentChase(uint32_t c, uint16_t wait=120){
  for(uint8_t d=0; d<NUM_DIGITS; d++){
    for(uint8_t s=0; s<7; s++){
      clearShow();
      lightSegment(d, s, c);
      strip.show();
      delay(wait);
    }
  }
}
void digitsSweep(){
  for(uint8_t val=0; val<=9; val++){
    clearShow();
    for(uint8_t d=0; d<NUM_DIGITS; d++) showDigit(d, val, Color(0,255,64));
    strip.show();
    delay(450);
  }
}

// ------------- Setup / Loop -------------
void setup(){
  Serial.begin(115200); delay(100);
  Serial.println("\nNeoPixel LED Test");
  strip.begin();
  strip.setBrightness(60);   // moderat starten
  clearShow();

  // 1) Helligkeits-Ramp
  brightnessRamp(5, 120, 250);

  // 2) Vollfarben
  strip.fill(Color(255,0,0));   strip.show(); delay(500);
  strip.fill(Color(0,255,0));   strip.show(); delay(500);
  strip.fill(Color(0,0,255));   strip.show(); delay(500);
  strip.fill(Color(255,255,255));strip.show(); delay(500);
  clearShow();

  // 3) Wipes
  colorWipe(Color(255,80,0), 8);   delay(200);
  colorWipeBack(Color(0,200,255), 8); delay(200);
  clearShow();

  // 4) 7-Segment-Tests (optional)
  segmentChase(Color(0,255,64), 120);
  digitsSweep();

  // 5) Theater & Rainbow
  theaterChase(Color(255,255,255), 10, 60);
  rainbow(2, 8);
  clearShow();
}

void loop(){
  // Endlos: langsamer Rainbow als „Lebenszeichen“
  rainbow(1, 12);
}
