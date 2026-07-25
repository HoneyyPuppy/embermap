#include <Arduino.h>
#include <SensorService.h>
#include <WiFiService.h>
#include <common_config.h>
#include <Preferences.h>
#include <nvs_flash.h>
#include "RoutingTable.h"
#include "MeshNetwork.h"

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

// Biến cục bộ phục vụ cơ chế Auto-Channel Scanning (chỉ chạy trong NetworkTask)
bool isScanning = false;
uint8_t scanChannel = 1;
unsigned long lastChannelSwitchTime = 0;
const unsigned long CHANNEL_LISTEN_TIME = 800; // ms
const unsigned long ROUTE_TIMEOUT = 12000;

// Hằng số chu kỳ của các Task
const TickType_t SENSOR_PERIOD = pdMS_TO_TICKS(3000);

// Khai báo Task Handles
TaskHandle_t networkTaskHandle = NULL;
TaskHandle_t sensorTaskHandle = NULL;

// ==================== TASK 1: NETWORK TASK (Core 1) ====================
void networkTask(void *pvParameters) {
    unsigned long lastRouteBroadcastTime = 0;
    unsigned long lastEvacBroadcastTime = 0;
    
    for (;;) {
        // Tạm dừng mọi hoạt động định tuyến và chuyển kênh khi đang chạy OTA
        if (meshNetwork.isOtaActive()) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        unsigned long currentMillis = millis();

        // 1. Quét dọn các parent hết hạn
        routingTable.purgeExpired(ROUTE_TIMEOUT);

        // Đọc trạng thái định tuyến từ routingTable và đồng bộ sang biến shared
        bool hasRoute = routingTable.hasRoute();
        float cost = routingTable.getCost();
        const uint8_t* nextHop = routingTable.getNextHopMac();

        // 2. Tính toán định tuyến thoát hiểm con người (APF độc lập)
        float tempVal = 27.2;
        int gasVal = 120;
        if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
            tempVal = currentTemp;
            gasVal = currentGas;
            xSemaphoreGive(dataMutex);
        }

        float localRepulsive = SensorService::calculateRepulsivePotential(tempVal, gasVal);

        float localEvacPotential = 9999.0;
        uint8_t evacNextHopId = 0xFF;
        uint8_t evacNextHopMac[6] = {0};
        
        bool safeEscape = routingTable.calculateEvacuation(localRepulsive, localEvacPotential, evacNextHopId, evacNextHopMac);

        // Đồng bộ dữ liệu sang Core 0 chạy an toàn
        if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
            sharedHasRoute = hasRoute;
            sharedCost = cost;
            memcpy(sharedNextHop, nextHop, 6);

            sharedHasEvacRoute = safeEscape;
            sharedEvacPotential = localEvacPotential;
            sharedEvacNextHopId = evacNextHopId;
            memcpy(sharedEvacNextHopMac, evacNextHopMac, 6);
            xSemaphoreGive(dataMutex);
        }

        // 3. Xử lý quét kênh tự động khi mất định tuyến vô tuyến
        if (!hasRoute) {
            if (!isScanning) {
                isScanning = true;
                routingTable.clearCandidates(); // Xóa sạch candidate cũ ngay khi quét kênh để tránh phát quảng bá ảo trên kênh khác
                scanChannel = 1;
                lastChannelSwitchTime = currentMillis - CHANNEL_LISTEN_TIME;
                meshNetwork.stopScanningAck();
                Serial.println("[Scan] Mất định tuyến. Bắt đầu chế độ tự động quét kênh sóng...");
            }

            if (isScanning && (currentMillis - lastChannelSwitchTime >= CHANNEL_LISTEN_TIME)) {
                scanChannel = (scanChannel % 11) + 1;
                WiFiService::forceChannel(scanChannel);
                meshNetwork.sendRouteRequest(scanChannel);
                lastChannelSwitchTime = currentMillis;
            }
        } else {
            if (isScanning) {
                isScanning = false;
                Serial.printf("[Scan] Đã tìm thấy tuyến đường trên Kênh %d! Khóa kênh hoạt động.\n", scanChannel);
            }
        }

        // 4. Định kỳ phát quảng bá tuyến mạng (Gradient)
        if (hasRoute && (currentMillis - lastRouteBroadcastTime >= 5000)) {
            lastRouteBroadcastTime = currentMillis;
            meshNetwork.rebroadcastRouteUpdate();
        }

        // 5. Định kỳ quảng bá thế năng thoát hiểm (APF thoát hiểm)
        if (currentMillis - lastEvacBroadcastTime >= 3000) {
            lastEvacBroadcastTime = currentMillis;
            meshNetwork.broadcastEvacPotential(routingTable.getMyEvacPotential());
            
            uint8_t dataNextHopId = routingTable.getDataNextHopId();
            Serial.printf("[Path Monitor] Node %d | Duong gui tin (Data NextHop): %s | Duong thoat hiem (Evac NextHop): %s | U_evac = %.1f\n",
                          satelliteId,
                          (dataNextHopId == 0xFF ? "MAT TUYEN" : (dataNextHopId == 0xFE ? "CHUA HOC MAC" : String(dataNextHopId).c_str())),
                          (evacNextHopId == 0xFF ? "MAT TUYEN" : String(evacNextHopId).c_str()),
                          routingTable.getMyEvacPotential());
        }

        vTaskDelay(pdMS_TO_TICKS(100)); // Nghỉ 100ms
    }
}

// ==================== TASK 2: SENSOR & ALARM TASK (Core 0) ====================
void sensorTask(void *pvParameters) {
    TickType_t lastWakeTime = xTaskGetTickCount();

    for (;;) {
        float temp = 0.0;
        int gas = 0;
        bool emergency = false;

        // Đọc cảm biến thực tế (DS18B20 & MQ2)
        SensorService::read(temp, gas, emergency);

        if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
            currentTemp = temp;
            currentGas = gas;
            isEmergency = emergency;
            xSemaphoreGive(dataMutex);
        }

        // Quyết định trạng thái LED NeoPixel dựa vào định tuyến thoát hiểm
        float repulsivePot = 0.0;
        
        // Nếu cách Master xa hơn khoảng cách chuẩn (do phải đi vòng tránh lửa ở hành lang khác) -> Sáng đèn Vàng
        if (satelliteId == 1 && sharedEvacPotential > 10.0 && sharedEvacPotential < 9999.0) repulsivePot = 1.0;
        else if (satelliteId == 2 && sharedEvacPotential > 18.0 && sharedEvacPotential < 9999.0) repulsivePot = 1.0;
        else if (satelliteId == 3 && sharedEvacPotential > 22.0 && sharedEvacPotential < 9999.0) repulsivePot = 1.0;
        else if (satelliteId == 4 && sharedEvacPotential > 12.0 && sharedEvacPotential < 9999.0) repulsivePot = 1.0;
        else if (satelliteId == 5 && sharedEvacPotential > 37.0 && sharedEvacPotential < 9999.0) repulsivePot = 1.0;

        if (emergency || sharedEvacPotential >= 9999.0) {
            // Đỏ nhấp nháy: Bản thân bị cháy hoặc bị kẹt hoàn toàn không lối thoát
            SensorService::updateAlarm(true, sharedHasEvacRoute, repulsivePot);
            if (sharedEvacPotential >= 9999.0) {
                Serial.println("[Evac Alert] BỊ KẸT HOÀN TOÀN! Lối thoát hiểm đã bị lửa chặn đứng.");
            }
        } else {
            // Xanh (An toàn tuyệt đối) hoặc Vàng (Cảnh báo đi vòng tránh lửa ở xa)
            SensorService::updateAlarm(false, sharedHasEvacRoute, repulsivePot);
        }

        // Gửi telemetry về Master qua lớp mạng Gradient nếu không chạy OTA
        if (!meshNetwork.isOtaActive()) {
            meshNetwork.sendSensorData(temp, gas, emergency);
        }

        vTaskDelayUntil(&lastWakeTime, SENSOR_PERIOD);
    }
}

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
