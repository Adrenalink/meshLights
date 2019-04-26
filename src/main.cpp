#define FASTLED_INTERNAL // this needs to come before #include <FastLED.h> to suppress pragma messages during compile time in the Arduino IDE.
/*
 *  Meshed ESP32 nodes with synchronized animation effects
 *  some code was based on FastLED 100 line "demo reel" and LED_Synch_Mesh_Send by Carl F Sutter (2017)
 */

#include <Arduino.h>
#include <FastLED.h>
#include <painlessMesh.h>

// LED setup
#define NUM_LEDS          60
#define DATA_PIN          13
#define LED_TYPE          WS2812B   // WS2812B or WS2811?
#define BRIGHTNESS        128       // built-in with FastLED, range: 0-255 (recall that each pixel uses about 60mA at full brightness, so full strip power consumption is roughly: 60mA * num_LEDs * (BRIGHTNESS/255)
#define FRAMES_PER_SECOND 120

CRGB leds[NUM_LEDS];

// Mesh setup
#define   MESH_PREFIX     "LEDMesh01"
#define   MESH_PASSWORD   "foofoofoo"
#define   MESH_PORT       5555

// Mesh states
#define ALONE     1
#define CONNECTED 2

// LED function prototypes
void sinelon();
void sinelon_white();
void rainbow();
void addGlitter(fract8 chanceOfGlitter);
void stepAnimation(int display_mode);
void shiftHue();
void setupLEDs();

// Mesh function prototypes
void receivedCallback(uint32_t from, String &msg);
void newConnectionCallback(uint32_t nodeId);
void changedConnectionCallback();
void nodeTimeAdjustedCallback(int32_t offset);
void sortNodeList(SimpleList<uint32_t> &nodes);
void sendMessage();
void setupMesh();
void updateMesh();

// Global vars
int display_mode = ALONE;   // animation type -- init animation as single node
bool amController = false;  // flag to designate that this node is the current controller
uint8_t gHue = 192;         // rotating "base color" used by many of the patterns (0=red, 32=orange, 64=yellow, 128=blue, 192=purple, 255=also red?)

painlessMesh mesh;

//////////////////////////////////////////////////////////////////////////////////////////////
// LED FUNCTIONS
//////////////////////////////////////////////////////////////////////////////////////////////

void setupLEDs() {
  FastLED.addLeds<LED_TYPE, DATA_PIN, GRB>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(BRIGHTNESS);
}

void sinelon() { // a colored dot sweeping back and forth, with fading trails
  fadeToBlackBy(leds, NUM_LEDS, 20);
  int pos = beatsin16(22,0,NUM_LEDS);
  leds[pos] = CHSV(gHue, 255, BRIGHTNESS);
}

void sinelon_white() { // slightly different cylon effect, this one a white dot without the trails caused by fadeToBlackBy().
  int pos = beatsin16(22,0,NUM_LEDS);
  leds[pos] = CRGB::White;
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
      //addGlitter(10);
      sinelon_white();
      FastLED.show();
    break;
  }
} // stepAnimation

void shiftHue() {
  if (gHue > 254) { gHue = 0; } // keeping things between 0 and 255
  else { gHue++; } // slowly cycle the "base color" through the rainbow
}

//////////////////////////////////////////////////////////////////////////////////////////////
// MESH FUNCTIONS
//////////////////////////////////////////////////////////////////////////////////////////////

void setupMesh() {
  display_mode = ALONE;
  Task taskSendMessage( TASK_SECOND * 1 , TASK_FOREVER, &sendMessage );

  //mesh.setDebugMsgTypes( ERROR | MESH_STATUS | CONNECTION | SYNC | COMMUNICATION | GENERAL | MSG_TYPES | REMOTE ); // all types on
  mesh.setDebugMsgTypes( ERROR | MESH_STATUS | CONNECTION | SYNC | COMMUNICATION | MSG_TYPES | REMOTE ); // all types on
  //mesh.setDebugMsgTypes( ERROR | STARTUP );  // set before mesh init() so that you can see startup messages

  mesh.init(MESH_PREFIX, MESH_PASSWORD, MESH_PORT);
  mesh.onReceive(&receivedCallback);
  mesh.onNewConnection(&newConnectionCallback);
  mesh.onChangedConnections(&changedConnectionCallback);
  mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);

  mesh.scheduler.addTask(taskSendMessage);
  taskSendMessage.enable();

  // set yourself to the controller if you're alone on the mesh
  if (mesh.getNodeList().size() == 0) { amController = true; }
}

void updateMesh() {
  mesh.update();
  Serial.printf("\n>>>>> mesh.getNodeTime = %i\n", mesh.getNodeTime());

  if (amController && mesh.getNodeList().size() > 0) {
    display_mode = CONNECTED;
  }

  stepAnimation(display_mode);  // check for animation update
}

// send a broadcast message to all the nodes specifying the new animation mode for all of them
void sendMessage() {
  String msg;
  msg += String(display_mode);
  mesh.sendBroadcast(msg);
}

// this gets called when the designated controller sends a command to start a new animation
// init any animation specific vars for the new mode, and reset the timer vars
void receivedCallback( uint32_t from, String &msg ) {
  Serial.printf("Setting display mode to %s. Received from %u\n", msg.c_str(), from);
  display_mode = msg.toInt(); // get the new display mode
} // receivedCallback

void newConnectionCallback(uint32_t nodeId) {
    Serial.printf("--> startHere: New Connection, nodeId = %u\n", nodeId);
} // newConnectionCallback

// this gets called when a node is added or removed from the mesh, so set the controller to the node with the lowest chip id
void changedConnectionCallback() {
  SimpleList<uint32_t> nodes;
  uint32_t myNodeID = mesh.getNodeId();
  uint32_t lowestNodeID = myNodeID;

  Serial.printf("Changed connections %s\n",mesh.subConnectionJson().c_str());

  nodes = mesh.getNodeList();
  Serial.printf("Num nodes: %d\n", nodes.size());
  Serial.printf("Connection list:");

  for (SimpleList<uint32_t>::iterator node = nodes.begin(); node != nodes.end(); ++node) {
    Serial.printf(" %u", *node);
    if (*node < lowestNodeID) lowestNodeID = *node;
  }
  Serial.println();

  if (lowestNodeID == myNodeID) {
    Serial.printf("Node %u: I am the controller now", myNodeID);
    Serial.println();
    amController = true;

    // ON CHANGE, IF THE NODE COUNT IS "0" THEN THE CONTROLLER IS ALONE AND GOES BACK TO THE "SEEKING" ANIMATION
    if (nodes.size() > 0) {
      display_mode = CONNECTED;
      // need clock sync data to be sent here?  I think just leds[pos] needs to be sent... See nodeTimeAdjustedCallback method.
    }
    else {
      display_mode = ALONE;
    }
  }
  else {
    Serial.printf("Node %u is the controller now", lowestNodeID);
    Serial.println();
    amController = false;
  }
} // changedConnectionCallback

void nodeTimeAdjustedCallback(int32_t offset) {
    Serial.printf("Adjusted time %u. Offset = %d\n", mesh.getNodeTime(),offset);
} // changedConnectionCallback

// sort the given list of nodes
void sortNodeList(SimpleList<uint32_t> &nodes) {
  SimpleList<uint32_t> nodes_sorted;
  SimpleList<uint32_t>::iterator smallest_node;

  // sort the node list
  while (nodes.size() > 0) {
    // find the smallest one
    smallest_node = nodes.begin();
    for (SimpleList<uint32_t>::iterator node = nodes.begin(); node != nodes.end(); ++node) {
      if (*node < *smallest_node) smallest_node = node;
    }

    // add it to the sorted list and remove it from the old list
    nodes_sorted.push_back(*smallest_node);
    nodes.erase(smallest_node);
  } // while

  // copy the sorted list back into the now empty nodes list
  for (SimpleList<uint32_t>::iterator node = nodes_sorted.begin(); node != nodes_sorted.end(); ++node) nodes.push_back(*node);
} // sortNodeList

void setup() {
  Serial.begin(115200);
  setupLEDs(); // creates new LED object
  setupMesh(); // creates a new mesh network
}

void loop() {
  updateMesh(); // check connected status and update meshed nodes, checks for controller status and calls stepAnimation()
  EVERY_N_MILLISECONDS( 20 ) { shiftHue(); } // increment base hue for a shifting rainbow effect
}
