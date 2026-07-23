#include <Arduino.h>
#include <WiFi.h>
#include <common_config.h>
#include <WiFiService.h>
#include <WebService.h>
#include <SensorService.h>
#include <ConfigService.h>
#include "MeshGateway.h"

// Biến trạng thái cảm biến Master
float master_temp = 26.0;
int master_gas = 0;

unsigned long lastPotentialBroadcastTime = 0;
unsigned long lastSendTime = 0;
const unsigned long POTENTIAL_BROADCAST_INTERVAL = 5000;
const unsigned long WEB_POST_INTERVAL = 3000;

String backendUrl = "";
MeshGateway meshGateway;

// ==================== SETUP ====================
void setup() {
    Serial.begin(115200);
    SensorService::init("MASTER");

    // Khởi tạo và khởi động tác vụ chạy ngầm giám sát nút BOOT (GPIO 0) bất kể nghẽn mạng
    ConfigService::startResetButtonTask(0);

    WiFi.mode(WIFI_STA);

    // Kết nối WiFi qua config portal động
    WiFiService::connectWithPortal("Embermap-Config-Portal", backendUrl);

    WiFi.mode(WIFI_STA); 
    
    // Khởi tạo Gateway Mesh
    meshGateway.init();
}

// ==================== LOOP ====================
void loop() {
    // Không cần gọi checkResetButton ở đây nữa vì đã có task FreeRTOS chạy ngầm xử lý độc lập

    unsigned long currentMillis = millis();

    bool isEmergency = false;
    SensorService::read(master_temp, master_gas, isEmergency);
    SensorService::updateAlarm(isEmergency, true);

    if (currentMillis - lastPotentialBroadcastTime >= POTENTIAL_BROADCAST_INTERVAL) {
        meshGateway.broadcastPotential();
        lastPotentialBroadcastTime = currentMillis;
    }

    if (currentMillis - lastSendTime >= WEB_POST_INTERVAL) {
        lastSendTime = currentMillis;
        
        if (WiFi.status() != WL_CONNECTED) {
            WiFi.disconnect();
            WiFi.begin(); // Tự động kết nối lại theo cấu hình đã lưu
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\n--- Pushing Mesh Readings to Web Dashboard ---");
            WebService::postReading(backendUrl.c_str(), "temp-master", "temp", master_temp);
            WebService::postReading(backendUrl.c_str(), "mq2-master", "mq2", (float)master_gas);

            WebService::postReading(backendUrl.c_str(), "temp-sat-1", "temp", meshGateway.getSatTemp(1));
            WebService::postReading(backendUrl.c_str(), "mq2-sat-1", "mq2", (float)meshGateway.getSatGas(1));

            WebService::postReading(backendUrl.c_str(), "temp-sat-2", "temp", meshGateway.getSatTemp(2));
            WebService::postReading(backendUrl.c_str(), "mq2-sat-2", "mq2", (float)meshGateway.getSatGas(2));

            WebService::postReading(backendUrl.c_str(), "temp-sat-3", "temp", meshGateway.getSatTemp(3));
            WebService::postReading(backendUrl.c_str(), "mq2-sat-3", "mq2", (float)meshGateway.getSatGas(3));
        } else {
            Serial.println("\n[Warning] WiFi offline, cannot upload to Web!");
        }
    }

    delay(30); 
}
