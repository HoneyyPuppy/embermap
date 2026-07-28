#include "MeshNetwork.h"

MeshNetwork* MeshNetwork::s_instance = nullptr;

MeshNetwork::MeshNetwork(uint8_t satelliteId, RoutingTable& routingTable)
    : m_satelliteId(satelliteId), m_routingTable(routingTable),
      m_router(satelliteId, routingTable), m_evacProtocol(satelliteId, routingTable),
      m_otaReceiver(satelliteId) {
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
    
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        return false;
    }

    m_router.init(m_myMac, m_broadcastMac);
    m_evacProtocol.init(m_myMac, m_broadcastMac);
    m_otaReceiver.init(m_myMac);

    return true;
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

    switch (packet.packetType) {
        case PACKET_ROUTE_UPDATE:
            m_router.handleRouteUpdate(recv_info, packet);
            break;
        case PACKET_SENSOR_DATA:
            m_router.handleSensorForward(packet);
            break;
        case PACKET_ROUTE_REQUEST:
            m_router.handleRouteRequest();
            break;
        case PACKET_EVAC_ADVERT:
            m_evacProtocol.handleEvacAdvert(senderMac, packet);
            break;
        case PACKET_OTA_START:
            m_otaReceiver.handleStart(senderMac, packet);
            break;
        case PACKET_OTA_CHUNK:
            m_otaReceiver.handleChunk(packet);
            break;
        case PACKET_OTA_END:
            m_otaReceiver.handleEnd(packet);
            break;
        case PACKET_ETX_PING:
            Serial.printf("[ETX] Nhận gói Ping từ Node %d (%02X:%02X...)\n", packet.id, senderMac[0], senderMac[1]);
            break;
        default:
            break;
    }
}

void MeshNetwork::handleSent(const esp_now_send_info_t *tx_info, esp_now_send_status_t status) {
    if (memcmp(tx_info->des_addr, m_broadcastMac, 6) != 0) {
        m_routingTable.updateEtx(tx_info->des_addr, status == ESP_NOW_SEND_SUCCESS);
        m_routingTable.recordTxTime(tx_info->des_addr);
    }

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

void MeshNetwork::sendEtxPing(const uint8_t* targetMac) {
    MeshPacket pingPkt = {};
    pingPkt.packetType = PACKET_ETX_PING;
    memcpy(pingPkt.sourceMac, m_myMac, 6);
    memcpy(pingPkt.destMac, targetMac, 6);
    pingPkt.id = m_satelliteId;

    if (!esp_now_is_peer_exist(targetMac)) {
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, targetMac, 6);
        peerInfo.channel = 0;
        peerInfo.encrypt = false;
        peerInfo.ifidx = WIFI_IF_STA;
        esp_now_add_peer(&peerInfo);
    }

    esp_err_t result = esp_now_send(targetMac, (uint8_t *)&pingPkt, sizeof(pingPkt));
    m_routingTable.recordTxTime(targetMac);
    Serial.printf("[ETX Ping] Đang gửi Ping tới MAC: %02X:%02X... | Result: %s\n",
                  targetMac[0], targetMac[1], result == ESP_OK ? "OK" : "FAIL");
}
