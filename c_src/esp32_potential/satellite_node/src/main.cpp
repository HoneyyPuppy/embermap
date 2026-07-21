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
float repulsivePotential = 0.0;
float totalPotential = 9999.0;
uint8_t sharedNextHop[6] = {0, 0, 0, 0, 0, 0};

// Biến cục bộ phục vụ cơ chế Auto-Channel Scanning (chỉ chạy trong NetworkTask)
bool isScanning = false;
uint8_t scanChannel = 1;
unsigned long lastChannelSwitchTime = 0;
const unsigned long CHANNEL_LISTEN_TIME = 800; // ms
const unsigned long NEIGHBOR_TIMEOUT = 12000;

// Hằng số chu kỳ của các Task
const TickType_t SENSOR_PERIOD = pdMS_TO_TICKS(3000);
const TickType_t DISPLAY_PERIOD = pdMS_TO_TICKS(1000);

// Khai báo Task Handles
TaskHandle_t networkTaskHandle = NULL;
TaskHandle_t sensorTaskHandle = NULL;
TaskHandle_t displayTaskHandle = NULL;

// Task 1: Quản lý Định tuyến và Quét kênh (Độ ưu tiên cao - Core 1)
void networkTask(void *pvParameters) {
    unsigned long lastPotentialBroadcastTime = 0;
    
    for (;;) {
        unsigned long currentMillis = millis();

        // 1. Quét dọn các parent hết hạn
        routingTable.purgeExpired(NEIGHBOR_TIMEOUT);

        // Đọc các giá trị cảm biến hiện tại dưới khóa bảo vệ để tính toán thế năng định tuyến
        float localTemp = 0.0;
        int localGas = 0;
        if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
            localTemp = currentTemp;
            localGas = currentGas;
            xSemaphoreGive(dataMutex);
        }

        // Tính toán thế năng chướng ngại vật (repulsive potential)
        float rep = 0.0;
        if (localTemp > 45.0) {
            rep += (localTemp - 40.0) * 100.0;
        }
        if (localGas > 200) {
            rep += (localGas - 200) * 3.0;
        }

        // Tính toán thế năng cơ bản dựa trên Hop Count
        float basePot = 9999.0;
        if (routingTable.getHopCount() != 255) {
            basePot = routingTable.getHopCount() * 100.0;
        }
        float localTotalPotential = basePot + rep;

        // Tìm Next Hop láng giềng có thế năng hiệu dụng thấp nhất
        bool hasRoute = routingTable.selectNextHop(localTotalPotential);
        const uint8_t* nextHop = routingTable.getNextHopMac();

        // Đồng bộ trạng thái định tuyến và thế năng tính toán sang các biến shared
        if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
            sharedHasRoute = hasRoute;
            repulsivePotential = rep;
            totalPotential = hasRoute ? localTotalPotential : 9999.0;
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

        // 3. Định kỳ phát quảng bá thế năng (mỗi 5 giây)
        if (hasRoute && (currentMillis - lastPotentialBroadcastTime >= 5000)) {
            lastPotentialBroadcastTime = currentMillis;
            meshNetwork.broadcastPotential(localTotalPotential);
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
        float sendPotential = 9999.0;

        // Đọc cảm biến thực tế (DS18B20 & MQ2)
        SensorService::read(temp, gas, emergency);

        // Đọc trạng thái định tuyến an toàn để gửi tin và điều khiển alarm
        if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
            currentTemp = temp;
            currentGas = gas;
            isEmergency = emergency;
            hasRoute = sharedHasRoute;
            sendPotential = totalPotential;
            xSemaphoreGive(dataMutex);
        }

        // Cập nhật trạng thái alarm dựa trên repulsivePotential
        float rep = 0.0;
        if (temp > 45.0) {
            rep += (temp - 40.0) * 100.0;
        }
        if (gas > 200) {
            rep += (gas - 200) * 3.0;
        }
        SensorService::updateAlarm(emergency, hasRoute, rep);

        // Ra lệnh gửi dữ liệu cảm biến kèm thế năng định tuyến
        meshNetwork.sendSensorData(temp, gas, emergency, sendPotential);

        // Chờ chính xác 3 giây
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
        float potVal = 9999.0;
        uint8_t nextHop[6] = {0};

        // Đọc dữ liệu an toàn để hiển thị OLED
        if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
            temp = currentTemp;
            gas = currentGas;
            hasRoute = sharedHasRoute;
            potVal = totalPotential;
            memcpy(nextHop, sharedNextHop, 6);
            xSemaphoreGive(dataMutex);
        }

        // Vẽ giao diện màn hình OLED
        SensorService::displaySatellite("APF", SATELLITE_ID, temp, gas, hasRoute, potVal, nextHop);

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
        Serial.println("[APF Network] Khởi động thành công.");
    } else {
        Serial.println("[APF Network FAIL] Khởi động thất bại!");
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
    vTaskDelete(NULL); // Giải phóng loop() mặc định
}
