#ifndef COMMON_CONFIG_H
#define COMMON_CONFIG_H

// ==================== CẤU HÌNH HỆ THỐNG MESH DÙNG CHUNG ====================

#define WIFI_SSID_COMMON      "Trung Tam Doi Moi Sang Tao"
#define WIFI_PASSWORD_COMMON  "12345678"
#define SERVER_URL_COMMON     "http://192.168.1.24:8000/device-readings/ingest"
#define WIFI_CHANNEL_COMMON   1

#define HAS_OLED              0   // Đặt thành 1 nếu có màn hình OLED vật lý, 0 nếu không sử dụng
#define HAS_TEMP_SENSOR       0   // Đặt thành 1 nếu cắm cảm biến nhiệt độ DS18B20, 0 nếu dùng giả lập
#define HAS_GAS_SENSOR        1   // Đặt thành 1 nếu cắm cảm biến khí gas MQ-2, 0 nếu dùng giả lập
#define HAS_NEOPIXEL          0   // Đặt thành 1 nếu cắm LED NeoPixel, 0 nếu không sử dụng
#define HAS_BUZZER            0   // Đặt thành 1 nếu cắm còi báo động Buzzer, 0 nếu không sử dụng

#endif // COMMON_CONFIG_H
