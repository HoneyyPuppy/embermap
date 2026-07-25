#include "MeshGateway.h"
#include <esp_wifi.h>
#include <esp_mac.h>
#include <LittleFS.h>
#include <MD5Builder.h>

MeshGateway* MeshGateway::s_instance = nullptr;

MeshGateway::MeshGateway() {
    s_instance = this;
    memset(m_broadcastMac, 0xFF, 6);
    memset(m_myMac, 0, 6);
    m_otaTargetSatId = 0xFF;
    
    // Khởi tạo giá trị mặc định cho dữ liệu cảm biến và MAC
    for (int i = 0; i < 6; i++) {
        m_satTemp[i] = 26.0;
        m_satGas[i] = 0;
        memset(m_satMac[i], 0, 6);
    }

    m_otaSender.active = false;
    m_otaSender.fileSize = 0;
    m_otaSender.totalChunks = 0;
    m_otaSender.sentSeq = 0;
    m_otaSender.acknowledgedSeq = 0;
    m_otaSender.endSent = false;
    m_otaSender.lastAckTime = 0;
    m_otaSender.targetSatId = 0xFF;
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
                // Lưu lại địa chỉ MAC thực tế của Satellite
                memcpy(m_satMac[packet.id], recv_info->src_addr, 6);
            }
        }
    }
    else if (packet.packetType == PACKET_ROUTE_REQUEST) {
        Serial.println("[Mesh] Nhận yêu cầu quét kênh (Route Request) -> Quảng bá phản hồi.");
        broadcastRouteUpdate();
    }
    else if (packet.packetType == PACKET_OTA_ACK) {
        if (!m_otaSender.active) return;
        
        uint16_t ackSeq = packet.ota.ack.lastReceivedSeq;
        Serial.printf("[OTA Master] Nhận phản hồi ACK từ Node %d: seq = 0x%04X\n", 
                      packet.id, ackSeq);
        
        if (ackSeq == 0xFFFF) {
            // Đối phương sẵn sàng nhận
            if (m_otaSender.acknowledgedSeq == 0 && m_otaSender.sentSeq == 0) {
                Serial.println("[OTA Master] Đối phương SẴN SÀNG. Bắt đầu truyền...");
                m_otaSender.lastAckTime = millis();
            }
        } 
        else if (ackSeq == 0xFFFE) {
            // Nâng cấp thành công
            Serial.printf("[OTA Master SUCCESS] Nút %d nâng cấp thành công và đang khởi động lại!\n", packet.id);
            m_otaSender.active = false;
        } 
        else if (ackSeq == 0xFFFD) {
            // Lỗi checksum
            Serial.printf("[OTA Master FAIL] Nút %d báo lỗi xác thực checksum firmware!\n", packet.id);
            m_otaSender.active = false;
        } 
        else {
            // ACK mảnh đã nhận
            if (ackSeq >= m_otaSender.acknowledgedSeq) {
                m_otaSender.acknowledgedSeq = ackSeq + 1;
                m_otaSender.lastAckTime = millis();
            }
        }
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

const uint8_t* MeshGateway::getSatMac(uint8_t nodeId) const {
    if (nodeId < 6) return m_satMac[nodeId];
    return nullptr;
}

bool MeshGateway::startOtaUpdate(uint8_t targetSatId) {
    if (m_otaSender.active) {
        Serial.println("[OTA Master] Đang có phiên OTA hoạt động! Hủy yêu cầu mới.");
        return false;
    }

    if (targetSatId >= 6) return false;
    
    // Kiểm tra xem đã học được MAC của Satellite chưa
    bool hasMac = false;
    for (int i = 0; i < 6; i++) {
        if (m_satMac[targetSatId][i] != 0) { hasMac = true; break; }
    }
    if (!hasMac) {
        Serial.printf("[OTA Master Error] Chưa học được địa chỉ MAC của Satellite %d! Hãy đợi gửi dữ liệu.\n", targetSatId);
        return false;
    }

    File file = LittleFS.open("/satellite_firmware.bin", "r");
    if (!file) {
        Serial.println("[OTA Master Error] Không tìm thấy file satellite_firmware.bin trên LittleFS!");
        return false;
    }
    uint32_t fileSize = file.size();
    
    // Tính toán MD5 checksum
    MD5Builder md5;
    md5.begin();
    md5.addStream(file, fileSize);
    md5.calculate();
    String md5String = md5.toString();
    file.close();

    Serial.printf("[OTA Master] Khởi động phiên OTA cho Node %d\n", targetSatId);
    Serial.printf("- Kích thước file: %u bytes\n", fileSize);
    Serial.printf("- MD5: %s\n", md5String.c_str());

    // Thiết lập trạng thái truyền
    m_otaSender.active = true;
    m_otaSender.fileSize = fileSize;
    m_otaSender.totalChunks = (fileSize + 179) / 180;
    m_otaSender.sentSeq = 0;
    m_otaSender.acknowledgedSeq = 0;
    m_otaSender.endSent = false;
    m_otaSender.lastAckTime = millis();
    m_otaSender.targetSatId = targetSatId;
    memcpy(m_otaSender.targetMac, m_satMac[targetSatId], 6);

    // Đăng ký peer trong ESP-NOW nếu chưa đăng ký
    if (!esp_now_is_peer_exist(m_otaSender.targetMac)) {
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, m_otaSender.targetMac, 6);
        peerInfo.channel = WiFi.channel();
        peerInfo.encrypt = false;
        esp_now_add_peer(&peerInfo);
    }

    // Gửi lệnh khởi động OTA (START)
    MeshPacket packet = {};
    packet.packetType = PACKET_OTA_START;
    memcpy(packet.sourceMac, m_myMac, 6);
    memcpy(packet.destMac, m_otaSender.targetMac, 6);
    packet.id = 0;
    packet.ota.start.otaFileSize = fileSize;
    strcpy(packet.ota.start.otaMd5, md5String.c_str());

    esp_now_send(m_otaSender.targetMac, (uint8_t *)&packet, sizeof(packet));
    Serial.println("[OTA Master] Đã gửi lệnh PACKET_OTA_START. Đang chờ phản hồi...");
    return true;
}

void MeshGateway::processOtaTransmission() {
    if (!m_otaSender.active) return;
    
    unsigned long currentMillis = millis();
    
    // 1. Kiểm tra Timeout (Hơn 6 giây không nhận được ACK mới)
    if (currentMillis - m_otaSender.lastAckTime > 6000) {
        Serial.printf("[OTA Master] Timeout! Gửi lại từ sequence %d...\n", m_otaSender.acknowledgedSeq);
        m_otaSender.sentSeq = m_otaSender.acknowledgedSeq;
        m_otaSender.lastAckTime = currentMillis; // Reset timer
        
        // Gửi lại gói START nếu chưa có ACK đầu tiên
        if (m_otaSender.acknowledgedSeq == 0 && m_otaSender.sentSeq == 0) {
            Serial.println("[OTA Master] Phát lại lệnh START...");
            
            File file = LittleFS.open("/satellite_firmware.bin", "r");
            if (!file) return;
            uint32_t fileSize = file.size();
            MD5Builder md5;
            md5.begin();
            md5.addStream(file, fileSize);
            md5.calculate();
            String md5String = md5.toString();
            file.close();
            
            MeshPacket packet = {};
            packet.packetType = PACKET_OTA_START;
            memcpy(packet.sourceMac, m_myMac, 6);
            memcpy(packet.destMac, m_otaSender.targetMac, 6);
            packet.id = 0;
            packet.ota.start.otaFileSize = fileSize;
            strcpy(packet.ota.start.otaMd5, md5String.c_str());

            esp_now_send(m_otaSender.targetMac, (uint8_t *)&packet, sizeof(packet));
            return;
        }
    }
    
    // 2. Gửi các mảnh trong Cửa sổ trượt (Cửa sổ 15 mảnh)
    if (m_otaSender.sentSeq - m_otaSender.acknowledgedSeq < 15 && m_otaSender.sentSeq < m_otaSender.totalChunks) {
        File file = LittleFS.open("/satellite_firmware.bin", "r");
        if (!file) {
            Serial.println("[OTA Master Error] Không mở được file satellite_firmware.bin!");
            m_otaSender.active = false;
            return;
        }
        
        file.seek(m_otaSender.sentSeq * 180);
        
        MeshPacket packet = {};
        packet.packetType = PACKET_OTA_CHUNK;
        memcpy(packet.sourceMac, m_myMac, 6);
        memcpy(packet.destMac, m_otaSender.targetMac, 6);
        packet.id = 0;
        packet.ota.chunk.chunkSeq = m_otaSender.sentSeq;
        
        int bytesRead = file.read(packet.ota.chunk.chunkData, 180);
        packet.ota.chunk.chunkLen = bytesRead;
        file.close();
        
        if (bytesRead > 0) {
            esp_err_t err = esp_now_send(m_otaSender.targetMac, (uint8_t *)&packet, sizeof(packet));
            if (err == ESP_OK) {
                m_otaSender.sentSeq++;
            } else {
                Serial.printf("[OTA Master Warning] Lỗi gửi chunk %d, mã: %d\n", m_otaSender.sentSeq, err);
            }
            delay(5); // Delay tránh nghẽn vô tuyến
        }
    }
    
    // 3. Nếu đã nhận hết tất cả ACK của các mảnh -> Gửi lệnh END
    if (m_otaSender.acknowledgedSeq == m_otaSender.totalChunks && !m_otaSender.endSent) {
        Serial.println("[OTA Master] Đã gửi hết các mảnh. Phát lệnh kết thúc OTA và chờ đối phương nạp...");
        
        MeshPacket packet = {};
        packet.packetType = PACKET_OTA_END;
        memcpy(packet.sourceMac, m_myMac, 6);
        memcpy(packet.destMac, m_otaSender.targetMac, 6);
        packet.id = 0;
        
        esp_now_send(m_otaSender.targetMac, (uint8_t *)&packet, sizeof(packet));
        m_otaSender.endSent = true;
        m_otaSender.lastAckTime = currentMillis; // Chờ phản hồi reboot
    }
}
