#include "DashboardServer.h"

bool DashboardServer::m_otaPending = false;
uint8_t DashboardServer::m_otaTargetSatId = 0xFF;

static WebServer server(80);
static File otaFile;

void DashboardServer::start(std::function<String()> statusJsonCallback) {
    // Giao diện Dashboard + OTA
    server.on("/", HTTP_GET, []() {
        server.send(200, "text/html", DASHBOARD_HTML);
    });

    // API lấy trạng thái Mesh
    server.on("/api/status", HTTP_GET, [statusJsonCallback]() {
        server.send(200, "application/json", statusJsonCallback());
    });

    // Upload OTA cho Master Node (Tự cập nhật)
    server.on("/update-master", HTTP_POST, []() {
        server.sendHeader("Connection", "close");
        server.send(200, "text/plain", (Update.hasError()) ? "NÂNG CẤP MASTER THẤT BẠI!" : "NÂNG CẤP MASTER THÀNH CÔNG! Đang khởi động lại...");
        delay(1000);
        ESP.restart();
    }, []() {
        HTTPUpload& upload = server.upload();
        if (upload.status == UPLOAD_FILE_START) {
            Serial.printf("[OTA Master] Bắt đầu nhận file: %s\n", upload.filename.c_str());
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                Update.printError(Serial);
            }
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                Update.printError(Serial);
            }
        } else if (upload.status == UPLOAD_FILE_END) {
            if (Update.end(true)) {
                Serial.printf("[OTA Master] Thành công! Kích thước: %u bytes\n", upload.totalSize);
            } else {
                Update.printError(Serial);
            }
        }
    });

    // Upload OTA cho Satellite Nodes (Ghi LittleFS và đổi cờ báo phát Mesh)
    server.on("/update-satellite", HTTP_POST, []() {
        String satIdStr = server.arg("sat_id");
        if (satIdStr == "all" || satIdStr == "0") {
            m_otaTargetSatId = 0xFE;
        } else {
            m_otaTargetSatId = satIdStr.toInt();
        }
        Serial.printf("[Web Server] Nhận yêu cầu OTA cho Satellite Node %d\n", m_otaTargetSatId);

        server.sendHeader("Connection", "close");
        server.send(200, "text/plain", "Đã tải lên Satellite firmware thành công! Bắt đầu truyền vô tuyến qua mạng Mesh...");
        m_otaPending = true; // Kích hoạt cờ báo gửi OTA cho vệ tinh
    }, []() {
        HTTPUpload& upload = server.upload();
        if (upload.status == UPLOAD_FILE_START) {
            Serial.printf("[OTA Satellite] Nhận file từ Web: %s\n", upload.filename.c_str());
            otaFile = LittleFS.open("/satellite_firmware.bin", "w");
            if (!otaFile) {
                Serial.println("[OTA Satellite Error] Không thể mở file trên Flash LittleFS!");
            }
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (otaFile) {
                otaFile.write(upload.buf, upload.currentSize);
            }
        } else if (upload.status == UPLOAD_FILE_END) {
            if (otaFile) {
                otaFile.close();
                Serial.printf("[OTA Satellite] Lưu file thành công! Kích thước: %u bytes\n", upload.totalSize);
            }
        }
    });

    server.begin();
    Serial.println("[Web Server] Đã kích hoạt Web Server chế độ hoạt động bình thường trên cổng 80.");
}

void DashboardServer::handle() {
    server.handleClient();
}

bool DashboardServer::isSatelliteOtaPending() {
    return m_otaPending;
}

void DashboardServer::clearSatelliteOtaPending() {
    m_otaPending = false;
}

uint8_t DashboardServer::getOtaTargetSatId() {
    return m_otaTargetSatId;
}
