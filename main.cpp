/*
 *  Meshed ESP32 nodes with synchronized animation effects
 *  some code was based on FastLED 100 line "demo reel" and LED_Synch_Mesh_Send by Carl F Sutter (2017)
 */

#include <Arduino.h>
#include "mesh.h"
#include "LED.h"

void setup() {
  Serial.begin(115200);
  setupLEDs(); // creates new LED object
  setupMesh(); // creates a new mesh network
}

void loop() {
  updateMesh(); // check connected status and update meshed nodes, checks for controller status and calls stepAnimation()
  EVERY_N_MILLISECONDS( 20 ) { shiftHue(); } // increment base hue for a shifting rainbow effect
}
