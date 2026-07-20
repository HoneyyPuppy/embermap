#ifndef WEB_SERVICE_H
#define WEB_SERVICE_H

#include <Arduino.h>

class WebService {
public:
    static void postReading(const char* serverUrl, const char* deviceId, const char* sensorType, float value);
};

#endif // WEB_SERVICE_H
