#include <WiFi.h>
#include <HTTPClient.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_mac.h>

// ==================== CẤU HÌNH WIFI & SERVER ====================
const char* WIFI_SSID     = "Tho Con";
const char* WIFI_PASSWORD = "26042012";
const char* SERVER_URL    = "http://10.42.0.1:8000/device-readings/ingest";

// ==================== ĐỊNH NGHĨA CẤU TRÚC GÓI TIN APF MESH ====================
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

// Địa chỉ MAC quảng bá
uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint8_t myMac[6];

// Lưu trữ dữ liệu cảm biến thu thập từ các node
float master_temp = 26.0;
int master_gas = 0;
float s1_temp = 26.0, s2_temp = 26.0, s3_temp = 26.0;
int s1_gas = 0, s2_gas = 0, s3_gas = 0;

unsigned long lastPotentialBroadcastTime = 0;
unsigned long lastSendTime = 0;
const unsigned long POTENTIAL_BROADCAST_INTERVAL = 5000; // Quảng bá thế năng mỗi 5 giây
const unsigned long WEB_POST_INTERVAL = 3000;            // Đẩy dữ liệu lên Web mỗi 3 giây

// ==================== HÀM GỬI HTTP POST LÊN SERVER ====================
void postReading(const char* deviceId, const char* sensorType, float value) {
  if (WiFi.status() != WL_CONNECTED) return;
  
  HTTPClient http;
  http.begin(SERVER_URL);
  http.setConnectTimeout(150); // Khống chế TCP connection handshake tối đa 150ms
  http.setTimeout(150);       // Khống chế thời gian chờ phản hồi tối đa 150ms
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

// ==================== HÀM PHÁT QUẢNG BÁ THẾ NĂNG HẤP DẪN (POTENTIAL) ====================
void broadcastPotential() {
  MeshPacket packet;
  packet.packetType = PACKET_POTENTIAL_ADVERT;
  memcpy(packet.sourceMac, myMac, 6);
  memcpy(packet.destMac, broadcastMac, 6);
  memset(packet.forwardMac, 0, 6);
  packet.id = 0;             // ID = 0 là Master Node
  packet.temp = 0.0;
  packet.gasRaw = 0;
  packet.emergency = false;
  packet.potential = 0.0;    // Master là tâm hấp dẫn, có thế năng thấp nhất = 0.0
  packet.hopCount = 0;

  esp_err_t result = esp_now_send(broadcastMac, (uint8_t *)&packet, sizeof(packet));
  
  Serial.print("[APF Master] Phát quảng bá Thế năng (U = 0.0) -> ");
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

  // Nhận dữ liệu cảm biến
  if (incomingPacket.packetType == PACKET_SENSOR_DATA) {
    if (memcmp(incomingPacket.forwardMac, myMac, 6) == 0) {
      Serial.printf("[APF Master] Nhận Sensor Data từ Node %d qua Mesh. Temp: %.1f, Gas: %d\n", 
                    incomingPacket.id, incomingPacket.temp, incomingPacket.gasRaw);

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
  Serial.println(WIFI_SSID);
  
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
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

  // Khởi động WiFi ở chế độ STA để lấy MAC chính xác
  WiFi.mode(WIFI_STA);
  esp_read_mac(myMac, ESP_MAC_WIFI_STA);
  Serial.printf("\n=========================================\n");
  Serial.printf("MAC MASTER STA (APF): %02X:%02X:%02X:%02X:%02X:%02X\n", 
                myMac[0], myMac[1], myMac[2], myMac[3], myMac[4], myMac[5]);
  Serial.printf("=========================================\n");

  // Kết nối WiFi lúc khởi động
  connectWiFi();

  // Khởi động ESP-NOW song song với chế độ kết nối mạng WiFi
  WiFi.mode(WIFI_STA); 
  
  if (esp_now_init() == ESP_OK) {
    Serial.println("[ESP-NOW OK] Khởi tạo thành công.");
    esp_now_register_recv_cb(OnDataRecv);  
  } else {
    Serial.println("[ESP-NOW FAIL] Lỗi khởi tạo!");
  }

  // Tắt chế độ Modem Sleep để Master luôn mở RF nhận/phát ESP-NOW ổn định nhất
  WiFi.setSleep(false);

  // Đăng ký Peer quảng bá để có thể gửi gói tin định tuyến thế năng
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastMac, 6);
  peerInfo.channel = WiFi.channel(); // Lấy đúng kênh WiFi thực tế sau khi đã kết nối router
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_STA;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("[ESP-NOW] Thêm Peer Quảng bá thất bại!");
  }

  // Phát thế năng hấp dẫn ngay lập tức lúc khởi động
  broadcastPotential();
}

// ==================== LOOP ====================
void loop() {
  unsigned long currentMillis = millis();

  // Tạm thời sử dụng dữ liệu giả lập cho cảm biến Master
  master_temp = 26.5; 
  master_gas = 120;

  // 1. Định kỳ phát quảng bá thế năng của Master (luôn là 0.0)
  if (currentMillis - lastPotentialBroadcastTime >= POTENTIAL_BROADCAST_INTERVAL) {
    broadcastPotential();
    lastPotentialBroadcastTime = currentMillis;
  }

  // 2. Đẩy dữ liệu toàn bộ các Node lên Web Server (Mỗi 3 giây)
  if (currentMillis - lastSendTime >= WEB_POST_INTERVAL) {
    lastSendTime = currentMillis;
    
    // Tự kết nối lại nếu rớt WiFi
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n--- Đẩy dữ liệu Mesh thu thập được lên Web Dashboard ---");
      // Đẩy dữ liệu tại chỗ của Master Node
      postReading("temp-master", "temp", master_temp);
      postReading("mq2-master", "mq2", (float)master_gas);

      // Đẩy dữ liệu thu được từ Satellite Node 1 (Mesh)
      postReading("temp-sat-1", "temp", s1_temp);
      postReading("mq2-sat-1", "mq2", (float)s1_gas);

      // Đẩy dữ liệu thu được từ Satellite Node 2 (Mesh)
      postReading("temp-sat-2", "temp", s2_temp);
      postReading("mq2-sat-2", "mq2", (float)s2_gas);

      // Đẩy dữ liệu thu được từ Satellite Node 3 (Mesh)
      postReading("temp-sat-3", "temp", s3_temp);
      postReading("mq2-sat-3", "mq2", (float)s3_gas);
    } else {
      Serial.println("\n[Cảnh báo] Mất kết nối WiFi, không thể gửi dữ liệu lên Web!");
    }
  }

  delay(30); 
}
