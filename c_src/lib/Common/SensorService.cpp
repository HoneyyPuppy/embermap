#include "SensorService.h"
#include "GasCalibrator.h"
#include <common_config.h>

// Pins
#ifdef CONFIG_IDF_TARGET_ESP32S3
#define ONE_WIRE_BUS 4    // Chân D3 trên XIAO ESP32-S3
#define MQ2_PIN 1         // Chân D0 trên XIAO ESP32-S3
#define BUZZER_PIN 6      // Chân D5 trên XIAO ESP32-S3 (thay cho GPIO 25 không có trên board)
#define NEOPIXEL_PIN 5    // Chân D4 trên XIAO ESP32-S3 (thay cho GPIO 26 không có trên board)
#else
#define ONE_WIRE_BUS 4
#define MQ2_PIN 34
#define BUZZER_PIN 25
#define NEOPIXEL_PIN 26
#endif
#define NUMPIXELS 15

OneWire SensorService::oneWire(ONE_WIRE_BUS);
DallasTemperature SensorService::sensors(&SensorService::oneWire);
Adafruit_NeoPixel SensorService::pixels(NUMPIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

const float SensorService::TEMP_THRESHOLD = 50.0;
const int SensorService::GAS_THRESHOLD = 300;

// Khởi tạo các biến tĩnh phục vụ LED điều hướng
bool SensorService::m_emergency = false;
bool SensorService::m_hasRoute = false;
float SensorService::m_repulsivePotential = 0.0;
uint8_t SensorService::m_satelliteId = 0;
uint8_t SensorService::m_nextHopId = 255;
uint8_t SensorService::m_targetNeighbor = 255;
uint8_t SensorService::m_neighborNextHopId = 255;
int SensorService::m_simulatedGas = 120;
bool SensorService::m_useSimulatedGas = true;
TaskHandle_t SensorService::m_ledTaskHandle = NULL;

void SensorService::calibrateMq2() {
#if HAS_GAS_SENSOR
    GasCalibrator::start(MQ2_PIN);
#endif
}

void SensorService::setSimulatedGas(int gasVal) {
    m_simulatedGas = gasVal;
    m_useSimulatedGas = true;
}

void SensorService::disableSimulatedGas() {
    m_useSimulatedGas = false;
}

void SensorService::init(const char* nodeName) {
#if HAS_GAS_SENSOR
    GasCalibrator::init("sensor-calib");
#endif

#if HAS_NEOPIXEL
    pixels.begin();
    pixels.setPixelColor(0, pixels.Color(0, 0, 255)); // Blue for startup
    pixels.show();

    // Tạo Task chạy ngầm xử lý hiệu ứng dải LED (Core 1)
    if (m_ledTaskHandle == NULL) {
        xTaskCreatePinnedToCore(
            ledAnimationTask,
            "LedAnimTask",
            3072,
            NULL,
            1,
            &m_ledTaskHandle,
            1
        );
    }
#endif

#if HAS_TEMP_SENSOR
    sensors.begin();
#endif

#if HAS_BUZZER
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
#endif
}

void SensorService::read(float &temp, int &gas, bool &emergency) {
#if HAS_TEMP_SENSOR
    sensors.requestTemperatures();
    temp = sensors.getTempCByIndex(0);
    if (temp == DEVICE_DISCONNECTED_C) {
        temp = 27.2; // Fallback if hardware not connected
    }
#else
    temp = 27.2; // Giả lập nhiệt độ khi không sử dụng cảm biến thật
#endif

#if HAS_GAS_SENSOR
    if (m_useSimulatedGas) {
        gas = m_simulatedGas;
        
        static unsigned long lastLogTime = 0;
        if (millis() - lastLogTime > 3000) {
            Serial.printf("[Sensor MQ2 Sim] Đang dùng giá trị giả lập Gas: %d (ngưỡng cảnh báo: %d)\n", gas, GAS_THRESHOLD);
            lastLogTime = millis();
        }
    } else {
        uint32_t sumVolts = 0;
        for (int i = 0; i < 10; i++) {
            sumVolts += analogReadMilliVolts(MQ2_PIN);
            delay(5);
        }
        uint32_t voltMv = sumVolts / 10;
        float rs = GasCalibrator::getRs(voltMv);
        float r0 = GasCalibrator::getR0();
        float ratio = rs / r0;
        
        // Quy đổi tỷ lệ ratio sang thang đo gas số nguyên (0 - 1000)
        // ratio = 9.83 (sạch tuyệt đối) -> gas = 0
        // ratio giảm dần khi có khói. Nếu ratio <= 1.0 (rất độc/cháy) -> gas ~ 900+
        float gasVal = 1000.0f * (GasCalibrator::CLEAN_AIR_RATIO - ratio) / GasCalibrator::CLEAN_AIR_RATIO;
        if (gasVal < GasCalibrator::DEADBAND) gasVal = 0.0f; // Ngưỡng lọc nhiễu dao động nhẹ ở không khí sạch (Deadband)
        if (gasVal > 1000.0f) gasVal = 1000.0f;
        gas = (int)gasVal;

        // Log liên tục mỗi 3 giây để người dùng kiểm tra trạng thái hiệu chuẩn
        static unsigned long lastLogTime = 0;
        if (millis() - lastLogTime > 3000) {
            if (GasCalibrator::isCalibrating()) {
                Serial.println("[Sensor MQ2] Đang đo đạc hiệu chuẩn R0 chạy ngầm...");
            } else {
                Serial.printf("[Sensor MQ2] Volt: %u mV | Rs: %.2f kOhm | R0: %.2f kOhm | Ratio: %.2f | Gas: %d\n",
                              voltMv, rs, r0, ratio, gas);
            }
            lastLogTime = millis();
        }
    }
#else
    gas = m_simulatedGas;   // Giả lập nồng độ gas khi không sử dụng cảm biến thật
#endif
    emergency = (temp >= TEMP_THRESHOLD || gas >= GAS_THRESHOLD);
}

void SensorService::updateAlarm(bool emergency, bool hasRoute, float repulsivePotential, uint8_t satelliteId, uint8_t nextHopId, uint8_t targetNeighbor, uint8_t neighborNextHopId) {
    static bool lastEmergency = false;
    static bool lastHasRoute = false;
    static uint8_t lastNextHopId = 255;
    static uint8_t lastNeighborNextHopId = 255;

    if (emergency != lastEmergency || hasRoute != lastHasRoute || nextHopId != lastNextHopId || neighborNextHopId != lastNeighborNextHopId) {
        String directionStr = "TẮT";
        if (emergency) {
            directionStr = "CẢNH BÁO CHÁY (CHỚP ĐỎ)";
        } else if (!hasRoute) {
            directionStr = "MẤT ĐỊNH TUYẾN (THỞ CAM)";
        } else {
            if (nextHopId == targetNeighbor) {
                directionStr = "CHẠY XUÔI (Hướng sang Node " + String(targetNeighbor) + ")";
            } else if (neighborNextHopId == satelliteId) {
                directionStr = "CHẠY NGƯỢC (Hướng từ Node " + String(targetNeighbor) + " về mình)";
            }
        }
        
        Serial.printf("[LED Control] Cập nhật LED -> Node: %d | Trạng thái: %s | Lối thoát: %d | Hướng LED: %s\n",
                      satelliteId,
                      emergency ? "NGUY HIỂM" : "AN TOÀN",
                      nextHopId,
                      directionStr.c_str());

        lastEmergency = emergency;
        lastHasRoute = hasRoute;
        lastNextHopId = nextHopId;
        lastNeighborNextHopId = neighborNextHopId;
    }

    m_emergency = emergency;
    m_hasRoute = hasRoute;
    m_repulsivePotential = repulsivePotential;
    m_satelliteId = satelliteId;
    m_nextHopId = nextHopId;
    m_targetNeighbor = targetNeighbor;
    m_neighborNextHopId = neighborNextHopId;

#if HAS_BUZZER
    if (emergency) {
        digitalWrite(BUZZER_PIN, HIGH);
    } else {
        digitalWrite(BUZZER_PIN, LOW);
    }
#endif
}

void SensorService::ledAnimationTask(void* pvParameters) {
    uint8_t chaseIndex = 1; 
    bool stateBlink = false;
    uint32_t step = 0;

    for (;;) {
        step++;
        stateBlink = !stateBlink;

#if HAS_NEOPIXEL
        // 1. Đèn trạng thái (Bóng số 0)
        if (GasCalibrator::isCalibrating()) {
            pixels.setPixelColor(0, pixels.Color(0, 0, 255)); // Xanh dương: Đang hiệu chuẩn
        } else if (m_emergency) {
            pixels.setPixelColor(0, stateBlink ? pixels.Color(255, 0, 0) : pixels.Color(0, 0, 0)); // Chớp đỏ
        } else if (m_hasRoute) {
            if (m_repulsivePotential > 0.0) {
                pixels.setPixelColor(0, pixels.Color(255, 255, 0)); // Vàng: Cảnh báo APF
            } else {
                pixels.setPixelColor(0, pixels.Color(0, 255, 0)); // Xanh lá: OK
            }
        } else {
            pixels.setPixelColor(0, pixels.Color(255, 165, 0)); // Cam: Mất mạng/kẹt
        }

        // 2. Dải LED điều hướng (Bóng 1 đến 14)
        if (GasCalibrator::isCalibrating()) {
            // Hiệu ứng thở (Breathe) xanh dương
            float breath = (sin(step * 0.15f) + 1.0f) * 0.5f;
            uint8_t bVal = (uint8_t)(breath * 200.0f) + 20;
            for (int i = 1; i < NUMPIXELS; i++) {
                pixels.setPixelColor(i, pixels.Color(0, 0, bVal));
            }
        } else if (m_emergency) {
            // Chớp đỏ toàn dải
            uint32_t redColor = stateBlink ? pixels.Color(255, 0, 0) : pixels.Color(0, 0, 0);
            for (int i = 1; i < NUMPIXELS; i++) {
                pixels.setPixelColor(i, redColor);
            }
        } else if (!m_hasRoute) {
            // Hiệu ứng thở (Breathe) màu cam
            float breath = (sin(step * 0.15f) + 1.0f) * 0.5f;
            uint8_t rVal = (uint8_t)(breath * 200.0f) + 30;
            uint8_t gVal = (uint8_t)(breath * 100.0f) + 10;
            for (int i = 1; i < NUMPIXELS; i++) {
                pixels.setPixelColor(i, pixels.Color(rVal, gVal, 0));
            }
        } else {
            // Có định tuyến!
            if (m_satelliteId == 0) {
                // Tại Master Node: Sáng xanh lá đứng toàn dải báo Exit
                for (int i = 1; i < NUMPIXELS; i++) {
                    pixels.setPixelColor(i, pixels.Color(0, 255, 0));
                }
            } else {
                bool runLed = false;
                bool forward = true;

                if (m_nextHopId == m_targetNeighbor) {
                    // Thoát đi RA khỏi mình (xuôi dòng hướng về láng giềng Y)
                    runLed = true;
                    forward = true;
                } else if (m_neighborNextHopId == m_satelliteId) {
                    // Thoát đi VÀO mình từ láng giềng Y (ngược dòng hướng từ Y về mình)
                    runLed = true;
                    forward = false;
                }

                if (runLed) {
                    // Dọn dẹp dải LED 1-14 về tối
                    for (int i = 1; i < NUMPIXELS; i++) {
                        pixels.setPixelColor(i, pixels.Color(0, 0, 0));
                    }

                    if (forward) {
                        chaseIndex++;
                        if (chaseIndex >= NUMPIXELS) {
                            chaseIndex = 1;
                        }
                    } else {
                        if (chaseIndex <= 1) {
                            chaseIndex = NUMPIXELS - 1;
                        } else {
                            chaseIndex--;
                        }
                    }

                    // Vẽ 3 bóng chạy đuổi với đuôi mờ dần
                    for (int offset = 0; offset < 3; offset++) {
                        int ledPos = 0;
                        if (forward) {
                            ledPos = chaseIndex - offset;
                            if (ledPos < 1) ledPos += (NUMPIXELS - 1);
                        } else {
                            ledPos = chaseIndex + offset;
                            if (ledPos >= NUMPIXELS) ledPos -= (NUMPIXELS - 1);
                        }
                        
                        uint8_t brightness = 255 - (offset * 80);
                        pixels.setPixelColor(ledPos, pixels.Color(0, brightness, 0));
                    }
                } else {
                    // Tắt dải LED 1-14 nếu hành lang không thuộc luồng thoát hiểm hiện tại
                    for (int i = 1; i < NUMPIXELS; i++) {
                        pixels.setPixelColor(i, pixels.Color(0, 0, 0));
                    }
                }
            }
        }
        pixels.show();
#endif
        vTaskDelay(pdMS_TO_TICKS(80)); // Hiệu ứng động chạy mượt mà ở chu kỳ 80ms
    }
}

float SensorService::calculateRepulsivePotential(float temp, int gas) {
    #define TEMP_SAFE_THRESHOLD   37.0
    #define TEMP_DANGER_THRESHOLD 50.0
    #define GAS_SAFE_THRESHOLD    150
    #define GAS_DANGER_THRESHOLD  500
    #define U_WARNING_MAX         500.0

    float u_temp = 0.0;
    float u_gas = 0.0;

    // 1. Tính thế năng đẩy của Nhiệt độ (DS18B20)
    if (temp <= TEMP_SAFE_THRESHOLD) {
        u_temp = 0.0;
    } else if (temp >= TEMP_DANGER_THRESHOLD) {
        u_temp = 9999.0;
    } else {
        float ratio_temp = (temp - TEMP_SAFE_THRESHOLD) / (TEMP_DANGER_THRESHOLD - TEMP_SAFE_THRESHOLD);
        u_temp = ratio_temp * U_WARNING_MAX;
    }

    // 2. Tính thế năng đẩy của Khói/Gas (MQ-2)
    if (gas <= GAS_SAFE_THRESHOLD) {
        u_gas = 0.0;
    } else if (gas >= GAS_DANGER_THRESHOLD) {
        u_gas = 9999.0;
    } else {
        float ratio_gas = (float)(gas - GAS_SAFE_THRESHOLD) / (GAS_DANGER_THRESHOLD - GAS_SAFE_THRESHOLD);
        u_gas = ratio_gas * U_WARNING_MAX;
    }

    // 3. Sensor Fusion: Lấy giá trị lớn nhất (tệ nhất) để ra quyết định định tuyến
    float u_final = u_temp > u_gas ? u_temp : u_gas;

    if (u_final > 9999.0) {
        u_final = 9999.0;
    }

    return u_final;
}
