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

void MeshNetwork::sendSensorData(float temp, int gas, bool emergency, float totalPotential) {
    if (!m_routingTable.hasRoute()) {
        Serial.println("[APF Warning] Không gửi được dữ liệu: Mất định tuyến (Thế năng vô cực).");
        return;
    }

    MeshPacket packet = {};
    packet.packetType = PACKET_SENSOR_DATA;
    memcpy(packet.sourceMac, m_myMac, 6);
    memset(packet.destMac, 0, 6);
    memcpy(packet.forwardMac, m_routingTable.getNextHopMac(), 6);
    packet.id = m_satelliteId;
    packet.temp = temp;
    packet.gasRaw = gas;
    packet.emergency = emergency;
    packet.potential = totalPotential;
    packet.hopCount = m_routingTable.getHopCount();

    m_pendingSend.packet = packet;
    m_pendingSend.active = true;

    const uint8_t* targetMac = m_routingTable.getNextHopMac();
    if (!esp_now_is_peer_exist(targetMac)) {
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, targetMac, 6);
        peerInfo.channel = 0;
        peerInfo.encrypt = false;
        peerInfo.ifidx = WIFI_IF_STA;
        esp_now_add_peer(&peerInfo);
    }

    esp_err_t result = esp_now_send(targetMac, (uint8_t *)&m_pendingSend.packet, sizeof(m_pendingSend.packet));
    Serial.printf("[APF Send] Gửi dữ liệu cảm biến tới Next Hop: %02X:%02X... | Result: %s\n", 
                  targetMac[0], targetMac[1], result == ESP_OK ? "Queued" : "Fail");
}

void MeshNetwork::broadcastPotential(float totalPotential) {
    MeshPacket packet = {};
    packet.packetType = PACKET_POTENTIAL_ADVERT;
    memcpy(packet.sourceMac, m_myMac, 6);
    memcpy(packet.destMac, m_broadcastMac, 6);
    memset(packet.forwardMac, 0, 6);
    packet.id = m_satelliteId;
    packet.temp = 0.0;
    packet.gasRaw = 0;
    packet.emergency = false;
    packet.potential = totalPotential;
    packet.hopCount = m_routingTable.getHopCount();

    memcpy(packet.routePath, m_routingTable.getParentRoutePath(), m_routingTable.getParentRoutePathLen());
    packet.routePathLen = m_routingTable.getParentRoutePathLen();
    if (packet.routePathLen < MAX_ROUTE_PATH) {
        packet.routePath[packet.routePathLen] = m_satelliteId;
        packet.routePathLen++;
    }

    esp_err_t result = esp_now_send(m_broadcastMac, (uint8_t *)&packet, sizeof(packet));
    Serial.printf("[APF] Quảng bá thế năng U = %.1f (Hop = %d) -> %s\n", 
                  totalPotential, m_routingTable.getHopCount(), result == ESP_OK ? "OK" : "FAIL");
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

    if (packet.packetType == PACKET_POTENTIAL_ADVERT) {
        if (!m_routingTable.isAllowedNeighbor(packet.id)) {
            return;
        }
        int rssi = recv_info->rx_ctrl->rssi;
        m_routingTable.updateNeighbor(senderMac, packet.potential, packet.hopCount, packet.routePath, packet.routePathLen, rssi);
    }
    else if (packet.packetType == PACKET_SENSOR_DATA) {
        if (memcmp(packet.forwardMac, m_myMac, 6) == 0) {
            if (m_routingTable.hasRoute()) {
                MeshPacket forwardPacket = packet;
                memcpy(forwardPacket.forwardMac, m_routingTable.getNextHopMac(), 6);
                esp_now_send(m_routingTable.getNextHopMac(), (uint8_t *)&forwardPacket, sizeof(forwardPacket));
                Serial.printf("[APF Forward] Trung chuyển gói tin của Node %d qua Next Hop thế năng thấp: %02X:%02X...\n", 
                              forwardPacket.id, m_routingTable.getNextHopMac()[0], m_routingTable.getNextHopMac()[1]);
            } else {
                Serial.println("[APF Warning] Không thể trung chuyển: Mất định tuyến!");
            }
        }
    }
    else if (packet.packetType == PACKET_ROUTE_REQUEST) {
        if (m_routingTable.hasRoute()) {
            Serial.println("[APF] Nhận yêu cầu quét kênh (Route Request) -> Phát lại phản hồi thế năng.");
            broadcastPotential(m_routingTable.getBasePotential());
        }
    }
}

void MeshNetwork::handleSent(const esp_now_send_info_t *tx_info, esp_now_send_status_t status) {
    if (m_pendingSend.active && memcmp(tx_info->des_addr, m_pendingSend.packet.forwardMac, 6) == 0) {
        if (status == ESP_NOW_SEND_SUCCESS) {
            Serial.printf("[APF OK] Gửi thành công tới Next Hop: %02X:%02X!\n", 
                          tx_info->des_addr[0], tx_info->des_addr[1]);
            m_pendingSend.active = false;
        } else {
            Serial.printf("[APF FAIL] Gửi tới Next Hop: %02X:%02X thất bại! Kích hoạt failover...\n", 
                          tx_info->des_addr[0], tx_info->des_addr[1]);
            
            triggerFailover(tx_info->des_addr);
        }
    }
}

void MeshNetwork::triggerFailover(const uint8_t *failedMac) {
    m_routingTable.removeNeighbor(failedMac);
    
    if (m_routingTable.selectNextHop(99999.0)) {
        const uint8_t* nextHop = m_routingTable.getNextHopMac();
        memcpy(m_pendingSend.packet.forwardMac, nextHop, 6);
        
        if (!esp_now_is_peer_exist(nextHop)) {
            esp_now_peer_info_t peerInfo = {};
            memcpy(peerInfo.peer_addr, nextHop, 6);
            peerInfo.channel = 0;
            peerInfo.encrypt = false;
            peerInfo.ifidx = WIFI_IF_STA;
            esp_now_add_peer(&peerInfo);
        }
        
        esp_err_t result = esp_now_send(nextHop, (uint8_t *)&m_pendingSend.packet, sizeof(m_pendingSend.packet));
        Serial.printf("[APF Backup Send] Gửi lại dữ liệu tới Next Hop dự phòng: %02X:%02X... | Result: %s\n", 
                      nextHop[0], nextHop[1], result == ESP_OK ? "Queued" : "Fail");
    } else {
        Serial.println("[APF Failover Warning] Không còn láng giềng nào khác khả dụng để failover!");
        m_pendingSend.active = false;
    }
}
