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
unsigned long lastPotentialBroadcastTime = 0;
const unsigned long SENSOR_READ_INTERVAL = 3000;
const unsigned long POTENTIAL_BROADCAST_INTERVAL = 5000;
const unsigned long NEIGHBOR_TIMEOUT = 12000;

// Cảm biến
float currentTemp = 0.0;
int currentGas = 0;
bool isEmergency = false;

float repulsivePotential = 0.0;
float basePotential = 9999.0;
float totalPotential = 9999.0;

void setup() {
    Serial.begin(115200);
    SensorService::init("SATELLITE");

    if (meshNetwork.init()) {
        Serial.println("[APF Network] Khởi động thành công.");
    } else {
        Serial.println("[APF Network FAIL] Khởi động thất bại!");
    }

    WiFiService::forceChannel(WIFI_CHANNEL_COMMON);

    Serial.println("Khởi động Node Vệ tinh APF OK, đang quét tìm thế năng...");
}

void loop() {
    unsigned long currentMillis = millis();

    routingTable.purgeExpired(NEIGHBOR_TIMEOUT);

    // Tính toán thế năng chướng ngại vật (repulsive potential)
    float rep = 0.0;
    if (currentTemp > 45.0) {
        rep += (currentTemp - 40.0) * 100.0;
    }
    if (currentGas > 200) {
        rep += (currentGas - 200) * 3.0;
    }
    repulsivePotential = rep;

    // Tính toán thế năng cơ bản dựa trên Hop Count
    if (routingTable.getHopCount() == 255) {
        basePotential = 9999.0;
    } else {
        basePotential = routingTable.getHopCount() * 100.0;
    }
    
    totalPotential = basePotential + repulsivePotential;

    // Tìm Next Hop láng giềng có thế năng hiệu dụng thấp nhất
    bool hasRoute = routingTable.selectNextHop(totalPotential);

    if (hasRoute) {
        if (isScanning) {
            isScanning = false;
            Serial.printf("[Scan] Đã tìm thấy tuyến đường trên Kênh %d! Khóa kênh hoạt động.\n", scanChannel);
        }
    } else {
        totalPotential = 9999.0; // Thế năng vô cực khi mất định tuyến

        if (!isScanning) {
            isScanning = true;
            scanChannel = 1;
            lastChannelSwitchTime = currentMillis - CHANNEL_LISTEN_TIME;
            meshNetwork.stopScanningAck();
            Serial.println("[Scan] Mất định tuyến. Bắt đầu chế độ tự động quét kênh sóng...");
        }

        if (isScanning && (currentMillis - lastChannelSwitchTime >= CHANNEL_LISTEN_TIME)) {
            scanChannel = (scanChannel % 11) + 1;
            WiFiService::forceChannel(scanChannel);
            meshNetwork.sendRouteRequest(scanChannel);
            lastChannelSwitchTime = currentMillis;
        }
    }

    if (currentMillis - lastSensorReadTime >= SENSOR_READ_INTERVAL) {
        lastSensorReadTime = currentMillis;

        SensorService::read(currentTemp, currentGas, isEmergency);
        SensorService::updateAlarm(isEmergency, hasRoute, repulsivePotential);
        
        uint8_t nextHop[6];
        memcpy(nextHop, routingTable.getNextHopMac(), 6);
        SensorService::displaySatellite("APF", SATELLITE_ID, currentTemp, currentGas, hasRoute, totalPotential, nextHop);

        meshNetwork.sendSensorData(currentTemp, currentGas, isEmergency, totalPotential);
    }

    if (hasRoute && (currentMillis - lastPotentialBroadcastTime >= POTENTIAL_BROADCAST_INTERVAL)) {
        lastPotentialBroadcastTime = currentMillis;
        meshNetwork.broadcastPotential(totalPotential);
    }

    delay(30);
}
