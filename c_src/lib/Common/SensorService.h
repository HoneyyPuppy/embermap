#ifndef SENSOR_SERVICE_H
#define SENSOR_SERVICE_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_NeoPixel.h>

class SensorService {
private:
    static Adafruit_SSD1306 display;
    static OneWire oneWire;
    static DallasTemperature sensors;
    static Adafruit_NeoPixel pixels;
    static bool oledConnected;

public:
    static const float TEMP_THRESHOLD;
    static const int GAS_THRESHOLD;

    static void init(const char* nodeName);
    static void read(float &temp, int &gas, bool &emergency);
    static void updateAlarm(bool emergency, bool hasRoute, float repulsivePotential = 0.0);
    static void displayMaster(const char* protocolName, float temp, int gas, bool wifiConnected, const String &ipAddress);
    static void displaySatellite(const char* protocolName, int nodeId, float temp, int gas, bool hasRoute, float routingInfo, const uint8_t *nextHopMac);
};

#endif // SENSOR_SERVICE_H
