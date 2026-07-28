#ifndef MASTER_BEACON_H
#define MASTER_BEACON_H

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <mesh_packet.h>

class MasterBeacon {
public:
    MasterBeacon();
    void init(const uint8_t* myMac, const uint8_t* broadcastMac);
    void broadcastRouteUpdate();
    void broadcastEvacPotential(float potential);
private:
    uint8_t m_myMac[6];
    uint8_t m_broadcastMac[6];
};

#endif // MASTER_BEACON_H
