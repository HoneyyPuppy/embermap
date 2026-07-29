#ifndef OTA_SENDER_H
#define OTA_SENDER_H

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <MD5Builder.h>
#include <mesh_packet.h>

class OtaSender {
public:
    OtaSender();
    void init(const uint8_t* myMac);
    bool start(uint8_t targetSatId, const uint8_t* targetMac);
    void processTransmission();
    void handleAck(const MeshPacket& packet);
    bool isActive() const;

private:
    struct OtaSenderState {
        bool active;
        bool startAcked;
        uint32_t fileSize;
        uint16_t totalChunks;
        uint16_t sentSeq;
        uint16_t acknowledgedSeq;
        bool endSent;
        unsigned long lastAckTime;
        uint8_t targetMac[6];
        uint8_t targetSatId;
    };
    
    OtaSenderState m_state;
    uint8_t m_myMac[6];
};

#endif // OTA_SENDER_H
