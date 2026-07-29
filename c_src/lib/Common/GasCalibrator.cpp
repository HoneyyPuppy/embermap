#include "GasCalibrator.h"
#include <Preferences.h>

// Định nghĩa các hằng số vật lý của cảm biến MQ-2
const float GasCalibrator::CLEAN_AIR_RATIO = 9.83f;
const float GasCalibrator::MQ2_RL = 5.1f;
const float GasCalibrator::MQ2_VIN = 5.0f;
const float GasCalibrator::R0_MIN = 0.5f;
const float GasCalibrator::R0_MAX = 150.0f;
const int GasCalibrator::CALIB_SAMPLES = 50;
const float GasCalibrator::DEADBAND = 60.0f;

// Khởi tạo các biến tĩnh
float GasCalibrator::m_r0 = 10.0f;
bool GasCalibrator::m_isCalibrating = false;
uint8_t GasCalibrator::m_pin = 34;

void GasCalibrator::init(const char* namespaceName) {
    Preferences prefs;
    prefs.begin(namespaceName, true); // Chế độ chỉ đọc
    float savedR0 = prefs.getFloat("mq2_r0", -1.0f);
    prefs.end();
    
    if (savedR0 > 0.0f) {
        m_r0 = savedR0;
        Serial.printf("[Sensor Calib] Đã tải R0 thành công từ bộ nhớ Flash: %.2f kOhm\n", m_r0);
    } else {
        m_r0 = 10.0f;
        Serial.println("[Sensor Calib] Chưa có R0 trong Flash. Sử dụng R0 mặc định = 10.0 kOhm.");
    }
}

float GasCalibrator::getRs(uint32_t voltMv) {
    if (voltMv > 3300) voltMv = 3300;
    float voltV = (float)voltMv / 1000.0f;
    if (voltV < 0.01f) voltV = 0.01f; // Tránh chia cho 0
    return MQ2_RL * (MQ2_VIN - voltV) / voltV;
}

void GasCalibrator::start(uint8_t pin) {
    if (m_isCalibrating) {
        Serial.println("[Sensor Calib Warning] Phiên hiệu chuẩn đang chạy ngầm, vui lòng đợi...");
        return;
    }
    m_pin = pin;
    m_isCalibrating = true;
    
    // Tạo Task chạy ngầm trên Core 1 để tránh block luồng chính (không chặn Web, Mesh, hay OTA)
    xTaskCreatePinnedToCore(
        calibrationTask,
        "GasCalibTask",
        3072,
        NULL,
        1,
        NULL,
        1
    );
}

void GasCalibrator::calibrationTask(void* pvParameters) {
    Serial.println("[Sensor Calib] Bắt đầu đo hiệu chuẩn MQ-2 chạy ngầm (5 giây, hãy giữ không khí sạch)...");
    
    uint32_t sumVolts = 0;
    for (int i = 0; i < CALIB_SAMPLES; i++) {
        sumVolts += analogReadMilliVolts(m_pin);
        vTaskDelay(pdMS_TO_TICKS(100)); // Nhường CPU cho các task khác
    }
    
    uint32_t avgVolt = sumVolts / CALIB_SAMPLES;
    float rs = getRs(avgVolt);
    float calculatedR0 = rs / CLEAN_AIR_RATIO;
    
    if (calculatedR0 >= R0_MIN && calculatedR0 <= R0_MAX) {
        m_r0 = calculatedR0;
        Serial.printf("[Sensor Calib] Hiệu chuẩn THÀNH CÔNG! R0 = %.2f kOhm (Điện áp tb: %u mV)\n", m_r0, avgVolt);
        
        // Lưu vào bộ nhớ Flash NVS
        Preferences prefs;
        if (prefs.begin("sensor-calib", false)) {
            prefs.putFloat("mq2_r0", m_r0);
            prefs.end();
            Serial.println("[Sensor Calib] Đã lưu giá trị R0 mới vào bộ nhớ Flash.");
        } else {
            Serial.println("[Sensor Calib Error] Lỗi mở Preferences, không thể lưu R0!");
        }
    } else {
        Serial.printf("[Sensor Calib] LỖI! R0 tính toán bất thường: %.2f kOhm. Dùng giá trị cũ: %.2f kOhm.\n",
                      calculatedR0, m_r0);
    }
    
    m_isCalibrating = false;
    vTaskDelete(NULL); // Tự hủy Task khi hoàn tất
}
