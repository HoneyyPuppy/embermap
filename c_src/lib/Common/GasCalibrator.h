#ifndef GAS_CALIBRATOR_H
#define GAS_CALIBRATOR_H

#include <Arduino.h>

class GasCalibrator {
private:
    static float m_r0;
    static bool m_isCalibrating;
    static uint8_t m_pin;
    
    static void calibrationTask(void* pvParameters);

public:
    // Hằng số cấu hình cảm biến MQ-2
    static const float CLEAN_AIR_RATIO;
    static const float MQ2_RL;
    static const float MQ2_VIN;
    static const float R0_MIN;
    static const float R0_MAX;
    static const int CALIB_SAMPLES;
    static const float DEADBAND;

    static void init(const char* namespaceName = "sensor-calib");
    static float getR0() { return m_r0; }
    static float getRs(uint32_t voltMv);
    static void start(uint8_t pin);
    static bool isCalibrating() { return m_isCalibrating; }
};

#endif // GAS_CALIBRATOR_H
