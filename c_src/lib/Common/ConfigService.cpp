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
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
            background-color: #f4f4f6;
            color: #1f2937;
            display: flex;
            justify-content: center;
            align-items: center;
            height: 100vh;
            margin: 0;
            padding: 15px;
            box-sizing: border-box;
        }
        .card {
            background: #ffffff;
            border: 1px solid #e5e7eb;
            padding: 35px 30px;
            border-radius: 14px;
            width: 100%;
            max-width: 420px;
            box-shadow: 0 10px 25px -5px rgba(0, 0, 0, 0.04), 0 8px 10px -6px rgba(0, 0, 0, 0.02);
            text-align: center;
        }
        h2 {
            margin-top: 0;
            color: #111827;
            font-size: 22px;
            font-weight: 600;
            letter-spacing: -0.5px;
        }
        p {
            color: #6b7280;
            font-size: 13.5px;
            margin-bottom: 25px;
            line-height: 1.5;
        }
        .form-group {
            text-align: left;
            margin-bottom: 18px;
        }
        label {
            display: block;
            margin-bottom: 6px;
            font-weight: 600;
            font-size: 11px;
            color: #4b5563;
            text-transform: uppercase;
            letter-spacing: 0.5px;
        }
        input[type="text"], input[type="password"], select {
            width: 100%;
            padding: 11px 14px;
            border: 1px solid #d1d5db;
            background: #ffffff;
            border-radius: 8px;
            color: #111827;
            font-size: 14px;
            box-sizing: border-box;
            outline: none;
            transition: all 0.2s ease;
        }
        input[type="text"]:focus, input[type="password"]:focus, select:focus {
            border-color: #2563eb;
            box-shadow: 0 0 0 3px rgba(37, 99, 235, 0.12);
        }
        button {
            width: 100%;
            padding: 12px;
            background: #111827;
            border: none;
            border-radius: 8px;
            color: white;
            font-weight: 600;
            font-size: 14px;
            cursor: pointer;
            transition: background 0.2s;
            margin-top: 10px;
        }
        button:hover {
            background: #1f2937;
        }
        .footer {
            margin-top: 25px;
            font-size: 11px;
            color: #9ca3af;
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
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
            background-color: #f4f4f6;
            color: #1f2937;
            display: flex;
            justify-content: center;
            align-items: center;
            height: 100vh;
            margin: 0;
        }
        .card {
            background: #ffffff;
            border: 1px solid #e5e7eb;
            padding: 30px;
            border-radius: 14px;
            max-width: 400px;
            text-align: center;
            box-shadow: 0 10px 25px -5px rgba(0, 0, 0, 0.04);
        }
        h3 { color: #10b981; margin-top: 0; font-size: 20px; }
        p { color: #4b5563; font-size: 14px; line-height: 1.5; }
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
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
            background-color: #f4f4f6;
            color: #1f2937;
            margin: 0;
            padding: 30px 20px;
            box-sizing: border-box;
            min-height: 100vh;
        }
        .container { max-width: 1100px; margin: 0 auto; }
        header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding-bottom: 20px;
            border-bottom: 1px solid #e5e7eb;
            margin-bottom: 35px;
        }
        h1 { margin: 0; color: #111827; font-size: 24px; font-weight: 700; letter-spacing: -0.5px; }
        .status-badge {
            background-color: #e0f2fe;
            color: #0369a1;
            padding: 6px 12px;
            border-radius: 20px;
            font-size: 12px;
            font-weight: 600;
            letter-spacing: 0.5px;
        }
        .grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
            gap: 20px;
            margin-bottom: 45px;
        }
        .card {
            background: #ffffff;
            border: 1px solid #e5e7eb;
            border-radius: 12px;
            padding: 22px;
            box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.03), 0 2px 4px -1px rgba(0, 0, 0, 0.01);
            transition: transform 0.2s, border-color 0.2s;
        }
        .card:hover { transform: translateY(-2px); border-color: #cbd5e1; }
        .card.fire {
            border-color: #ef4444;
            background: #fef2f2;
            box-shadow: 0 8px 20px -6px rgba(239, 68, 68, 0.12);
        }
        .card-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 18px;
            border-bottom: 1px solid #f3f4f6;
            padding-bottom: 10px;
        }
        .card-title { margin: 0; font-size: 16px; font-weight: 600; color: #111827; }
        .badge {
            padding: 4px 8px;
            border-radius: 12px;
            font-size: 11px;
            font-weight: 600;
        }
        .badge-safe { background: #d1fae5; color: #065f46; }
        .badge-fire { background: #fee2e2; color: #991b1b; animation: pulse 1.5s infinite; }
        @keyframes pulse { 0% { opacity: 0.7; } 50% { opacity: 1; } 100% { opacity: 0.7; } }
        .metric { display: flex; justify-content: space-between; margin-bottom: 8px; font-size: 14px; }
        .metric-label { color: #6b7280; }
        .metric-value { font-weight: 600; color: #111827; }
        .ota-section {
            background: #ffffff;
            border: 1px solid #e5e7eb;
            border-radius: 14px;
            padding: 30px;
            box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.03);
        }
        h2 { color: #111827; margin-top: 0; margin-bottom: 25px; font-size: 18px; font-weight: 600; }
        .ota-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 30px; }
        @media (max-width: 768px) { .ota-grid { grid-template-columns: 1fr; } }
        .ota-card {
            background: #fafafa;
            border: 1px solid #f3f4f6;
            border-radius: 10px;
            padding: 20px;
        }
        .upload-form { display: flex; flex-direction: column; gap: 15px; }
        .file-input { display: none; }
        .file-label {
            border: 2px dashed #cbd5e1;
            background: #ffffff;
            border-radius: 8px;
            padding: 25px;
            text-align: center;
            cursor: pointer;
            transition: all 0.2s;
            color: #6b7280;
            font-size: 13.5px;
        }
        .file-label:hover { border-color: #2563eb; background: #eff6ff; color: #1d4ed8; }
        .btn {
            background: #111827;
            border: none;
            color: white;
            padding: 11px;
            border-radius: 6px;
            font-weight: 600;
            font-size: 13.5px;
            cursor: pointer;
            transition: background 0.2s;
        }
        .btn:hover { background: #1f2937; }
        .btn:disabled { background: #9ca3af; cursor: not-allowed; }
        .progress-bar { height: 6px; background: #e5e7eb; border-radius: 3px; overflow: hidden; display: none; margin-top: 5px; }
        .progress-fill { height: 100%; width: 0%; background: #111827; transition: width 0.1s; }
        .progress-text { font-size: 11px; color: #6b7280; text-align: right; display: none; }
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
                    <h3 style="margin-top:0;color:#111827;font-size:15px;font-weight:600;">Nâng cấp Master Node (Tự cập nhật)</h3>
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
                    <h3 style="margin-top:0;color:#111827;font-size:15px;font-weight:600;">Nâng cấp các Vệ tinh (Qua mạng Mesh)</h3>
                    <form id="sat-form" action="/update-satellite" method="POST" enctype="multipart/form-data" class="upload-form">
                        <select name="sat_id" id="sat-id-select" style="width:100%;padding:10px;border-radius:6px;background:#ffffff;color:#111827;border:1px solid #d1d5db;outline:none;box-sizing:border-box;margin-bottom:10px;font-size:13.5px;">
                            <option value="all">Tất cả các Vệ tinh (Tuần tự)</option>
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
                const originalText = btn.innerText;
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
                            btn.innerText = originalText;
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
