#ifndef MESH_PACKET_H
#define MESH_PACKET_H

#include <Arduino.h>

// Kiểu gói tin định nghĩa chung
#define PACKET_ROUTE_UPDATE      1   // Gradient routing advertisement
#define PACKET_SENSOR_DATA       2   // Sensor payload
#define PACKET_RPL_DIO           3   // RPL DIO advertisement
#define PACKET_RPL_DAO           4   // RPL DAO advertisement
#define PACKET_POTENTIAL_ADVERT  5   // APF potential advertisement
#define PACKET_ROUTE_REQUEST     6   // Active routing request for channel probing
#define PACKET_EVAC_ADVERT       7   // Evacuation path potential advertisement for humans

#define MAX_ROUTE_PATH           8   // Hạn mức số Hop tối đa để tránh lặp vòng định tuyến

typedef struct __attribute__((packed)) {
    uint8_t packetType;       // Kiểu gói tin (1 đến 7)
    uint8_t sourceMac[6];     // MAC gốc phát tin
    uint8_t destMac[6];       // MAC đích cuối
    uint8_t forwardMac[6];    // MAC Next Hop
    int id;                   // ID nút phát (Master = 0, Satellite = SATELLITE_ID)
    float temp;               // Cảm biến nhiệt độ
    int gasRaw;               // Cảm biến khí gas
    bool emergency;           // Cờ khẩn cấp
    
    // Các trường dữ liệu phục vụ định tuyến của cả 3 giao thức
    float routingCost;        // Gradient cost
    uint16_t rank;            // RPL rank
    uint8_t version;          // RPL version
    float potential;          // APF potential
    uint8_t hopCount;         // APF hop count
    
    // Chỉ đường thoát hiểm độc lập
    float evacPotential;      // Evacuation routing potential for humans (APF based, independent)

    // Cơ chế chống lặp vòng định tuyến (Loop Prevention)
    uint8_t routePath[MAX_ROUTE_PATH]; // Mảng chứa ID các nút đã đi qua
    uint8_t routePathLen;              // Số phần tử thực tế trong mảng routePath
} MeshPacket;

// Hàm tiện ích inline kiểm tra xem đường truyền đã đi qua Node ID chỉ định chưa
inline bool pathContainsNode(const uint8_t* path, uint8_t len, uint8_t nodeId) {
    for (uint8_t i = 0; i < len; i++) {
        if (path[i] == nodeId) return true;
    }
    return false;
}

// Hàm tính toán Cost tăng thêm dựa trên cường độ sóng RSSI (chống chọn đường chập chờn)
inline float calculateLinkCost(int rssi) {
    if (rssi >= -60) return 1.0;
    if (rssi <= -90) return 8.0;
    // Nội suy tuyến tính từ [-60, -90] sang [1.0, 8.0]
    return 1.0 + (float)(rssi - (-60)) / (-90 - (-60)) * (8.0 - 1.0);
}

#endif // MESH_PACKET_H
