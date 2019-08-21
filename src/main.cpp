/*
 *  Meshed ESP32 nodes with synchronized animation effects.
 * 
 *  This adds leader election, display/effect logic, additional messaging functionality and error checking to the amazing 
 *  PainlessMesh and FastLED projects for the sake of keeping LED strands in sync across a mesh.
 * 
 *  some of the initial idea was based on FastLED 100 line "demo reel" and LED_Synch_Mesh_Send by Carl F Sutter (2017)
 */

#define FASTLED_INTERNAL               // this needs to come before #include <FastLED.h> to suppress pragma messages during compile time in the Arduino IDE.
#define ARDUINOJSON_USE_LONG_LONG 1    // default to 'long long' rather than just 'long', node time is calculated in microseconds, giving the json library some sizing expectations.

#include <Arduino.h>
#include <FastLED.h>
#include <painlessMesh.h>
#include <ArduinoJson.h>

// LED setup
#define NUM_LEDS              60           // how many LEDs in your strand?
#define DATA_PIN              13           // your board's data pin connected to your LEDs
#define LED_TYPE              WS2812B      // WS2812B or WS2811?
#define BRIGHTNESS            128          // built-in with FastLED, range: 0-255 (recall that each pixel uses ~60mA when set to white at full brightness, so full strip power consumption is roughly: 60mA * NUM_LEDs * (BRIGHTNESS / 255)
#define HUE_DELAY             12           // num milliseconds (ms) between hue shifts.  Drop this number to speed up the rainbow effect, raise it to slow it down.
#define AMOUNT_OF_GLITTER     10           // "glitter" effect applied to the controller node for visual identification.  range: 0-255.
#define FADE_BY_DISTANCE      false        // boolean that makes the brightness of the LEDs based on wifi signal strength.  Set to false if you want them to use the global BRIGHTNESS value instead.
#define NUM_RAINBOWS          .25          // number of complete rainbows to show on the LED strip at once.  This is the (poorly documented) "deltaHue" variable; basically it determines the increment size of hue shifts between pixels.  Based on my implementation, a value of "1" visually spreads the rainbow effect over the whole strip, "2" will compress it and show two full rainbows patterns, etc.  Values between 0 and 1 (.8 for example) also work, but stretch rather than compress the rainbow on the strip.

// Mesh setup
#define   MESH_SSID           "LEDMesh01"  // the broadcast name of your little mesh network
#define   MESH_PASSWORD       "foofoofoo"  // network password
#define   MESH_PORT           5555         // in a busy space?  Isolate your mesh with a specific port as well
#define   ELECTION_DELAY      10           // num seconds between forced controller elections
#define   MESSAGE_DELAY       2            // num seconds between broadcast messages
#define   MAX_MESSAGE_AGE     250000       // num microseconds ago that a message from the controller can be acted upon. (250,000 microseconds = 250 milliseconds(ms), which seems to work well)
#define   SUPER_CONTROLLER_ID 302673429    // this gives you a special node id that changes the animation.  I'm using it for an art car as a special node in the mesh.  It might be used to the effect of a teacher coming into the classroom.
  
// Mesh states
#define ALONE     1
#define CONNECTED 2

// LED function prototypes
void setupLEDs();
void addGlitter(fract8 chanceOfGlitter);
void stepAnimation(int displayMode);
void shiftHue();

// Mesh function prototypes
void setupMesh();
void updateMesh();
void sendMessage(String *msg);
void controllerElection();
void receivedCallback(uint32_t from, String &msg);
void newConnectionCallback(uint32_t nodeId);
void changedConnectionCallback();
void nodeTimeAdjustedCallback(int32_t offset);
void sortNodeList(SimpleList<uint32_t> &nodes);

// Global vars
bool amController = false;              // flag to designate that this node is the current controller, which sets the mesh-time and pace for cycling animations
long knownControllerID = 0;             // a little validation that you're getting broadcasts from who you expect.  Gets set during a controller election.
uint8_t displayMode = ALONE;            // animation type -- init animation as single node.  Can be set to either ALONE or CONNECTED.
uint8_t aloneHue = random(0,223);       // random color set on each reboot, used for the color in the "alone" animation, 223 gives room for a random number 0-32 to be added for confetti effect.
uint8_t animationDelay = random(8,18);  // random animation speed, between (x,y) milliseconds, used to create a unique color/vibration scheme for each individual light when in "alone" mode
uint8_t gHue = 0;                       // global, rotating color used to shift the rainbow animation

Scheduler userScheduler;
painlessMesh mesh;                      // first there was mesh,
CRGB leds[NUM_LEDS];                    // then there was light!

//////////////////////////////////////////////////////////////////////////////////////////////
// BASICS
//////////////////////////////////////////////////////////////////////////////////////////////

void setup() {
  Serial.begin(115200);

  // Creates a new mesh network
  setupMesh();

  // Constructs LED strand and sets brightness 
  setupLEDs();
}

void loop() {
  // management tasks: check connected status, update meshed nodes, check controller status and calls stepAnimation()
  updateMesh(); 

  // increment base hue for a shifting rainbow effect
  EVERY_N_MILLISECONDS(HUE_DELAY) { shiftHue(); } 
  
  // force a controller election on regular intervals
  EVERY_N_SECONDS(ELECTION_DELAY) { controllerElection(); } 
}

//////////////////////////////////////////////////////////////////////////////////////////////
// LED FUNCTIONS
//////////////////////////////////////////////////////////////////////////////////////////////

void setupLEDs() {
  // create an object called "leds" and set brightness
  FastLED.addLeds<LED_TYPE, DATA_PIN, GRB>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(BRIGHTNESS);
}

// random colored speckles that blink in and fade smoothly
void confetti() {
  // a nice fade effect when transitioning back from the connected/rainbow animation
  fadeToBlackBy(leds, NUM_LEDS, 10);
  int pos = random16(NUM_LEDS);
  leds[pos] += CHSV(aloneHue + random8(32), 200, 255);
}

void banana_mode() {
  fadeToBlackBy(leds, NUM_LEDS, 10);
  uint8_t starthue = 45;
  uint8_t endhue = 70;
  
  fill_gradient(leds, NUM_LEDS, CHSV(starthue,255,255), CHSV(endhue,255,255), FORWARD_HUES);    // If we don't have this, the colour fill will flip around. 
  addGlitter(AMOUNT_OF_GLITTER * 2);  
}

void addGlitter(fract8 chanceOfGlitter) {
  if (random8() < chanceOfGlitter) { 
    leds[random16(NUM_LEDS)] += CRGB::White; 
  }
}

void stepAnimation(int displayMode) {
  switch (displayMode) {
    // "confetti" effect, not part of a mesh, searching for connections
    case ALONE:
      // this gives the confetti animation a unique animation rate on each reboot
      EVERY_N_MILLISECONDS(animationDelay) { confetti(); }
      
      FastLED.show();
    break;

    // "rainbow" effect, you're connected!
    case CONNECTED:
      // another data dimension, but might be annoying.  Fades the brightness of the LEDs depending on the wifi signal strength.
      if (FADE_BY_DISTANCE && amController == false) {
        uint8_t newBrightness = BRIGHTNESS - (-1 * WiFi.RSSI());
              
        FastLED.setBrightness(newBrightness);
      }
      
      // if the "super controller" is in the network use the alternate animation, otherwise, FastLED's built-in rainbow generator
      if (knownControllerID == SUPER_CONTROLLER_ID) { 
        banana_mode(); 
      }
      else { 
        fill_rainbow(leds, NUM_LEDS, gHue, 255/NUM_LEDS*NUM_RAINBOWS); 
      }
      
      // the controller gets a bit of glitter for visual identification
      if (amController == true) { addGlitter(AMOUNT_OF_GLITTER); }
      
      FastLED.show();
    break;
  }
}

// Increments the base hue (gHue) to animate the rainbow effect
void shiftHue() { 
  if (gHue == 0) {
    // as the controller, announce when resetting base hue
    if (amController == true && mesh.getNodeList().size() > 0) {
      String msg = "KEYFRAME";
      sendMessage(&msg); 
    }
  }

  gHue++; // as a uint8_t type, value will 'roll over' from 255 back to 0
}

//////////////////////////////////////////////////////////////////////////////////////////////
// MESH FUNCTIONS
//////////////////////////////////////////////////////////////////////////////////////////////

void setupMesh() {
  Task taskSendMessage( TASK_SECOND*MESSAGE_DELAY, TASK_FOREVER, []() { String msg = String(displayMode); sendMessage(&msg); });

  // set before mesh init() so that you can see startup messages
  //mesh.setDebugMsgTypes(ERROR | MESH_STATUS | CONNECTION | SYNC | COMMUNICATION | GENERAL | MSG_TYPES | REMOTE); // all types on
  mesh.setDebugMsgTypes(ERROR | MESH_STATUS | STARTUP);

  mesh.init(MESH_SSID, MESH_PASSWORD, &userScheduler, MESH_PORT);
  mesh.onReceive(&receivedCallback);
  mesh.onNewConnection(&newConnectionCallback);
  mesh.onChangedConnections(&changedConnectionCallback);
  mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);

  userScheduler.addTask(taskSendMessage);  
  taskSendMessage.enable();
}

void updateMesh() {
  mesh.update();

  if (amController == true && mesh.getNodeList().size() > 0) {
    displayMode = CONNECTED;
  }

  // animation update
  stepAnimation(displayMode);
}

// human-readable output for wifi status
const char* wl_status_to_string(wl_status_t status) {
  switch (status) {
    case WL_NO_SHIELD: return "INITIALIZING... (or NO SHIELD)";
    case WL_IDLE_STATUS: return "IDLE";
    case WL_NO_SSID_AVAIL: return "NO SSID AVAIL";
    case WL_SCAN_COMPLETED: return "SCAN COMPLETED";
    case WL_CONNECTED: return "CONNECTED";
    case WL_CONNECT_FAILED: return "CONNECTION FAILED";
    case WL_CONNECTION_LOST: return "CONNECTION LOST";
    case WL_DISCONNECTED: return "DISCONNECTED";
    default: return "?? WIFI STATUS UNKNOWN ??";
  }
}

// human-readable output for wifi mode
const char* wifi_mode_to_string(wifi_mode_t mode) {
  switch (mode) {
    case 0: return "OFF (NULL MODE)";
    case 1: return "STATION";
    case 2: return "SOFT AP";
    case 3: return "STATION+AP";
    default: return "?? WIFI MODE UNKNOWN ??";
  }
}

void controllerElection() {
  uint32_t myNodeID = mesh.getNodeId();
  uint32_t lowestNodeID = myNodeID;
  bool badNodeDectected = false;
  
  SimpleList<uint32_t> nodes;
  nodes = mesh.getNodeList();

  Serial.printf("\n>> CONTROLLER ELECTION\n");
  Serial.printf(" . Number of nodes in mesh: %d\n", nodes.size() + 1);
  Serial.printf(" . Mesh members: %u (< this node)", myNodeID);
    
  for (SimpleList<uint32_t>::iterator node = nodes.begin(); node != nodes.end(); ++node) {
    Serial.printf(" %u", *node);
    
    // Sometimes an orphaned node (ID "0") shows up and throws off controller elections.  need to filter it from the list.  
    // Below code really needs to be tested...
    if (*node == 0) { 
      badNodeDectected = true;    
      nodes.remove(0);
    }
    else {
      if (*node < lowestNodeID) { lowestNodeID = *node; }
    }
  }

  Serial.println();

  // this is just for visibility.  The clean-up happens very fast when an orphaned node is detected.
  if (badNodeDectected) {
    Serial.printf("  --------------------------------------------------\n");
    Serial.printf("  !! NODE ID \"0\" DETECTED -- DELETING FROM MESH LIST\n");
    Serial.printf("  --------------------------------------------------\n");
  }

  Serial.printf(" . Election result: ");

  if (lowestNodeID == myNodeID) {
    Serial.printf("I am the controller (node id: %u)\n", myNodeID);
    amController = true;
  }
  else {
    Serial.printf("Node %u is the controller\n", lowestNodeID);
    amController = false;
  }

  // only act on keyframe messages from the known controller in the mesh
  knownControllerID = lowestNodeID;  
  
  String ipAddr = WiFi.localIP().toString();
  
  if (ipAddr == "0.0.0.0") {  // tried: WiFi.status() != WL_CONNECTED
    Serial.printf(" . NO IP ADDRESS - WiFi Mode: %s, Status: %s (IP: %s)\n", wifi_mode_to_string(WiFi.getMode()), wl_status_to_string(WiFi.status()), ipAddr.c_str());
  }  
  else {
    String signalHealth;

    // RSSI is the relative received signal strength in a wireless environment.  The higher the number, the stronger the signal.
    // these values may need to by tweaked but seems to be fairly accurate in my use, in terms of "good" vs. "bad".
    if (WiFi.RSSI() > -60) { signalHealth = "GREAT"; }
    if (WiFi.RSSI() <= -60 && WiFi.RSSI() > -70) { signalHealth = "GOOD"; }
    if (WiFi.RSSI() <= -70 && WiFi.RSSI() > -90) { signalHealth = "WEAK"; }
    if (WiFi.RSSI() <= -90 && WiFi.RSSI() > -100) { signalHealth = "BAD"; }
    if (WiFi.RSSI() <= -100) { signalHealth = "*VERY BAD*"; }
  
    Serial.printf(" . Wifi Mode: %s, Status: %s. Signal strength: %s, %ddBm. (IP: %s) ", wifi_mode_to_string(WiFi.getMode()), wl_status_to_string(WiFi.status()), signalHealth.c_str(), WiFi.RSSI(), ipAddr.c_str());

    // dim the LEDs as the signal starts to fade.  Can be turned off by setting FADE_BY_DISTANCE to false.  Doesn't apply to the elected controller.
    if (FADE_BY_DISTANCE && amController == false) {
      uint8_t newBrightness = BRIGHTNESS - (-1 * WiFi.RSSI());
      Serial.printf("(Fading brightness to %d).", newBrightness);
    }

    Serial.println();
  }

  Serial.println();
}

// send a broadcast message to all the nodes specifying the new animation mode for all of them
void sendMessage(String *msg) {
  String currentTime = String(mesh.getNodeTime());
  String json_msg;

  if (*msg == "KEYFRAME") {
    json_msg = "{\"msg\":\"KEYFRAME\",\"timestamp\":" + currentTime +"}";  
    Serial.printf(">> CONTROLLER KEYFRAME - broadcast message sent: %s\n", json_msg.c_str());
  }
  else {
    json_msg = "{\"msg\":" + String(displayMode) +",\"timestamp\":" + currentTime +"}";
  }
  
  mesh.sendBroadcast(json_msg);
}

// this gets called when the designated controller sends a command to start a new animation
// init any animation specific vars for the new mode, and reset the timer vars
void receivedCallback(uint32_t from, String &jsonString) {
  StaticJsonDocument<200> jsonDoc;

  DeserializationError jsonError = deserializeJson(jsonDoc, jsonString);
  if (jsonError) { Serial.printf("!! ERROR: deserializeJson() failed: %s", jsonError.c_str()); }
  
  String receivedMessage = jsonDoc["msg"];
  uint32_t timeStamp = jsonDoc["timestamp"];
  
  // this is a call from the controller to reset your global hue.  This gets all the rainbow animations synchronized.
  if (receivedMessage == "KEYFRAME" && from == knownControllerID) { 
    // time between sending and receiving a broadcast, in microseconds.  Rolls over every 71 minutes because uint32_t will overflow.
    uint32_t currentTime = mesh.getNodeTime();
    uint32_t messageAge = currentTime - timeStamp;

    Serial.printf(" > KEYFRAME from %u -- Timestamp: %zu, offset: %zu ms. Local gHue is %u. ", from, timeStamp, messageAge/1000, gHue);
    
    // message time in transit is within bounds
    if (messageAge < MAX_MESSAGE_AGE) {
      // when receiving a KEYFRAME message, only reset the global hue if it's out of sync
      if (255-gHue>12 && 255-gHue<243) { 
        // testing this out.  Instead of a slightly delayed "reset to zero" message, trying to calculate how far ahead the controller is by the time the message was received.
        uint32_t newHue = (messageAge/1000)/HUE_DELAY;
        
        if (gHue != newHue) { // don't bother setting a new value if they're already in sync
          gHue = newHue; 
          Serial.printf("(RESETTING gHue to %u.)", newHue);
        }
      }
    }
    else {
      // discard older messages.  Divide by 1,000 to convert microseconds to milliseconds.
      Serial.printf("(IGNORED: message is older than %zu ms.)", MAX_MESSAGE_AGE/1000); 
    }

  Serial.println();
  }

  else if (from == knownControllerID) {
      Serial.printf("Display update from %u.  Setting mode to %s.", from, receivedMessage.c_str());
      
      // get the new display mode
      displayMode = receivedMessage.toInt(); 
  }
}

void newConnectionCallback(uint32_t nodeId) {
    Serial.printf("\n>> NEW CONNECTION, nodeId = %u\n", nodeId);
}

// this gets called when a node is added or removed from the mesh, so set the controller to the node with the lowest chip id
void changedConnectionCallback() {
  Serial.printf("\n > CHANGED CONNECTIONS: %s\n", mesh.subConnectionJson().c_str());
 
  // calling an election when mesh configuration changes
  controllerElection();

  SimpleList<uint32_t> nodes = mesh.getNodeList();

  // if the node count is zero, go back to the "alone" animation
  if (nodes.size() > 0) {
    displayMode = CONNECTED;
  }
  else {
    displayMode = ALONE;
  }
}

void nodeTimeAdjustedCallback(int32_t offset) {
    Serial.printf(" + TIME: Adjusted time to %u, Offset was %d.\n", mesh.getNodeTime(), offset);
}

// sort the given list of nodes
void sortNodeList(SimpleList<uint32_t> &nodes) {
  SimpleList<uint32_t> nodes_sorted;
  SimpleList<uint32_t>::iterator smallest_node;

  // sort the node list
  while (nodes.size() > 0) {
    // find the smallest one
    smallest_node = nodes.begin();
    
    for (SimpleList<uint32_t>::iterator node = nodes.begin(); node != nodes.end(); ++node) { if (*node < *smallest_node) smallest_node = node; }

    // add it to the sorted list and remove it from the old list
    nodes_sorted.push_back(*smallest_node);
    nodes.erase(smallest_node);
  }
 
  // copy the sorted list back into the now empty nodes list
  for (SimpleList<uint32_t>::iterator node = nodes_sorted.begin(); node != nodes_sorted.end(); ++node) nodes.push_back(*node);
}