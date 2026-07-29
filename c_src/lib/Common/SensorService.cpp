#include "SensorService.h"
#include <common_config.h>
#include <Preferences.h>

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

float SensorService::MQ2_R0 = 10.0f;

float SensorService::getRs(uint32_t voltMv) {
    if (voltMv < 50) voltMv = 50; 
    if (voltMv > 3300) voltMv = 3300; 
    float voltV = (float)voltMv / 1000.0f;
    float vin = 5.0f; // MQ-2 hoạt động ở nguồn 5V
    float rl = 5.1f;  // Trở kháng tải mặc định 5.1 kOhm trên hầu hết module MQ-2
    return rl * (vin - voltV) / voltV;
}

void SensorService::calibrateMq2() {
    Serial.println("[Sensor Calib] Bắt đầu đo hiệu chuẩn MQ-2 trong 5 giây (Hãy giữ không khí sạch)...");
    uint32_t sumVolts = 0;
    int samples = 50;
    for (int i = 0; i < samples; i++) {
        sumVolts += analogReadMilliVolts(MQ2_PIN);
        delay(100);
    }
    uint32_t avgVolt = sumVolts / samples;
    float rs = getRs(avgVolt);
    
    // R0 = Rs / 9.83 (MQ-2 ratio in clean air)
    float calculatedR0 = rs / 9.83f;
    
    // Giới hạn giá trị R0 hợp lệ từ 0.5 kOhm đến 150 kOhm
    if (calculatedR0 >= 0.5f && calculatedR0 <= 150.0f) {
        MQ2_R0 = calculatedR0;
        Serial.printf("[Sensor Calib] Hiệu chuẩn THÀNH CÔNG! R0 = %.2f kOhm (Điện áp tb: %u mV)\n", MQ2_R0, avgVolt);
        
        // Lưu giá trị R0 vào NVS Preferences
        Preferences prefs;
        prefs.begin("sensor-calib", false);
        prefs.putFloat("mq2_r0", MQ2_R0);
        prefs.end();
        Serial.println("[Sensor Calib] Đã lưu R0 vào bộ nhớ Flash.");
    } else {
        Serial.printf("[Sensor Calib] LỖI! R0 tính toán bất thường: %.2f kOhm. Dùng giá trị R0 hiện tại: %.2f kOhm.\n", 
                      calculatedR0, MQ2_R0);
    }
}

void SensorService::init(const char* nodeName) {
    // Tải giá trị R0 đã lưu từ Flash
    Preferences prefs;
    prefs.begin("sensor-calib", false); // Dùng false để tự động tạo namespace nếu chưa tồn tại
    float savedR0 = prefs.getFloat("mq2_r0", -1.0f);
    prefs.end();
    
    if (savedR0 > 0.0f) {
        MQ2_R0 = savedR0;
        Serial.printf("[Sensor Calib] Đã tải R0 thành công từ bộ nhớ Flash: %.2f kOhm\n", MQ2_R0);
    } else {
        MQ2_R0 = 10.0f; // Giá trị mặc định nếu chưa được hiệu chuẩn lần nào
        Serial.println("[Sensor Calib] Chưa có R0 trong Flash. Sử dụng R0 mặc định = 10.0 kOhm.");
    }

#if HAS_NEOPIXEL
    pixels.begin();
    pixels.setPixelColor(0, pixels.Color(0, 0, 255)); // Blue for startup
    pixels.show();
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
    uint32_t voltMv = analogReadMilliVolts(MQ2_PIN);
    float rs = getRs(voltMv);
    float ratio = rs / MQ2_R0;
    
    // Quy đổi tỷ lệ ratio sang thang đo gas số nguyên (0 - 1000)
    // ratio = 9.83 (sạch tuyệt đối) -> gas = 0
    // ratio giảm dần khi có khói. Nếu ratio <= 1.0 (rất độc/cháy) -> gas ~ 900+
    float gasVal = 1000.0f * (9.83f - ratio) / 9.83f;
    if (gasVal < 0.0f) gasVal = 0.0f;
    if (gasVal > 1000.0f) gasVal = 1000.0f;
    gas = (int)gasVal;

    // Log liên tục mỗi 3 giây để người dùng kiểm tra trạng thái hiệu chuẩn
    static unsigned long lastLogTime = 0;
    if (millis() - lastLogTime > 3000) {
        Serial.printf("[Sensor MQ2] Volt: %u mV | Rs: %.2f kOhm | R0: %.2f kOhm | Ratio: %.2f | Gas: %d\n",
                      voltMv, rs, MQ2_R0, ratio, gas);
        lastLogTime = millis();
    }
#else
    gas = 120;   // Giả lập nồng độ gas khi không sử dụng cảm biến thật
#endif
    emergency = (temp >= TEMP_THRESHOLD || gas >= GAS_THRESHOLD);
}

void SensorService::updateAlarm(bool emergency, bool hasRoute, float repulsivePotential) {
#if HAS_BUZZER
    if (emergency) {
        digitalWrite(BUZZER_PIN, HIGH);
    } else {
        digitalWrite(BUZZER_PIN, LOW);
    }
#endif

#if HAS_NEOPIXEL
    if (emergency) {
        pixels.setPixelColor(0, pixels.Color(255, 0, 0)); // Red flashing
    } else {
        if (hasRoute) {
            if (repulsivePotential > 0.0) {
                pixels.setPixelColor(0, pixels.Color(255, 255, 0)); // Yellow warning (APF)
            } else {
                pixels.setPixelColor(0, pixels.Color(0, 255, 0)); // Green: OK
            }
        } else {
            pixels.setPixelColor(0, pixels.Color(255, 165, 0)); // Orange: Lost route / offline
        }
    }
    pixels.show();
#endif
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
