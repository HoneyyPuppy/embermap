#ifndef MESH_PACKET_H
#define MESH_PACKET_H

#include <Arduino.h>

#define PACKET_ROUTE_UPDATE 1
#define PACKET_SENSOR_DATA   2

typedef struct __attribute__((packed)) {
  uint8_t packetType;       // PACKET_ROUTE_UPDATE hoặc PACKET_SENSOR_DATA
  uint8_t sourceMac[6];     // Địa chỉ MAC gốc phát ra gói tin
  uint8_t destMac[6];       // Địa chỉ MAC của đích cuối cùng (Master)
  uint8_t forwardMac[6];    // Địa chỉ MAC của hop tiếp theo (Next Hop)
  int id;                   // ID của nút gốc phát tin
  float temp;               // Dữ liệu cảm biến nhiệt độ
  int gasRaw;               // Dữ liệu cảm biến MQ-2
  bool emergency;           // Cờ khẩn cấp
  float routingCost;        // Chi phí định tuyến (dành cho ROUTE_UPDATE)
} MeshPacket;

#endif // MESH_PACKET_H
