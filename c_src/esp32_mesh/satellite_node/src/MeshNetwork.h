#ifndef MESH_NETWORK_H
#define MESH_NETWORK_H
#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <mesh_packet.h>
#include "RoutingTable.h"
#include "OtaReceiver.h"
#include "MeshRouter.h"
#include "EvacProtocol.h"

class MeshNetwork {
public:
    MeshNetwork(uint8_t satelliteId, RoutingTable& routingTable);
    bool init();
    void sendSensorData(float temp, int gas, bool emergency);
    
    MeshRouter& router() { return m_router; }
    EvacProtocol& evacProtocol() { return m_evacProtocol; }
    OtaReceiver& otaReceiver() { return m_otaReceiver; }
    bool isOtaActive() const { return m_otaReceiver.isActive(); }
    void stopScanningAck() { m_pendingSend.active = false; }

    static void onRecvStatic(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len);
    static void onSentStatic(const esp_now_send_info_t *tx_info, esp_now_send_status_t status);

private:
    uint8_t m_satelliteId;
    RoutingTable& m_routingTable;
    uint8_t m_myMac[6];
    uint8_t m_broadcastMac[6];
    
    MeshRouter m_router;
    EvacProtocol m_evacProtocol;
    OtaReceiver m_otaReceiver;
    
    struct PendingSend {
        MeshPacket packet;
        uint8_t currentParentIdx;
        bool active;
    } m_pendingSend;

    void handleRecv(const esp_now_recv_info_t *recv_info, const MeshPacket& packet);
    void handleSent(const esp_now_send_info_t *tx_info, esp_now_send_status_t status);
    void updateAndSendToParent(uint8_t idx);
    
    static MeshNetwork* s_instance;
};
#endif
