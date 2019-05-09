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
#define BRIGHTNESS        128       // built-in with FastLED, range: 0-255 (recall that each pixel uses about 60mA at full brightness, so full strip power consumption is roughly: 60mA * NUM_LEDs * (BRIGHTNESS / 255)
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
void setupLEDs();
void sinelon_white();
void rainbow();
void addGlitter(fract8 chanceOfGlitter);
void stepAnimation(int display_mode);
void shiftHue();

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
int display_mode = ALONE;          // animation type -- init animation as single node
bool amController = false;         // flag to designate that this node is the current controller
uint16_t gHue = 0;                 // rotating color used to shift the rainbow animation
uint8_t aloneHue = random(0,254);  // random color set on each reboot, used for the color in the confetti() ("alone") function/animation
uint8_t animationDelay = random(5,20); // random animation speed, between (x,y), used to create a unique color/vibration scheme for each individual light

painlessMesh mesh;

//////////////////////////////////////////////////////////////////////////////////////////////
// LED FUNCTIONS
//////////////////////////////////////////////////////////////////////////////////////////////

void setupLEDs() {
  FastLED.addLeds<LED_TYPE, DATA_PIN, GRB>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(BRIGHTNESS);
}

void sinelon_white() { // white cylon effect
  int pos = beatsin16(22,0,NUM_LEDS);
  leds[pos] = CRGB::White;
}

void confetti()
{
  // random colored speckles that blink in and fade smoothly
  fadeToBlackBy(leds, NUM_LEDS, 10); // a nice fade effect when transitioning back from the connected/rainbow animation
  int pos = random16(NUM_LEDS);
  leds[pos] += CHSV(aloneHue + random8(32), 200, 255);
}

void rainbow() {
  fill_rainbow(leds, NUM_LEDS, gHue, 7); // FastLED's built-in rainbow generator
}

void addGlitter(fract8 chanceOfGlitter) {
  if (random8() < chanceOfGlitter) { leds[ random16(NUM_LEDS) ] += CRGB::White; }
}

void stepAnimation(int display_mode) {
  switch (display_mode) {
    case ALONE: // "confetti" effect, searching for connections
      EVERY_N_MILLISECONDS( animationDelay ) { confetti(); }
      FastLED.show();
    break;

    case CONNECTED: // "rainbow" effect, connected!
      rainbow();

      if (amController == true) { // the controller gets a bit of glitter
        addGlitter(30);
      }

      //sinelon_white();
      FastLED.show();
    break;
  }
} // stepAnimation

void shiftHue() {
  if (gHue > 254) {
    gHue = 0; // keeping things between 0 and 255

    if (amController == true && mesh.getNodeList().size() > 0) { // experimental, trying to get animations in sync.  Every time gHue cycles, the controller will send out a "keyframe" to the other nodes.
      String keyframe_msg = "KEYFRAME";
      mesh.sendBroadcast(keyframe_msg);

      Serial.printf(">> CONTROLLER KEYFRAME broadcast message sent.\n");
    }
  }
  else { gHue++; } // slowly cycle the "base color" through the rainbow
}

//////////////////////////////////////////////////////////////////////////////////////////////
// MESH FUNCTIONS
//////////////////////////////////////////////////////////////////////////////////////////////

void setupMesh() {
  display_mode = ALONE;
  Task taskSendMessage(TASK_SECOND * 1 , TASK_FOREVER, &sendMessage);

  //mesh.setDebugMsgTypes(ERROR | MESH_STATUS | CONNECTION | SYNC | COMMUNICATION | GENERAL | MSG_TYPES | REMOTE); // all types on
  mesh.setDebugMsgTypes(ERROR | MESH_STATUS | STARTUP);  // set before mesh init() so that you can see startup messages

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

  if (amController == true && mesh.getNodeList().size() > 0) {
    display_mode = CONNECTED;
  }

  stepAnimation(display_mode);  // check for animation update
}

void controllerElection() {
  SimpleList<uint32_t> nodes;
  uint32_t myNodeID = mesh.getNodeId();
  uint32_t lowestNodeID = myNodeID;

  nodes = mesh.getNodeList();
  Serial.printf("Num nodes: %d\n", nodes.size());
  Serial.printf("Connection list:");

  for (SimpleList<uint32_t>::iterator node = nodes.begin(); node != nodes.end(); ++node) {
    Serial.printf(" %u", *node);
    if (*node < lowestNodeID) lowestNodeID = *node;
  }
  Serial.println();

  Serial.printf(">> CONTROLLER ELECTION\n");
  Serial.printf(" > Election result: ");

  if (lowestNodeID == myNodeID) {
    Serial.printf("I am the controller now (node id: %u)", myNodeID);
    Serial.println();
    amController = true;
  }
  else {
    Serial.printf("Node %u is the controller", lowestNodeID);
    Serial.println();
    amController = false;
  }
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
  if (msg == "KEYFRAME") { // this is a call from the controller to reset your global hue.  This gets all the rainbow animations synchronized.
    Serial.printf(">> Keyframe received from %u\n", from);
    gHue = 0;
  }
  else {
    Serial.printf("Setting display mode to %s. Received from %u\n", msg.c_str(), from);
    display_mode = msg.toInt(); // get the new display mode
  }
} // receivedCallback

void newConnectionCallback(uint32_t nodeId) {
    Serial.printf("\n--> startHere: New Connection, nodeId = %u\n", nodeId);
} // newConnectionCallback

// this gets called when a node is added or removed from the mesh, so set the controller to the node with the lowest chip id
void changedConnectionCallback() {
  Serial.printf("Changed connections %s\n",mesh.subConnectionJson().c_str());
  Serial.printf("\n>>amContoller set to: %s\n", amController ? "true" : "false");

  // CONTROLLER ELECTION
  controllerElection();

  // IF THE NODE COUNT IS "0" GO BACK TO THE "ALONE" ANIMATION
  SimpleList<uint32_t> nodes = mesh.getNodeList();

  if (nodes.size() > 0) {
    display_mode = CONNECTED;
  }
  else {
    display_mode = ALONE;
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

//////////////////////////////////////////////////////////////////////////////////////////////
// BASICS
//////////////////////////////////////////////////////////////////////////////////////////////

void setup() {
  Serial.begin(115200);

  setupLEDs(); // creates a new LED object
  setupMesh(); // creates a new mesh network
}

void loop() {
  updateMesh(); // check connected status and update meshed nodes, checks for controller status and calls stepAnimation()
  EVERY_N_MILLISECONDS( 10 ) { shiftHue(); } // increment base hue for a shifting rainbow effect
  EVERY_N_MILLISECONDS( 10000 ) { controllerElection(); } // force a controller election on regular intervals
}
