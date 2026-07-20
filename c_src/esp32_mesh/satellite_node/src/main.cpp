#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <common_config.h>
#include <mesh_packet.h>
#include <WiFiService.h>
#include <SensorService.h>

// ==================== CẤU HÌNH NODE VỆ TINH ====================
#define SATELLITE_ID 2         // ID duy nhất cho nút vệ tinh này

// Địa chỉ MAC quảng bá
uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint8_t myMac[6];

// Biến quản lý định tuyến (Routing table của node)
uint8_t nextHopMac[6] = {0, 0, 0, 0, 0, 0}; // Địa chỉ MAC của node tiếp theo hướng về Master
float myCost = 999.0;                      // Khoảng cách (số hop) về Master, mặc định vô cực
bool hasRoute = false;                     // Trạng thái đã tìm được đường về Master hay chưa
unsigned long lastRouteUpdateReceived = 0; // Thời điểm nhận được gói cập nhật định tuyến cuối cùng
const unsigned long ROUTE_TIMEOUT = 12000;  // Quá thời gian 12 giây không nhận tin -> coi như mất tuyến

// Timers
unsigned long lastSensorReadTime = 0;
unsigned long lastRouteBroadcastTime = 0;
const unsigned long SENSOR_READ_INTERVAL = 3000;
const unsigned long ROUTE_BROADCAST_INTERVAL = 5000;

// Cảm biến
float currentTemp = 0.0;
int currentGas = 0;
bool isEmergency = false;

// ==================== HÀM CẬP NHẬT HOẶC ĐĂNG KÝ PEER MỚI ====================
void updateNextHopPeer(const uint8_t *newMac) {
    if (hasRoute && memcmp(nextHopMac, newMac, 6) == 0) {
        if (esp_now_is_peer_exist(newMac)) {
            return;
        }
    }

    if (hasRoute) {
        esp_now_del_peer(nextHopMac);
    }

    memcpy(nextHopMac, newMac, 6);
    hasRoute = true;

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, nextHopMac, 6);
    peerInfo.channel = WIFI_CHANNEL_COMMON;
    peerInfo.encrypt = false;
    peerInfo.ifidx = WIFI_IF_STA;

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("[Mesh] Lỗi đăng ký Next Hop!");
    } else {
        Serial.printf("[Mesh] Đã đăng ký Next Hop mới: %02X:%02X:%02X:%02X:%02X:%02X\n",
                      nextHopMac[0], nextHopMac[1], nextHopMac[2], nextHopMac[3], nextHopMac[4], nextHopMac[5]);
    }
}

// ==================== PHÁT QUẢNG BÁ ĐỊNH TUYẾN ====================
void rebroadcastRouteUpdate() {
    if (!hasRoute) return;

    MeshPacket packet = {};
    packet.packetType = PACKET_ROUTE_UPDATE;
    memcpy(packet.sourceMac, myMac, 6);
    memset(packet.destMac, 0, 6);
    memset(packet.forwardMac, 0, 6);
    packet.id = SATELLITE_ID;
    packet.temp = 0.0;
    packet.gasRaw = 0;
    packet.emergency = false;
    packet.routingCost = myCost;

    esp_err_t result = esp_now_send(broadcastMac, (uint8_t *)&packet, sizeof(packet));
    Serial.printf("[Mesh] Quảng bá lại tuyến (Cost = %.1f) -> %s\n", myCost, result == ESP_OK ? "OK" : "FAIL");
}

// ==================== GỬI DỮ LIỆU CẢM BIẾN LÊN NEXT HOP ====================
void sendSensorData() {
    if (!hasRoute) {
        Serial.println("[Mesh Warning] Đang ngắt kết nối (mất định tuyến), không gửi được dữ liệu.");
        return;
    }

    MeshPacket packet = {};
    packet.packetType = PACKET_SENSOR_DATA;
    memcpy(packet.sourceMac, myMac, 6);
    memset(packet.destMac, 0, 6);
    memcpy(packet.forwardMac, nextHopMac, 6);
    packet.id = SATELLITE_ID;
    packet.temp = currentTemp;
    packet.gasRaw = currentGas;
    packet.emergency = isEmergency;
    packet.routingCost = myCost;

    esp_err_t result = esp_now_send(nextHopMac, (uint8_t *)&packet, sizeof(packet));
    
    Serial.print("[Mesh] Đã gửi Sensor Data lên Next Hop. Status: ");
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
    Serial.printf("[Debug Rx] Nhận gói từ MAC: %02X:%02X:%02X:%02X:%02X:%02X | Len: %d\n", 
                  senderMac[0], senderMac[1], senderMac[2], senderMac[3], senderMac[4], senderMac[5], len);

    if (len < sizeof(MeshPacket)) return;

    MeshPacket incomingPacket;
    memcpy(&incomingPacket, incomingDataRaw, sizeof(incomingPacket));

    if (incomingPacket.packetType == PACKET_ROUTE_UPDATE) {
        float newCost = incomingPacket.routingCost + 1.0;

        if (!hasRoute || newCost < myCost || (hasRoute && memcmp(nextHopMac, senderMac, 6) == 0)) {
            myCost = newCost;
            lastRouteUpdateReceived = millis();
            
            updateNextHopPeer(senderMac);
            Serial.printf("[Mesh] Cập nhật Route qua: %02X:%02X:%02X:%02X:%02X:%02X | Cost = %.1f\n", 
                          senderMac[0], senderMac[1], senderMac[2], senderMac[3], senderMac[4], senderMac[5], myCost);
        }
    }
    else if (incomingPacket.packetType == PACKET_SENSOR_DATA) {
        if (memcmp(incomingPacket.forwardMac, myMac, 6) == 0) {
            if (hasRoute) {
                memcpy(incomingPacket.forwardMac, nextHopMac, 6);
                
                esp_now_send(nextHopMac, (uint8_t *)&incomingPacket, sizeof(incomingPacket));
                Serial.printf("[Mesh Forward] Chuyển tiếp gói tin của Node %d lên Next Hop: %02X:%02X...\n", 
                              incomingPacket.id, nextHopMac[0], nextHopMac[1]);
            } else {
                Serial.println("[Mesh Warning] Nhận gói trung chuyển nhưng không có tuyến đi!");
            }
        }
    }
}

// ==================== CALLBACK GỬI DỮ LIỆU ====================
void OnDataSent(const esp_now_send_info_t *tx_info, esp_now_send_status_t status) {
}

// ==================== SETUP ====================
void setup() {
    Serial.begin(115200);
    SensorService::init("SATELLITE");

    WiFi.mode(WIFI_STA);
    esp_wifi_get_mac(WIFI_IF_STA, myMac);
    Serial.printf("\n=========================================\n");
    Serial.printf("MAC SATELLITE %d STA: %02X:%02X:%02X:%02X:%02X:%02X\n", 
                  SATELLITE_ID, myMac[0], myMac[1], myMac[2], myMac[3], myMac[4], myMac[5]);
    Serial.printf("=========================================\n");

    if (esp_now_init() == ESP_OK) {
        Serial.println("[ESP-NOW OK] Khởi tạo thành công.");
        esp_now_register_recv_cb(OnDataRecv);
        esp_now_register_send_cb(OnDataSent);
    } else {
        Serial.println("[ESP-NOW FAIL] Lỗi khởi tạo ESP-NOW!");
    }

    WiFiService::forceChannel(WIFI_CHANNEL_COMMON);

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, broadcastMac, 6);
    peerInfo.channel = WIFI_CHANNEL_COMMON;
    peerInfo.encrypt = false;
    peerInfo.ifidx = WIFI_IF_STA;
    
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("[ESP-NOW] Đăng ký Peer quảng bá thất bại!");
    }

    Serial.println("Khởi động Node Vệ tinh OK, đang quét tìm Master qua Mesh... ");
}

// ==================== LOOP ====================
void loop() {
    unsigned long currentMillis = millis();

    if (hasRoute && (currentMillis - lastRouteUpdateReceived >= ROUTE_TIMEOUT)) {
        Serial.println("[Mesh Warning] Mất liên lạc định tuyến. Reset Cost về vô cực!");
        myCost = 999.0;
        hasRoute = false;
        memset(nextHopMac, 0, 6);
    }

    if (currentMillis - lastSensorReadTime >= SENSOR_READ_INTERVAL) {
        lastSensorReadTime = currentMillis;

        SensorService::read(currentTemp, currentGas, isEmergency);
        SensorService::updateAlarm(isEmergency, hasRoute);
        SensorService::displaySatellite("Gradient", SATELLITE_ID, currentTemp, currentGas, hasRoute, myCost, nextHopMac);

        sendSensorData();
    }

    if (hasRoute && (currentMillis - lastRouteBroadcastTime >= ROUTE_BROADCAST_INTERVAL)) {
        lastRouteBroadcastTime = currentMillis;
        rebroadcastRouteUpdate();
    }

    delay(30);
}
