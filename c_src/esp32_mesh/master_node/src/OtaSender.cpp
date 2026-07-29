#include "OtaSender.h"

OtaSender::OtaSender() {
    m_state.active = false;
    m_state.fileSize = 0;
    m_state.totalChunks = 0;
    m_state.sentSeq = 0;
    m_state.acknowledgedSeq = 0;
    m_state.endSent = false;
    m_state.lastAckTime = 0;
    m_state.targetSatId = 0xFF;
    memset(m_state.targetMac, 0, 6);
    memset(m_myMac, 0, 6);
}

void OtaSender::init(const uint8_t* myMac) {
    if (myMac) {
        memcpy(m_myMac, myMac, 6);
    }
}

bool OtaSender::isActive() const {
    return m_state.active;
}

bool OtaSender::start(uint8_t targetSatId, const uint8_t* targetMac) {
    if (m_state.active) {
        Serial.println("[OTA Master] Đang có phiên OTA hoạt động! Hủy yêu cầu mới.");
        return false;
    }

    if (targetSatId >= 6) return false;
    
    if (!targetMac) {
        Serial.printf("[OTA Master Error] Chưa học được địa chỉ MAC của Satellite %d! Hãy đợi gửi dữ liệu.\n", targetSatId);
        return false;
    }

    bool hasMac = false;
    for (int i = 0; i < 6; i++) {
        if (targetMac[i] != 0) { hasMac = true; break; }
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
    m_state.active = true;
    m_state.startAcked = false;
    m_state.fileSize = fileSize;
    m_state.totalChunks = (fileSize + 179) / 180;
    m_state.sentSeq = 0;
    m_state.acknowledgedSeq = 0;
    m_state.endSent = false;
    m_state.lastAckTime = millis();
    m_state.targetSatId = targetSatId;
    memcpy(m_state.targetMac, targetMac, 6);

    // Đăng ký peer trong ESP-NOW nếu chưa đăng ký
    if (!esp_now_is_peer_exist(m_state.targetMac)) {
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, m_state.targetMac, 6);
        peerInfo.channel = WiFi.channel();
        peerInfo.encrypt = false;
        esp_now_add_peer(&peerInfo);
    }

    // Gửi lệnh khởi động OTA (START)
    MeshPacket packet = {};
    packet.packetType = PACKET_OTA_START;
    memcpy(packet.sourceMac, m_myMac, 6);
    memcpy(packet.destMac, m_state.targetMac, 6);
    packet.id = 0;
    packet.ota.start.otaFileSize = fileSize;
    strcpy(packet.ota.start.otaMd5, md5String.c_str());

    esp_now_send(m_state.targetMac, (uint8_t *)&packet, sizeof(packet));
    Serial.println("[OTA Master] Đã gửi lệnh PACKET_OTA_START. Đang chờ phản hồi...");
    return true;
}

void OtaSender::processTransmission() {
    if (!m_state.active) return;
    
    unsigned long currentMillis = millis();
    
    // 1. Kiểm tra Timeout (Hơn 6 giây không nhận được ACK mới, hoặc 1 giây nếu cửa sổ đang đầy)
    bool windowFull = m_state.startAcked && (m_state.sentSeq - m_state.acknowledgedSeq >= 15);
    unsigned long timeoutVal = windowFull ? 1000 : 6000;
    
    if (currentMillis - m_state.lastAckTime > timeoutVal) {
        if (m_state.endSent) {
            Serial.printf("[OTA Master SUCCESS] Hết thời gian chờ phản hồi reboot từ Node %d. Giả định nâng cấp thành công và đóng phiên OTA!\n", m_state.targetSatId);
            m_state.active = false;
            return;
        }
        
        // Kiểm tra xem có bị kẹt ở cùng một sequence không (đề phòng Vệ tinh bị reset/sụt áp tắt ngóm hoặc offline)
        static uint16_t lastFailedSeq = 0xFFFF;
        static uint8_t retryCount = 0;
        
        if (m_state.acknowledgedSeq == lastFailedSeq) {
            retryCount++;
            uint8_t maxRetries = m_state.startAcked ? 10 : 5; // Chờ tối đa 30s nếu chưa ready (5 lần timeout 6s), hoặc 10s nếu kẹt giữa chừng
            if (retryCount > maxRetries) {
                Serial.printf("[OTA Master Error] Vệ tinh %d không phản hồi tại seq %d (Ready: %s). Hủy phiên OTA!\n", 
                              m_state.targetSatId, m_state.acknowledgedSeq, m_state.startAcked ? "CÓ" : "KHÔNG");
                m_state.active = false;
                retryCount = 0;
                lastFailedSeq = 0xFFFF;
                return;
            }
        } else {
            lastFailedSeq = m_state.acknowledgedSeq;
            retryCount = 0;
        }

        Serial.printf("[OTA Master] %s! Gửi lại từ sequence %d...\n", 
                      windowFull ? "Cửa sổ đầy - Gửi lại nhanh" : "Timeout", m_state.acknowledgedSeq);
        m_state.sentSeq = m_state.acknowledgedSeq;
        m_state.lastAckTime = currentMillis; // Reset timer
        
        // Gửi lại gói START nếu chưa nhận được ACK START đầu tiên
        if (!m_state.startAcked) {
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
            memcpy(packet.destMac, m_state.targetMac, 6);
            packet.id = 0;
            packet.ota.start.otaFileSize = fileSize;
            strcpy(packet.ota.start.otaMd5, md5String.c_str());

            esp_now_send(m_state.targetMac, (uint8_t *)&packet, sizeof(packet));
            return;
        }
    }
    
    // 2. Gửi các mảnh trong Cửa sổ trượt (Cửa sổ 15 mảnh) - Chỉ gửi khi đối phương đã phản hồi READY (startAcked == true)
    if (m_state.startAcked && m_state.sentSeq - m_state.acknowledgedSeq < 15 && m_state.sentSeq < m_state.totalChunks) {
        File file = LittleFS.open("/satellite_firmware.bin", "r");
        if (file) {
            file.seek(m_state.sentSeq * 180);
            while (m_state.sentSeq - m_state.acknowledgedSeq < 15 && m_state.sentSeq < m_state.totalChunks) {
                MeshPacket packet = {};
                packet.packetType = PACKET_OTA_CHUNK;
                memcpy(packet.sourceMac, m_myMac, 6);
                memcpy(packet.destMac, m_state.targetMac, 6);
                packet.id = 0;
                packet.ota.chunk.chunkSeq = m_state.sentSeq;
                
                int bytesRead = file.read(packet.ota.chunk.chunkData, 180);
                packet.ota.chunk.chunkLen = bytesRead;
                
                if (bytesRead > 0) {
                    esp_err_t err = esp_now_send(m_state.targetMac, (uint8_t *)&packet, sizeof(packet));
                    if (err == ESP_OK) {
                        m_state.sentSeq++;
                    } else {
                        Serial.printf("[OTA Master Warning] Lỗi gửi chunk %d, mã: %d\n", m_state.sentSeq, err);
                        break;
                    }
                    delay(15); // Delay 15ms để giảm dòng điện đỉnh khi ghi Flash của Vệ tinh (tránh sụt áp/brownout)
                } else {
                    break;
                }
            }
            file.close();
        } else {
            Serial.println("[OTA Master Error] Không mở được file satellite_firmware.bin!");
            m_state.active = false;
        }
    }
    
    // 3. Nếu đã nhận hết tất cả ACK của các mảnh -> Gửi lệnh END
    if (m_state.acknowledgedSeq == m_state.totalChunks && !m_state.endSent) {
        Serial.println("[OTA Master] Đã gửi hết các mảnh. Phát lệnh kết thúc OTA và chờ đối phương nạp...");
        
        MeshPacket packet = {};
        packet.packetType = PACKET_OTA_END;
        memcpy(packet.sourceMac, m_myMac, 6);
        memcpy(packet.destMac, m_state.targetMac, 6);
        packet.id = 0;
        
        esp_now_send(m_state.targetMac, (uint8_t *)&packet, sizeof(packet));
        m_state.endSent = true;
        m_state.lastAckTime = currentMillis; // Chờ phản hồi reboot
    }
}

void OtaSender::handleAck(const MeshPacket& packet) {
    if (!m_state.active) return;
    if (packet.id != m_state.targetSatId) return;
        
    uint16_t ackSeq = packet.ota.ack.lastReceivedSeq;
    Serial.printf("[OTA Master] Nhận phản hồi ACK từ Node %d: seq = 0x%04X\n", 
                  packet.id, ackSeq);
    
    if (ackSeq == 0xFFFF) {
        // Đối phương sẵn sàng nhận
        if (!m_state.startAcked) {
            Serial.println("[OTA Master] Đối phương SẴN SÀNG. Bắt đầu truyền...");
            m_state.startAcked = true;
            m_state.lastAckTime = millis();
        }
    } 
    else if (ackSeq == 0xFFFE) {
        // Nâng cấp thành công
        Serial.printf("[OTA Master SUCCESS] Nút %d nâng cấp thành công và đang khởi động lại!\n", packet.id);
        m_state.active = false;
    } 
    else if (ackSeq == 0xFFFD) {
        // Lỗi checksum
        Serial.printf("[OTA Master FAIL] Nút %d báo lỗi xác thực checksum firmware!\n", packet.id);
        m_state.active = false;
    } 
    else {
        // ACK mảnh đã nhận
        if (ackSeq >= m_state.acknowledgedSeq) {
            m_state.acknowledgedSeq = ackSeq + 1;
            m_state.lastAckTime = millis();
        }
    }
}
