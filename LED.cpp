#include "LED.h"

// LED setup
#define NUM_LEDS          60
#define DATA_PIN          13
#define LED_TYPE          WS2812B   // WS2812B or WS2811?
#define BRIGHTNESS        128       // built-in with FastLED, range: 0-255 (recall that each pixel uses about 60mA at full brightness, so full strip power consumption is roughly: 60mA * num_LEDs * (BRIGHTNESS/255)
#define FRAMES_PER_SECOND 120

CRGB leds[NUM_LEDS];

uint8_t gHue = 192;         // rotating "base color" used by many of the patterns (0=red, 32=orange, 64=yellow, 128=blue, 192=purple, 255=also red?)

void setupLEDs() {
  FastLED.addLeds<LED_TYPE, DATA_PIN, GRB>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(BRIGHTNESS);
}

void sinelon() { // a colored dot sweeping back and forth, with fading trails
  fadeToBlackBy(leds, NUM_LEDS, 20);
  int pos = beatsin16(13,0,NUM_LEDS);
  leds[pos] = CHSV(gHue, 255, 192);
}

void rainbow() {
  fill_rainbow(leds, NUM_LEDS, gHue, 7); // FastLED's built-in rainbow generator
}

void addGlitter(fract8 chanceOfGlitter) {
  if (random8() < chanceOfGlitter) { leds[ random16(NUM_LEDS) ] += CRGB::White; }
}

void stepAnimation(int display_mode) {
  switch (display_mode) {
    case 1: // "cylon" effect, searching for connections
      sinelon();
      FastLED.show();
    break;

    case 2: // "rainbow" effect, connected!
      rainbow();
      addGlitter(10);
      FastLED.show();
    break;
  }
} // stepAnimation

void shiftHue() {
  if (gHue > 254) { gHue = 0; } // keeping things between 0 and 255
  else { gHue++; } // slowly cycle the "base color" through the rainbow
}
