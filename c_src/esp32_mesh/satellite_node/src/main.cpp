#include <Arduino.h>
#include <SensorService.h>
#include <WiFiService.h>
#include <common_config.h>
#include "RoutingTable.h"
#include "MeshNetwork.h"

// ID của Satellite Node này
#define SATELLITE_ID 2

// Cấu hình láng giềng tĩnh (0xFF = Tự động bắt mọi láng giềng dựa trên RSSI)
const uint8_t ALLOWED_NEIGHBORS[] = {0xFF};

RoutingTable routingTable(SATELLITE_ID);
MeshNetwork meshNetwork(SATELLITE_ID, routingTable);

// Mutex bảo vệ tài nguyên dùng chung
SemaphoreHandle_t dataMutex = NULL;

// Các biến trạng thái dùng chung (được bảo vệ bởi dataMutex)
float currentTemp = 0.0;
int currentGas = 0;
bool isEmergency = false;
bool sharedHasRoute = false;
float sharedCost = 999.0;
uint8_t sharedNextHop[6] = {0, 0, 0, 0, 0, 0};

// Biến cục bộ phục vụ cơ chế Auto-Channel Scanning (chỉ chạy trong NetworkTask)
bool isScanning = false;
uint8_t scanChannel = 1;
unsigned long lastChannelSwitchTime = 0;
const unsigned long CHANNEL_LISTEN_TIME = 800; // ms
const unsigned long ROUTE_TIMEOUT = 12000;

// Hằng số chu kỳ của các Task
const TickType_t SENSOR_PERIOD = pdMS_TO_TICKS(3000);
const TickType_t DISPLAY_PERIOD = pdMS_TO_TICKS(1000);

// Khai báo Task Handles
TaskHandle_t networkTaskHandle = NULL;
TaskHandle_t sensorTaskHandle = NULL;
TaskHandle_t displayTaskHandle = NULL;

// Task 1: Quản lý Định tuyến và Quét kênh (Độ ưu tiên cao - Core 1)
void networkTask(void *pvParameters) {
    unsigned long lastRouteBroadcastTime = 0;
    
    for (;;) {
        unsigned long currentMillis = millis();

        // 1. Quét dọn các parent hết hạn
        routingTable.purgeExpired(ROUTE_TIMEOUT);

        // Đọc trạng thái định tuyến từ routingTable và đồng bộ sang biến shared
        bool hasRoute = routingTable.hasRoute();
        float cost = routingTable.getCost();
        const uint8_t* nextHop = routingTable.getNextHopMac();

        if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
            sharedHasRoute = hasRoute;
            sharedCost = cost;
            memcpy(sharedNextHop, nextHop, 6);
            xSemaphoreGive(dataMutex);
        }

        // 2. Xử lý quét kênh tự động khi mất định tuyến
        if (!hasRoute) {
            if (!isScanning) {
                isScanning = true;
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

        // 3. Định kỳ phát quảng bá tuyến định tuyến
        if (hasRoute && (currentMillis - lastRouteBroadcastTime >= 5000)) {
            lastRouteBroadcastTime = currentMillis;
            meshNetwork.rebroadcastRouteUpdate();
        }

        vTaskDelay(pdMS_TO_TICKS(100)); // Nghỉ 100ms nhường CPU
    }
}

// Task 2: Đọc cảm biến định kỳ (Độ ưu tiên trung bình - Core 1)
void sensorTask(void *pvParameters) {
    TickType_t lastWakeTime = xTaskGetTickCount();

    for (;;) {
        float temp = 0.0;
        int gas = 0;
        bool emergency = false;
        bool hasRoute = false;

        // Đọc cảm biến thực tế (DS18B20 & MQ2)
        SensorService::read(temp, gas, emergency);

        // Đọc trạng thái định tuyến an toàn để điều khiển alarm
        if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
            hasRoute = sharedHasRoute;
            currentTemp = temp;
            currentGas = gas;
            isEmergency = emergency;
            xSemaphoreGive(dataMutex);
        }

        // Cập nhật trạng thái đèn/còi cảnh báo
        SensorService::updateAlarm(emergency, hasRoute);

        // Ra lệnh gửi dữ liệu cảm biến
        meshNetwork.sendSensorData(temp, gas, emergency);

        // Chờ chính xác 3 giây (chu kỳ cố định)
        vTaskDelayUntil(&lastWakeTime, SENSOR_PERIOD);
    }
}

// Task 3: Hiển thị OLED (Độ ưu tiên thấp - Core 0)
void displayTask(void *pvParameters) {
    TickType_t lastWakeTime = xTaskGetTickCount();

    for (;;) {
        float temp = 0.0;
        int gas = 0;
        bool hasRoute = false;
        float cost = 999.0;
        uint8_t nextHop[6] = {0};

        // Đọc dữ liệu an toàn để hiển thị OLED
        if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
            temp = currentTemp;
            gas = currentGas;
            hasRoute = sharedHasRoute;
            cost = sharedCost;
            memcpy(nextHop, sharedNextHop, 6);
            xSemaphoreGive(dataMutex);
        }

        // Vẽ giao diện màn hình OLED
        SensorService::displaySatellite("Mesh", SATELLITE_ID, temp, gas, hasRoute, cost, nextHop);

        // Chờ chính xác 1 giây
        vTaskDelayUntil(&lastWakeTime, DISPLAY_PERIOD);
    }
}

void setup() {
    Serial.begin(115200);
    SensorService::init("SATELLITE");

    // Thiết lập danh sách bộ lọc láng giềng tĩnh
    routingTable.setAllowedNeighbors(ALLOWED_NEIGHBORS, sizeof(ALLOWED_NEIGHBORS) / sizeof(ALLOWED_NEIGHBORS[0]));

    if (meshNetwork.init()) {
        Serial.println("[Mesh Network] Khởi động thành công.");
    } else {
        Serial.println("[Mesh Network FAIL] Khởi động thất bại!");
    }

    WiFiService::forceChannel(WIFI_CHANNEL_COMMON);

    // Khởi tạo Mutex
    dataMutex = xSemaphoreCreateMutex();

    if (dataMutex != NULL) {
        // Tạo các tác vụ chạy song song
        xTaskCreatePinnedToCore(networkTask, "NetworkTask", 4096, NULL, 3, &networkTaskHandle, 1);
        xTaskCreatePinnedToCore(sensorTask, "SensorTask", 4096, NULL, 2, &sensorTaskHandle, 1);
        xTaskCreatePinnedToCore(displayTask, "DisplayTask", 4096, NULL, 1, &displayTaskHandle, 0);
        Serial.println("[FreeRTOS] Đã khởi tạo các Tasks đa nhiệm thành công.");
    } else {
        Serial.println("[FreeRTOS FAIL] Lỗi tạo Mutex!");
    }
}

void loop() {
    // Vòng loop trống vì FreeRTOS đã quản lý toàn bộ tác vụ thông qua các Task
    vTaskDelete(NULL); // Xóa task loop() mặc định để giải phóng tài nguyên CPU
}
