#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <common_config.h>
#include <mesh_packet.h>
#include <WiFiService.h>
#include <WebService.h>
#include <SensorService.h>

// Địa chỉ MAC quảng bá
uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint8_t myMac[6];

// RPL Master variables
uint16_t masterRank = 256; // Master Root Rank = 256 (min)
uint8_t dodagVersion = 1;

// Dữ liệu cảm biến Master và các vệ tinh
float master_temp = 26.0;
int master_gas = 0;
float s1_temp = 26.0, s2_temp = 26.0, s3_temp = 26.0;
int s1_gas = 0, s2_gas = 0, s3_gas = 0;

unsigned long lastDioBroadcastTime = 0;
unsigned long lastSendTime = 0;
const unsigned long DIO_BROADCAST_INTERVAL = 5000;
const unsigned long WEB_POST_INTERVAL = 3000;

// ==================== HÀM QUẢNG BÁ RPL DIO ====================
void broadcastDio() {
    MeshPacket packet = {};
    packet.packetType = PACKET_RPL_DIO;
    memcpy(packet.sourceMac, myMac, 6);
    memcpy(packet.destMac, broadcastMac, 6);
    memset(packet.forwardMac, 0, 6);
    packet.id = 0;
    packet.temp = 0.0;
    packet.gasRaw = 0;
    packet.emergency = false;
    packet.rank = masterRank;
    packet.version = dodagVersion;

    // Thiết lập routePath bắt đầu từ Master Node (ID = 0)
    packet.routePath[0] = 0;
    packet.routePathLen = 1;

    esp_err_t result = esp_now_send(broadcastMac, (uint8_t *)&packet, sizeof(packet));
    Serial.printf("[RPL Master] Broadcast DIO (Rank = %d, Ver = %d) -> %s\n", 
                  masterRank, dodagVersion, result == ESP_OK ? "SUCCESS" : "FAIL");
}

// ==================== CALLBACK NHẬN DỮ LIỆU ESP-NOW ====================
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingDataRaw, int len) {
    if (len < sizeof(MeshPacket)) return;

    MeshPacket incomingPacket;
    memcpy(&incomingPacket, incomingDataRaw, sizeof(incomingPacket));

    if (incomingPacket.packetType == PACKET_SENSOR_DATA) {
        if (memcmp(incomingPacket.forwardMac, myMac, 6) == 0) {
            Serial.printf("[RPL Master] Received sensor from Node %d. Temp: %.1f C, Gas: %d\n", 
                          incomingPacket.id, incomingPacket.temp, incomingPacket.gasRaw);

            if (incomingPacket.id == 1) {
                s1_temp = incomingPacket.temp;
                s1_gas = incomingPacket.gasRaw;
            } else if (incomingPacket.id == 2) {
                s2_temp = incomingPacket.temp;
                s2_gas = incomingPacket.gasRaw;
            } else if (incomingPacket.id == 3) {
                s3_temp = incomingPacket.temp;
                s3_gas = incomingPacket.gasRaw;
            }
        }
    }
    else if (incomingPacket.packetType == PACKET_RPL_DAO) {
        if (memcmp(incomingPacket.forwardMac, myMac, 6) == 0) {
            Serial.printf("[RPL Master] Route DAO registered from MAC: %02X:%02X... via Node %d\n",
                          incomingPacket.sourceMac[0], incomingPacket.sourceMac[1], incomingPacket.id);
        }
    }
    else if (incomingPacket.packetType == PACKET_ROUTE_REQUEST) {
        Serial.println("[RPL Master] Nhận yêu cầu quét kênh (Route Request) -> Quảng bá DIO phản hồi.");
        broadcastDio();
    }
}

// ==================== SETUP ====================
void setup() {
    Serial.begin(115200);
    SensorService::init("MASTER");

    WiFi.mode(WIFI_STA);
    esp_read_mac(myMac, ESP_MAC_WIFI_STA);
    Serial.printf("\n=========================================\n");
    Serial.printf("MAC MASTER STA (RPL): %02X:%02X:%02X:%02X:%02X:%02X\n", 
                  myMac[0], myMac[1], myMac[2], myMac[3], myMac[4], myMac[5]);
    Serial.printf("=========================================\n");

    WiFiService::connect(WIFI_SSID_COMMON, WIFI_PASSWORD_COMMON);

    WiFi.mode(WIFI_STA); 
    
    if (esp_now_init() == ESP_OK) {
        Serial.println("[ESP-NOW OK] Initialized successfully.");
        esp_now_register_recv_cb(OnDataRecv);  
    } else {
        Serial.println("[ESP-NOW FAIL] Failed to initialize!");
    }

    WiFi.setSleep(false);

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, broadcastMac, 6);
    peerInfo.channel = WiFi.channel();
    peerInfo.encrypt = false;
    peerInfo.ifidx = WIFI_IF_STA;
    
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("[ESP-NOW] Failed to add broadcast peer!");
    }

    broadcastDio();
}

// ==================== LOOP ====================
void loop() {
    unsigned long currentMillis = millis();

    bool isEmergency = false;
    SensorService::read(master_temp, master_gas, isEmergency);
    SensorService::updateAlarm(isEmergency, true);
    SensorService::displayMaster("RPL", master_temp, master_gas, WiFi.status() == WL_CONNECTED, WiFi.localIP().toString());

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
            Serial.println("\n--- Pushing Mesh Readings to Web Dashboard ---");
            WebService::postReading(SERVER_URL_COMMON, "temp-master", "temp", master_temp);
            WebService::postReading(SERVER_URL_COMMON, "mq2-master", "mq2", (float)master_gas);

            WebService::postReading(SERVER_URL_COMMON, "temp-sat-1", "temp", s1_temp);
            WebService::postReading(SERVER_URL_COMMON, "mq2-sat-1", "mq2", (float)s1_gas);

            WebService::postReading(SERVER_URL_COMMON, "temp-sat-2", "temp", s2_temp);
            WebService::postReading(SERVER_URL_COMMON, "mq2-sat-2", "mq2", (float)s2_gas);

            WebService::postReading(SERVER_URL_COMMON, "temp-sat-3", "temp", s3_temp);
            WebService::postReading(SERVER_URL_COMMON, "mq2-sat-3", "mq2", (float)s3_gas);
        } else {
            Serial.println("\n[Warning] WiFi offline, cannot upload to Web!");
        }
    }

    delay(30); 
}
