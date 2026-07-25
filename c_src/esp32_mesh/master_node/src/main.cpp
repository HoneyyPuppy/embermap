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

unsigned long lastRouteBroadcastTime = 0;
unsigned long lastEvacBroadcastTime = 0;
unsigned long lastSendTime = 0;
const unsigned long ROUTE_BROADCAST_INTERVAL = 5000;
const unsigned long EVAC_BROADCAST_INTERVAL = 3000;
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

    // Khởi động Web Server hoạt động bình thường phục vụ Dashboard và OTA
    ConfigService::startNormalWebServer([]() {
        String json = "{";
        
        // Trạng thái Master Node
        bool masterEmergency = (master_gas >= 400 || master_temp >= 55.0);
        json += "\"master\": {";
        json += "\"temp\":" + String(master_temp) + ",";
        json += "\"gas\":" + String(master_gas) + ",";
        json += "\"emergency\":" + String(masterEmergency ? "true" : "false");
        json += "},";
        
        // Trạng thái các Satellite Nodes
        json += "\"satellites\": [";
        for (int i = 1; i <= 5; i++) {
            bool active = false;
            const uint8_t* mac = meshGateway.getSatMac(i);
            if (mac) {
                for (int j = 0; j < 6; j++) {
                    if (mac[j] != 0) { active = true; break; }
                }
            }
            
            float temp = meshGateway.getSatTemp(i);
            int gas = meshGateway.getSatGas(i);
            bool emergency = (gas >= 400 || temp >= 55.0);
            
            json += "{";
            json += "\"id\":" + String(i) + ",";
            json += "\"temp\":" + String(temp) + ",";
            json += "\"gas\":" + String(gas) + ",";
            json += "\"active\":" + String(active ? "true" : "false") + ",";
            json += "\"emergency\":" + String(emergency ? "true" : "false") + ",";
            json += "\"nextHopId\":0,";
            json += "\"pot\":0.0";
            json += "}";
            if (i < 5) json += ",";
        }
        json += "]";
        
        json += "}";
        return json;
    });
}

uint8_t sequentialOtaTarget = 0; // 0: tắt, 1-5: Node đang được cập nhật tuần tự

// ==================== LOOP ====================
void loop() {
    // Xử lý các yêu cầu Web Client gửi tới Gateway
    ConfigService::handleNormalWebServer();

    // Xử lý tiến trình truyền tải OTA qua ESP-NOW
    meshGateway.processOtaTransmission();

    unsigned long currentMillis = millis();

    bool isEmergency = false;
    SensorService::read(master_temp, master_gas, isEmergency);
    SensorService::updateAlarm(isEmergency, true);

    // Kiểm tra xem Web Server có yêu cầu truyền OTA vô tuyến không
    if (ConfigService::isSatelliteOtaPending()) {
        uint8_t targetSatId = ConfigService::getOtaTargetSatId();
        if (targetSatId == 0xFE) { // Tất cả các vệ tinh tuần tự
            Serial.println("[OTA Master] Bắt đầu nâng cấp tuần tự toàn mạng...");
            sequentialOtaTarget = 1;
            while (sequentialOtaTarget <= 5) {
                const uint8_t* mac = meshGateway.getSatMac(sequentialOtaTarget);
                bool hasMac = false;
                if (mac) {
                    for (int j = 0; j < 6; j++) {
                        if (mac[j] != 0) { hasMac = true; break; }
                    }
                }
                if (hasMac) {
                    meshGateway.startOtaUpdate(sequentialOtaTarget);
                    break;
                }
                sequentialOtaTarget++;
            }
            if (sequentialOtaTarget > 5) {
                Serial.println("[OTA Master] Không tìm thấy vệ tinh nào trực tuyến!");
                sequentialOtaTarget = 0;
            }
        } else {
            sequentialOtaTarget = 0;
            meshGateway.startOtaUpdate(targetSatId);
        }
        ConfigService::clearSatelliteOtaPending();
    }

    // Nếu đang chạy nâng cấp tuần tự, tự động chuyển sang nút tiếp theo khi nút trước hoàn thành
    if (sequentialOtaTarget > 0 && sequentialOtaTarget <= 5) {
        if (!meshGateway.isOtaActive()) {
            sequentialOtaTarget++;
            while (sequentialOtaTarget <= 5) {
                const uint8_t* mac = meshGateway.getSatMac(sequentialOtaTarget);
                bool hasMac = false;
                if (mac) {
                    for (int j = 0; j < 6; j++) {
                        if (mac[j] != 0) { hasMac = true; break; }
                    }
                }
                if (hasMac) {
                    meshGateway.startOtaUpdate(sequentialOtaTarget);
                    break;
                }
                sequentialOtaTarget++;
            }
            if (sequentialOtaTarget > 5) {
                Serial.println("[OTA Master] Hoàn thành cập nhật phần mềm toàn hệ thống vệ tinh!");
                sequentialOtaTarget = 0;
            }
        }
    }

    if (currentMillis - lastRouteBroadcastTime >= ROUTE_BROADCAST_INTERVAL) {
        meshGateway.broadcastRouteUpdate();
        lastRouteBroadcastTime = currentMillis;
    }

    if (currentMillis - lastEvacBroadcastTime >= EVAC_BROADCAST_INTERVAL) {
        // Master Node là cửa thoát hiểm chính nên U_evac luôn = 0.0
        meshGateway.broadcastEvacPotential(0.0);
        lastEvacBroadcastTime = currentMillis;
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

            WebService::postReading(backendUrl.c_str(), "temp-sat-4", "temp", meshGateway.getSatTemp(4));
            WebService::postReading(backendUrl.c_str(), "mq2-sat-4", "mq2", (float)meshGateway.getSatGas(4));

            WebService::postReading(backendUrl.c_str(), "temp-sat-5", "temp", meshGateway.getSatTemp(5));
            WebService::postReading(backendUrl.c_str(), "mq2-sat-5", "mq2", (float)meshGateway.getSatGas(5));
        } else {
            Serial.println("\n[Warning] WiFi offline, cannot upload to Web!");
        }
    }

    delay(30); 
}
