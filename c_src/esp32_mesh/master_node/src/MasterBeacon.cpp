#include "MasterBeacon.h"

MasterBeacon::MasterBeacon() {
    memset(m_myMac, 0, 6);
    memset(m_broadcastMac, 0, 6);
}

void MasterBeacon::init(const uint8_t* myMac, const uint8_t* broadcastMac) {
    if (myMac) memcpy(m_myMac, myMac, 6);
    if (broadcastMac) memcpy(m_broadcastMac, broadcastMac, 6);
}

void MasterBeacon::broadcastRouteUpdate() {
    MeshPacket packet = {};
    packet.packetType = PACKET_ROUTE_UPDATE;
    memcpy(packet.sourceMac, m_myMac, 6);
    memcpy(packet.destMac, m_myMac, 6);
    memset(packet.forwardMac, 0, 6);
    packet.id = 0; // Master Node ID = 0
    packet.temp = 0.0;
    packet.gasRaw = 0;
    packet.emergency = false;
    packet.routingCost = 0.0;

    // Thiết lập routePath bắt đầu từ Master Node (ID = 0)
    packet.routePath[0] = 0;
    packet.routePathLen = 1;

    esp_err_t result = esp_now_send(m_broadcastMac, (uint8_t *)&packet, sizeof(packet));
    Serial.printf("[Mesh] Rebroadcast routing (Cost = 0.0) -> %s\n", result == ESP_OK ? "SUCCESS" : "FAIL");
}

void MasterBeacon::broadcastEvacPotential(float potential) {
    MeshPacket packet = {};
    packet.packetType = PACKET_EVAC_ADVERT;
    memcpy(packet.sourceMac, m_myMac, 6);
    memcpy(packet.destMac, m_broadcastMac, 6);
    memset(packet.forwardMac, 0, 6);
    packet.id = 0; // Master ID = 0
    packet.evacPotential = potential;
    packet.evacNextHopId = (potential >= 9999.0) ? 0xFF : 0; // Master bị chặn nếu U >= 9999.0

    esp_now_send(m_broadcastMac, (uint8_t *)&packet, sizeof(packet));
}
