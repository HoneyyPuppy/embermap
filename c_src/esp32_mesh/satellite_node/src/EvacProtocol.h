#ifndef EVAC_PROTOCOL_H
#define EVAC_PROTOCOL_H

#include <Arduino.h>
#include <esp_now.h>
#include <mesh_packet.h>
#include "RoutingTable.h"

class EvacProtocol {
public:
    EvacProtocol(uint8_t satelliteId, RoutingTable& rt);
    void init(const uint8_t* myMac, const uint8_t* broadcastMac);
    
    void handleEvacAdvert(const uint8_t* senderMac, const MeshPacket& pkt);
    void broadcastPotential(float potential);

private:
    uint8_t m_satelliteId;
    RoutingTable& m_routingTable;
    uint8_t m_myMac[6];
    uint8_t m_broadcastMac[6];
};

#endif
