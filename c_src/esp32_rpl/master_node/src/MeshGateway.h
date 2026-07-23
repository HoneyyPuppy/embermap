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
    void broadcastDio();
    
    float getSatTemp(uint8_t nodeId) const;
    int getSatGas(uint8_t nodeId) const;
    
    static void onRecvStatic(const esp_now_recv_info_t *recv_info, const uint8_t *incomingDataRaw, int len);
    void handleRecv(const esp_now_recv_info_t *recv_info, const MeshPacket& packet);

private:
    uint8_t m_myMac[6];
    uint8_t m_broadcastMac[6];
    
    uint16_t m_masterRank;
    uint8_t m_dodagVersion;
    
    float m_satTemp[4];
    int m_satGas[4];
    
    static MeshGateway* s_instance;
};

#endif // MESH_GATEWAY_H
