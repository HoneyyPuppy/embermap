#include "CaptivePortal.h"

bool CaptivePortal::m_active = false;

static WebServer server(80);
static DNSServer dnsServer;

void CaptivePortal::start(const char* portalSsid) {
    m_active = true;
    
    // Tắt wifi cũ và chạy chế độ Access Point
    WiFi.disconnect();
    WiFi.mode(WIFI_AP);
    WiFi.softAP(portalSsid);
    
    Serial.printf("[AP Portal] Bật WiFi AP: %s\n", portalSsid);
    Serial.printf("[AP Portal] Truy cập địa chỉ cấu hình: %s\n", WiFi.softAPIP().toString().c_str());

    // Cấu hình DNS Server (chuyển hướng tất cả tên miền về AP IP - Captive Portal)
    dnsServer.start(53, "*", WiFi.softAPIP());

    // Route trang chủ
    server.on("/", HTTP_GET, []() {
        int n = WiFi.scanNetworks();
        String wifiOptions = "";
        if (n == 0) {
            wifiOptions = "<option value=''>No networks found</option>";
        } else {
            for (int i = 0; i < n; ++i) {
                String ssid = WiFi.SSID(i);
                int rssi = WiFi.RSSI(i);
                wifiOptions += "<option value='" + ssid + "'>" + ssid + " (" + String(rssi) + " dBm)</option>";
            }
        }
        server.send(200, "text/html", getPortalHtml(wifiOptions, String(SERVER_URL_COMMON)));
    });

    // Xử lý gửi form cấu hình
    server.on("/save", HTTP_POST, []() {
        String ssid = server.arg("ssid");
        String manualSsid = server.arg("manual_ssid");
        String pass = server.arg("pass");
        String url = server.arg("url");

        if (manualSsid.length() > 0) {
            ssid = manualSsid;
        }

        server.send(200, "text/html", SUCCESS_HTML);

        delay(1000);
        ConfigStorage::saveConfig(ssid, pass, url);
        delay(1000);
        ESP.restart();
    });

    // Các routes phục vụ tính năng Captive Portal tự động của iOS và Android
    server.on("/generate_204", HTTP_GET, []() {
        server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
        server.send(302, "text/plain", "");
    });
    server.on("/success.txt", HTTP_GET, []() {
        server.send(200, "text/plain", "success");
    });
    server.onNotFound([]() {
        server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
        server.send(302, "text/plain", "");
    });

    server.begin();
}

void CaptivePortal::handle() {
    if (m_active) {
        dnsServer.processNextRequest();
        server.handleClient();
    }
}

bool CaptivePortal::isActive() {
    return m_active;
}
