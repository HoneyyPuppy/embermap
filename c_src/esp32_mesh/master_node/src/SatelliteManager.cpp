#include "SatelliteManager.h"

SatelliteManager::SatelliteManager() {
    for (int i = 0; i < 6; i++) {
        m_temp[i] = 26.0;
        m_gas[i] = 0;
        memset(m_mac[i], 0, 6);
    }
}

void SatelliteManager::updateSensorData(uint8_t nodeId, float temp, int gas, const uint8_t* senderMac) {
    if (nodeId < 6) {
        m_temp[nodeId] = temp;
        m_gas[nodeId] = gas;
        if (senderMac) {
            memcpy(m_mac[nodeId], senderMac, 6);
        }
    }
}

float SatelliteManager::getTemp(uint8_t nodeId) const {
    if (nodeId < 6) return m_temp[nodeId];
    return 26.0;
}

int SatelliteManager::getGas(uint8_t nodeId) const {
    if (nodeId < 6) return m_gas[nodeId];
    return 0;
}

const uint8_t* SatelliteManager::getMac(uint8_t nodeId) const {
    if (nodeId < 6) return m_mac[nodeId];
    return nullptr;
}
