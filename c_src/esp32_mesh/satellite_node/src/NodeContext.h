#ifndef NODE_CONTEXT_H
#define NODE_CONTEXT_H
#include <Arduino.h>
#include "RoutingTable.h"
class MeshNetwork; // forward declaration
extern uint8_t satelliteId;
extern RoutingTable routingTable;
extern MeshNetwork meshNetwork;
extern SemaphoreHandle_t dataMutex;
extern float currentTemp;
extern int currentGas;
extern bool isEmergency;
extern bool sharedHasRoute;
extern float sharedCost;
extern uint8_t sharedNextHop[6];
extern bool sharedHasEvacRoute;
extern float sharedEvacPotential;
extern uint8_t sharedEvacNextHopId;
extern uint8_t sharedEvacNextHopMac[6];
#endif
