#include "MeshGateway.h"
#include <esp_wifi.h>
#include <esp_mac.h>

MeshGateway* MeshGateway::s_instance = nullptr;

MeshGateway::MeshGateway() {
    s_instance = this;
    memset(m_broadcastMac, 0xFF, 6);
    memset(m_myMac, 0, 6);
    
    // Khởi tạo giá trị mặc định cho dữ liệu cảm biến
    for (int i = 0; i < 6; i++) {
        m_satTemp[i] = 26.0;
        m_satGas[i] = 0;
    }
}

bool MeshGateway::init() {
    esp_read_mac(m_myMac, ESP_MAC_WIFI_STA);
    Serial.printf("\n=========================================\n");
    Serial.printf("MAC MASTER STA: %02X:%02X:%02X:%02X:%02X:%02X\n", 
                  m_myMac[0], m_myMac[1], m_myMac[2], m_myMac[3], m_myMac[4], m_myMac[5]);
    Serial.printf("=========================================\n");

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESP-NOW FAIL] Failed to initialize ESP-NOW!");
        return false;
    }
    
    Serial.println("[ESP-NOW OK] Initialized successfully.");
    esp_now_register_recv_cb(onRecvStatic);
    WiFi.setSleep(false);

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, m_broadcastMac, 6);
    peerInfo.channel = WiFi.channel();
    peerInfo.encrypt = false;
    peerInfo.ifidx = WIFI_IF_STA;
    
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("[ESP-NOW] Failed to add broadcast peer!");
        return false;
    }

    broadcastRouteUpdate();
    return true;
}

void MeshGateway::broadcastRouteUpdate() {
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

float MeshGateway::getSatTemp(uint8_t nodeId) const {
    if (nodeId < 6) return m_satTemp[nodeId];
    return 26.0;
}

int MeshGateway::getSatGas(uint8_t nodeId) const {
    if (nodeId < 6) return m_satGas[nodeId];
    return 0;
}

void MeshGateway::onRecvStatic(const esp_now_recv_info_t *recv_info, const uint8_t *incomingDataRaw, int len) {
    if (s_instance && len >= sizeof(MeshPacket)) {
        MeshPacket packet;
        memcpy(&packet, incomingDataRaw, sizeof(packet));
        s_instance->handleRecv(recv_info, packet);
    }
}

void MeshGateway::handleRecv(const esp_now_recv_info_t *recv_info, const MeshPacket& packet) {
    if (packet.packetType == PACKET_SENSOR_DATA) {
        if (memcmp(packet.forwardMac, m_myMac, 6) == 0) {
            Serial.printf("[Mesh] Received sensor from Node %d. Temp: %.1f C, Gas: %d\n", 
                          packet.id, packet.temp, packet.gasRaw);

            if (packet.id < 6) {
                m_satTemp[packet.id] = packet.temp;
                m_satGas[packet.id] = packet.gasRaw;
            }
        }
    }
    else if (packet.packetType == PACKET_ROUTE_REQUEST) {
        Serial.println("[Mesh] Nhận yêu cầu quét kênh (Route Request) -> Quảng bá phản hồi.");
        broadcastRouteUpdate();
    }
}

void MeshGateway::broadcastEvacPotential(float potential) {
    MeshPacket packet = {};
    packet.packetType = PACKET_EVAC_ADVERT;
    memcpy(packet.sourceMac, m_myMac, 6);
    memcpy(packet.destMac, m_broadcastMac, 6);
    memset(packet.forwardMac, 0, 6);
    packet.id = 0; // Master ID = 0
    packet.evacPotential = potential;

    esp_now_send(m_broadcastMac, (uint8_t *)&packet, sizeof(packet));
}
