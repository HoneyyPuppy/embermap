#include "SensorTask.h"
#include "NodeContext.h"
#include "MeshNetwork.h"
#include <SensorService.h>

static const TickType_t SENSOR_PERIOD = pdMS_TO_TICKS(3000);

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
