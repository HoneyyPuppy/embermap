#ifndef MESH_NETWORK_H
#define MESH_NETWORK_H

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <mesh_packet.h>
#include "RoutingTable.h"

class MeshNetwork {
public:
    MeshNetwork(uint8_t satelliteId, RoutingTable& routingTable);
    bool init();
    void sendSensorData(float temp, int gas, bool emergency);
    void rebroadcastRouteUpdate();
    void sendRouteRequest(uint8_t channel);
    
    static void onRecvStatic(const esp_now_recv_info_t *recv_info, const uint8_t *incomingDataRaw, int len);
    static void onSentStatic(const esp_now_send_info_t *tx_info, esp_now_send_status_t status);

    void handleRecv(const esp_now_recv_info_t *recv_info, const MeshPacket& packet);
    void handleSent(const esp_now_send_info_t *tx_info, esp_now_send_status_t status);

    bool isWaitingForScanAck() const { return m_pendingSend.active; }
    void stopScanningAck() { m_pendingSend.active = false; }

private:
    uint8_t m_satelliteId;
    RoutingTable& m_routingTable;
    uint8_t m_myMac[6];
    uint8_t m_broadcastMac[6];

    struct PendingSend {
        MeshPacket packet;
        uint8_t currentParentIdx;
        bool active;
    } m_pendingSend;

    void updateAndSendToParent(uint8_t idx);
    
    static MeshNetwork* s_instance;
};

#endif // MESH_NETWORK_H
