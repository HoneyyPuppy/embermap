#include <Arduino.h>
#include <SensorService.h>
#include <WiFiService.h>
#include <common_config.h>
#include "RoutingTable.h"
#include "MeshNetwork.h"

// ID của Satellite Node này
#define SATELLITE_ID 2

RoutingTable routingTable(SATELLITE_ID);
MeshNetwork meshNetwork(SATELLITE_ID, routingTable);

// Biến phục vụ cơ chế Auto-Channel Scanning
bool isScanning = false;
uint8_t scanChannel = 1;
unsigned long lastChannelSwitchTime = 0;
const unsigned long CHANNEL_LISTEN_TIME = 800; // Thời gian chờ trên mỗi kênh (ms)

// Timers
unsigned long lastSensorReadTime = 0;
unsigned long lastRouteBroadcastTime = 0;
const unsigned long SENSOR_READ_INTERVAL = 3000;
const unsigned long ROUTE_BROADCAST_INTERVAL = 5000;
const unsigned long ROUTE_TIMEOUT = 12000;

// Cảm biến
float currentTemp = 0.0;
int currentGas = 0;
bool isEmergency = false;

void setup() {
    Serial.begin(115200);
    SensorService::init("SATELLITE");

    if (meshNetwork.init()) {
        Serial.println("[Mesh Network] Khởi động thành công.");
    } else {
        Serial.println("[Mesh Network FAIL] Khởi động thất bại!");
    }

    WiFiService::forceChannel(WIFI_CHANNEL_COMMON);

    Serial.println("Khởi động Node Vệ tinh OK, đang quét tìm Master qua Mesh... ");
}

void loop() {
    unsigned long currentMillis = millis();

    routingTable.purgeExpired(ROUTE_TIMEOUT);

    // Cơ chế Auto-Channel Scanning chủ động khi mất định tuyến
    if (!routingTable.hasRoute()) {
        if (!isScanning) {
            isScanning = true;
            scanChannel = 1;
            lastChannelSwitchTime = currentMillis - CHANNEL_LISTEN_TIME; // Kích hoạt ngay lập tức
            meshNetwork.stopScanningAck();
            Serial.println("[Scan] Mất định tuyến. Bắt đầu chế độ tự động quét kênh sóng...");
        }

        if (isScanning && (currentMillis - lastChannelSwitchTime >= CHANNEL_LISTEN_TIME)) {
            scanChannel = (scanChannel % 11) + 1;
            WiFiService::forceChannel(scanChannel);
            meshNetwork.sendRouteRequest(scanChannel);
            lastChannelSwitchTime = currentMillis;
        }
    } else {
        if (isScanning) {
            isScanning = false;
            Serial.printf("[Scan] Đã tìm thấy tuyến đường trên Kênh %d! Khóa kênh hoạt động.\n", scanChannel);
        }
    }

    if (currentMillis - lastSensorReadTime >= SENSOR_READ_INTERVAL) {
        lastSensorReadTime = currentMillis;

        SensorService::read(currentTemp, currentGas, isEmergency);
        SensorService::updateAlarm(isEmergency, routingTable.hasRoute());
        
        uint8_t nextHop[6];
        memcpy(nextHop, routingTable.getNextHopMac(), 6);
        SensorService::displaySatellite("Mesh", SATELLITE_ID, currentTemp, currentGas, routingTable.hasRoute(), routingTable.getCost(), nextHop);

        meshNetwork.sendSensorData(currentTemp, currentGas, isEmergency);
    }

    if (routingTable.hasRoute() && (currentMillis - lastRouteBroadcastTime >= ROUTE_BROADCAST_INTERVAL)) {
        lastRouteBroadcastTime = currentMillis;
        meshNetwork.rebroadcastRouteUpdate();
    }

    delay(30);
}
