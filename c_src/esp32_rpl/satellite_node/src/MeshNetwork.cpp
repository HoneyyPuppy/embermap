#include "MeshNetwork.h"

MeshNetwork* MeshNetwork::s_instance = nullptr;

MeshNetwork::MeshNetwork(uint8_t satelliteId, RoutingTable& routingTable)
    : m_satelliteId(satelliteId), m_routingTable(routingTable) {
    s_instance = this;
    memset(m_broadcastMac, 0xFF, 6);
    m_pendingSend.active = false;
}

bool MeshNetwork::init() {
    WiFi.mode(WIFI_STA);
    esp_wifi_get_mac(WIFI_IF_STA, m_myMac);
    
    if (esp_now_init() != ESP_OK) {
        return false;
    }
    
    esp_now_register_recv_cb(onRecvStatic);
    esp_now_register_send_cb(onSentStatic);

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, m_broadcastMac, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    peerInfo.ifidx = WIFI_IF_STA;
    
    return (esp_now_add_peer(&peerInfo) == ESP_OK);
}

void MeshNetwork::sendSensorData(float temp, int gas, bool emergency) {
    if (!m_routingTable.hasParent() || m_routingTable.getParentCount() == 0) {
        Serial.println("[RPL Warning] Không thể gửi cảm biến: Chưa gia nhập cây DODAG.");
        return;
    }

    MeshPacket packet = {};
    packet.packetType = PACKET_SENSOR_DATA;
    memcpy(packet.sourceMac, m_myMac, 6);
    memset(packet.destMac, 0, 6);
    packet.id = m_satelliteId;
    packet.temp = temp;
    packet.gasRaw = gas;
    packet.emergency = emergency;
    packet.rank = m_routingTable.getRank();
    packet.version = m_routingTable.getDodagVersion();

    m_pendingSend.packet = packet;
    m_pendingSend.currentParentIdx = 0;
    m_pendingSend.active = true;

    updateAndSendToParent(0);
}

void MeshNetwork::updateAndSendToParent(uint8_t idx) {
    uint8_t parentCount = m_routingTable.getParentCount();
    if (idx >= parentCount) {
        Serial.println("[Failover Warning] Đã thử qua tất cả các Parent nhưng đều thất bại!");
        m_pendingSend.active = false;
        return;
    }
    
    const ParentCandidate* candidates = m_routingTable.getCandidates();
    const uint8_t *targetMac = candidates[idx].mac;
    memcpy(m_pendingSend.packet.forwardMac, targetMac, 6);
    
    if (!esp_now_is_peer_exist(targetMac)) {
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, targetMac, 6);
        peerInfo.channel = 0;
        peerInfo.encrypt = false;
        peerInfo.ifidx = WIFI_IF_STA;
        esp_now_add_peer(&peerInfo);
    }
    
    esp_err_t result = esp_now_send(targetMac, (uint8_t *)&m_pendingSend.packet, sizeof(m_pendingSend.packet));
    if (idx == 0) {
        Serial.printf("[RPL Send] Gửi dữ liệu cảm biến tới Primary Parent: %02X:%02X... | Result: %s\n", 
                      targetMac[0], targetMac[1], result == ESP_OK ? "Queued" : "Fail");
    } else {
        Serial.printf("[Backup Send] Gửi lại dữ liệu tới Backup Parent [%d]: %02X:%02X... | Result: %s\n", 
                      idx, targetMac[0], targetMac[1], result == ESP_OK ? "Queued" : "Fail");
    }
}

void MeshNetwork::sendDao() {
    if (!m_routingTable.hasParent()) return;

    MeshPacket packet = {};
    packet.packetType = PACKET_RPL_DAO;
    memcpy(packet.sourceMac, m_myMac, 6);
    memset(packet.destMac, 0, 6);
    memcpy(packet.forwardMac, m_routingTable.getParentMac(), 6);
    packet.id = m_satelliteId;
    packet.temp = 0.0;
    packet.gasRaw = 0;
    packet.emergency = false;
    packet.rank = m_routingTable.getRank();
    packet.version = m_routingTable.getDodagVersion();

    esp_err_t result = esp_now_send(m_routingTable.getParentMac(), (uint8_t *)&packet, sizeof(packet));
    Serial.printf("[RPL] Gửi gói tin DAO lên Parent -> %s\n", result == ESP_OK ? "OK" : "FAIL");
}

void MeshNetwork::broadcastDio() {
    if (!m_routingTable.hasParent()) return;

    MeshPacket packet = {};
    packet.packetType = PACKET_RPL_DIO;
    memcpy(packet.sourceMac, m_myMac, 6);
    memcpy(packet.destMac, m_broadcastMac, 6);
    memset(packet.forwardMac, 0, 6);
    packet.id = m_satelliteId;
    packet.temp = 0.0;
    packet.gasRaw = 0;
    packet.emergency = false;
    packet.rank = m_routingTable.getRank();
    packet.version = m_routingTable.getDodagVersion();

    memcpy(packet.routePath, m_routingTable.getParentRoutePath(), m_routingTable.getParentRoutePathLen());
    packet.routePathLen = m_routingTable.getParentRoutePathLen();
    if (packet.routePathLen < MAX_ROUTE_PATH) {
        packet.routePath[packet.routePathLen] = m_satelliteId;
        packet.routePathLen++;
    }

    esp_err_t result = esp_now_send(m_broadcastMac, (uint8_t *)&packet, sizeof(packet));
    Serial.printf("[RPL] Phát tiếp DIO (Rank = %d, Ver = %d) -> %s\n", 
                  m_routingTable.getRank(), m_routingTable.getDodagVersion(), result == ESP_OK ? "OK" : "FAIL");
}

void MeshNetwork::sendRouteRequest(uint8_t channel) {
    MeshPacket req = {};
    req.packetType = PACKET_ROUTE_REQUEST;
    memcpy(req.sourceMac, m_myMac, 6);
    req.id = m_satelliteId;
    
    esp_now_send(m_broadcastMac, (uint8_t *)&req, sizeof(req));
    Serial.printf("[Scan] Đang dò Kênh %d... Phát Route Request.\n", channel);
}

void MeshNetwork::onRecvStatic(const esp_now_recv_info_t *recv_info, const uint8_t *incomingDataRaw, int len) {
    if (s_instance && len >= sizeof(MeshPacket)) {
        MeshPacket packet;
        memcpy(&packet, incomingDataRaw, sizeof(packet));
        s_instance->handleRecv(recv_info, packet);
    }
}

void MeshNetwork::onSentStatic(const esp_now_send_info_t *tx_info, esp_now_send_status_t status) {
    if (s_instance) {
        s_instance->handleSent(tx_info, status);
    }
}

void MeshNetwork::handleRecv(const esp_now_recv_info_t *recv_info, const MeshPacket& packet) {
    const uint8_t* senderMac = recv_info->src_addr;

    if (packet.packetType == PACKET_RPL_DIO) {
        if (!m_routingTable.isAllowedNeighbor(packet.id)) {
            return;
        }
        if (pathContainsNode(packet.routePath, packet.routePathLen, m_satelliteId)) {
            Serial.println("[RPL Warning] Phát hiện lặp vòng định tuyến (Loop)! Bỏ qua gói cập nhật.");
            return;
        }

        uint16_t senderRank = packet.rank;
        int rssi = recv_info->rx_ctrl->rssi;
        float linkCost = calculateLinkCost(rssi);
        uint16_t rankIncrease = (uint16_t)(linkCost * 256.0);
        uint16_t calculatedRank = senderRank + rankIncrease;

        Serial.printf("[RPL RX] Nhận DIO từ MAC: %02X:%02X... | Sender Rank: %d | Rank tính toán: %d (RSSI: %d dBm, LinkCost: %.1f)\n",
                      senderMac[0], senderMac[1], senderRank, calculatedRank, rssi, linkCost);

        m_routingTable.updateCandidate(senderMac, calculatedRank, packet.version, packet.routePath, packet.routePathLen);
        
        Serial.printf("[RPL Update] Cập nhật Parent Candidate: MAC=%02X:%02X... | Rank=%d (RSSI=%d dBm, LinkCost=%.1f)\n", 
                      senderMac[0], senderMac[1], calculatedRank, rssi, linkCost);

        Serial.println("[RPL Table] Danh sách Parent Candidates hiện tại:");
        uint8_t parentCount = m_routingTable.getParentCount();
        const ParentCandidate* candidates = m_routingTable.getCandidates();
        for (int i = 0; i < parentCount; i++) {
            Serial.printf("  [%d] MAC: %02X:%02X... | Rank: %d%s\n", 
                          i, candidates[i].mac[0], candidates[i].mac[1], candidates[i].rank,
                          i == 0 ? " (Primary)" : " (Backup)");
        }

        Serial.print("[RPL RoutePath] Tuyến đường: ");
        for (uint8_t i = 0; i < m_routingTable.getParentRoutePathLen(); i++) {
            Serial.printf("%d -> ", m_routingTable.getParentRoutePath()[i]);
        }
        Serial.printf("%d (Bản thân)\n", m_satelliteId);
    }
    else if (packet.packetType == PACKET_SENSOR_DATA) {
        if (memcmp(packet.forwardMac, m_myMac, 6) == 0) {
            if (m_routingTable.hasParent()) {
                MeshPacket forwardPacket = packet;
                memcpy(forwardPacket.forwardMac, m_routingTable.getParentMac(), 6);
                esp_now_send(m_routingTable.getParentMac(), (uint8_t *)&forwardPacket, sizeof(forwardPacket));
                Serial.printf("[RPL Forward] Chuyển tiếp gói tin của Node %d lên Parent: %02X:%02X...\n", 
                              forwardPacket.id, m_routingTable.getParentMac()[0], m_routingTable.getParentMac()[1]);
            } else {
                Serial.println("[RPL Warning] Không thể chuyển tiếp: Chưa có Parent!");
            }
        }
    }
    else if (packet.packetType == PACKET_ROUTE_REQUEST) {
        if (m_routingTable.hasParent()) {
            Serial.println("[RPL] Nhận yêu cầu quét kênh (Route Request) -> Phát lại phản hồi DIO.");
            broadcastDio();
        }
    }
}

void MeshNetwork::handleSent(const esp_now_send_info_t *tx_info, esp_now_send_status_t status) {
    if (m_pendingSend.active && memcmp(tx_info->des_addr, m_pendingSend.packet.forwardMac, 6) == 0) {
        if (status == ESP_NOW_SEND_SUCCESS) {
            Serial.printf("[RPL OK] Gửi thành công tới Parent [%d]: %02X:%02X!\n", 
                          m_pendingSend.currentParentIdx, tx_info->des_addr[0], tx_info->des_addr[1]);
            m_pendingSend.active = false;
            sendDao();
        } else {
            Serial.printf("[RPL FAIL] Gửi tới Parent [%d]: %02X:%02X thất bại! Kích hoạt failover...\n", 
                          m_pendingSend.currentParentIdx, tx_info->des_addr[0], tx_info->des_addr[1]);
            
            uint8_t failedIdx = m_pendingSend.currentParentIdx;
            if (failedIdx < m_routingTable.getParentCount()) {
                const ParentCandidate* candidates = m_routingTable.getCandidates();
                Serial.printf("[RPL FAIL] Loại bỏ Parent lỗi: %02X:%02X ra khỏi danh sách.\n", 
                              candidates[failedIdx].mac[0], candidates[failedIdx].mac[1]);
                m_routingTable.removeCandidateAtIndex(failedIdx);
            }
            
            updateAndSendToParent(m_pendingSend.currentParentIdx);
        }
    }
}
