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

public:
    static const float TEMP_THRESHOLD;
    static const int GAS_THRESHOLD;

    static void init(const char* nodeName);
    static void read(float &temp, int &gas, bool &emergency);
    static void updateAlarm(bool emergency, bool hasRoute, float repulsivePotential = 0.0);
    static float calculateRepulsivePotential(float temp, int gas);
};

#endif // SENSOR_SERVICE_H
