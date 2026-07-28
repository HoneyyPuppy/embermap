#ifndef PORTAL_PAGE_H
#define PORTAL_PAGE_H

#include <Arduino.h>

inline String getPortalHtml(const String& wifiOptions, const String& defaultServerUrl) {
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
                <input type="text" name="url" id="url" value=")rawhtml" + defaultServerUrl + R"rawhtml(" placeholder="http://[SERVER_IP]:8000/device-readings/ingest" required>
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

#endif // PORTAL_PAGE_H
