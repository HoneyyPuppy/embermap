#include "MeshRouter.h"

MeshRouter::MeshRouter(uint8_t satelliteId, RoutingTable& rt)
    : m_satelliteId(satelliteId), m_routingTable(rt) {
    memset(m_myMac, 0, 6);
    memset(m_broadcastMac, 0, 6);
}

void MeshRouter::init(const uint8_t* myMac, const uint8_t* broadcastMac) {
    memcpy(m_myMac, myMac, 6);
    memcpy(m_broadcastMac, broadcastMac, 6);
}

void MeshRouter::handleRouteUpdate(const esp_now_recv_info_t* info, const MeshPacket& pkt) {
    const uint8_t* senderMac = info->src_addr;
    if (!m_routingTable.isAllowedNeighbor(pkt.id)) {
        return;
    }
    if (pathContainsNode(pkt.routePath, pkt.routePathLen, m_satelliteId)) {
        Serial.println("[Mesh Warning] Phát hiện lặp vòng định tuyến (Loop)! Bỏ qua gói cập nhật.");
        return;
    }

    int rssi = info->rx_ctrl->rssi;
    float linkEtx = m_routingTable.getLinkEtx(senderMac);
    float newCost = pkt.routingCost + linkEtx;

    m_routingTable.updateCandidate(senderMac, newCost, pkt.routePath, pkt.routePathLen);
    
    Serial.printf("[Mesh Table] Cập nhật Parent Candidate: MAC=%02X:%02X... | Cost=%.1f (RSSI=%d dBm, ETX=%.1f)\n",
                  senderMac[0], senderMac[1], newCost, rssi, linkEtx);

    Serial.println("[Mesh Table] Danh sách Parent Candidates hiện tại:");
    uint8_t parentCount = m_routingTable.getParentCount();
    const ParentCandidate* candidates = m_routingTable.getCandidates();
    for (int i = 0; i < parentCount; i++) {
        Serial.printf("  [%d] MAC: %02X:%02X... | Cost: %.1f%s\n", 
                      i, candidates[i].mac[0], candidates[i].mac[1], candidates[i].cost,
                      i == 0 ? " (Primary)" : " (Backup)");
    }
    
    Serial.print("[Mesh RoutePath] Tuyến đường: ");
    for (uint8_t i = 0; i < m_routingTable.getParentRoutePathLen(); i++) {
        Serial.printf("%d -> ", m_routingTable.getParentRoutePath()[i]);
    }
    Serial.printf("%d (Bản thân)\n", m_satelliteId);
}

void MeshRouter::handleSensorForward(const MeshPacket& pkt) {
    if (memcmp(pkt.forwardMac, m_myMac, 6) == 0) {
        if (m_routingTable.hasRoute()) {
            MeshPacket forwardPacket = pkt;
            memcpy(forwardPacket.forwardMac, m_routingTable.getNextHopMac(), 6);
            esp_now_send(m_routingTable.getNextHopMac(), (uint8_t *)&forwardPacket, sizeof(forwardPacket));
            Serial.printf("[Mesh Forward] Chuyển tiếp gói tin của Node %d lên Next Hop: %02X:%02X...\n", 
                          forwardPacket.id, m_routingTable.getNextHopMac()[0], m_routingTable.getNextHopMac()[1]);
        } else {
            Serial.println("[Mesh Warning] Nhận gói trung chuyển nhưng không có tuyến đi!");
        }
    }
}

void MeshRouter::handleRouteRequest() {
    if (m_routingTable.hasRoute()) {
        Serial.println("[Mesh] Nhận yêu cầu quét kênh (Route Request) -> Phát lại phản hồi tuyến.");
        rebroadcastRouteUpdate();
    }
}

void MeshRouter::rebroadcastRouteUpdate() {
    MeshPacket packet = {};
    packet.packetType = PACKET_ROUTE_UPDATE;
    memcpy(packet.sourceMac, m_myMac, 6);
    memcpy(packet.destMac, m_broadcastMac, 6);
    memset(packet.forwardMac, 0, 6);
    packet.id = m_satelliteId;
    packet.routingCost = m_routingTable.getCost();

    memcpy(packet.routePath, m_routingTable.getParentRoutePath(), m_routingTable.getParentRoutePathLen());
    packet.routePathLen = m_routingTable.getParentRoutePathLen();
    if (packet.routePathLen < MAX_ROUTE_PATH) {
        packet.routePath[packet.routePathLen] = m_satelliteId;
        packet.routePathLen++;
    }

    esp_err_t result = esp_now_send(m_broadcastMac, (uint8_t *)&packet, sizeof(packet));
    Serial.printf("[Mesh] Quảng bá lại tuyến (Cost = %.1f) -> %s\n", m_routingTable.getCost(), result == ESP_OK ? "OK" : "FAIL");
}

void MeshRouter::sendRouteRequest(uint8_t channel) {
    MeshPacket req = {};
    req.packetType = PACKET_ROUTE_REQUEST;
    memcpy(req.sourceMac, m_myMac, 6);
    req.id = m_satelliteId;
    
    esp_now_send(m_broadcastMac, (uint8_t *)&req, sizeof(req));
    Serial.printf("[Scan] Đang dò Kênh %d... Phát Route Request.\n", channel);
}
