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

// ==================== ĐỊNH NGHĨA CẤU TRÚC GÓI TIN RPL MESH ====================
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

// Địa chỉ MAC quảng bá
uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint8_t myMac[6];

// RPL Routing Variables
uint8_t parentMac[6] = {0, 0, 0, 0, 0, 0};
uint16_t myRank = 0xFFFF; // Cực đại lúc khởi động (chưa gia nhập DODAG)
uint8_t dodagVersion = 0;
bool hasParent = false;
unsigned long lastParentContactTime = 0;
const unsigned long PARENT_TIMEOUT = 12000;          // Mất liên lạc sau 12 giây
const uint16_t RANK_INCREASE = 256;                 // Bước nhảy Rank trong RPL

// Timers
unsigned long lastSensorReadTime = 0;
unsigned long lastDioRebroadcastTime = 0;
unsigned long lastDaoSendTime = 0;
const unsigned long SENSOR_READ_INTERVAL = 3000;    // Đọc cảm biến & gửi dữ liệu mỗi 3 giây
const unsigned long DIO_REBROADCAST_INTERVAL = 5000; // Quảng bá DIO mỗi 5 giây
const unsigned long DAO_SEND_INTERVAL = 4000;       // Gửi DAO báo cáo lộ trình mỗi 4 giây

// Variables
float currentTemp = 0.0;
int currentGas = 0;
bool isEmergency = false;

// Ngưỡng cảnh báo nguy hiểm (Cháy/Gas)
const float TEMP_THRESHOLD = 50.0;
const int GAS_THRESHOLD = 300;

// ==================== CẬP NHẬT PARENT / NEXT HOP PEER ====================
void updateParentPeer(const uint8_t *newParentMac) {
  // Nếu parent cũ đã đăng ký, không cần xóa trừ khi thay đổi MAC
  if (hasParent && memcmp(parentMac, newParentMac, 6) == 0) {
    if (esp_now_is_peer_exist(newParentMac)) {
      return;
    }
  }

  // Xóa parent cũ khỏi danh sách Peer nếu có
  if (hasParent) {
    esp_now_del_peer(parentMac);
  }

  // Lưu parent mới
  memcpy(parentMac, newParentMac, 6);
  hasParent = true;

  // Đăng ký Peer mới vào ESP-NOW
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, parentMac, 6);
  peerInfo.channel = 8; // Khóa kênh 8 giống Master
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_STA;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("[RPL] Lỗi đăng ký Parent Peer!");
  } else {
    Serial.printf("[RPL] Đã đăng ký Parent mới: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  parentMac[0], parentMac[1], parentMac[2], parentMac[3], parentMac[4], parentMac[5]);
  }
}

// ==================== HÀM GỬI BÁO CÁO LỘ TRÌNH DAO ====================
void sendDao() {
  if (!hasParent) return;

  MeshPacket packet;
  packet.packetType = PACKET_RPL_DAO;
  memcpy(packet.sourceMac, myMac, 6);
  memset(packet.destMac, 0, 6); // Sẽ truyền qua parent lên Root
  memcpy(packet.forwardMac, parentMac, 6); // Parent chính là forwardMac
  packet.id = SATELLITE_ID;
  packet.temp = 0.0;
  packet.gasRaw = 0;
  packet.emergency = false;
  packet.rank = myRank;
  packet.version = dodagVersion;

  esp_err_t result = esp_now_send(parentMac, (uint8_t *)&packet, sizeof(packet));
  Serial.print("[RPL] Gửi gói tin DAO lên Parent -> ");
  if (result == ESP_OK) {
    Serial.println("OK");
  } else {
    Serial.print("FAIL: ");
    Serial.println(result);
  }
}

// ==================== HÀM QUẢNG BÁ DIO ĐỂ NUÔI MẠNG MESH DOWNSTREAM ====================
void broadcastDio() {
  if (!hasParent) return; // Chỉ phát DIO khi đã gia nhập DODAG hình cây

  MeshPacket packet;
  packet.packetType = PACKET_RPL_DIO;
  memcpy(packet.sourceMac, myMac, 6);
  memcpy(packet.destMac, broadcastMac, 6);
  memset(packet.forwardMac, 0, 6);
  packet.id = SATELLITE_ID;
  packet.temp = 0.0;
  packet.gasRaw = 0;
  packet.emergency = false;
  packet.rank = myRank;
  packet.version = dodagVersion;

  esp_err_t result = esp_now_send(broadcastMac, (uint8_t *)&packet, sizeof(packet));
  Serial.printf("[RPL] Phát tiếp DIO (Rank = %d, Ver = %d) -> %s\n", 
                myRank, dodagVersion, result == ESP_OK ? "OK" : "FAIL");
}

// ==================== GỬI DỮ LIỆU CẢM BIẾN LÊN PARENT ====================
void sendSensorData() {
  if (!hasParent) {
    Serial.println("[RPL Warning] Không thể gửi cảm biến: Chưa gia nhập cây DODAG.");
    return;
  }

  MeshPacket packet;
  packet.packetType = PACKET_SENSOR_DATA;
  memcpy(packet.sourceMac, myMac, 6);
  memset(packet.destMac, 0, 6); // Sẽ được Master xử lý khi đến đích
  memcpy(packet.forwardMac, parentMac, 6); // Chuyển tiếp tới Parent tiếp theo
  packet.id = SATELLITE_ID;
  packet.temp = currentTemp;
  packet.gasRaw = currentGas;
  packet.emergency = isEmergency;
  packet.rank = myRank;
  packet.version = dodagVersion;

  esp_err_t result = esp_now_send(parentMac, (uint8_t *)&packet, sizeof(packet));
  
  Serial.print("[RPL Data] Gửi gói tin cảm biến tới Parent -> ");
  if (result == ESP_OK) {
    Serial.println("OK");
  } else {
    Serial.print("FAIL: ");
    Serial.println(result);
  }
}

// ==================== CALLBACK NHẬN DỮ LIỆU ESP-NOW ====================
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingDataRaw, int len) {
  const uint8_t* senderMac = recv_info->src_addr;
  Serial.printf("[Debug Rx] Nhận gói từ MAC: %02X:%02X... | Len: %d\n", senderMac[0], senderMac[1], len);

  if (len < sizeof(MeshPacket)) return;

  MeshPacket incomingPacket;
  memcpy(&incomingPacket, incomingDataRaw, sizeof(incomingPacket));

  // 1. Xử lý gói tin quảng bá DIO (RPL Route Discovery)
  if (incomingPacket.packetType == PACKET_RPL_DIO) {
    uint16_t senderRank = incomingPacket.rank;
    uint16_t calculatedRank = senderRank + RANK_INCREASE;

    Serial.printf("[RPL RX] Nhận DIO từ MAC: %02X:%02X... | Sender Rank: %d | Rank tính toán: %d\n",
                  senderMac[0], senderMac[1], senderRank, calculatedRank);

    // Luật chọn Parent trong RPL: Chọn node có Rank nhỏ nhất
    if (!hasParent || calculatedRank < myRank || (hasParent && memcmp(parentMac, senderMac, 6) == 0)) {
      myRank = calculatedRank;
      dodagVersion = incomingPacket.version;
      lastParentContactTime = millis();
      
      updateParentPeer(senderMac);
      Serial.printf("[RPL Update] Cập nhật Rank = %d qua Parent: %02X:%02X...\n", 
                    myRank, parentMac[0], parentMac[1]);

      // Gửi ngay gói tin DAO báo cáo lên Parent mới
      sendDao();
    }
  }
  // 2. Chuyển tiếp (Forward) dữ liệu cảm biến từ nút con khác ở tầng dưới
  else if (incomingPacket.packetType == PACKET_SENSOR_DATA) {
    // Nếu gói tin có địa chỉ forwardMac khớp với địa chỉ MAC của mình
    if (memcmp(incomingPacket.forwardMac, myMac, 6) == 0) {
      if (hasParent) {
        // Thay đổi địa chỉ forwardMac thành Parent của mình và gửi tiếp đi
        memcpy(incomingPacket.forwardMac, parentMac, 6);
        
        esp_now_send(parentMac, (uint8_t *)&incomingPacket, sizeof(incomingPacket));
        Serial.printf("[RPL Forward] Chuyển tiếp gói tin của Node %d lên Parent: %02X:%02X...\n", 
                      incomingPacket.id, parentMac[0], parentMac[1]);
      } else {
        Serial.println("[RPL Warning] Không thể chuyển tiếp: Chưa có Parent!");
      }
    }
  }
}

// ==================== CALLBACK GỬI DỮ LIỆU ====================
void OnDataSent(const esp_now_send_info_t *tx_info, esp_now_send_status_t status) {
}

// ==================== CHẨN ĐOÁN & ÉP KÊNH SÓNG WIFI SATELLITE ====================
void forceWifiChannel(uint8_t channel) {
  WiFi.mode(WIFI_STA);
  esp_wifi_start();
  
  esp_err_t err = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  Serial.printf("Trạng thái ép kênh %d: %s\n", channel, esp_err_to_name(err));
  Serial.printf("Kênh WiFi hiện tại của Satellite: %d\n", WiFi.channel());
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);

  // Khởi tạo OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("[OLED FAIL] Lỗi khởi tạo màn hình SSD1306"));
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("Khoi dong...");
  display.display();

  // Khởi tạo NeoPixel
  pixels.begin();
  pixels.setPixelColor(0, pixels.Color(0, 0, 255)); // Màu xanh lam báo hiệu đang khởi động
  pixels.show();

  // Khởi tạo cảm biến DS18B20
  sensors.begin();

  // Khởi tạo Buzzer
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // Lấy địa chỉ MAC
  WiFi.mode(WIFI_STA);
  esp_wifi_get_mac(WIFI_IF_STA, myMac);
  Serial.printf("\n=========================================\n");
  Serial.printf("MAC SATELLITE %d STA (RPL): %02X:%02X:%02X:%02X:%02X:%02X\n", 
                SATELLITE_ID, myMac[0], myMac[1], myMac[2], myMac[3], myMac[4], myMac[5]);
  Serial.printf("=========================================\n");

  // Khởi tạo ESP-NOW
  if (esp_now_init() == ESP_OK) {
    Serial.println("[ESP-NOW OK] Khởi tạo thành công.");
    esp_now_register_recv_cb(OnDataRecv);
    esp_now_register_send_cb(OnDataSent);
  } else {
    Serial.println("[ESP-NOW FAIL] Lỗi khởi tạo ESP-NOW!");
  }

  // Ép kênh sóng cố định về kênh 8 (kênh hoạt động của Master) sau khi init ESP-NOW
  forceWifiChannel(8);

  // Đăng ký Peer quảng bá để có thể nhận và gửi tiếp gói tin DIO quảng bá
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastMac, 6);
  peerInfo.channel = 8;
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_STA;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("[ESP-NOW] Đăng ký Peer quảng bá thất bại!");
  }

  Serial.println("Khởi động Node Vệ tinh RPL OK, đang quét tìm Parent qua DIO...");
}

// ==================== LOOP ====================
void loop() {
  unsigned long currentMillis = millis();

  // 1. Kiểm tra timeout liên lạc với Parent
  if (hasParent && (currentMillis - lastParentContactTime >= PARENT_TIMEOUT)) {
    Serial.println("[RPL Warning] Mất liên lạc với Parent. Reset Rank về vô cực!");
    myRank = 0xFFFF;
    hasParent = false;
    memset(parentMac, 0, 6);
  }

  // 2. Đọc cảm biến và gửi dữ liệu lên parent định kỳ
  if (currentMillis - lastSensorReadTime >= SENSOR_READ_INTERVAL) {
    lastSensorReadTime = currentMillis;

    // Đọc DS18B20
    sensors.requestTemperatures();
    currentTemp = sensors.getTempCByIndex(0);
    if (currentTemp == DEVICE_DISCONNECTED_C) {
      currentTemp = 28.5; // Giả lập nếu không có cảm biến vật lý
    }

    // Đọc MQ-2
    currentGas = analogRead(MQ2_PIN);

    // Xác định trạng thái khẩn cấp
    isEmergency = (currentTemp >= TEMP_THRESHOLD || currentGas >= GAS_THRESHOLD);

    // Điều khiển còi và Led NeoPixel
    if (isEmergency) {
      digitalWrite(BUZZER_PIN, HIGH);
      pixels.setPixelColor(0, pixels.Color(255, 0, 0)); // Màu đỏ nhấp nháy cảnh báo nguy hại
    } else {
      digitalWrite(BUZZER_PIN, LOW);
      if (hasParent) {
        pixels.setPixelColor(0, pixels.Color(0, 255, 0)); // Màu xanh lá cây: Hoạt động bình thường
      } else {
        pixels.setPixelColor(0, pixels.Color(255, 165, 0)); // Màu cam: Chưa nhận định tuyến
      }
    }
    pixels.show();

    // Cập nhật màn hình OLED
    display.clearDisplay();
    display.setCursor(0,0);
    display.printf("NODE ID: %d (RPL)\n", SATELLITE_ID);
    display.printf("Temp: %.1f C\n", currentTemp);
    display.printf("Gas: %d\n", currentGas);
    if (hasParent) {
      display.printf("Rank: %d\n", myRank);
      display.printf("Parent: %02X:%02X...\n", parentMac[0], parentMac[1]);
    } else {
      display.println("Status: Mất tuyến");
    }
    display.display();

    // Gửi gói tin cảm biến qua cây định tuyến
    sendSensorData();
  }

  // 3. Định kỳ phát quảng bá tiếp DIO cho các node tầng dưới
  if (hasParent && (currentMillis - lastDioRebroadcastTime >= DIO_REBROADCAST_INTERVAL)) {
    lastDioRebroadcastTime = currentMillis;
    broadcastDio();
  }

  // 4. Định kỳ gửi DAO báo cáo lộ trình ngược
  if (hasParent && (currentMillis - lastDaoSendTime >= DAO_SEND_INTERVAL)) {
    lastDaoSendTime = currentMillis;
    sendDao();
  }

  delay(30);
}
