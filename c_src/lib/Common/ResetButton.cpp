#include "ResetButton.h"

static uint8_t g_resetPin = 0;

void ResetButton::check(uint8_t pin) {
    static bool initialized = false;
    if (!initialized) {
        pinMode(pin, INPUT_PULLUP);
        initialized = true;
    }

    static unsigned long pressStart = 0;
    static bool pressed = false;

    if (digitalRead(pin) == LOW) {
        if (!pressed) {
            pressStart = millis();
            pressed = true;
        } else {
            if (millis() - pressStart >= 3000) { // Nhấn giữ trong 3 giây
                Serial.println("\n[Config] BOOT button held for 3s! Clearing config...");
                LittleFS.remove("/config.txt");
                Serial.println("[Config] Config file deleted. Restarting...");
                delay(1000);
                ESP.restart();
            }
        }
    } else {
        pressed = false;
    }
}

static void resetButtonTaskFunc(void* pvParameters) {
    for (;;) {
        ResetButton::check(g_resetPin);
        vTaskDelay(pdMS_TO_TICKS(50)); // Quét phím mỗi 50ms
    }
}

void ResetButton::startTask(uint8_t pin) {
    g_resetPin = pin;
    xTaskCreate(resetButtonTaskFunc, "ResetBtnTask", 2048, NULL, 1, NULL);
    Serial.printf("[Config] Đã khởi tạo task chạy ngầm giám sát nút BOOT trên GPIO %d\n", pin);
}
