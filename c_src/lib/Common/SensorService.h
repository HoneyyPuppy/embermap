#ifndef SENSOR_SERVICE_H
#define SENSOR_SERVICE_H

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_NeoPixel.h>

class SensorService {
private:
    static OneWire oneWire;
    static DallasTemperature sensors;
    static Adafruit_NeoPixel pixels;

    // Các biến trạng thái điều hướng LED
    static bool m_emergency;
    static bool m_hasRoute;
    static float m_repulsivePotential;
    static uint8_t m_satelliteId;
    static uint8_t m_nextHopId;
    static uint8_t m_targetNeighbor;     // ID láng giềng ở đầu kia của dải LED
    static uint8_t m_neighborNextHopId; // Lối thoát hiểm của Node láng giềng ở đầu kia dải LED
    static TaskHandle_t m_ledTaskHandle;

    static int m_simulatedGas;
    static bool m_useSimulatedGas;
    
    static void ledAnimationTask(void* pvParameters);

public:
    static const float TEMP_THRESHOLD;
    static const int GAS_THRESHOLD;

    static void calibrateMq2();
    static void setSimulatedGas(int gasVal);
    static void disableSimulatedGas();

    static void init(const char* nodeName);
    static void read(float &temp, int &gas, bool &emergency);
    static void updateAlarm(bool emergency, bool hasRoute, float repulsivePotential = 0.0, uint8_t satelliteId = 0, uint8_t nextHopId = 255, uint8_t targetNeighbor = 255, uint8_t neighborNextHopId = 255);
    static float calculateRepulsivePotential(float temp, int gas);
};

#endif // SENSOR_SERVICE_H
