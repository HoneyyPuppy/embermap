#include "WiFiService.h"
#include <esp_wifi.h>
#include "ConfigStorage.h"
#include "CaptivePortal.h"

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

bool WiFiService::connectWithPortal(const char* portalSsid, String &loadedUrl) {
    ConfigStorage::init();
    String ssid, pass, url;
    
    if (ConfigStorage::loadConfig(ssid, pass, url)) {
        Serial.printf("Connecting to Saved WiFi: %s\n", ssid.c_str());
        WiFi.begin(ssid.c_str(), pass.c_str());
        unsigned long startAttemptTime = millis();
        
        while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 15000) { // 15s timeout
            delay(500);
            Serial.print(".");
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\n[WIFI OK] IP: " + WiFi.localIP().toString());
            Serial.printf("==> WiFi Channel: %d\n", WiFi.channel());
            loadedUrl = url;
            return true;
        }
        Serial.println("\n[WIFI FAIL] Could not connect to saved WiFi router.");
    } else {
        Serial.println("[Config] No saved configuration found.");
    }
    
    // Khởi động Portal nếu kết nối lỗi hoặc chưa có cấu hình
    CaptivePortal::start(portalSsid);
    
    // Lặp liên tục xử lý yêu cầu của Portal cho tới khi tự reset
    while (CaptivePortal::isActive()) {
        CaptivePortal::handle();
        delay(10);
    }
    return false;
}

void WiFiService::forceChannel(uint8_t channel) {
    WiFi.mode(WIFI_STA);
    esp_wifi_start();
    esp_err_t err = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    Serial.printf("Force WiFi Channel %d status: %s\n", channel, esp_err_to_name(err));
}
