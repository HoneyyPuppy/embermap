#ifndef MESH_PACKET_H
#define MESH_PACKET_H

#include <Arduino.h>

#define PACKET_SENSOR_DATA   2
#define PACKET_RPL_DIO       3   // DODAG Information Object (Quảng bá xuống)
#define PACKET_RPL_DAO       4   // Destination Advertisement Object (Báo cáo ngược lên)

typedef struct __attribute__((packed)) {
  uint8_t packetType;       // PACKET_SENSOR_DATA, PACKET_RPL_DIO, PACKET_RPL_DAO
  uint8_t sourceMac[6];     // MAC address of origin
  uint8_t destMac[6];       // MAC address of final destination (usually Master)
  uint8_t forwardMac[6];    // Next hop (Parent)
  int id;                   // ID of origin node
  float temp;               // Temperature
  int gasRaw;               // MQ-2 Gas
  bool emergency;           // Emergency flag
  uint16_t rank;            // RPL Rank (DODAG Rank)
  uint8_t version;          // DODAG version
} MeshPacket;

#endif // MESH_PACKET_H
