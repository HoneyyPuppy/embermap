#include "MeshGateway.h"
#include <esp_wifi.h>
#include <esp_mac.h>

MeshGateway* MeshGateway::s_instance = nullptr;

MeshGateway::MeshGateway() {
    s_instance = this;
    memset(m_broadcastMac, 0xFF, 6);
    memset(m_myMac, 0, 6);
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

    m_beacon.init(m_myMac, m_broadcastMac);
    m_otaSender.init(m_myMac);
    
    m_beacon.broadcastRouteUpdate();
    return true;
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
            m_satManager.updateSensorData(packet.id, packet.temp, packet.gasRaw, recv_info->src_addr);
        }
    }
    else if (packet.packetType == PACKET_ROUTE_REQUEST) {
        Serial.println("[Mesh] Nhận yêu cầu quét kênh (Route Request) -> Quảng bá phản hồi.");
        m_beacon.broadcastRouteUpdate();
    }
    else if (packet.packetType == PACKET_OTA_ACK) {
        m_otaSender.handleAck(packet);
    }
}
