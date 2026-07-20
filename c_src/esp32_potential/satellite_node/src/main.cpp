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
    uint8_t routePath[MAX_ROUTE_PATH];
    uint8_t routePathLen;
    float linkCost;
} Neighbor;

#define MAX_NEIGHBORS 10
Neighbor neighbors[MAX_NEIGHBORS];
int neighborCount = 0;

// Next Hop MAC đang dùng để truyền dữ liệu
uint8_t nextHopMac[6] = {0, 0, 0, 0, 0, 0};
bool hasRoute = false;

// Lịch sử đường truyền về Master (để chống lặp vòng định tuyến)
uint8_t parentRoutePath[MAX_ROUTE_PATH];
uint8_t parentRoutePathLen = 0;

// Biến phục vụ cơ chế Auto-Channel Scanning
bool isScanning = false;
uint8_t scanChannel = 1;
unsigned long lastChannelSwitchTime = 0;
const unsigned long CHANNEL_LISTEN_TIME = 800; // Thời gian chờ trên mỗi kênh (ms)

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
void updateNeighbor(const uint8_t *mac, float potential, uint8_t hopCount, const uint8_t *routePath, uint8_t routePathLen, int rssi) {
    unsigned long now = millis();
    int foundIdx = -1;
    float linkCost = calculateLinkCost(rssi);

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
        memcpy(neighbors[foundIdx].routePath, routePath, routePathLen);
        neighbors[foundIdx].routePathLen = routePathLen;
        neighbors[foundIdx].linkCost = linkCost;
    } else {
        if (neighborCount < MAX_NEIGHBORS) {
            memcpy(neighbors[neighborCount].mac, mac, 6);
            neighbors[neighborCount].potential = potential;
            neighbors[neighborCount].hopCount = hopCount;
            neighbors[neighborCount].lastSeen = now;
            memcpy(neighbors[neighborCount].routePath, routePath, routePathLen);
            neighbors[neighborCount].routePathLen = routePathLen;
            neighbors[neighborCount].linkCost = linkCost;
            neighborCount++;
            Serial.printf("[APF Table] Thêm láng giềng mới: %02X:%02X... | RSSI = %d dBm, LinkCost = %.1f\n", 
                          mac[0], mac[1], rssi, linkCost);
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
        // Kiểm tra tránh lặp vòng định tuyến
        if (pathContainsNode(neighbors[i].routePath, neighbors[i].routePathLen, SATELLITE_ID)) {
            continue;
        }
        // Tính toán thế năng hiệu dụng cộng thêm chi phí đường truyền dựa trên cường độ sóng
        float effectivePotential = neighbors[i].potential + neighbors[i].linkCost * 100.0;
        if (effectivePotential < minPotential) {
            minPotential = effectivePotential;
            minIdx = i;
        }
    }

    if (minIdx != -1 && minPotential < totalPotential) {
        memcpy(nextHopMac, neighbors[minIdx].mac, 6);
        myHopCount = neighbors[minIdx].hopCount + 1;
        basePotential = neighbors[minIdx].potential + neighbors[minIdx].linkCost * 100.0;
        hasRoute = true;

        // Cập nhật lịch sử đường truyền từ Parent được chọn
        memcpy(parentRoutePath, neighbors[minIdx].routePath, neighbors[minIdx].routePathLen);
        parentRoutePathLen = neighbors[minIdx].routePathLen;

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
    parentRoutePathLen = 0; // Reset lịch sử đường truyền
    return false;
}

// ==================== PHÁT QUẢNG BÁ THẾ NĂNG CỦA BẢN THÂN ====================
void broadcastPotential() {
    MeshPacket packet = {};
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

    // Đính kèm ID bản thân vào routePath để truyền tiếp (Loop Prevention)
    memcpy(packet.routePath, parentRoutePath, parentRoutePathLen);
    packet.routePathLen = parentRoutePathLen;
    if (packet.routePathLen < MAX_ROUTE_PATH) {
        packet.routePath[packet.routePathLen] = SATELLITE_ID;
        packet.routePathLen++;
    }

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

    MeshPacket packet = {};
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
    Serial.printf("[APF Data] Gửi cảm biến lên Next Hop -> %s\n", result == ESP_OK ? "OK" : "FAIL");
}

// ==================== CALLBACK NHẬN DỮ LIỆU ESP-NOW ====================
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingDataRaw, int len) {
    const uint8_t* senderMac = recv_info->src_addr;

    if (len < sizeof(MeshPacket)) return;

    MeshPacket incomingPacket;
    memcpy(&incomingPacket, incomingDataRaw, sizeof(incomingPacket));

    if (incomingPacket.packetType == PACKET_POTENTIAL_ADVERT) {
        int rssi = recv_info->rx_ctrl->rssi;
        updateNeighbor(senderMac, incomingPacket.potential, incomingPacket.hopCount, incomingPacket.routePath, incomingPacket.routePathLen, rssi);
        if (selectNextHop()) {
            if (isScanning) {
                isScanning = false;
                Serial.printf("[Scan] Đã tìm thấy tuyến đường trên Kênh %d! Khóa kênh hoạt động.\n", scanChannel);
            }
        }
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
    else if (incomingPacket.packetType == PACKET_ROUTE_REQUEST) {
        if (hasRoute) {
            Serial.println("[APF] Nhận yêu cầu quét kênh (Route Request) -> Phát lại phản hồi thế năng.");
            broadcastPotential();
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

    WiFiService::forceChannel(WIFI_CHANNEL_COMMON);

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

        SensorService::read(currentTemp, currentGas, isEmergency);

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
        SensorService::updateAlarm(isEmergency, hasRoute, repulsivePotential);
        SensorService::displaySatellite("APF", SATELLITE_ID, currentTemp, currentGas, hasRoute, totalPotential, nextHopMac);

        sendSensorData();
    }

    // Cơ chế Auto-Channel Scanning chủ động khi mất định tuyến
    if (!hasRoute) {
        if (!isScanning) {
            isScanning = true;
            scanChannel = 1;
            lastChannelSwitchTime = currentMillis - CHANNEL_LISTEN_TIME; // Kích hoạt ngay lập tức
            Serial.println("[Scan] Mất định tuyến. Bắt đầu chế độ tự động quét kênh sóng...");
        }

        if (isScanning && (currentMillis - lastChannelSwitchTime >= CHANNEL_LISTEN_TIME)) {
            // Chuyển kênh vòng tròn từ 1 đến 11
            scanChannel = (scanChannel % 11) + 1;
            WiFiService::forceChannel(scanChannel);
            
            // Gửi gói Route Request dò tìm chủ động
            MeshPacket req = {};
            req.packetType = PACKET_ROUTE_REQUEST;
            memcpy(req.sourceMac, myMac, 6);
            req.id = SATELLITE_ID;
            
            esp_now_send(broadcastMac, (uint8_t *)&req, sizeof(req));
            Serial.printf("[Scan] Đang dò Kênh %d... Phát Route Request.\n", scanChannel);
            
            lastChannelSwitchTime = currentMillis;
        }
    }

    if (currentMillis - lastPotentialBroadcastTime >= POTENTIAL_BROADCAST_INTERVAL) {
        lastPotentialBroadcastTime = currentMillis;
        broadcastPotential();
    }

    delay(30);
}
