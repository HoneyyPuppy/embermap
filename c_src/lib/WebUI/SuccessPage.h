#ifndef SUCCESS_PAGE_H
#define SUCCESS_PAGE_H

#include <Arduino.h>

static const char SUCCESS_HTML[] PROGMEM = R"rawhtml(
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
)rawhtml";

#endif // SUCCESS_PAGE_H
