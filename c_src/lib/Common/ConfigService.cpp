#include "ConfigService.h"
#include <LittleFS.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <common_config.h>

bool ConfigService::m_portalActive = false;
const String ConfigService::CONFIG_FILE = "/config.txt";

static WebServer server(80);
static DNSServer dnsServer;

bool ConfigService::init() {
    if (!LittleFS.begin(true)) {
        Serial.println("[LittleFS] Error mounting file system. Formatted instead.");
        return false;
    }
    return true;
}

bool ConfigService::loadConfig(String &ssid, String &pass, String &url) {
    if (!LittleFS.exists(CONFIG_FILE)) {
        return false;
    }
    File file = LittleFS.open(CONFIG_FILE, "r");
    if (!file) {
        return false;
    }
    ssid = file.readStringUntil('\n'); ssid.trim();
    pass = file.readStringUntil('\n'); pass.trim();
    url = file.readStringUntil('\n');   url.trim();
    file.close();
    return (ssid.length() > 0);
}

bool ConfigService::saveConfig(const String &ssid, const String &pass, const String &url) {
    File file = LittleFS.open(CONFIG_FILE, "w");
    if (!file) {
        Serial.println("[Config] Failed to open config file for writing");
        return false;
    }
    file.println(ssid);
    file.println(pass);
    file.println(url);
    file.close();
    Serial.println("[Config] Saved new configuration successfully.");
    return true;
}

// Hàm phụ để render trang HTML Portal
static String getPortalHtml() {
    // Quét WiFi
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

    String html = R"rawhtml(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Embermap Config Portal</title>
    <style>
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #0f172a 0%, #1e1b4b 100%);
            color: #f8fafc;
            display: flex;
            justify-content: center;
            align-items: center;
            height: 100vh;
            margin: 0;
            padding: 15px;
            box-sizing: border-box;
        }
        .card {
            background: rgba(30, 41, 59, 0.7);
            backdrop-filter: blur(16px);
            border: 1px solid rgba(255, 255, 255, 0.1);
            padding: 30px;
            border-radius: 16px;
            width: 100%;
            max-width: 450px;
            box-shadow: 0 10px 30px rgba(0, 0, 0, 0.5);
            text-align: center;
        }
        h2 {
            margin-top: 0;
            color: #38bdf8;
            font-size: 24px;
            letter-spacing: 0.5px;
        }
        p {
            color: #94a3b8;
            font-size: 14px;
            margin-bottom: 25px;
        }
        .form-group {
            text-align: left;
            margin-bottom: 18px;
        }
        label {
            display: block;
            margin-bottom: 6px;
            font-weight: 500;
            font-size: 13px;
            color: #cbd5e1;
        }
        input[type="text"], input[type="password"], select {
            width: 100%;
            padding: 12px;
            border: 1px solid rgba(255, 255, 255, 0.15);
            background: rgba(15, 23, 42, 0.6);
            border-radius: 8px;
            color: #ffffff;
            font-size: 14px;
            box-sizing: border-box;
            outline: none;
            transition: all 0.3s ease;
        }
        input[type="text"]:focus, input[type="password"]:focus, select:focus {
            border-color: #38bdf8;
            box-shadow: 0 0 8px rgba(56, 189, 248, 0.4);
        }
        button {
            width: 100%;
            padding: 14px;
            background: linear-gradient(95deg, #0ea5e9 0%, #2563eb 100%);
            border: none;
            border-radius: 8px;
            color: white;
            font-weight: 600;
            font-size: 16px;
            cursor: pointer;
            transition: transform 0.2s, box-shadow 0.2s;
            margin-top: 10px;
        }
        button:hover {
            transform: translateY(-1px);
            box-shadow: 0 5px 15px rgba(37, 99, 235, 0.4);
        }
        .footer {
            margin-top: 25px;
            font-size: 11px;
            color: #64748b;
        }
    </style>
</head>
<body>
    <div class="card">
        <h2>Embermap Gateway</h2>
        <p>Cấu hình Mạng WiFi & API Server cho Master Node</p>
        <form action="/save" method="POST">
            <div class="form-group">
                <label for="ssid">Chọn Mạng WiFi:</label>
                <select name="ssid" id="ssid" required>
                    <option value="">-- Chọn mạng WiFi --</option>
                    )rawhtml" + wifiOptions + R"rawhtml(
                </select>
            </div>
            <div class="form-group">
                <label for="manual_ssid">Nhập tên WiFi thủ công (nếu ẩn):</label>
                <input type="text" name="manual_ssid" id="manual_ssid" placeholder="Tên SSID WiFi">
            </div>
            <div class="form-group">
                <label for="pass">Mật khẩu WiFi:</label>
                <input type="password" name="pass" id="pass" placeholder="Nhập mật khẩu WiFi" required>
            </div>
            <div class="form-group">
                <label for="url">FastAPI Backend API URL:</label>
                <input type="text" name="url" id="url" value=")rawhtml" + String(SERVER_URL_COMMON) + R"rawhtml(" placeholder="http://[SERVER_IP]:8000/device-readings/ingest" required>
            </div>
            <button type="submit">Lưu Cấu Hình</button>
        </form>
        <div class="footer">Embermap IoT Gateway Config Portal v2.0</div>
    </div>
</body>
</html>
)rawhtml";
    return html;
}

void ConfigService::startConfigPortal(const char* portalSsid) {
    m_portalActive = true;
    
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
        server.send(200, "text/html", getPortalHtml());
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

        server.send(200, "text/html", R"rawhtml(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Cấu hình thành công</title>
    <style>
        body {
            font-family: 'Segoe UI', sans-serif;
            background: #0f172a;
            color: #f8fafc;
            display: flex;
            justify-content: center;
            align-items: center;
            height: 100vh;
            margin: 0;
        }
        .card {
            background: rgba(30, 41, 59, 0.7);
            padding: 30px;
            border-radius: 12px;
            max-width: 400px;
            text-align: center;
            box-shadow: 0 4px 20px rgba(0,0,0,0.3);
        }
        h3 { color: #10b981; }
    </style>
</head>
<body>
    <div class="card">
        <h3>Cấu hình đã được lưu!</h3>
        <p>ESP32 đang tự động khởi động lại để kết nối WiFi và API Server mới...</p>
    </div>
</body>
</html>
)rawhtml");

        delay(1000);
        ConfigService::saveConfig(ssid, pass, url);
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

void ConfigService::handlePortal() {
    if (m_portalActive) {
        dnsServer.processNextRequest();
        server.handleClient();
    }
}

void ConfigService::checkResetButton(uint8_t pin) {
    static bool initialized = false;
    if (!initialized) {
        pinMode(pin, INPUT_PULLUP);
        initialized = true;
    }

    static unsigned long pressStart = 0;
    static bool pressed = false;

    if (digitalRead(pin) == LOW) {
        if (!pressed) {
            pressStart = millis();
            pressed = true;
        } else {
            if (millis() - pressStart >= 3000) { // Nhấn giữ trong 3 giây
                Serial.println("\n[Config] BOOT button held for 3s! Clearing config...");
                LittleFS.remove(CONFIG_FILE);
                Serial.println("[Config] Config file deleted. Restarting...");
                delay(1000);
                ESP.restart();
            }
        }
    } else {
        pressed = false;
    }
}

static uint8_t g_resetPin = 0;
static void resetButtonTaskFunc(void* pvParameters) {
    for (;;) {
        ConfigService::checkResetButton(g_resetPin);
        vTaskDelay(pdMS_TO_TICKS(50)); // Quét phím mỗi 50ms
    }
}

void ConfigService::startResetButtonTask(uint8_t pin) {
    g_resetPin = pin;
    xTaskCreate(resetButtonTaskFunc, "ResetBtnTask", 2048, NULL, 1, NULL);
    Serial.printf("[Config] Đã khởi tạo task chạy ngầm giám sát nút BOOT trên GPIO %d\n", pin);
}
