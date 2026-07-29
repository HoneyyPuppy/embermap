#include "OtaReceiver.h"

OtaReceiver::OtaReceiver(uint8_t satelliteId) : m_satelliteId(satelliteId) {
    m_state.active = false;
    m_state.fileSize = 0;
    m_state.totalChunks = 0;
    m_state.expectedSeq = 0;
    m_state.lastPacketTime = 0;
    memset(m_state.masterMac, 0, 6);
    memset(m_state.currentMd5, 0, sizeof(m_state.currentMd5));
}

void OtaReceiver::init(const uint8_t* myMac) {
    memcpy(m_myMac, myMac, 6);
}

void OtaReceiver::handleStart(const uint8_t* senderMac, const MeshPacket& pkt) {
    Serial.printf("[OTA Recv] Nhận gói bắt đầu OTA. File size: %u, MD5: %s\n", 
                  pkt.ota.start.otaFileSize, pkt.ota.start.otaMd5);
    
    // Nếu đang nhận dở firmware cùng MD5 -> Đây là gói START trùng lặp do Master mất ACK
    // Chỉ cần gửi lại ACK sẵn sàng mà KHÔNG hủy phiên đang chạy
    if (m_state.active && strcmp(pkt.ota.start.otaMd5, m_state.currentMd5) == 0) {
        Serial.println("[OTA Recv] Gói START trùng lặp (cùng MD5). Gửi lại ACK sẵn sàng.");
        sendAck(0xFFFF);
        return;
    }

    // Luôn gọi abort() trước begin() để giải phóng bộ đệm RAM bị khóa từ phiên cũ (nếu có)
    Update.abort();

    // Đảm bảo peer của người gửi (Master) đã được đăng ký
    if (!esp_now_is_peer_exist(senderMac)) {
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, senderMac, 6);
        peerInfo.channel = 0;
        peerInfo.encrypt = false;
        esp_now_add_peer(&peerInfo);
    }
    
    m_state.active = true;
    m_state.fileSize = pkt.ota.start.otaFileSize;
    m_state.totalChunks = (m_state.fileSize + 179) / 180;
    m_state.expectedSeq = 0;
    m_state.lastPacketTime = millis();
    memcpy(m_state.masterMac, senderMac, 6);
    strncpy(m_state.currentMd5, pkt.ota.start.otaMd5, sizeof(m_state.currentMd5) - 1);
    m_state.currentMd5[sizeof(m_state.currentMd5) - 1] = '\0';
    
    Update.setMD5(pkt.ota.start.otaMd5);
    if (!Update.begin(m_state.fileSize, U_FLASH)) {
        Serial.print("[OTA Recv Error] Update.begin failed: ");
        Update.printError(Serial);
        m_state.active = false;
        sendAck(0xFFFF); // Báo lỗi
    } else {
        Serial.println("[OTA Recv] Update.begin thành công. Đang đợi chunks...");
        sendAck(0xFFFF); // Báo sẵn sàng nhận
    }
}

void OtaReceiver::handleChunk(const MeshPacket& pkt) {
    if (!m_state.active) return;
    
    uint16_t seq = pkt.ota.chunk.chunkSeq;
    uint8_t len = pkt.ota.chunk.chunkLen;
    
    if (seq == m_state.expectedSeq) {
        size_t written = Update.write((uint8_t*)pkt.ota.chunk.chunkData, len);
        if (written == len) {
            m_state.expectedSeq++;
            m_state.lastPacketTime = millis();
            
            // Gửi ACK mỗi 5 gói hoặc khi nhận gói cuối cùng
            // (Phải nhỏ hơn cửa sổ trượt của Master = 15 để tránh deadlock)
            if (m_state.expectedSeq % 5 == 0 || m_state.expectedSeq == m_state.totalChunks) {
                sendAck(m_state.expectedSeq - 1);
            }
        } else {
            Serial.printf("[OTA Recv Error] Ghi mảnh %d thất bại!\n", seq);
            sendAck(m_state.expectedSeq - 1);
        }
    } 
    else if (seq < m_state.expectedSeq) {
        // Gói tin trùng lặp, phản hồi ACK ngay lập tức
        sendAck(m_state.expectedSeq - 1);
    } 
    else {
        // Mất gói! Phản hồi ACK mảnh cuối cùng thành công để yêu cầu gửi lại
        Serial.printf("[OTA Recv Warning] Nhận lệch mảnh! Nhận: %d, Đợi: %d\n", seq, m_state.expectedSeq);
        sendAck(m_state.expectedSeq - 1);
    }
}

void OtaReceiver::handleEnd(const MeshPacket& pkt) {
    if (!m_state.active) return;
    
    Serial.println("[OTA Recv] Nhận lệnh kết thúc. Đang xác thực MD5...");
    if (Update.end(true)) {
        Serial.println("[OTA Recv SUCCESS] Nâng cấp thành công! Đang khởi động lại...");
        sendAck(0xFFFE); // Gửi ACK thành công
        delay(1000);
        ESP.restart();
    } else {
        Serial.print("[OTA Recv FAIL] Xác thực MD5 thất bại: ");
        Update.printError(Serial);
        sendAck(0xFFFD); // Gửi ACK lỗi xác thực
        m_state.active = false;
    }
}

bool OtaReceiver::isActive() const {
    return m_state.active;
}

void OtaReceiver::sendAck(uint16_t seq) {
    MeshPacket packet = {};
    packet.packetType = PACKET_OTA_ACK;
    memcpy(packet.sourceMac, m_myMac, 6);
    memcpy(packet.destMac, m_state.masterMac, 6);
    packet.id = m_satelliteId;
    packet.ota.ack.lastReceivedSeq = seq;
    
    esp_now_send(m_state.masterMac, (uint8_t *)&packet, sizeof(packet));
}
