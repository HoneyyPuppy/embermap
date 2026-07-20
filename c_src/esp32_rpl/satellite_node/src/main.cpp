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

// RPL Routing Variables
uint8_t parentMac[6] = {0, 0, 0, 0, 0, 0};
uint16_t myRank = 0xFFFF; // Cực đại lúc khởi động (chưa gia nhập DODAG)
uint8_t dodagVersion = 0;
bool hasParent = false;
unsigned long lastParentContactTime = 0;
const unsigned long PARENT_TIMEOUT = 12000;
const uint16_t RANK_INCREASE = 256;

// Lịch sử đường truyền về Master (để chống lặp vòng định tuyến)
uint8_t parentRoutePath[MAX_ROUTE_PATH];
uint8_t parentRoutePathLen = 0;

// Timers
unsigned long lastSensorReadTime = 0;
unsigned long lastDioRebroadcastTime = 0;
unsigned long lastDaoSendTime = 0;
const unsigned long SENSOR_READ_INTERVAL = 3000;
const unsigned long DIO_REBROADCAST_INTERVAL = 5000;
const unsigned long DAO_SEND_INTERVAL = 4000;

// Cảm biến
float currentTemp = 0.0;
int currentGas = 0;
bool isEmergency = false;

// ==================== CẤP NHẬT PARENT / NEXT HOP PEER ====================
void updateParentPeer(const uint8_t *newParentMac) {
    if (hasParent && memcmp(parentMac, newParentMac, 6) == 0) {
        if (esp_now_is_peer_exist(newParentMac)) {
            return;
        }
    }

    if (hasParent) {
        esp_now_del_peer(parentMac);
    }

    memcpy(parentMac, newParentMac, 6);
    hasParent = true;

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, parentMac, 6);
    peerInfo.channel = WIFI_CHANNEL_COMMON;
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

    MeshPacket packet = {};
    packet.packetType = PACKET_RPL_DAO;
    memcpy(packet.sourceMac, myMac, 6);
    memset(packet.destMac, 0, 6);
    memcpy(packet.forwardMac, parentMac, 6);
    packet.id = SATELLITE_ID;
    packet.temp = 0.0;
    packet.gasRaw = 0;
    packet.emergency = false;
    packet.rank = myRank;
    packet.version = dodagVersion;

    esp_err_t result = esp_now_send(parentMac, (uint8_t *)&packet, sizeof(packet));
    Serial.printf("[RPL] Gửi gói tin DAO lên Parent -> %s\n", result == ESP_OK ? "OK" : "FAIL");
}

// ==================== HÀM QUẢNG BÁ DIO ĐỂ NUÔI MẠNG MESH DOWNSTREAM ====================
void broadcastDio() {
    if (!hasParent) return;

    MeshPacket packet = {};
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

    // Đính kèm ID bản thân vào routePath để truyền tiếp (Loop Prevention)
    memcpy(packet.routePath, parentRoutePath, parentRoutePathLen);
    packet.routePathLen = parentRoutePathLen;
    if (packet.routePathLen < MAX_ROUTE_PATH) {
        packet.routePath[packet.routePathLen] = SATELLITE_ID;
        packet.routePathLen++;
    }

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

    MeshPacket packet = {};
    packet.packetType = PACKET_SENSOR_DATA;
    memcpy(packet.sourceMac, myMac, 6);
    memset(packet.destMac, 0, 6);
    memcpy(packet.forwardMac, parentMac, 6);
    packet.id = SATELLITE_ID;
    packet.temp = currentTemp;
    packet.gasRaw = currentGas;
    packet.emergency = isEmergency;
    packet.rank = myRank;
    packet.version = dodagVersion;

    esp_err_t result = esp_now_send(parentMac, (uint8_t *)&packet, sizeof(packet));
    Serial.printf("[RPL Data] Gửi gói tin cảm biến tới Parent -> %s\n", result == ESP_OK ? "OK" : "FAIL");
}

// ==================== CALLBACK NHẬN DỮ LIỆU ESP-NOW ====================
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingDataRaw, int len) {
    const uint8_t* senderMac = recv_info->src_addr;
    Serial.printf("[Debug Rx] Nhận gói từ MAC: %02X:%02X... | Len: %d\n", senderMac[0], senderMac[1], len);

    if (len < sizeof(MeshPacket)) return;

    MeshPacket incomingPacket;
    memcpy(&incomingPacket, incomingDataRaw, sizeof(incomingPacket));

    if (incomingPacket.packetType == PACKET_RPL_DIO) {
        // Kiểm tra tránh lặp vòng định tuyến
        if (pathContainsNode(incomingPacket.routePath, incomingPacket.routePathLen, SATELLITE_ID)) {
            Serial.println("[RPL Warning] Phát hiện lặp vòng định tuyến (Loop)! Bỏ qua gói cập nhật.");
            return;
        }

        uint16_t senderRank = incomingPacket.rank;
        uint16_t calculatedRank = senderRank + RANK_INCREASE;

        Serial.printf("[RPL RX] Nhận DIO từ MAC: %02X:%02X... | Sender Rank: %d | Rank tính toán: %d\n",
                      senderMac[0], senderMac[1], senderRank, calculatedRank);

        if (!hasParent || calculatedRank < myRank || (hasParent && memcmp(parentMac, senderMac, 6) == 0)) {
            myRank = calculatedRank;
            dodagVersion = incomingPacket.version;
            lastParentContactTime = millis();
            
            // Lưu lịch sử đường truyền của Parent
            memcpy(parentRoutePath, incomingPacket.routePath, incomingPacket.routePathLen);
            parentRoutePathLen = incomingPacket.routePathLen;

            updateParentPeer(senderMac);
            Serial.printf("[RPL Update] Cập nhật Rank = %d qua Parent: %02X:%02X...\n", 
                          myRank, parentMac[0], parentMac[1]);

            sendDao();
        }
    }
    else if (incomingPacket.packetType == PACKET_SENSOR_DATA) {
        if (memcmp(incomingPacket.forwardMac, myMac, 6) == 0) {
            if (hasParent) {
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

// ==================== SETUP ====================
void setup() {
    Serial.begin(115200);
    SensorService::init("SATELLITE");

    WiFi.mode(WIFI_STA);
    esp_wifi_get_mac(WIFI_IF_STA, myMac);
    Serial.printf("\n=========================================\n");
    Serial.printf("MAC SATELLITE %d STA (RPL): %02X:%02X:%02X:%02X:%02X:%02X\n", 
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

    Serial.println("Khởi động Node Vệ tinh RPL OK, đang quét tìm Parent qua DIO...");
}

// ==================== LOOP ====================
void loop() {
    unsigned long currentMillis = millis();

    if (hasParent && (currentMillis - lastParentContactTime >= PARENT_TIMEOUT)) {
        Serial.println("[RPL Warning] Mất liên lạc với Parent. Reset Rank về vô cực!");
        myRank = 0xFFFF;
        hasParent = false;
        memset(parentMac, 0, 6);
        parentRoutePathLen = 0; // Reset lịch sử đường truyền
    }

    if (currentMillis - lastSensorReadTime >= SENSOR_READ_INTERVAL) {
        lastSensorReadTime = currentMillis;

        SensorService::read(currentTemp, currentGas, isEmergency);
        SensorService::updateAlarm(isEmergency, hasParent);
        SensorService::displaySatellite("RPL", SATELLITE_ID, currentTemp, currentGas, hasParent, myRank, parentMac);

        sendSensorData();
    }

    if (hasParent && (currentMillis - lastDioRebroadcastTime >= DIO_REBROADCAST_INTERVAL)) {
        lastDioRebroadcastTime = currentMillis;
        broadcastDio();
    }

    if (hasParent && (currentMillis - lastDaoSendTime >= DAO_SEND_INTERVAL)) {
        lastDaoSendTime = currentMillis;
        sendDao();
    }

    delay(30);
}
