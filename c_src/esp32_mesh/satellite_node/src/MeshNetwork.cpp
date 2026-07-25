#include "MeshNetwork.h"
#include <Update.h>

MeshNetwork* MeshNetwork::s_instance = nullptr;

MeshNetwork::MeshNetwork(uint8_t satelliteId, RoutingTable& routingTable)
    : m_satelliteId(satelliteId), m_routingTable(routingTable) {
    s_instance = this;
    memset(m_broadcastMac, 0xFF, 6);
    m_pendingSend.active = false;

    // Khởi tạo trạng thái bộ thu OTA
    m_otaState.active = false;
    m_otaState.fileSize = 0;
    m_otaState.totalChunks = 0;
    m_otaState.expectedSeq = 0;
    m_otaState.lastPacketTime = 0;
    memset(m_otaState.masterMac, 0, 6);
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
    if (!m_routingTable.hasRoute() || m_routingTable.getParentCount() == 0) {
        Serial.println("[Mesh Warning] Đang ngắt kết nối (mất định tuyến), không gửi được dữ liệu.");
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
    packet.routingCost = m_routingTable.getCost();

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
        Serial.printf("[Mesh Send] Gửi dữ liệu cảm biến tới Primary Parent: %02X:%02X... | Result: %s\n", 
                      targetMac[0], targetMac[1], result == ESP_OK ? "Queued" : "Fail");
    } else {
        Serial.printf("[Backup Send] Gửi lại dữ liệu tới Backup Parent [%d]: %02X:%02X... | Result: %s\n", 
                      idx, targetMac[0], targetMac[1], result == ESP_OK ? "Queued" : "Fail");
    }
}

void MeshNetwork::rebroadcastRouteUpdate() {
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

    // Cập nhật địa chỉ MAC của láng giềng vật lý phục vụ chỉ đường thoát hiểm
    m_routingTable.updatePhysicalNeighborMac(packet.id, senderMac);

    if (packet.packetType == PACKET_ROUTE_UPDATE) {
        if (!m_routingTable.isAllowedNeighbor(packet.id)) {
            return;
        }
        if (pathContainsNode(packet.routePath, packet.routePathLen, m_satelliteId)) {
            Serial.println("[Mesh Warning] Phát hiện lặp vòng định tuyến (Loop)! Bỏ qua gói cập nhật.");
            return;
        }

        int rssi = recv_info->rx_ctrl->rssi;
        float linkCost = calculateLinkCost(rssi);
        float newCost = packet.routingCost + linkCost;

        m_routingTable.updateCandidate(senderMac, newCost, packet.routePath, packet.routePathLen);
        
        Serial.printf("[Mesh Table] Cập nhật Parent Candidate: MAC=%02X:%02X... | Cost=%.1f (RSSI=%d dBm, LinkCost=%.1f)\n",
                      senderMac[0], senderMac[1], newCost, rssi, linkCost);

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
    else if (packet.packetType == PACKET_SENSOR_DATA) {
        if (memcmp(packet.forwardMac, m_myMac, 6) == 0) {
            if (m_routingTable.hasRoute()) {
                MeshPacket forwardPacket = packet;
                memcpy(forwardPacket.forwardMac, m_routingTable.getNextHopMac(), 6);
                esp_now_send(m_routingTable.getNextHopMac(), (uint8_t *)&forwardPacket, sizeof(forwardPacket));
                Serial.printf("[Mesh Forward] Chuyển tiếp gói tin của Node %d lên Next Hop: %02X:%02X...\n", 
                              forwardPacket.id, m_routingTable.getNextHopMac()[0], m_routingTable.getNextHopMac()[1]);
            } else {
                Serial.println("[Mesh Warning] Nhận gói trung chuyển nhưng không có tuyến đi!");
            }
        }
    }
    else if (packet.packetType == PACKET_ROUTE_REQUEST) {
        if (m_routingTable.hasRoute()) {
            Serial.println("[Mesh] Nhận yêu cầu quét kênh (Route Request) -> Phát lại phản hồi tuyến.");
            rebroadcastRouteUpdate();
        }
    }
    else if (packet.packetType == PACKET_EVAC_ADVERT) {
        m_routingTable.updateEvacPotential(packet.id, packet.evacPotential);
        Serial.printf("[Evac Recv] Nhận thế năng thoát hiểm từ Node %d: U = %.1f (sender=%02X:%02X)\n", 
                      packet.id, packet.evacPotential, senderMac[0], senderMac[1]);
    }
    else if (packet.packetType == PACKET_OTA_START) {
        Serial.printf("[OTA Recv] Nhận gói bắt đầu OTA. File size: %u, MD5: %s\n", 
                      packet.ota.start.otaFileSize, packet.ota.start.otaMd5);
        
        // Đảm bảo peer của người gửi (Master) đã được đăng ký
        if (!esp_now_is_peer_exist(senderMac)) {
            esp_now_peer_info_t peerInfo = {};
            memcpy(peerInfo.peer_addr, senderMac, 6);
            peerInfo.channel = 0;
            peerInfo.encrypt = false;
            esp_now_add_peer(&peerInfo);
        }
        
        m_otaState.active = true;
        m_otaState.fileSize = packet.ota.start.otaFileSize;
        m_otaState.totalChunks = (m_otaState.fileSize + 179) / 180;
        m_otaState.expectedSeq = 0;
        m_otaState.lastPacketTime = millis();
        memcpy(m_otaState.masterMac, senderMac, 6);
        
        Update.setMD5(packet.ota.start.otaMd5);
        if (!Update.begin(m_otaState.fileSize, U_FLASH)) {
            Serial.print("[OTA Recv Error] Update.begin failed: ");
            Update.printError(Serial);
            sendOtaAck(0xFFFF); // Báo lỗi
        } else {
            Serial.println("[OTA Recv] Update.begin thành công. Đang đợi chunks...");
            sendOtaAck(0xFFFF); // Báo sẵn sàng nhận
        }
    }
    else if (packet.packetType == PACKET_OTA_CHUNK) {
        if (!m_otaState.active) return;
        
        uint16_t seq = packet.ota.chunk.chunkSeq;
        uint8_t len = packet.ota.chunk.chunkLen;
        
        if (seq == m_otaState.expectedSeq) {
            size_t written = Update.write((uint8_t*)packet.ota.chunk.chunkData, len);
            if (written == len) {
                m_otaState.expectedSeq++;
                m_otaState.lastPacketTime = millis();
                
                // Gửi ACK mỗi 20 gói hoặc khi nhận gói cuối cùng của ứng dụng
                if (m_otaState.expectedSeq % 20 == 0 || m_otaState.expectedSeq == m_otaState.totalChunks) {
                    sendOtaAck(m_otaState.expectedSeq - 1);
                }
            } else {
                Serial.printf("[OTA Recv Error] Ghi mảnh %d thất bại!\n", seq);
                sendOtaAck(m_otaState.expectedSeq - 1);
            }
        } 
        else if (seq < m_otaState.expectedSeq) {
            // Gói tin trùng lặp, phản hồi ACK ngay lập tức
            sendOtaAck(m_otaState.expectedSeq - 1);
        } 
        else {
            // Mất gói! Phản hồi ACK mảnh cuối cùng thành công để yêu cầu gửi lại
            Serial.printf("[OTA Recv Warning] Nhận lệch mảnh! Nhận: %d, Đợi: %d\n", seq, m_otaState.expectedSeq);
            sendOtaAck(m_otaState.expectedSeq - 1);
        }
    }
    else if (packet.packetType == PACKET_OTA_END) {
        if (!m_otaState.active) return;
        
        Serial.println("[OTA Recv] Nhận lệnh kết thúc. Đang xác thực MD5...");
        if (Update.end(true)) {
            Serial.println("[OTA Recv SUCCESS] Nâng cấp thành công! Đang khởi động lại...");
            sendOtaAck(0xFFFE); // Gửi ACK thành công
            delay(1000);
            ESP.restart();
        } else {
            Serial.print("[OTA Recv FAIL] Xác thực MD5 thất bại: ");
            Update.printError(Serial);
            sendOtaAck(0xFFFD); // Gửi ACK lỗi xác thực
            m_otaState.active = false;
        }
    }
}

void MeshNetwork::handleSent(const esp_now_send_info_t *tx_info, esp_now_send_status_t status) {
    if (m_pendingSend.active && memcmp(tx_info->des_addr, m_pendingSend.packet.forwardMac, 6) == 0) {
        if (status == ESP_NOW_SEND_SUCCESS) {
            Serial.printf("[Mesh OK] Gửi thành công tới Parent [%d]: %02X:%02X!\n", 
                          m_pendingSend.currentParentIdx, tx_info->des_addr[0], tx_info->des_addr[1]);
            m_pendingSend.active = false;
        } else {
            Serial.printf("[Mesh FAIL] Gửi tới Parent [%d]: %02X:%02X thất bại! Kích hoạt failover...\n", 
                          m_pendingSend.currentParentIdx, tx_info->des_addr[0], tx_info->des_addr[1]);
            
            uint8_t failedIdx = m_pendingSend.currentParentIdx;
            if (failedIdx < m_routingTable.getParentCount()) {
                const ParentCandidate* candidates = m_routingTable.getCandidates();
                Serial.printf("[Mesh FAIL] Loại bỏ Parent lỗi: %02X:%02X ra khỏi danh sách.\n", 
                               candidates[failedIdx].mac[0], candidates[failedIdx].mac[1]);
                m_routingTable.removeCandidateAtIndex(failedIdx);
            }
            
            updateAndSendToParent(m_pendingSend.currentParentIdx);
        }
    }
}

void MeshNetwork::broadcastEvacPotential(float potential) {
    MeshPacket packet = {};
    packet.packetType = PACKET_EVAC_ADVERT;
    memcpy(packet.sourceMac, m_myMac, 6);
    memcpy(packet.destMac, m_broadcastMac, 6);
    memset(packet.forwardMac, 0, 6);
    packet.id = m_satelliteId;
    packet.evacPotential = potential;

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

void MeshNetwork::sendOtaAck(uint16_t lastReceivedSeq) {
    MeshPacket packet = {};
    packet.packetType = PACKET_OTA_ACK;
    memcpy(packet.sourceMac, m_myMac, 6);
    memcpy(packet.destMac, m_otaState.masterMac, 6);
    packet.id = m_satelliteId;
    packet.ota.ack.lastReceivedSeq = lastReceivedSeq;
    
    esp_now_send(m_otaState.masterMac, (uint8_t *)&packet, sizeof(packet));
}
