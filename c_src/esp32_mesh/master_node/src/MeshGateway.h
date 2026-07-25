#ifndef MESH_GATEWAY_H
#define MESH_GATEWAY_H

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <mesh_packet.h>

class MeshGateway {
public:
    MeshGateway();
    bool init();
    void broadcastRouteUpdate();
    void broadcastEvacPotential(float potential);
    
    float getSatTemp(uint8_t nodeId) const;
    int getSatGas(uint8_t nodeId) const;
    const uint8_t* getSatMac(uint8_t nodeId) const;
    
    static void onRecvStatic(const esp_now_recv_info_t *recv_info, const uint8_t *incomingDataRaw, int len);
    void handleRecv(const esp_now_recv_info_t *recv_info, const MeshPacket& packet);

    // Các phương thức OTA phát sóng
    bool startOtaUpdate(uint8_t targetSatId);
    void processOtaTransmission();
    bool isOtaActive() const { return m_otaSender.active; }
    void setOtaTargetSatId(uint8_t id) { m_otaTargetSatId = id; }
    uint8_t getOtaTargetSatId() const { return m_otaTargetSatId; }

private:
    uint8_t m_myMac[6];
    uint8_t m_broadcastMac[6];
    
    float m_satTemp[6];
    int m_satGas[6];
    uint8_t m_satMac[6][6]; // Lưu MAC học được từ các Satellite
    uint8_t m_otaTargetSatId;
    
    struct OtaSenderState {
        bool active;
        uint32_t fileSize;
        uint16_t totalChunks;
        uint16_t sentSeq;
        uint16_t acknowledgedSeq;
        bool endSent;
        unsigned long lastAckTime;
        uint8_t targetMac[6];
        uint8_t targetSatId;
    } m_otaSender;
    
    static MeshGateway* s_instance;
};

#endif // MESH_GATEWAY_H
