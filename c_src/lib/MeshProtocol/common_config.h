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

// ==================== BẢN ĐỒ TOPOLOGY TRUNG TÂM (ĐỊNH NGHĨA SƠ ĐỒ TÒA NHÀ) ====================
typedef struct {
    uint8_t nodeId;
    uint8_t allowedRadio[5];
    uint8_t allowedRadioCount;
    uint8_t physIds[3];
    float physDists[3];
    uint8_t physCount;
} NodeTopologyConfig;

// --- KỊCH BẢN A: 4 ESP32 (1 Master + 3 Vệ tinh) - BỎ COMMENT ĐỂ CHẠY ---
/*
const NodeTopologyConfig TOPOLOGY_MAP[] = {
    // ID,  Allowed Radio,               Count, Phys IDs,     Phys Distances,       Phys Count
    { 1,    {0, 2},                       2,     {0, 2},       {10.0, 15.0},         2 },
    { 2,    {1, 3},                       2,     {1, 3},       {15.0, 12.0},         2 },
    { 3,    {2},                          1,     {2},          {12.0},               1 }
};
*/

// --- KỊCH BẢN B: 6 ESP32 (1 Master + 5 Vệ tinh) - HÃY COMMENT KỊCH BẢN A VÀ BỎ COMMENT KỊCH BẢN B NẾU MUỐN CHẠY ---
const NodeTopologyConfig TOPOLOGY_MAP[] = {
    // ID,  Allowed Radio,               Count, Phys IDs,     Phys Distances,       Phys Count
    { 1,    {0, 2},                       2,     {0, 2},       {10.0, 8.0},          2 },
    { 2,    {1, 3},                       2,     {1, 3},       {8.0, 10.0},          2 },
    { 3,    {2, 4, 5},                    3,     {2, 4, 5},    {10.0, 10.0, 15.0},   3 },
    { 4,    {0, 3},                       2,     {0, 3},       {12.0, 10.0},         2 },
    { 5,    {3},                          1,     {3},          {15.0},               1 }
};

#endif // COMMON_CONFIG_H
