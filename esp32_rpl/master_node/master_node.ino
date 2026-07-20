#include <WiFi.h>
#include <HTTPClient.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include "../../shared_config/common_config.h"
#include "mesh_packet.h"

// Địa chỉ MAC quảng bá
uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint8_t myMac[6];

// Dữ liệu cảm biến thu thập từ các node
float master_temp = 26.0;
int master_gas = 0;
float s1_temp = 26.0, s2_temp = 26.0, s3_temp = 26.0;
int s1_gas = 0, s2_gas = 0, s3_gas = 0;

unsigned long lastDioBroadcastTime = 0;
unsigned long lastSendTime = 0;
const unsigned long DIO_BROADCAST_INTERVAL = 5000;
const unsigned long WEB_POST_INTERVAL = 3000;

// ==================== HÀM GỬI HTTP POST LÊN SERVER ====================
void postReading(const char* deviceId, const char* sensorType, float value) {
  if (WiFi.status() != WL_CONNECTED) return;
  
  HTTPClient http;
  http.begin(SERVER_URL_COMMON);
  http.setConnectTimeout(150);
  http.setTimeout(150);
  http.addHeader("Content-Type", "application/json");

  String body = "{";
  body += "\"buildingCode\":\"B01\",";
  body += "\"deviceId\":\""     + String(deviceId)      + "\",";
  body += "\"sensorType\":\""   + String(sensorType)    + "\",";
  body += "\"value\":"          + String(value, 2)      + ",";
  body += "\"unit\":\""         + String(sensorType == "temp" ? "C" : "raw") + "\"";
  body += "}";

  int httpCode = http.POST(body);
  http.end();

  if (httpCode == 200) {
    Serial.println("[Web OK] " + String(deviceId) + " = " + String(value));
  } else {
    Serial.println("[Web FAIL] " + String(deviceId) + " code=" + httpCode);
  }
}

// ==================== HÀM PHÁT QUẢNG BÁ DIO ====================
void broadcastDio() {
  MeshPacket packet;
  packet.packetType = PACKET_RPL_DIO;
  memcpy(packet.sourceMac, myMac, 6);
  memcpy(packet.destMac, broadcastMac, 6);
  memset(packet.forwardMac, 0, 6);
  packet.id = 0;
  packet.temp = 0.0;
  packet.gasRaw = 0;
  packet.emergency = false;
  packet.rank = 256;         // Gốc DODAG có Rank = 256
  packet.version = 1;

  esp_err_t result = esp_now_send(broadcastMac, (uint8_t *)&packet, sizeof(packet));
  
  Serial.print("[RPL Master] Phát quảng bá DIO (Rank = 256, Ver = 1) -> ");
  if (result == ESP_OK) {
    Serial.println("THÀNH CÔNG");
  } else {
    Serial.print("THẤT BẠI: ");
    Serial.println(result);
  }
}

// ==================== CALLBACK NHẬN DỮ LIỆU ESP-NOW ====================
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingDataRaw, int len) {
  if (len < sizeof(MeshPacket)) return;

  MeshPacket incomingPacket;
  memcpy(&incomingPacket, incomingDataRaw, sizeof(incomingPacket));

  if (incomingPacket.packetType == PACKET_RPL_DAO) {
    Serial.printf("[RPL Master] Nhận gói DAO từ Node %d | Source MAC: %02X:%02X... | Parent: %02X:%02X...\n",
                  incomingPacket.id,
                  incomingPacket.sourceMac[0], incomingPacket.sourceMac[1],
                  incomingPacket.forwardMac[0], incomingPacket.forwardMac[1]);
  }
  else if (incomingPacket.packetType == PACKET_SENSOR_DATA) {
    if (memcmp(incomingPacket.forwardMac, myMac, 6) == 0) {
      Serial.printf("[RPL Master] Nhận Sensor Data từ Node %d qua Mesh (Rank %d). Temp: %.1f, Gas: %d\n", 
                    incomingPacket.id, incomingPacket.rank, incomingPacket.temp, incomingPacket.gasRaw);

      if (incomingPacket.id == 1) {
        s1_temp = incomingPacket.temp;
        s1_gas = incomingPacket.gasRaw;
      }
      else if (incomingPacket.id == 2) {
        s2_temp = incomingPacket.temp;
        s2_gas = incomingPacket.gasRaw;
      }
      else if (incomingPacket.id == 3) {
        s3_temp = incomingPacket.temp;
        s3_gas = incomingPacket.gasRaw;
      }
    }
  }
}

// ==================== KẾT NỐI WIFI ====================
void connectWiFi() {
  Serial.print("Đang kết nối WiFi: ");
  Serial.println(WIFI_SSID_COMMON);
  
  WiFi.begin(WIFI_SSID_COMMON, WIFI_PASSWORD_COMMON);
  unsigned long startAttemptTime = millis();
  
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    delay(500);
    Serial.print(".");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WIFI OK] IP: " + WiFi.localIP().toString());
    Serial.print("==> KÊNH WIFI MASTER ĐANG CHẠY LÀ: ");
    Serial.println(WiFi.channel());
  } else {
    Serial.println("\n[WIFI FAIL] Không kết nối được WiFi. Chạy chế độ ngoại tuyến...");
  }
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  esp_read_mac(myMac, ESP_MAC_WIFI_STA);
  Serial.printf("\n=========================================\n");
  Serial.printf("MAC MASTER STA (RPL): %02X:%02X:%02X:%02X:%02X:%02X\n", 
                myMac[0], myMac[1], myMac[2], myMac[3], myMac[4], myMac[5]);
  Serial.printf("=========================================\n");

  connectWiFi();

  WiFi.mode(WIFI_STA); 
  
  if (esp_now_init() == ESP_OK) {
    Serial.println("[ESP-NOW OK] Khởi tạo thành công.");
    esp_now_register_recv_cb(OnDataRecv);  
  } else {
    Serial.println("[ESP-NOW FAIL] Lỗi khởi tạo!");
  }

  WiFi.setSleep(false);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastMac, 6);
  peerInfo.channel = WiFi.channel();
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_STA;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("[ESP-NOW] Thêm Peer Quảng bá thất bại!");
  }

  broadcastDio();
}

// ==================== LOOP ====================
void loop() {
  unsigned long currentMillis = millis();

  master_temp = 26.5; 
  master_gas = 120;

  if (currentMillis - lastDioBroadcastTime >= DIO_BROADCAST_INTERVAL) {
    broadcastDio();
    lastDioBroadcastTime = currentMillis;
  }

  if (currentMillis - lastSendTime >= WEB_POST_INTERVAL) {
    lastSendTime = currentMillis;
    
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID_COMMON, WIFI_PASSWORD_COMMON);
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n--- Đẩy dữ liệu Mesh thu thập được lên Web Dashboard ---");
      postReading("temp-master", "temp", master_temp);
      postReading("mq2-master", "mq2", (float)master_gas);

      postReading("temp-sat-1", "temp", s1_temp);
      postReading("mq2-sat-1", "mq2", (float)s1_gas);

      postReading("temp-sat-2", "temp", s2_temp);
      postReading("mq2-sat-2", "mq2", (float)s2_gas);

      postReading("temp-sat-3", "temp", s3_temp);
      postReading("mq2-sat-3", "mq2", (float)s3_gas);
    } else {
      Serial.println("\n[Cảnh báo] Mất kết nối WiFi, không thể gửi dữ liệu lên Web!");
    }
  }

  delay(30); 
}
