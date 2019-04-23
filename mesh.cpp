#include "mesh.h"
#include "led.h"

// Mesh setup
#define   MESH_PREFIX     "LEDMesh01"
#define   MESH_PASSWORD   "foofoofoo"
#define   MESH_PORT       5555

// Mesh states
#define ALONE     1
#define CONNECTED 2

// Global vars
int display_mode = ALONE;   // animation type -- init animation as single node
bool amController = false;  // flag to designate that this node is the current controller

painlessMesh mesh;

void setupMesh() {
  display_mode = ALONE;
  Task taskSendMessage( TASK_SECOND * 1 , TASK_FOREVER, &sendMessage );

  //mesh.setDebugMsgTypes( ERROR | MESH_STATUS | CONNECTION | SYNC | COMMUNICATION | GENERAL | MSG_TYPES | REMOTE ); // all types on
  mesh.setDebugMsgTypes( ERROR | STARTUP );  // set before mesh init() so that you can see startup messages

  mesh.init(MESH_PREFIX, MESH_PASSWORD, MESH_PORT);
  mesh.onReceive(&receivedCallback);
  mesh.onNewConnection(&newConnectionCallback);
  mesh.onChangedConnections(&changedConnectionCallback);
  mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);

  mesh.scheduler.addTask(taskSendMessage);
  taskSendMessage.enable();

  // may be able to get rid of this...
  // make this one the controller if there are no others on the mesh
  //if (mesh.getNodeList().size() == 0) { amController = true; }
}

void updateMesh() {
  mesh.update();

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
      // need clock sync data to be sent here?  See nodeTimeAdjustedCallback method.
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
