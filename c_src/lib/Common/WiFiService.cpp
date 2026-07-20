#include "WiFiService.h"
#include <esp_wifi.h>

void WiFiService::connect(const char* ssid, const char* password) {
    Serial.printf("Connecting to WiFi: %s\n", ssid);
    WiFi.begin(ssid, password);
    unsigned long startAttemptTime = millis();
    
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
        delay(500);
        Serial.print(".");
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[WIFI OK] IP: " + WiFi.localIP().toString());
        Serial.printf("==> WiFi Channel: %d\n", WiFi.channel());
    } else {
        Serial.println("\n[WIFI FAIL] Could not connect to WiFi. Running offline mode...");
    }
}

void WiFiService::forceChannel(uint8_t channel) {
    WiFi.mode(WIFI_STA);
    esp_wifi_start();
    esp_err_t err = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    Serial.printf("Force WiFi Channel %d status: %s\n", channel, esp_err_to_name(err));
}
