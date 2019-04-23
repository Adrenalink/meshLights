#define FASTLED_INTERNAL // this needs to come before #include <FastLED.h> to suppress pragma messages during compile time in the Arduino IDE.
#include <FastLED.h>

// LED animation function prototypes
void sinelon();
void rainbow();
void addGlitter(fract8 chanceOfGlitter);
void stepAnimation(int display_mode);
void shiftHue();
void setupLEDs();
