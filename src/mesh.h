#include <painlessMesh.h>

// Prototypes
void receivedCallback( uint32_t from, String &msg );
void newConnectionCallback(uint32_t nodeId);
void changedConnectionCallback();
void nodeTimeAdjustedCallback(int32_t offset);
void sortNodeList(SimpleList<uint32_t> &nodes);
void sendMessage();
void setupMesh();
void updateMesh();
