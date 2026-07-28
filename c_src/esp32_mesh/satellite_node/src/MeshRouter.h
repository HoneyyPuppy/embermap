#ifndef MESH_ROUTER_H
#define MESH_ROUTER_H

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <mesh_packet.h>
#include "RoutingTable.h"

class MeshRouter {
public:
    MeshRouter(uint8_t satelliteId, RoutingTable& rt);
    void init(const uint8_t* myMac, const uint8_t* broadcastMac);
    
    void handleRouteUpdate(const esp_now_recv_info_t* info, const MeshPacket& pkt);
    void handleSensorForward(const MeshPacket& pkt);
    void handleRouteRequest();
    
    void rebroadcastRouteUpdate();
    void sendRouteRequest(uint8_t channel);

private:
    uint8_t m_satelliteId;
    RoutingTable& m_routingTable;
    uint8_t m_myMac[6];
    uint8_t m_broadcastMac[6];
};

#endif
