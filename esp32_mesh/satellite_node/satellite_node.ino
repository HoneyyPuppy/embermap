#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_mac.h>

// ==================== CẤU HÌNH ĐỊNH DANH NODE ====================
// TRƯỚC KHI NẠP CHO MỖI CON: Đổi NODE_ID thành các số khác nhau (1, 2, 3...)
#define NODE_ID     2   

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

// Biến lưu trữ thông tin định tuyến (Gradient Routing)
float my_cost = 999.0;                  // Ban đầu chưa có đường đi (vô cực)
uint8_t next_hop_mac[6] = {0,0,0,0,0,0}; // Địa chỉ MAC của node tiếp theo để gửi tin
unsigned long lastRouteRecvTime = 0;
const unsigned long ROUTE_TIMEOUT = 12000;  // Nếu 12 giây không có cập nhật định tuyến, reset đường truyền

unsigned long lastSendTime = 0;
const unsigned long DATA_SEND_INTERVAL = 3000; // Gửi dữ liệu định kỳ mỗi 3 giây

// ==================== TODO: CẢM BIẾN VẬT LÝ & LED & CÒI CỤC BỘ ====================
// TODO: Cấu hình chân kết nối và khởi tạo cảm biến DS18B20 (chân GPIO 4)
// TODO: Cấu hình chân kết nối cảm biến MQ-2 (chân GPIO 32, 33)
// TODO: Cấu hình dải LED WS2812B NeoPixel (chân GPIO 12, 30 bóng LED)
// TODO: Cấu hình còi báo động Buzzer (chân GPIO 13)
// TODO: Xây dựng hàm đọc nhiệt độ từ DS18B20 thực tế
// TODO: Xây dựng hàm đọc nồng độ gas MQ-2 thực tế
// TODO: Xây dựng thuật toán còi/LED cảnh báo tại chỗ theo nhiệt độ và trạng thái gas

// ==================== HÀM QUẢN LÝ PEER (ĐĂNG KÝ GỬI TIN) ====================
void updateNextHopPeer(const uint8_t *newMac) {
  // Nếu MAC mới là MAC rỗng (00:00...) thì bỏ qua
  bool isZero = true;
  for (int i = 0; i < 6; i++) {
    if (newMac[i] != 0) isZero = false;
  }
  if (isZero) return;

  // Nếu MAC mới trùng với next_hop_mac hiện tại và đã đăng ký thì không cần làm gì
  if (memcmp(next_hop_mac, newMac, 6) == 0 && esp_now_is_peer_exist(newMac)) {
    return;
  }

  // Xóa peer cũ nếu có
  if (esp_now_is_peer_exist(next_hop_mac)) {
    esp_now_del_peer(next_hop_mac);
  }

  // Lưu MAC mới
  memcpy(next_hop_mac, newMac, 6);

  // Đăng ký peer mới
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, next_hop_mac, 6);
  peerInfo.channel = 8; // Ép kênh 8 giống Master
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_STA;

  if (esp_now_add_peer(&peerInfo) == ESP_OK) {
    Serial.printf("[Mesh] Đã đăng ký Next Hop mới: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  next_hop_mac[0], next_hop_mac[1], next_hop_mac[2],
                  next_hop_mac[3], next_hop_mac[4], next_hop_mac[5]);
  } else {
    Serial.println("[Mesh] Lỗi đăng ký Next Hop!");
  }
}

// ==================== CALLBACK TRẠNG THÁI GỬI TIN ====================
void OnDataSent(const esp_now_send_info_t *tx_info, esp_now_send_status_t status) {
  // Trạng thái gửi tin
  // Dùng để debug chất lượng liên kết khi cần
}

// ==================== CALLBACK NHẬN DỮ LIỆU ESP-NOW ====================
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingDataRaw, int len) {
  const uint8_t* senderMac = recv_info->src_addr;
  Serial.printf("[Debug Rx] Nhận gói từ MAC: %02X:%02X:%02X:%02X:%02X:%02X | Len: %d\n", 
                senderMac[0], senderMac[1], senderMac[2], senderMac[3], senderMac[4], senderMac[5], len);

  if (len < sizeof(MeshPacket)) return;

  MeshPacket incomingPacket;
  memcpy(&incomingPacket, incomingDataRaw, sizeof(incomingPacket));

  // 1. Xử lý Gói tin Cập nhật Định tuyến (Route Update)
  if (incomingPacket.packetType == PACKET_ROUTE_UPDATE) {
    float newCost = incomingPacket.routingCost + 1.0;
    
    // Nếu tìm thấy đường đi ngắn hơn (hoặc cập nhật từ nút cha hiện tại)
    if (newCost < my_cost || memcmp(next_hop_mac, senderMac, 6) == 0) {
      bool costChanged = (newCost != my_cost || memcmp(next_hop_mac, senderMac, 6) != 0);
      
      my_cost = newCost;
      updateNextHopPeer(senderMac);
      lastRouteRecvTime = millis();

      if (costChanged) {
        Serial.printf("[Mesh] Cập nhật Route qua: %02X:%02X:%02X:%02X:%02X:%02X | Cost = %.1f\n",
                      senderMac[0], senderMac[1], senderMac[2],
                      senderMac[3], senderMac[4], senderMac[5], my_cost);
        
        // Rebroadcast gói cập nhật định tuyến này cho các node ở xa hơn
        MeshPacket updatePacket = incomingPacket;
        memcpy(updatePacket.sourceMac, myMac, 6);
        updatePacket.routingCost = my_cost;

        esp_now_send(broadcastMac, (uint8_t *)&updatePacket, sizeof(updatePacket));
      }
    }
  }
  // 2. Xử lý Gói tin Dữ liệu Cảm biến (Sensor Data Forwarding / Relay)
  else if (incomingPacket.packetType == PACKET_SENSOR_DATA) {
    // Chỉ chuyển tiếp nếu forwardMac chỉ định đến chính MAC của ta
    if (memcmp(incomingPacket.forwardMac, myMac, 6) == 0) {
      if (my_cost < 999.0) {
        Serial.printf("[Mesh] Nhận gói Relay từ Node %d. Đang chuyển tiếp tới Next Hop...\n", incomingPacket.id);
        
        // Thay đổi forwardMac thành Next Hop của mình
        memcpy(incomingPacket.forwardMac, next_hop_mac, 6);
        
        // Chuyển tiếp đi
        esp_now_send(next_hop_mac, (uint8_t *)&incomingPacket, sizeof(incomingPacket));
      } else {
        Serial.printf("[Mesh] Nhận gói Relay từ Node %d nhưng không có đường đi! Hủy gói tin.\n", incomingPacket.id);
      }
    }
  }
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);

  // Khởi tạo WiFi chế độ STA để đọc MAC chính xác
  WiFi.mode(WIFI_STA);
  esp_read_mac(myMac, ESP_MAC_WIFI_STA);
  Serial.printf("\n=========================================\n");
  Serial.printf("MAC SATELLITE %d STA: %02X:%02X:%02X:%02X:%02X:%02X\n", 
                NODE_ID, myMac[0], myMac[1], myMac[2], myMac[3], myMac[4], myMac[5]);
  Serial.printf("=========================================\n");

  // TODO: Khởi tạo cảm biến vật lý, NeoPixel và còi báo động tại chỗ

  // Khởi tạo ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Khởi động ESP-NOW thất bại!");
    return;
  }

  // --- ÉP KÊNH WIFI VỀ KÊNH 8 TRÙNG VỚI MASTER ---
  esp_wifi_set_promiscuous(true);
  esp_err_t err = esp_wifi_set_channel(8, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  
  Serial.printf("Trạng thái ép kênh 8: %s\n", esp_err_to_name(err));
  Serial.printf("Kênh WiFi hiện tại của Satellite: %d\n", WiFi.channel());

  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  // Đăng ký Peer quảng bá để rebroadcast gói tin định tuyến
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastMac, 6);
  peerInfo.channel = 8;
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_STA;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("[Mesh] Đăng ký Peer Quảng bá thất bại!");
  }

  Serial.println("Khởi động Node Vệ tinh OK, đang quét tìm Master qua Mesh...");
}

// ==================== LOOP ====================
void loop() {
  unsigned long currentMillis = millis();

  // 1. Kiểm tra Timeout liên kết định tuyến
  if (my_cost < 999.0 && (currentMillis - lastRouteRecvTime > ROUTE_TIMEOUT)) {
    Serial.println("[Mesh Warning] Mất liên lạc định tuyến. Reset Cost về vô cực!");
    my_cost = 999.0;
    memset(next_hop_mac, 0, 6); // Xóa Next Hop
  }

  // 2. TODO: Đọc cảm biến thực tế tại chỗ
  // Giả lập dữ liệu cảm biến
  float local_temp = 27.2;
  int local_gas = 150;
  bool isEmergency = false;

  // TODO: Điều khiển LED và còi báo động tại chỗ

  // 3. Định kỳ gửi dữ liệu cảm biến lên Next Hop
  if (currentMillis - lastSendTime >= DATA_SEND_INTERVAL) {
    lastSendTime = currentMillis;

    if (my_cost < 999.0) {
      MeshPacket dataPacket;
      dataPacket.packetType = PACKET_SENSOR_DATA;
      memcpy(dataPacket.sourceMac, myMac, 6);
      memset(dataPacket.destMac, 0, 6); // Đích cuối cùng mặc định là Master được định vị ở đầu kia
      memcpy(dataPacket.forwardMac, next_hop_mac, 6);
      dataPacket.id = NODE_ID;
      dataPacket.temp = local_temp;
      dataPacket.gasRaw = local_gas;
      dataPacket.emergency = isEmergency;
      dataPacket.routingCost = my_cost;

      esp_err_t result = esp_now_send(next_hop_mac, (uint8_t *)&dataPacket, sizeof(dataPacket));
      
      Serial.printf("[Mesh] Đã gửi Sensor Data lên Next Hop. Status: %s\n", 
                    (result == ESP_OK) ? "OK" : "FAIL");
    } else {
      Serial.println("[Mesh] Đang ngắt kết nối (mất định tuyến), không gửi được dữ liệu.");
    }
  }

  delay(30);
}
