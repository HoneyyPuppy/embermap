#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_NeoPixel.h>

// ==================== CẤU HÌNH NODE VỆ TINH ====================
#define SATELLITE_ID 2         // ID duy nhất cho nút vệ tinh này

// Pins
#define ONE_WIRE_BUS 4        // Cảm biến DS18B20 (chân IO4)
#define MQ2_PIN 34            // Cảm biến khói MQ-2 (chân IO34, ADC1)
#define BUZZER_PIN 25         // Còi báo động (chân IO25)
#define NEOPIXEL_PIN 26       // LED RGB NeoPixel (chân IO26)
#define NUMPIXELS 1           // Số lượng LED NeoPixel

// OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Sensors
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
Adafruit_NeoPixel pixels(NUMPIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

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

// Variables
float currentTemp = 0.0;
int currentGas = 0;
bool isEmergency = false;

// Ngưỡng cảnh báo nguy hiểm
const float TEMP_THRESHOLD = 50.0;
const int GAS_THRESHOLD = 300;

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
      // Dịch chuyển các phần tử sau lên
      for (int j = i; j < neighborCount - 1; j++) {
        neighbors[j] = neighbors[j+1];
      }
      neighborCount--;
      i--; // giảm i để quét lại vị trí vừa dịch chuyển
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

  // Tìm láng giềng có THẾ NĂNG nhỏ nhất (Attractive nhất / ít nguy hiểm nhất)
  for (int i = 0; i < neighborCount; i++) {
    if (neighbors[i].potential < minPotential) {
      minPotential = neighbors[i].potential;
      minIdx = i;
    }
  }

  // standard APF rule: Chỉ đi xuống nơi có thế năng thấp hơn mình
  if (minIdx != -1 && minPotential < totalPotential) {
    memcpy(nextHopMac, neighbors[minIdx].mac, 6);
    myHopCount = neighbors[minIdx].hopCount + 1;
    basePotential = myHopCount * 100.0;
    hasRoute = true;

    // Đăng ký Next Hop làm peer ESP-NOW
    if (!esp_now_is_peer_exist(nextHopMac)) {
      esp_now_peer_info_t peerInfo = {};
      memcpy(peerInfo.peer_addr, nextHopMac, 6);
      peerInfo.channel = 8;
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
  packet.potential = totalPotential; // Quảng bá thế năng tổng hợp hiện tại
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
  memcpy(packet.forwardMac, nextHopMac, 6); // Gửi tới Next Hop có thế năng thấp nhất
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

  // 1. Nhận tin quảng bá thế năng từ láng giềng
  if (incomingPacket.packetType == PACKET_POTENTIAL_ADVERT) {
    updateNeighbor(senderMac, incomingPacket.potential, incomingPacket.hopCount);
    
    // Cập nhật lại đường đi thế năng tối ưu
    selectNextHop();
  }
  // 2. Nhận gói Sensor Data cần chuyển tiếp
  else if (incomingPacket.packetType == PACKET_SENSOR_DATA) {
    // Nếu gói tin chuyển tiếp có đích forwardMac là mình
    if (memcmp(incomingPacket.forwardMac, myMac, 6) == 0) {
      // Cập nhật lại Next Hop mới nhất tránh vùng nghẽn/vùng cháy
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

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("[OLED FAIL] Lỗi khởi tạo màn hình SSD1306"));
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("Khoi dong APF...");
  display.display();

  pixels.begin();
  pixels.setPixelColor(0, pixels.Color(0, 0, 255)); // Màu xanh lam lúc khởi động
  pixels.show();

  sensors.begin();
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

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

  forceWifiChannel(8);

  // Đăng ký Peer quảng bá để trao đổi thế năng
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastMac, 6);
  peerInfo.channel = 8;
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

  // 1. Đọc cảm biến và tính toán Lực đẩy thế năng (Repulsive Potential)
  if (currentMillis - lastSensorReadTime >= SENSOR_READ_INTERVAL) {
    lastSensorReadTime = currentMillis;

    sensors.requestTemperatures();
    currentTemp = sensors.getTempCByIndex(0);
    if (currentTemp == DEVICE_DISCONNECTED_C) {
      currentTemp = 28.5; // Giả lập
    }
    currentGas = analogRead(MQ2_PIN);

    // Xác định trạng thái khẩn cấp
    isEmergency = (currentTemp >= TEMP_THRESHOLD || currentGas >= GAS_THRESHOLD);

    // Tính toán thế năng đẩy (Repulsive Potential) tỷ lệ với độ nguy hiểm
    float rep = 0.0;
    if (currentTemp > 45.0) {
      rep += (currentTemp - 40.0) * 100.0; // Nhiệt tăng thêm 1 độ -> Cộng 100 thế năng đẩy
    }
    if (currentGas > 200) {
      rep += (currentGas - 200) * 3.0;    // Gas tăng -> Tăng thế năng đẩy
    }
    repulsivePotential = rep;

    // Tính toán Thế năng Tổng hợp của bản thân
    if (myHopCount == 255) {
      basePotential = 9999.0;
    } else {
      basePotential = myHopCount * 100.0;
    }
    totalPotential = basePotential + repulsivePotential;

    // Cập nhật lại tuyến đường tốt nhất dựa trên thế năng mới của mình
    selectNextHop();

    // Điều khiển còi và Led
    if (isEmergency) {
      digitalWrite(BUZZER_PIN, HIGH);
      pixels.setPixelColor(0, pixels.Color(255, 0, 0)); // Đỏ: cháy/nguy hiểm (Lực đẩy cực cao)
    } else {
      digitalWrite(BUZZER_PIN, LOW);
      if (hasRoute) {
        // Nếu thế năng đẩy cao (nhưng chưa đến mức báo cháy), đổi sang màu vàng cảnh báo
        if (repulsivePotential > 0.0) {
          pixels.setPixelColor(0, pixels.Color(255, 255, 0)); // Vàng: cảnh báo nóng/gas nhẹ
        } else {
          pixels.setPixelColor(0, pixels.Color(0, 255, 0));   // Xanh lá: an toàn
        }
      } else {
        pixels.setPixelColor(0, pixels.Color(255, 165, 0));   // Cam: mất định tuyến
      }
    }
    pixels.show();

    // Cập nhật màn hình OLED
    display.clearDisplay();
    display.setCursor(0,0);
    display.printf("NODE ID: %d (APF)\n", SATELLITE_ID);
    display.printf("Temp: %.1f C\n", currentTemp);
    display.printf("Gas: %d\n", currentGas);
    display.printf("My U: %.0f\n", totalPotential);
    if (hasRoute) {
      display.printf("Hop: %d\n", myHopCount);
      display.printf("Next: %02X:%02X...\n", nextHopMac[0], nextHopMac[1]);
    } else {
      display.println("Status: MAT TUYEN");
    }
    display.display();

    // Gửi cảm biến lên Next Hop có thế năng thấp nhất
    sendSensorData();
  }

  // 2. Định kỳ quảng bá thế năng của bản thân để cập nhật trường thế năng cho các láng giềng
  if (currentMillis - lastPotentialBroadcastTime >= POTENTIAL_BROADCAST_INTERVAL) {
    lastPotentialBroadcastTime = currentMillis;
    broadcastPotential();
  }

  delay(30);
}
