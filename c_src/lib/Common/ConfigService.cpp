#include "ConfigService.h"
#include <LittleFS.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <DNSServer.h>
#include <common_config.h>

bool ConfigService::m_portalActive = false;
bool ConfigService::m_satelliteOtaPending = false;
uint8_t ConfigService::m_otaTargetSatId = 0xFF;
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

static File otaFile;

void ConfigService::startNormalWebServer(std::function<String()> statusJsonCallback) {
    m_portalActive = false; // Tắt flag portal

    // Giao diện Dashboard + OTA
    server.on("/", HTTP_GET, []() {
        String html = R"rawhtml(
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Embermap Smart Fire Mesh Gateway</title>
    <style>
        body {
            font-family: 'Segoe UI', system-ui, sans-serif;
            background: linear-gradient(135deg, #0b0f19 0%, #1e1b4b 100%);
            color: #f8fafc;
            margin: 0;
            padding: 20px;
            box-sizing: border-box;
            min-height: 100vh;
        }
        .container { max-width: 1200px; margin: 0 auto; }
        header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding-bottom: 20px;
            border-bottom: 1px solid rgba(255, 255, 255, 0.1);
            margin-bottom: 30px;
        }
        h1 { margin: 0; color: #38bdf8; font-size: 26px; }
        .grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
            gap: 20px;
            margin-bottom: 45px;
        }
        .card {
            background: rgba(30, 41, 59, 0.45);
            backdrop-filter: blur(12px);
            border: 1px solid rgba(255, 255, 255, 0.08);
            border-radius: 16px;
            padding: 20px;
            transition: transform 0.3s, border-color 0.3s;
        }
        .card:hover { transform: translateY(-2px); border-color: rgba(56, 189, 248, 0.3); }
        .card.fire {
            border-color: #ef4444;
            background: rgba(239, 68, 68, 0.15);
            box-shadow: 0 0 15px rgba(239, 68, 68, 0.3);
        }
        .card-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 15px;
        }
        .card-title { margin: 0; font-size: 18px; font-weight: 600; color: #cbd5e1; }
        .badge {
            padding: 4px 8px;
            border-radius: 12px;
            font-size: 11px;
            font-weight: 600;
        }
        .badge-safe { background: rgba(16, 185, 129, 0.2); color: #10b981; }
        .badge-fire { background: rgba(239, 68, 68, 0.2); color: #f87171; animation: pulse 1.5s infinite; }
        @keyframes pulse { 0% { opacity: 0.6; } 50% { opacity: 1; } 100% { opacity: 0.6; } }
        .metric { display: flex; justify-content: space-between; margin-bottom: 8px; font-size: 14px; }
        .metric-label { color: #94a3b8; }
        .metric-value { font-weight: 600; color: #f1f5f9; }
        .ota-section {
            background: rgba(15, 23, 42, 0.6);
            border: 1px solid rgba(255, 255, 255, 0.1);
            border-radius: 16px;
            padding: 30px;
        }
        h2 { color: #38bdf8; margin-top: 0; margin-bottom: 25px; font-size: 20px; }
        .ota-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 30px; }
        @media (max-width: 768px) { .ota-grid { grid-template-columns: 1fr; } }
        .ota-card {
            background: rgba(30, 41, 59, 0.3);
            border: 1px solid rgba(255, 255, 255, 0.05);
            border-radius: 12px;
            padding: 20px;
        }
        .upload-form { display: flex; flex-direction: column; gap: 15px; }
        .file-input { display: none; }
        .file-label {
            border: 2px dashed rgba(56, 189, 248, 0.3);
            border-radius: 8px;
            padding: 25px;
            text-align: center;
            cursor: pointer;
            transition: all 0.3s;
            color: #94a3b8;
        }
        .file-label:hover { border-color: #38bdf8; background: rgba(56, 189, 248, 0.05); color: #f8fafc; }
        .btn {
            background: linear-gradient(95deg, #0ea5e9 0%, #2563eb 100%);
            border: none;
            color: white;
            padding: 12px;
            border-radius: 8px;
            font-weight: 600;
            cursor: pointer;
            transition: transform 0.2s, box-shadow 0.2s;
        }
        .btn:hover { transform: translateY(-1px); box-shadow: 0 4px 12px rgba(37, 99, 235, 0.3); }
        .progress-bar { height: 6px; background: rgba(255, 255, 255, 0.1); border-radius: 3px; overflow: hidden; display: none; }
        .progress-fill { height: 100%; width: 0%; background: #38bdf8; transition: width 0.1s; }
        .progress-text { font-size: 12px; color: #94a3b8; text-align: right; display: none; }
    </style>
</head>
<body>
    <div class="container">
        <header>
            <h1>Embermap Smart Fire Mesh Gateway</h1>
            <span class="status-badge">GATEWAY ONLINE</span>
        </header>

        <h2>Giám Sát Mạng Lưới Cảm Biến</h2>
        <div class="grid" id="node-grid">
            <!-- Dữ liệu được nạp động từ AJAX -->
        </div>

        <div class="ota-section">
            <h2>Nâng Cấp Phần Mềm Hệ Thống (OTA)</h2>
            <div class="ota-grid">
                <!-- OTA Master -->
                <div class="ota-card">
                    <h3 style="margin-top:0;color:#cbd5e1;font-size:16px;">Nâng cấp Master Node (Tự cập nhật)</h3>
                    <form id="master-form" action="/update-master" method="POST" enctype="multipart/form-data" class="upload-form">
                        <label for="master-file" id="master-label" class="file-label">Kéo thả hoặc click chọn file firmware.bin</label>
                        <input type="file" name="firmware" id="master-file" class="file-input" accept=".bin" required>
                        <div class="progress-bar"><div id="master-fill" class="progress-fill"></div></div>
                        <div id="master-text" class="progress-text">0%</div>
                        <button type="submit" id="master-btn" class="btn">Bắt đầu nâng cấp</button>
                    </form>
                </div>

                <!-- OTA Satellites -->
                <div class="ota-card">
                    <h3 style="margin-top:0;color:#cbd5e1;font-size:16px;">Nâng cấp các Vệ tinh (Qua mạng Mesh)</h3>
                    <form id="sat-form" action="/update-satellite" method="POST" enctype="multipart/form-data" class="upload-form">
                        <select name="sat_id" id="sat-id-select" style="width:100%;padding:10px;border-radius:6px;background:rgba(15,23,42,0.6);color:white;border:1px solid rgba(255,255,255,0.15);outline:none;box-sizing:border-box;margin-bottom:10px;">
                            <option value="1">Vệ tinh 1</option>
                            <option value="2">Vệ tinh 2</option>
                            <option value="3">Vệ tinh 3</option>
                            <option value="4">Vệ tinh 4</option>
                            <option value="5">Vệ tinh 5</option>
                        </select>
                        <label for="sat-file" id="sat-label" class="file-label">Kéo thả hoặc click chọn file firmware.bin</label>
                        <input type="file" name="firmware" id="sat-file" class="file-input" accept=".bin" required>
                        <div class="progress-bar"><div id="sat-fill" class="progress-fill"></div></div>
                        <div id="sat-text" class="progress-text">0%</div>
                        <button type="submit" id="sat-btn" class="btn">Gửi tới mạng Mesh</button>
                    </form>
                </div>
            </div>
        </div>
    </div>

    <script>
        function updateDashboard() {
            fetch('/api/status')
                .then(res => res.json())
                .then(data => {
                    const grid = document.getElementById('node-grid');
                    grid.innerHTML = '';
                    
                    grid.appendChild(createNodeCard('Master (Cửa thoát)', data.master, true));
                    
                    data.satellites.forEach(sat => {
                        if (sat.active) {
                            grid.appendChild(createNodeCard('Vệ tinh ' + sat.id, sat, false));
                        }
                    });
                })
                .catch(err => console.error('Error fetching status:', err));
        }
        
        function createNodeCard(name, node, isMaster) {
            const div = document.createElement('div');
            div.className = 'card' + (node.emergency ? ' fire' : '');
            
            const badgeClass = node.emergency ? 'badge-fire' : 'badge-safe';
            const badgeText = node.emergency ? 'NGUY HIỂM' : 'AN TOÀN';
            
            div.innerHTML = `
                <div class="card-header">
                    <h3 class="card-title">${name}</h3>
                    <span class="badge ${badgeClass}">${badgeText}</span>
                </div>
                <div class="metric">
                    <span class="metric-label">Nhiệt độ:</span>
                    <span class="metric-value">${node.temp.toFixed(1)} °C</span>
                </div>
                <div class="metric">
                    <span class="metric-label">Khói Gas:</span>
                    <span class="metric-value">${node.gas}</span>
                </div>
                \${!isMaster ? `
                <div class="metric">
                    <span class="metric-label">Next Hop ID:</span>
                    <span class="metric-value">\${node.nextHopId !== 255 ? node.nextHopId : 'MẤT TUYẾN'}</span>
                </div>
                <div class="metric">
                    <span class="metric-label">Thế năng APF:</span>
                    <span class="metric-value">\${node.pot !== 9999 ? node.pot.toFixed(1) : 'VÔ HẠN (KẸT)'}</span>
                </div>
                ` : ''}
            `;
            return div;
        }
        
        setInterval(updateDashboard, 3000);
        updateDashboard();

        function handleUpload(formId, inputId, labelId, progressFillId, progressTextId, btnId) {
            const form = document.getElementById(formId);
            const input = document.getElementById(inputId);
            const label = document.getElementById(labelId);
            const fill = document.getElementById(progressFillId);
            const text = document.getElementById(progressTextId);
            const bar = fill.parentElement;
            const btn = document.getElementById(btnId);
            
            input.addEventListener('change', () => {
                if (input.files.length > 0) {
                    label.innerText = input.files[0].name;
                }
            });
            
            form.addEventListener('submit', (e) => {
                e.preventDefault();
                if (input.files.length === 0) return;
                
                const formData = new FormData(form);
                
                const xhr = new XMLHttpRequest();
                xhr.open('POST', form.action, true);
                
                bar.style.display = 'block';
                text.style.display = 'block';
                btn.disabled = true;
                btn.innerText = 'Đang tải lên...';
                
                xhr.upload.addEventListener('progress', (e) => {
                    if (e.lengthComputable) {
                        const percent = Math.round((e.loaded / e.total) * 100);
                        fill.style.width = percent + '%';
                        text.innerText = percent + '% (' + Math.round(e.loaded/1024) + ' KB / ' + Math.round(e.total/1024) + ' KB)';
                    }
                });
                
                xhr.onreadystatechange = () => {
                    if (xhr.readyState === XMLHttpRequest.DONE) {
                        if (xhr.status === 200) {
                            alert(xhr.responseText);
                            location.reload();
                        } else {
                            alert('Tải lên thất bại! Vui lòng thử lại.');
                            btn.disabled = false;
                            btn.innerText = 'Bắt đầu nâng cấp';
                            bar.style.display = 'none';
                            text.style.display = 'none';
                        }
                    }
                };
                xhr.send(formData);
            });
        }
        
        handleUpload('master-form', 'master-file', 'master-label', 'master-fill', 'master-text', 'master-btn');
        handleUpload('sat-form', 'sat-file', 'sat-label', 'sat-fill', 'sat-text', 'sat-btn');
    </script>
</body>
</html>
        )rawhtml";
        server.send(200, "text/html", html);
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
        m_otaTargetSatId = satIdStr.toInt();
        Serial.printf("[Web Server] Nhận yêu cầu OTA cho Satellite Node %d\n", m_otaTargetSatId);

        server.sendHeader("Connection", "close");
        server.send(200, "text/plain", "Đã tải lên Satellite firmware thành công! Bắt đầu truyền vô tuyến qua mạng Mesh...");
        m_satelliteOtaPending = true; // Kích hoạt cờ báo gửi OTA cho vệ tinh
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

void ConfigService::handleNormalWebServer() {
    server.handleClient();
}

WebServer& ConfigService::getWebServer() {
    return server;
}
