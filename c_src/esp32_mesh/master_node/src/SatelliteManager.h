#ifndef SATELLITE_MANAGER_H
#define SATELLITE_MANAGER_H

#include <Arduino.h>

class SatelliteManager {
public:
    SatelliteManager();
    void updateSensorData(uint8_t nodeId, float temp, int gas, const uint8_t* senderMac);
    float getTemp(uint8_t nodeId) const;
    int getGas(uint8_t nodeId) const;
    const uint8_t* getMac(uint8_t nodeId) const;
private:
    float m_temp[6];
    int m_gas[6];
    uint8_t m_mac[6][6];
};

#endif // SATELLITE_MANAGER_H
