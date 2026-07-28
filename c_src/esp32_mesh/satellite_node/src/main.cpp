#include <Arduino.h>
#include <SensorService.h>
#include <WiFiService.h>
#include <common_config.h>
#include <Preferences.h>
#include <nvs_flash.h>
#include "RoutingTable.h"
#include "MeshNetwork.h"
#include "NodeContext.h"
#include "NetworkTask.h"
#include "SensorTask.h"

#define DEFAULT_SATELLITE_ID 1

uint8_t loadNodeIdFromPreferences() {
    // Khởi tạo phân vùng NVS sớm trước khi đọc Preferences ở pha global constructors
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    Preferences prefs;
    prefs.begin("node-settings", true);
    uint8_t id = prefs.getUChar("node_id", DEFAULT_SATELLITE_ID);
    prefs.end();
    return id;
}

uint8_t satelliteId = loadNodeIdFromPreferences();
RoutingTable routingTable(satelliteId);
MeshNetwork meshNetwork(satelliteId, routingTable);

// Mutex bảo vệ tài nguyên dùng chung giữa 2 core
SemaphoreHandle_t dataMutex = NULL;

// Các biến trạng thái dùng chung (được bảo vệ bởi dataMutex)
float currentTemp = 0.0;
int currentGas = 0;
bool isEmergency = false;
bool sharedHasRoute = false;
float sharedCost = 999.0;
uint8_t sharedNextHop[6] = {0, 0, 0, 0, 0, 0};

// Các biến chỉ đường thoát hiểm dùng chung
bool sharedHasEvacRoute = false;
float sharedEvacPotential = 9999.0;
uint8_t sharedEvacNextHopId = 0xFF;
uint8_t sharedEvacNextHopMac[6] = {0, 0, 0, 0, 0, 0};

// Khai báo Task Handles
TaskHandle_t networkTaskHandle = NULL;
TaskHandle_t sensorTaskHandle = NULL;

void serialListenerTask(void* pvParameters) {
    for (;;) {
        if (Serial.available()) {
            String cmd = Serial.readStringUntil('\n');
            cmd.trim();
            if (cmd.startsWith("SET_NODE_ID=")) {
                uint8_t newId = cmd.substring(12).toInt();
                if (newId >= 1 && newId <= 5) {
                    Preferences prefs;
                    prefs.begin("node-settings", false);
                    prefs.putUChar("node_id", newId);
                    prefs.end();
                    Serial.printf("[Settings] Đã cập nhật Node ID thành %d. Đang khởi động lại...\n", newId);
                    delay(1000);
                    ESP.restart();
                } else {
                    Serial.println("[Settings Error] Node ID chỉ được nằm trong khoảng từ 1 đến 5!");
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// ==================== SETUP ====================
void setup() {
    Serial.begin(115200);
    SensorService::init("SATELLITE");

    // Tạo task lắng nghe cấu hình ID từ Serial Monitor (Core 0)
    xTaskCreatePinnedToCore(serialListenerTask, "SerialListener", 2048, NULL, 1, NULL, 0);

    // Tìm và áp dụng cấu hình từ bảng Topology trung tâm trong common_config.h
    bool foundConfig = false;
    for (size_t i = 0; i < sizeof(TOPOLOGY_MAP) / sizeof(TOPOLOGY_MAP[0]); i++) {
        if (TOPOLOGY_MAP[i].nodeId == satelliteId) {
            routingTable.setAllowedNeighbors(TOPOLOGY_MAP[i].allowedRadio, TOPOLOGY_MAP[i].allowedRadioCount);
            routingTable.setPhysicalNeighbors(TOPOLOGY_MAP[i].physIds, TOPOLOGY_MAP[i].physDists, TOPOLOGY_MAP[i].physCount);
            foundConfig = true;
            Serial.printf("[Setup] Đã nhận cấu hình topology cho Node %d từ common_config.h\n", satelliteId);
            break;
        }
    }
    
    if (!foundConfig) {
        Serial.printf("[Setup Warning] Không tìm thấy cấu hình cho Node %d trong TOPOLOGY_MAP. Dùng cấu hình mặc định (kết nối trực tiếp Master).\n", satelliteId);
        uint8_t defaultRadio[] = {0};
        uint8_t defaultPhys[] = {0};
        float defaultDist[] = {10.0};
        routingTable.setAllowedNeighbors(defaultRadio, 1);
        routingTable.setPhysicalNeighbors(defaultPhys, defaultDist, 1);
    }

    if (meshNetwork.init()) {
        Serial.println("[Mesh Network] Khởi động thành công.");
    } else {
        Serial.println("[Mesh Network FAIL] Khởi động thất bại!");
    }

    WiFiService::forceChannel(WIFI_CHANNEL_COMMON);

    // Khởi tạo Mutex bảo vệ dữ liệu giữa 2 Core
    dataMutex = xSemaphoreCreateMutex();

    if (dataMutex != NULL) {
        // Tạo NetworkTask chạy trên Core 1 (RF core)
        xTaskCreatePinnedToCore(networkTask, "NetworkTask", 4096, NULL, 3, &networkTaskHandle, 1);
        
        // Tạo SensorTask chạy trên Core 0 (System core)
        xTaskCreatePinnedToCore(sensorTask, "SensorTask", 4096, NULL, 2, &sensorTaskHandle, 0);
        
        Serial.printf("[FreeRTOS] Đã phân chia đa nhiệm cho Node %d chạy song song Core 1 & Core 0 thành công (Không OLED).\n", satelliteId);
    } else {
        Serial.println("[FreeRTOS FAIL] Lỗi tạo Mutex!");
    }
}

void loop() {
    vTaskDelete(NULL);
}
