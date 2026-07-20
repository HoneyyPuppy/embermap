#ifndef MESH_PACKET_H
#define MESH_PACKET_H

#include <Arduino.h>

#define PACKET_SENSOR_DATA       2
#define PACKET_POTENTIAL_ADVERT  5   // Gói tin quảng bá thế năng của Node

typedef struct __attribute__((packed)) {
  uint8_t packetType;       // PACKET_SENSOR_DATA, PACKET_POTENTIAL_ADVERT
  uint8_t sourceMac[6];     // MAC address of origin
  uint8_t destMac[6];       // MAC address of final destination (Master)
  uint8_t forwardMac[6];    // Next hop MAC
  int id;                   // ID of origin node
  float temp;               // Temperature data
  int gasRaw;               // MQ-2 Gas
  bool emergency;           // Emergency flag
  float potential;          // Total Potential field of the sender
  uint8_t hopCount;         // Hop count from Master
} MeshPacket;

#endif // MESH_PACKET_H
