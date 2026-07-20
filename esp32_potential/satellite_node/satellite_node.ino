#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include "../../shared_config/common_config.h"
#include "mesh_packet.h"
#include "sensor_helper.h"

// ==================== CẤU HÌNH NODE VỆ TINH ====================
#define SATELLITE_ID 2         // ID duy nhất cho nút vệ tinh này

// Địa chỉ MAC quảng bá
uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint8_t myMac[6];

// APF Routing Variables
float basePotential = 9999.0;
float repulsivePotential = 0.0;
float totalPotential = 9999.0;
uint8_t myHopCount = 255;
unsigned long lastRouteUpdateReceived = 0;
const unsigned long ROUTE_TIMEOUT = 12000;

// Bảng lưu láng giềng (Neighbor Table)
typedef struct {
  uint8_t mac[6];
  float potential;
  uint8_t hopCount;
  unsigned long lastSeen;
} Neighbor;

#define MAX_NEIGHBORS 10
Neighbor neighbors[MAX_NEIGHBORS];
int neighborCount = 0;

// Next Hop MAC đang dùng để truyền dữ liệu
uint8_t nextHopMac[6] = {0, 0, 0, 0, 0, 0};
bool hasRoute = false;

// Timers
unsigned long lastSensorReadTime = 0;
unsigned long lastPotentialBroadcastTime = 0;
const unsigned long SENSOR_READ_INTERVAL = 3000;
const unsigned long POTENTIAL_BROADCAST_INTERVAL = 5000;

// Cảm biến
float currentTemp = 0.0;
int currentGas = 0;
bool isEmergency = false;

// ==================== CẬP NHẬT/THÊM LÁNG GIỀNG VÀO BẢNG ====================
void updateNeighbor(const uint8_t *mac, float potential, uint8_t hopCount) {
  unsigned long now = millis();
  int foundIdx = -1;

  for (int i = 0; i < neighborCount; i++) {
    if (memcmp(neighbors[i].mac, mac, 6) == 0) {
      foundIdx = i;
      break;
    }
  }

  if (foundIdx != -1) {
    neighbors[foundIdx].potential = potential;
    neighbors[foundIdx].hopCount = hopCount;
    neighbors[foundIdx].lastSeen = now;
  } else {
    if (neighborCount < MAX_NEIGHBORS) {
      memcpy(neighbors[neighborCount].mac, mac, 6);
      neighbors[neighborCount].potential = potential;
      neighbors[neighborCount].hopCount = hopCount;
      neighbors[neighborCount].lastSeen = now;
      neighborCount++;
      Serial.printf("[APF Table] Thêm láng giềng mới: %02X:%02X...\n", mac[0], mac[1]);
    }
  }
}

// dọn dẹp các láng giềng quá thời gian không thấy (timeout)
void purgeNeighbors() {
  unsigned long now = millis();
  for (int i = 0; i < neighborCount; i++) {
    if (now - neighbors[i].lastSeen >= ROUTE_TIMEOUT) {
      for (int j = i; j < neighborCount - 1; j++) {
        neighbors[j] = neighbors[j+1];
      }
      neighborCount--;
      i--;
    }
  }
}

// ==================== HÀM QUYẾT ĐỊNH NEXT HOP THEO THẾ NĂNG CỰC TIỂU ====================
bool selectNextHop() {
  purgeNeighbors();
  
  if (neighborCount == 0) {
    hasRoute = false;
    memset(nextHopMac, 0, 6);
    myHopCount = 255;
    basePotential = 9999.0;
    return false;
  }

  float minPotential = 99999.0;
  int minIdx = -1;

  for (int i = 0; i < neighborCount; i++) {
    if (neighbors[i].potential < minPotential) {
      minPotential = neighbors[i].potential;
      minIdx = i;
    }
  }

  if (minIdx != -1 && minPotential < totalPotential) {
    memcpy(nextHopMac, neighbors[minIdx].mac, 6);
    myHopCount = neighbors[minIdx].hopCount + 1;
    basePotential = myHopCount * 100.0;
    hasRoute = true;

    if (!esp_now_is_peer_exist(nextHopMac)) {
      esp_now_peer_info_t peerInfo = {};
      memcpy(peerInfo.peer_addr, nextHopMac, 6);
      peerInfo.channel = WIFI_CHANNEL_COMMON;
      peerInfo.encrypt = false;
      peerInfo.ifidx = WIFI_IF_STA;
      esp_now_add_peer(&peerInfo);
    }
    return true;
  }

  hasRoute = false;
  memset(nextHopMac, 0, 6);
  return false;
}

// ==================== PHÁT QUẢNG BÁ THẾ NĂNG CỦA BẢN THÂN ====================
void broadcastPotential() {
  MeshPacket packet;
  packet.packetType = PACKET_POTENTIAL_ADVERT;
  memcpy(packet.sourceMac, myMac, 6);
  memcpy(packet.destMac, broadcastMac, 6);
  memset(packet.forwardMac, 0, 6);
  packet.id = SATELLITE_ID;
  packet.temp = 0.0;
  packet.gasRaw = 0;
  packet.emergency = false;
  packet.potential = totalPotential;
  packet.hopCount = myHopCount;

  esp_err_t result = esp_now_send(broadcastMac, (uint8_t *)&packet, sizeof(packet));
  Serial.printf("[APF] Quảng bá thế năng U = %.1f (Hop = %d) -> %s\n", 
                totalPotential, myHopCount, result == ESP_OK ? "OK" : "FAIL");
}

// ==================== GỬI DỮ LIỆU CẢM BIẾN LÊN NEXT HOP ====================
void sendSensorData() {
  if (!hasRoute) {
    Serial.println("[APF Warning] Không gửi được dữ liệu: Mất định tuyến (Thế năng vô cực).");
    return;
  }

  MeshPacket packet;
  packet.packetType = PACKET_SENSOR_DATA;
  memcpy(packet.sourceMac, myMac, 6);
  memset(packet.destMac, 0, 6);
  memcpy(packet.forwardMac, nextHopMac, 6);
  packet.id = SATELLITE_ID;
  packet.temp = currentTemp;
  packet.gasRaw = currentGas;
  packet.emergency = isEmergency;
  packet.potential = totalPotential;
  packet.hopCount = myHopCount;

  esp_err_t result = esp_now_send(nextHopMac, (uint8_t *)&packet, sizeof(packet));
  
  Serial.printf("[APF Data] Gửi cảm biến lên Next Hop: %02X:%02X... -> %s\n", 
                nextHopMac[0], nextHopMac[1], result == ESP_OK ? "OK" : "FAIL");
}

// ==================== CALLBACK NHẬN DỮ LIỆU ESP-NOW ====================
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingDataRaw, int len) {
  const uint8_t* senderMac = recv_info->src_addr;

  if (len < sizeof(MeshPacket)) return;

  MeshPacket incomingPacket;
  memcpy(&incomingPacket, incomingDataRaw, sizeof(incomingPacket));

  if (incomingPacket.packetType == PACKET_POTENTIAL_ADVERT) {
    updateNeighbor(senderMac, incomingPacket.potential, incomingPacket.hopCount);
    selectNextHop();
  }
  else if (incomingPacket.packetType == PACKET_SENSOR_DATA) {
    if (memcmp(incomingPacket.forwardMac, myMac, 6) == 0) {
      if (selectNextHop()) {
        memcpy(incomingPacket.forwardMac, nextHopMac, 6);
        esp_now_send(nextHopMac, (uint8_t *)&incomingPacket, sizeof(incomingPacket));
        Serial.printf("[APF Forward] Trung chuyển gói tin của Node %d qua Next Hop thế năng thấp: %02X:%02X...\n", 
                      incomingPacket.id, nextHopMac[0], nextHopMac[1]);
      } else {
        Serial.println("[APF Warning] Không thể trung chuyển: Mất định tuyến!");
      }
    }
  }
}

// ==================== CALLBACK GỬI DỮ LIỆU ====================
void OnDataSent(const esp_now_send_info_t *tx_info, esp_now_send_status_t status) {
}

// ==================== ÉP KÊNH SÓNG WIFI SATELLITE ====================
void forceWifiChannel(uint8_t channel) {
  WiFi.mode(WIFI_STA);
  esp_wifi_start();
  esp_err_t err = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  Serial.printf("Trạng thái ép kênh %d: %s\n", channel, esp_err_to_name(err));
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);

  initSensors();

  WiFi.mode(WIFI_STA);
  esp_wifi_get_mac(WIFI_IF_STA, myMac);
  Serial.printf("\n=========================================\n");
  Serial.printf("MAC SATELLITE %d STA (APF): %02X:%02X:%02X:%02X:%02X:%02X\n", 
                SATELLITE_ID, myMac[0], myMac[1], myMac[2], myMac[3], myMac[4], myMac[5]);
  Serial.printf("=========================================\n");

  if (esp_now_init() == ESP_OK) {
    Serial.println("[ESP-NOW OK] Khởi tạo thành công.");
    esp_now_register_recv_cb(OnDataRecv);
    esp_now_register_send_cb(OnDataSent);
  } else {
    Serial.println("[ESP-NOW FAIL] Lỗi khởi tạo ESP-NOW!");
  }

  forceWifiChannel(WIFI_CHANNEL_COMMON);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastMac, 6);
  peerInfo.channel = WIFI_CHANNEL_COMMON;
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_STA;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("[ESP-NOW] Đăng ký Peer quảng bá thất bại!");
  }

  Serial.println("Khởi động Node Vệ tinh APF OK, đang quét tìm thế năng...");
}

// ==================== LOOP ====================
void loop() {
  unsigned long currentMillis = millis();

  if (currentMillis - lastSensorReadTime >= SENSOR_READ_INTERVAL) {
    lastSensorReadTime = currentMillis;

    readSensors(currentTemp, currentGas, isEmergency);

    float rep = 0.0;
    if (currentTemp > 45.0) {
      rep += (currentTemp - 40.0) * 100.0;
    }
    if (currentGas > 200) {
      rep += (currentGas - 200) * 3.0;
    }
    repulsivePotential = rep;

    if (myHopCount == 255) {
      basePotential = 9999.0;
    } else {
      basePotential = myHopCount * 100.0;
    }
    totalPotential = basePotential + repulsivePotential;

    selectNextHop();
    updateAlarmAndStatus(isEmergency, hasRoute, repulsivePotential);
    updateDisplay(SATELLITE_ID, currentTemp, currentGas, hasRoute, totalPotential, myHopCount, nextHopMac);

    sendSensorData();
  }

  if (currentMillis - lastPotentialBroadcastTime >= POTENTIAL_BROADCAST_INTERVAL) {
    lastPotentialBroadcastTime = currentMillis;
    broadcastPotential();
  }

  delay(30);
}
