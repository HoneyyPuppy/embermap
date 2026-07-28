#ifndef MESH_GATEWAY_H
#define MESH_GATEWAY_H

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <mesh_packet.h>
#include "SatelliteManager.h"
#include "MasterBeacon.h"
#include "OtaSender.h"

class MeshGateway {
public:
    MeshGateway();
    bool init();
    
    SatelliteManager& satManager() { return m_satManager; }
    MasterBeacon& beacon() { return m_beacon; }
    OtaSender& otaSender() { return m_otaSender; }

    static void onRecvStatic(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len);

private:
    uint8_t m_myMac[6];
    uint8_t m_broadcastMac[6];
    
    SatelliteManager m_satManager;
    MasterBeacon m_beacon;
    OtaSender m_otaSender;
    
    void handleRecv(const esp_now_recv_info_t *recv_info, const MeshPacket& packet);
    static MeshGateway* s_instance;
};
#endif
