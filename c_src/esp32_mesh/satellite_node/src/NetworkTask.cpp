#include "NetworkTask.h"
#include "NodeContext.h"
#include "MeshNetwork.h"
#include <SensorService.h>
#include <WiFiService.h>

static bool isScanning = false;
static uint8_t scanChannel = 1;
static unsigned long lastChannelSwitchTime = 0;
static const unsigned long CHANNEL_LISTEN_TIME = 800; // ms
static const unsigned long ROUTE_TIMEOUT = 12000;

static unsigned long lastRouteBroadcastTime = 0;
static unsigned long lastEvacBroadcastTime = 0;

void networkTask(void *pvParameters) {
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
                meshNetwork.router().sendRouteRequest(scanChannel);
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
            meshNetwork.router().rebroadcastRouteUpdate();
        }

        // 5. Định kỳ quảng bá thế năng thoát hiểm (APF thoát hiểm)
        if (currentMillis - lastEvacBroadcastTime >= 3000) {
            lastEvacBroadcastTime = currentMillis;
            meshNetwork.evacProtocol().broadcastPotential(routingTable.getMyEvacPotential());
            
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
