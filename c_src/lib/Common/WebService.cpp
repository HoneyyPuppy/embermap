#include "WebService.h"
#include <HTTPClient.h>
#include <WiFi.h>

void WebService::postReading(const char* serverUrl, const char* deviceId, const char* sensorType, float value) {
    if (WiFi.status() != WL_CONNECTED) return;
    
    HTTPClient http;
    http.begin(serverUrl);
    http.setConnectTimeout(1000);
    http.setTimeout(1000);
    http.addHeader("Content-Type", "application/json");

    String body = "{";
    body += "\"buildingCode\":\"B01\",";
    body += "\"deviceId\":\""     + String(deviceId)      + "\",";
    body += "\"sensorType\":\""   + String(sensorType)    + "\",";
    body += "\"value\":"          + String(value, 2)      + ",";
    body += "\"unit\":\""         + String(sensorType == "temp" ? "C" : "raw") + "\"";
    body += "}";

    int httpCode = http.POST(body);
    http.end();

    if (httpCode == 200) {
        Serial.printf("[Web OK] %s = %.2f\n", deviceId, value);
    } else {
        Serial.printf("[Web FAIL] %s code = %d\n", deviceId, httpCode);
    }
}
