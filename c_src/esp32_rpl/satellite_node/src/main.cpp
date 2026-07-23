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
bool sharedHasParent = false;
uint16_t sharedRank = 0xFFFF;
uint8_t sharedParentMac[6] = {0, 0, 0, 0, 0, 0};

// Biến cục bộ phục vụ cơ chế Auto-Channel Scanning (chỉ chạy trong NetworkTask)
bool isScanning = false;
uint8_t scanChannel = 1;
unsigned long lastChannelSwitchTime = 0;
const unsigned long CHANNEL_LISTEN_TIME = 800; // ms
const unsigned long PARENT_TIMEOUT = 12000;

// Hằng số chu kỳ của các Task
const TickType_t SENSOR_PERIOD = pdMS_TO_TICKS(3000);

// Khai báo Task Handles
TaskHandle_t networkTaskHandle = NULL;
TaskHandle_t sensorTaskHandle = NULL;

// Task 1: Quản lý Định tuyến và Quét kênh (Độ ưu tiên cao - Core 1)
void networkTask(void *pvParameters) {
    unsigned long lastDioRebroadcastTime = 0;
    unsigned long lastDaoSendTime = 0;
    
    for (;;) {
        unsigned long currentMillis = millis();

        // 1. Quét dọn các parent hết hạn
        routingTable.purgeExpired(PARENT_TIMEOUT);

        // Đọc trạng thái định tuyến từ routingTable và đồng bộ sang biến shared
        bool hasParent = routingTable.hasParent();
        uint16_t rank = routingTable.getRank();
        const uint8_t* parentMac = routingTable.getParentMac();

        if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
            sharedHasParent = hasParent;
            sharedRank = rank;
            memcpy(sharedParentMac, parentMac, 6);
            xSemaphoreGive(dataMutex);
        }

        // 2. Xử lý quét kênh tự động khi mất định tuyến
        if (!hasParent) {
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

        // 3. Định kỳ phát DIO (mỗi 5 giây)
        if (hasParent && (currentMillis - lastDioRebroadcastTime >= 5000)) {
            lastDioRebroadcastTime = currentMillis;
            meshNetwork.broadcastDio();
        }

        // 4. Định kỳ gửi DAO (mỗi 4 giây)
        if (hasParent && (currentMillis - lastDaoSendTime >= 4000)) {
            lastDaoSendTime = currentMillis;
            meshNetwork.sendDao();
        }

        vTaskDelay(pdMS_TO_TICKS(100)); // Nghỉ 100ms nhường CPU
    }
}

// Task 2: Đọc cảm biến định kỳ (Độ ưu tiên trung bình - Core 0)
void sensorTask(void *pvParameters) {
    TickType_t lastWakeTime = xTaskGetTickCount();

    for (;;) {
        float temp = 0.0;
        int gas = 0;
        bool emergency = false;
        bool hasParent = false;

        // Đọc cảm biến thực tế (DS18B20 & MQ2)
        SensorService::read(temp, gas, emergency);

        // Đọc trạng thái định tuyến an toàn để điều khiển alarm
        if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
            hasParent = sharedHasParent;
            currentTemp = temp;
            currentGas = gas;
            isEmergency = emergency;
            xSemaphoreGive(dataMutex);
        }

        // Cập nhật trạng thái đèn/còi cảnh báo
        SensorService::updateAlarm(emergency, hasParent);

        // Ra lệnh gửi dữ liệu cảm biến
        meshNetwork.sendSensorData(temp, gas, emergency);

        // Chờ chính xác 3 giây
        vTaskDelayUntil(&lastWakeTime, SENSOR_PERIOD);
    }
}

void setup() {
    Serial.begin(115200);
    SensorService::init("SATELLITE");

    // Thiết lập danh sách bộ lọc láng giềng tĩnh
    routingTable.setAllowedNeighbors(ALLOWED_NEIGHBORS, sizeof(ALLOWED_NEIGHBORS) / sizeof(ALLOWED_NEIGHBORS[0]));

    if (meshNetwork.init()) {
        Serial.println("[RPL Network] Khởi động thành công.");
    } else {
        Serial.println("[RPL Network FAIL] Khởi động thất bại!");
    }

    WiFiService::forceChannel(WIFI_CHANNEL_COMMON);

    // Khởi tạo Mutex
    dataMutex = xSemaphoreCreateMutex();

    if (dataMutex != NULL) {
        // Tạo NetworkTask chạy trên Core 1 (RF core)
        xTaskCreatePinnedToCore(networkTask, "NetworkTask", 4096, NULL, 3, &networkTaskHandle, 1);
        // Tạo SensorTask chạy trên Core 0 (System core) để việc đọc cảm biến không nghẽn vô tuyến
        xTaskCreatePinnedToCore(sensorTask, "SensorTask", 4096, NULL, 2, &sensorTaskHandle, 0);
        Serial.println("[FreeRTOS] Đã phân nhiệm RPL satellite node chạy trên Core 1 và Core 0 thành công (Không OLED).");
    } else {
        Serial.println("[FreeRTOS FAIL] Lỗi tạo Mutex!");
    }
}

void loop() {
    vTaskDelete(NULL); // Giải phóng loop() mặc định
}
