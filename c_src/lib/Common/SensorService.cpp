#include "SensorService.h"
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
#define NUMPIXELS 1

OneWire SensorService::oneWire(ONE_WIRE_BUS);
DallasTemperature SensorService::sensors(&SensorService::oneWire);
Adafruit_NeoPixel SensorService::pixels(NUMPIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

const float SensorService::TEMP_THRESHOLD = 50.0;
const int SensorService::GAS_THRESHOLD = 300;

void SensorService::init(const char* nodeName) {
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
    gas = analogRead(MQ2_PIN);
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
