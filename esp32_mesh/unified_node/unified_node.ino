#include <WiFi.h>
#include <HTTPClient.h>
#include <esp_now.h>
#include <esp_wifi.h>

// ==================== CẤU HÌNH PHÂN VAI TRÒ MÔ PHỎNG ====================
#define ROLE_PIN 18  // Nối chân 18 lên 3.3V (hoặc để trống) -> MASTER
                     // Nối chân 18 xuống GND -> SATELLITE (Vệ tinh)

// ==================== CẤU HÌNH WIFI & SERVER (CHO MASTER) ====================
// Trên Wokwi, để kết nối internet gửi dữ liệu, ta dùng SSID "Wokwi-GUEST"
const char* WIFI_SSID     = "Wokwi-GUEST"; 
const char* WIFI_PASSWORD = "";
const char* SERVER_URL    = "http://10.42.0.1:8000/device-readings/ingest"; 
// Ghi chú: Nếu test thật, hãy sửa thành "Tho Con" và mật khẩu "26042012"

// ==================== ĐỊNH NGHĨA CẤU TRÚC GÓI TIN MESH ====================
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

// Địa chỉ MAC quảng bá
uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint8_t myMac[6];
bool isMaster = false;

// --- BIẾN DÀNH RIÊNG CHO SATELLITE NODE ---
float my_cost = 999.0;
uint8_t next_hop_mac[6] = {0, 0, 0, 0, 0, 0};
unsigned long lastRouteRecvTime = 0;
const unsigned long ROUTE_TIMEOUT = 12000;
unsigned long lastSatelliteSendTime = 0;

// --- BIẾN DÀNH RIÊNG CHO MASTER NODE ---
float master_temp = 26.5;
int master_gas = 120;
float s1_temp = 26.0, s2_temp = 26.0, s3_temp = 26.0;
int s1_gas = 0, s2_gas = 0, s3_gas = 0;
unsigned long lastRouteBroadcastTime = 0;
unsigned long lastWebPostTime = 0;

#define RX2_PIN 16
#define TX2_PIN 17

// ==================== TODO: CẢM BIẾN VẬT LÝ & LED & CÒI CỤC BỘ ====================
// TODO: Cấu hình chân kết nối và khởi tạo cảm biến DS18B20 (chân GPIO 4)
// TODO: Cấu hình chân kết nối cảm biến MQ-2 (chân GPIO 32, 33)
// TODO: Cấu hình dải LED WS2812B NeoPixel (chân GPIO 12, 30 bóng LED)
// TODO: Cấu hình còi báo động Buzzer (chân GPIO 13)

// ==================== HÀM GỬI HTTP POST (MASTER) ====================
void postReading(const char* deviceId, const char* sensorType, float value) {
  if (WiFi.status() != WL_CONNECTED) return;
  
  HTTPClient http;
  http.begin(SERVER_URL);
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

// ==================== HÀM CẬP NHẬT PEER NEXT HOP (SATELLITE) ====================
void updateNextHopPeer(const uint8_t *newMac) {
  bool isZero = true;
  for (int i = 0; i < 6; i++) {
    if (newMac[i] != 0) isZero = false;
  }
  if (isZero) return;

  if (memcmp(next_hop_mac, newMac, 6) == 0 && esp_now_is_peer_exist(newMac)) {
    return;
  }

  if (esp_now_is_peer_exist(next_hop_mac)) {
    esp_now_del_peer(next_hop_mac);
  }

  memcpy(next_hop_mac, newMac, 6);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, next_hop_mac, 6);
  peerInfo.channel = 11; // Kênh giống Master
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_STA;

  if (esp_now_add_peer(&peerInfo) == ESP_OK) {
    Serial.printf("[Satellite] Đăng ký Next Hop mới: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  next_hop_mac[0], next_hop_mac[1], next_hop_mac[2],
                  next_hop_mac[3], next_hop_mac[4], next_hop_mac[5]);
  }
}

// ==================== CALLBACK NHẬN DỮ LIỆU ESP-NOW ====================
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingDataRaw, int len) {
  if (len < sizeof(MeshPacket)) return;

  MeshPacket packet;
  memcpy(&packet, incomingDataRaw, sizeof(packet));
  const uint8_t* senderMac = recv_info->src_addr;

  if (isMaster) {
    // --- LOGIC NHẬN CỦA MASTER ---
    if (packet.packetType == PACKET_SENSOR_DATA) {
      if (memcmp(packet.forwardMac, myMac, 6) == 0) {
        Serial.printf("[Master] Nhận Sensor Data từ Node %d qua Mesh. Temp: %.1f, Gas: %d\n", 
                      packet.id, packet.temp, packet.gasRaw);
        if (packet.id == 1) { s1_temp = packet.temp; s1_gas = packet.gasRaw; }
        else if (packet.id == 2) { s2_temp = packet.temp; s2_gas = packet.gasRaw; }
        else if (packet.id == 3) { s3_temp = packet.temp; s3_gas = packet.gasRaw; }
      }
    }
  } else {
    // --- LOGIC NHẬN CỦA SATELLITE ---
    if (packet.packetType == PACKET_ROUTE_UPDATE) {
      float newCost = packet.routingCost + 1.0;
      if (newCost < my_cost || memcmp(next_hop_mac, senderMac, 6) == 0) {
        bool costChanged = (newCost != my_cost || memcmp(next_hop_mac, senderMac, 6) != 0);
        my_cost = newCost;
        updateNextHopPeer(senderMac);
        lastRouteRecvTime = millis();

        if (costChanged) {
          Serial.printf("[Satellite] Nhận định tuyến ngắn hơn từ %02X:%02X... | Cost = %.1f\n", 
                        senderMac[0], senderMac[1], my_cost);
          
          // Phát tiếp broadcast cập nhật định tuyến
          MeshPacket updatePacket = packet;
          memcpy(updatePacket.sourceMac, myMac, 6);
          updatePacket.routingCost = my_cost;
          esp_now_send(broadcastMac, (uint8_t *)&updatePacket, sizeof(updatePacket));
        }
      }
    } 
    else if (packet.packetType == PACKET_SENSOR_DATA) {
      if (memcmp(packet.forwardMac, myMac, 6) == 0) {
        if (my_cost < 999.0) {
          Serial.printf("[Satellite] Chuyển tiếp (Relay) gói tin của Node %d lên Next Hop\n", packet.id);
          memcpy(packet.forwardMac, next_hop_mac, 6);
          esp_now_send(next_hop_mac, (uint8_t *)&packet, sizeof(packet));
        } else {
          Serial.printf("[Satellite] Nhận gói Relay của Node %d nhưng mất định tuyến! Hủy gói.\n", packet.id);
        }
      }
    }
  }
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  pinMode(ROLE_PIN, INPUT_PULLUP);
  delay(200); // Chờ pin ổn định

  // Đọc chân cấu hình vai trò
  isMaster = (digitalRead(ROLE_PIN) == HIGH);

  WiFi.mode(WIFI_STA);
  WiFi.macAddress(myMac);

  Serial.printf("\n=========================================\n");
  Serial.printf("VAI TRÒ: %s\n", isMaster ? "MASTER (Nút chính)" : "SATELLITE (Nút vệ tinh)");
  Serial.printf("MAC STA: %02X:%02X:%02X:%02X:%02X:%02X\n", 
                myMac[0], myMac[1], myMac[2], myMac[3], myMac[4], myMac[5]);
  Serial.printf("=========================================\n");

  if (isMaster) {
    // Khởi tạo cổng Serial2 để lắng nghe Satellite
    Serial2.begin(115200, SERIAL_8N1, RX2_PIN, TX2_PIN);

    // Master kết nối WiFi Internet
    Serial.printf("Master đang kết nối WiFi: %s\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    unsigned long startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 8000) {
      delay(500);
      Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("\n[WiFi OK] IP: %s | Kênh: %d\n", WiFi.localIP().toString().c_str(), WiFi.channel());
    } else {
      Serial.println("\n[WiFi FAIL] Chạy ngoại tuyến.");
    }
  } else {
    // Ghi chú: Trên mạch thật cần gọi ép kênh dưới đây. Trên Wokwi ta tạm comment để tránh lỗi Core Panic.
    // esp_wifi_set_promiscuous(true);
    // esp_wifi_set_channel(11, WIFI_SECOND_CHAN_NONE);
    // esp_wifi_set_promiscuous(false);
    Serial.println("Satellite đang hoạt động (Đã tắt ép kênh trên Wokwi).");
  }

  // Khởi tạo ESP-NOW
  if (esp_now_init() == ESP_OK) {
    Serial.println("Khởi động ESP-NOW thành công.");
    esp_now_register_recv_cb(OnDataRecv);
  } else {
    Serial.println("Lỗi khởi tạo ESP-NOW!");
  }

  // Đăng ký Peer quảng bá
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastMac, 6);
  peerInfo.channel = 11;
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_STA;
  esp_now_add_peer(&peerInfo);
}

// ==================== LOOP ====================
void loop() {
  unsigned long currentMillis = millis();

  if (isMaster) {
    // --- VÒNG LẶP MASTER ---
    // 1. Nhận log từ Satellite qua cổng Serial2 và in ra màn hình chính
    while (Serial2.available() > 0) {
      Serial.write(Serial2.read());
    }

    // 2. Định kỳ phát định tuyến (Cost = 0)
    if (currentMillis - lastRouteBroadcastTime >= 5000) {
      lastRouteBroadcastTime = currentMillis;
      MeshPacket packet = {};
      packet.packetType = PACKET_ROUTE_UPDATE;
      memcpy(packet.sourceMac, myMac, 6);
      memcpy(packet.destMac, myMac, 6);
      packet.id = 0;
      packet.routingCost = 0.0;
      esp_now_send(broadcastMac, (uint8_t *)&packet, sizeof(packet));
      Serial.println("[Master] Phát quảng bá định tuyến (Cost = 0.0)");
    }

    // 3. Định kỳ đẩy dữ liệu lên Web Server
    if (currentMillis - lastWebPostTime >= 3000) {
      lastWebPostTime = currentMillis;
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n--- Đẩy dữ liệu hệ thống lên Web Dashboard ---");
        postReading("temp-master", "temp", master_temp);
        postReading("mq2-master", "mq2", (float)master_gas);

        postReading("temp-sat-1", "temp", s1_temp);
        postReading("mq2-sat-1", "mq2", (float)s1_gas);

        postReading("temp-sat-2", "temp", s2_temp);
        postReading("mq2-sat-2", "mq2", (float)s2_gas);

        postReading("temp-sat-3", "temp", s3_temp);
        postReading("mq2-sat-3", "mq2", (float)s3_gas);
      }
    }
  } 
  else {
    // --- VÒNG LẶP SATELLITE ---
    // 1. Kiểm tra timeout liên kết
    if (my_cost < 999.0 && (currentMillis - lastRouteRecvTime > ROUTE_TIMEOUT)) {
      Serial.println("[Satellite Warning] Mất định tuyến! Reset Cost về vô cực.");
      my_cost = 999.0;
      memset(next_hop_mac, 0, 6);
    }

    // 2. Định kỳ gửi dữ liệu cảm biến giả lập của mình lên Next Hop
    if (currentMillis - lastSatelliteSendTime >= 3000) {
      lastSatelliteSendTime = currentMillis;
      if (my_cost < 999.0) {
        MeshPacket packet = {};
        packet.packetType = PACKET_SENSOR_DATA;
        memcpy(packet.sourceMac, myMac, 6);
        memcpy(packet.forwardMac, next_hop_mac, 6);
        packet.id = 2; // ID giả định cho vệ tinh
        packet.temp = 27.8;
        packet.gasRaw = 145;
        packet.routingCost = my_cost;

        esp_err_t res = esp_now_send(next_hop_mac, (uint8_t *)&packet, sizeof(packet));
        Serial.printf("[Satellite] Gửi dữ liệu cảm biến lên Next Hop. Trạng thái: %s\n", 
                      (res == ESP_OK) ? "OK" : "FAIL");
      } else {
        Serial.println("[Satellite] Không có định tuyến, không thể gửi dữ liệu.");
      }
    }
  }

  delay(30);
}
