#ifndef DASHBOARD_PAGE_H
#define DASHBOARD_PAGE_H

#include <Arduino.h>

static const char DASHBOARD_HTML[] PROGMEM = R"rawhtml(
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
                ${!isMaster ? `
                <div class="metric">
                    <span class="metric-label">Next Hop ID:</span>
                    <span class="metric-value">${node.nextHopId !== 255 ? node.nextHopId : 'MẤT TUYẾN'}</span>
                </div>
                <div class="metric">
                    <span class="metric-label">Thế năng APF:</span>
                    <span class="metric-value">${node.pot !== 9999 ? node.pot.toFixed(1) : 'VÔ HẠN (KẸT)'}</span>
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

#endif // DASHBOARD_PAGE_H
