#ifndef WIFI_SERVICE_H
#define WIFI_SERVICE_H

#include <Arduino.h>
#include <WiFi.h>

class WiFiService {
public:
    static void connect(const char* ssid, const char* password);
    static void forceChannel(uint8_t channel);
};

#endif // WIFI_SERVICE_H
