#include "EvacProtocol.h"

EvacProtocol::EvacProtocol(uint8_t satelliteId, RoutingTable& rt)
    : m_satelliteId(satelliteId), m_routingTable(rt) {
    memset(m_myMac, 0, 6);
    memset(m_broadcastMac, 0, 6);
}

void EvacProtocol::init(const uint8_t* myMac, const uint8_t* broadcastMac) {
    memcpy(m_myMac, myMac, 6);
    memcpy(m_broadcastMac, broadcastMac, 6);
}

void EvacProtocol::handleEvacAdvert(const uint8_t* senderMac, const MeshPacket& pkt) {
    if (m_routingTable.updateEvacPotential(pkt.id, pkt.evacPotential, pkt.evacNextHopId)) {
        Serial.printf("[Evac Recv] Nhận thế năng thoát hiểm từ Node %d: U = %.1f (sender=%02X:%02X)\n", 
                      pkt.id, pkt.evacPotential, senderMac[0], senderMac[1]);
    }
}

void EvacProtocol::broadcastPotential(float potential) {
    MeshPacket packet = {};
    packet.packetType = PACKET_EVAC_ADVERT;
    memcpy(packet.sourceMac, m_myMac, 6);
    memcpy(packet.destMac, m_broadcastMac, 6);
    memset(packet.forwardMac, 0, 6);
    packet.id = m_satelliteId;
    packet.evacPotential = potential;
    packet.evacNextHopId = m_routingTable.getEvacNextHopId();

    // 1. Gửi broadcast thông thường (tầm phủ ngắn, không đảm bảo)
    esp_now_send(m_broadcastMac, (uint8_t *)&packet, sizeof(packet));

    // 2. Gửi unicast cho từng láng giềng vật lý đã biết MAC (đáng tin cậy hơn)
    const PhysicalNeighbor* neighbors = m_routingTable.getPhysicalNeighbors();
    uint8_t count = m_routingTable.getPhysCount();
    for (uint8_t i = 0; i < count; i++) {
        if (neighbors[i].active && neighbors[i].id != 0) {
            // Kiểm tra MAC khác 00:00:00:00:00:00
            bool hasValidMac = false;
            for (int j = 0; j < 6; j++) {
                if (neighbors[i].mac[j] != 0) { hasValidMac = true; break; }
            }
            if (hasValidMac) {
                esp_now_send(neighbors[i].mac, (uint8_t *)&packet, sizeof(packet));
            }
        }
    }
}
