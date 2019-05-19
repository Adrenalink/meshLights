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
#define BRIGHTNESS        128       // built-in with FastLED, range: 0-255 (recall that each pixel uses ~60mA when set to white at full brightness, so full strip power consumption is roughly: 60mA * NUM_LEDs * (BRIGHTNESS / 255)
#define HUE_DELAY         10        // num milliseconds (ms) between hue shifts.  Drop this number to speed up the rainbow effect, raise it to slow it down.

// Mesh setup
#define   MESH_PREFIX     "LEDMesh01"
#define   MESH_PASSWORD   "foofoofoo"
#define   MESH_PORT       5555
#define   ELECTION_DELAY  10          // num seconds between forced controller elections

// Mesh states
#define ALONE     1
#define CONNECTED 2

// LED function prototypes
void setupLEDs();
void addGlitter(fract8 chanceOfGlitter);
void stepAnimation(int display_mode);
void sendKeyframe();
void shiftHue();

// Mesh function prototypes
void setupMesh();
void updateMesh();
void sendMessage();
void receivedCallback(uint32_t from, String &msg);
void newConnectionCallback(uint32_t nodeId);
void changedConnectionCallback();
void nodeTimeAdjustedCallback(int32_t offset);
void sortNodeList(SimpleList<uint32_t> &nodes);

// Global vars
bool amController = false;              // flag to designate that this node is the current controller, which sets the mesh-time and pace for cycling animations
uint8_t display_mode = ALONE;           // animation type -- init animation as single node
uint8_t aloneHue = random(0,223);       // random color set on each reboot, used for the color in the "alone" animation, 223 gives room for a random number 0-32 to be added for confetti effect.
uint8_t animationDelay = random(5,20);  // random animation speed, between (x,y), used to create a unique color/vibration scheme for each individual light when in "alone" mode
uint8_t gHue = 0;                       // global, rotating color used to shift the rainbow animation

painlessMesh mesh;   // first there was mesh,
CRGB leds[NUM_LEDS]; // then there was light!

//////////////////////////////////////////////////////////////////////////////////////////////
// LED FUNCTIONS
//////////////////////////////////////////////////////////////////////////////////////////////

void setupLEDs() {
  FastLED.addLeds<LED_TYPE, DATA_PIN, GRB>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(BRIGHTNESS);
}

void confetti() { // random colored speckles that blink in and fade smoothly
  fadeToBlackBy(leds, NUM_LEDS, 10); // a nice fade effect when transitioning back from the connected/rainbow animation
  int pos = random16(NUM_LEDS);
  leds[pos] += CHSV(aloneHue + random8(32), 200, 255);
}

void addGlitter(fract8 chanceOfGlitter) {
  if (random8() < chanceOfGlitter) { leds[ random16(NUM_LEDS) ] += CRGB::White; }
}

void stepAnimation(int display_mode) {
  switch (display_mode) {
    case ALONE: // "confetti" effect, not part of a mesh, searching for connections
      EVERY_N_MILLISECONDS(animationDelay) { confetti(); } // this gives the confetti animation a unique animation rate on each reboot
      FastLED.show();
    break;

    case CONNECTED: // "rainbow" effect, connected!
      fill_rainbow(leds, NUM_LEDS, gHue, 7); // FastLED's built-in rainbow generator
      if (amController == true) { addGlitter(30); } // the controller gets a bit of glitter
      FastLED.show();
    break;
  }
}

void sendKeyframe() {
  String keyframe_msg = "KEYFRAME";
  mesh.sendBroadcast(keyframe_msg);

  Serial.printf(">> CONTROLLER KEYFRAME - broadcast message sent.\n");
}

void shiftHue() {
  if (gHue > 254) {
    gHue = 0; // keeping things between 0 and 255.
    if (amController == true && mesh.getNodeList().size() > 0) { sendKeyframe(); } // as the controller, announce when resetting base hue (gHue)
  }
  else { gHue++; } // slowly cycle the "base color" through the rainbow
}

//////////////////////////////////////////////////////////////////////////////////////////////
// MESH FUNCTIONS
//////////////////////////////////////////////////////////////////////////////////////////////

void setupMesh() {
  display_mode = ALONE;

  Task taskSendMessage(TASK_SECOND * 2 , TASK_FOREVER, &sendMessage); // every 2 seconds send a message

  //mesh.setDebugMsgTypes(ERROR | MESH_STATUS | CONNECTION | SYNC | COMMUNICATION | GENERAL | MSG_TYPES | REMOTE); // all types on
  mesh.setDebugMsgTypes(ERROR | MESH_STATUS | STARTUP);  // set before mesh init() so that you can see startup messages

  mesh.init(MESH_PREFIX, MESH_PASSWORD, MESH_PORT);
  mesh.onReceive(&receivedCallback);
  mesh.onNewConnection(&newConnectionCallback);
  mesh.onChangedConnections(&changedConnectionCallback);
  mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);

  mesh.scheduler.addTask(taskSendMessage);
  taskSendMessage.enable();
}

void updateMesh() {
  mesh.update();

  if (amController == true && mesh.getNodeList().size() > 0) {
    display_mode = CONNECTED;
  }

  stepAnimation(display_mode);  // animation update
}

void controllerElection() {
  uint32_t myNodeID = mesh.getNodeId();
  uint32_t lowestNodeID = myNodeID;

  SimpleList<uint32_t> nodes;
  nodes = mesh.getNodeList();

  Serial.printf(">> CONTROLLER ELECTION\n");
  Serial.printf(" . Number of nodes in mesh: %d\n", nodes.size() + 1);
  Serial.printf(" . Other mesh members:");

  for (SimpleList<uint32_t>::iterator node = nodes.begin(); node != nodes.end(); ++node) {
    Serial.printf(" %u", *node);
    if (*node < lowestNodeID) lowestNodeID = *node;
  }

  Serial.println();
  Serial.printf(" . Election result: ");

  if (lowestNodeID == myNodeID) {
    Serial.printf("I am the controller (node id: %u)\n", myNodeID);
    amController = true;
  }
  else {
    Serial.printf("Node %u is the controller\n", lowestNodeID);
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
void receivedCallback(uint32_t from, String &msg) {
  if (msg == "KEYFRAME") { // this is a call from the controller to reset your global hue.  This gets all the rainbow animations synchronized.
    Serial.printf(">> KEYFRAME received from %u.  Local gHue is %u. ", from, gHue);

    // I'm not sure how well the below works, needs more testing.  It's supposed to keep things smooth -- only change animations if gHue is too far from the controller's.
    if (255-gHue>16 && 255-gHue<240) { // when receiving a KEYFRAME message, only reset the global hue to zero if they're out of sync
      gHue = 0;
      Serial.printf("RESETTING gHue to 0.");
    }

    Serial.println();
  }
  else {
    Serial.printf("Setting display mode to %s. Received from %u\n", msg.c_str(), from);
    display_mode = msg.toInt(); // get the new display mode
  }
}

void newConnectionCallback(uint32_t nodeId) {
    Serial.printf("\n--> Start here: New Connection, nodeId = %u\n", nodeId);
}

// this gets called when a node is added or removed from the mesh, so set the controller to the node with the lowest chip id
void changedConnectionCallback() {
  Serial.printf("\n> Changed connections %s\n",mesh.subConnectionJson().c_str());
  Serial.printf("\n>> STATUS: Is this node the controller? %s\n", amController ? "YES" : "NO");

  // calling an election when mesh configuration changes
  controllerElection();

  // IF THE NODE COUNT IS "0" GO BACK TO THE "ALONE" ANIMATION
  SimpleList<uint32_t> nodes = mesh.getNodeList();

  if (nodes.size() > 0) {
    display_mode = CONNECTED;
  }
  else {
    display_mode = ALONE;
  }
}

void nodeTimeAdjustedCallback(int32_t offset) {
    Serial.printf("Adjusted time %u. Offset = %d\n", mesh.getNodeTime(), offset);
}

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
  }

  // copy the sorted list back into the now empty nodes list
  for (SimpleList<uint32_t>::iterator node = nodes_sorted.begin(); node != nodes_sorted.end(); ++node) nodes.push_back(*node);
}

//////////////////////////////////////////////////////////////////////////////////////////////
// BASICS
//////////////////////////////////////////////////////////////////////////////////////////////

void setup() {
  Serial.begin(115200);

  setupMesh(); // Creates a new mesh network
  setupLEDs(); // Constructs LED strand and sets brightness
}

void loop() {
  updateMesh(); // management tasks: check connected status, update meshed nodes, check controller status and calls stepAnimation()

  EVERY_N_MILLISECONDS(HUE_DELAY) { shiftHue(); } // increment base hue for a shifting rainbow effect
  EVERY_N_SECONDS(ELECTION_DELAY) { controllerElection(); } // force a controller election on regular intervals
}
