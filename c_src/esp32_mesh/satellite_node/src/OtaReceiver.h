#ifndef OTA_RECEIVER_H
#define OTA_RECEIVER_H

#include <Arduino.h>
#include <esp_now.h>
#include <Update.h>
#include <mesh_packet.h>

class OtaReceiver {
public:
    OtaReceiver(uint8_t satelliteId);
    void init(const uint8_t* myMac);
    void handleStart(const uint8_t* senderMac, const MeshPacket& pkt);
    void handleChunk(const MeshPacket& pkt);
    void handleEnd(const MeshPacket& pkt);
    bool isActive() const;

private:
    void sendAck(uint16_t seq);

    uint8_t m_satelliteId;
    uint8_t m_myMac[6];
    
    struct State {
        bool active;
        uint32_t fileSize;
        uint16_t totalChunks;
        uint16_t expectedSeq;
        unsigned long lastPacketTime;
        uint8_t masterMac[6];
    } m_state;
};

#endif
