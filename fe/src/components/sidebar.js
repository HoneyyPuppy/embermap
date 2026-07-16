import { state } from '../state.js';
import { INF } from '../config.js';

export function updateRoutingTable() {
  const container = document.getElementById('routing-table');
  const positions = state.topology.node_positions;

  const floorNodes = Object.entries(positions)
    .filter(([, p]) => p.floor === state.currentFloor)
    .sort(([a], [b]) => a.localeCompare(b));

  if (floorNodes.length === 0 || Object.keys(state.currentNodeStates).length === 0) {
    container.innerHTML = '<p class="hint">Run convergence first</p>';
    return;
  }

  let html = '<table><thead><tr><th>Node</th><th>Cost</th><th>Next</th></tr></thead><tbody>';
  floorNodes.forEach(([id]) => {
    const s = state.currentNodeStates[id] || {};
    const cost = s.cost != null ? (s.cost >= INF ? '∞' : s.cost.toFixed(1)) : '—';
    const next = s.next_hop || '—';
    const cls = s.on_fire ? 'fire' : (s.cost >= INF ? 'unreachable' : '');
    html += `<tr class="${cls}"><td>${id}</td><td>${cost}</td><td>${next}</td></tr>`;
  });
  html += '</tbody></table>';
  container.innerHTML = html;
}

export function showTooltip(e, nodeId) {
  const tt = document.getElementById('tooltip');
  const s = state.currentNodeStates[nodeId] || {};
  const cost = s.cost != null ? (s.cost >= INF ? '∞' : s.cost.toFixed(1)) : '—';
  const props = state.topology.nodes[nodeId] || {};
  const smoke = s.smoke_level != null ? Math.round(s.smoke_level) : Math.round(props.smoke_level || 100);
  const threshold = s.smoke_threshold != null ? Math.round(s.smoke_threshold) : Math.round(props.smoke_threshold || 400);

  tt.innerHTML = `
    <div class="tt-id">${nodeId}</div>
    <div class="tt-row">Cost/Rank: <strong>${cost}</strong></div>
    <div class="tt-row">Next Hop: <strong>${s.next_hop || 'None'}</strong></div>
    <div class="tt-row">MQ2 Smoke: <strong>${smoke}/${threshold} PPM</strong></div>
    ${s.on_fire ? '<div class="tt-row" style="color:#ef4444">🔥 ALARM (FIRE)</div>' : ''}
  `;
  tt.classList.remove('hidden');
  moveTooltip(e);
}

export function moveTooltip(e) {
  const tt = document.getElementById('tooltip');
  tt.style.left = (e.clientX + 14) + 'px';
  tt.style.top = (e.clientY + 14) + 'px';
}

export function hideTooltip() {
  const tt = document.getElementById('tooltip');
  if (tt) tt.classList.add('hidden');
}

const hardwareData = {
  gradient: {
    ram: "Very Low (<1 KB)",
    ramBadge: "green",
    rom: "Very Low (~10 KB)",
    romBadge: "green",
    cpu: "O(1) - Very Low",
    cpuBadge: "green",
    reliability: "Medium (Loops)",
    reliabilityBadge: "yellow",
    overhead: "None (Passive)",
    overheadBadge: "green",
    esp32: "Lãng phí (Dưới năng lực)",
    esp32Badge: "yellow",
    recom: "Dòng MCU 8-bit giá rẻ (ATmega328P, STM8). Với ESP32, chạy Gradient là lãng phí tài nguyên CPU/RAM.",
    perf: {
      small: { conv: "5 ticks", msg: "0", rating: "Tối ưu", badge: "green" },
      med: { conv: "6 ticks", msg: "0", rating: "Tối ưu", badge: "green" },
      large: { conv: "27 ticks", msg: "0", rating: "Kém", badge: "red" }
    }
  },
  link_state: {
    ram: "High (30-100 KB)",
    ramBadge: "red",
    rom: "High (~80 KB)",
    romBadge: "red",
    cpu: "O(E log V) - High",
    cpuBadge: "red",
    reliability: "High (Short loops)",
    reliabilityBadge: "blue",
    overhead: "Very High (Floods)",
    overheadBadge: "red",
    esp32: "Tốt (Xử lý Dijkstra tốt)",
    esp32Badge: "blue",
    recom: "Thích hợp làm Gateway trung tâm. ESP32 có 520KB RAM dư sức chạy Dijkstra mượt mà.",
    perf: {
      small: { conv: "0 ticks", msg: "28", rating: "Thừa", badge: "yellow" },
      med: { conv: "6 ticks", msg: "124", rating: "Tốt", badge: "green" },
      large: { conv: "8 ticks", msg: "1548", rating: "Quá tải", badge: "red" }
    }
  },
  dsdv: {
    ram: "Medium (~15 KB)",
    ramBadge: "yellow",
    rom: "Medium (~35 KB)",
    romBadge: "yellow",
    cpu: "O(N) - Medium",
    cpuBadge: "yellow",
    reliability: "Excellent (Loop-free)",
    reliabilityBadge: "green",
    overhead: "High (Updates)",
    overheadBadge: "yellow",
    esp32: "Khá tốt (Đủ bộ nhớ)",
    esp32Badge: "blue",
    recom: "ESP32 chạy tốt thuật toán này nhưng băng thông phát sóng cập nhật định kỳ sẽ tiêu tốn nhiều pin.",
    perf: {
      small: { conv: "0 ticks", msg: "94", rating: "Thừa", badge: "yellow" },
      med: { conv: "6 ticks", msg: "290", rating: "Tốt", badge: "green" },
      large: { conv: "35 ticks", msg: "12049", rating: "Quá tải", badge: "red" }
    }
  },
  aodv: {
    ram: "Medium (~12 KB)",
    ramBadge: "yellow",
    rom: "High (~90 KB)",
    romBadge: "red",
    cpu: "O(D) - Medium",
    cpuBadge: "yellow",
    reliability: "Excellent (Loop-free)",
    reliabilityBadge: "green",
    overhead: "High on change",
    overheadBadge: "yellow",
    esp32: "Rất tốt (Tiết kiệm RAM)",
    esp32Badge: "green",
    recom: "ESP32 đáp ứng tốt AODV. Thích hợp cho mạng truyền thông theo nhu cầu (on-demand).",
    perf: {
      small: { conv: "0 ticks", msg: "29", rating: "Thừa", badge: "yellow" },
      med: { conv: "30 ticks", msg: "443", rating: "Tốt", badge: "green" },
      large: { conv: "30 ticks", msg: "5184", rating: "Kém", badge: "yellow" }
    }
  },
  potential_field: {
    ram: "Very Low (<1 KB)",
    ramBadge: "green",
    rom: "Very Low (~12 KB)",
    romBadge: "green",
    cpu: "O(1) - Very Low",
    cpuBadge: "green",
    reliability: "High (Repulsion)",
    reliabilityBadge: "blue",
    overhead: "None (Passive)",
    overheadBadge: "green",
    esp32: "Tốt (Phản ứng cháy nhanh)",
    esp32Badge: "blue",
    recom: "Chạy cực nhẹ trên ESP32. Lực đẩy ảo quanh đám cháy giúp dẫn luồng gói tin thoát nạn tức thời.",
    perf: {
      small: { conv: "5 ticks", msg: "0", rating: "Tối ưu", badge: "green" },
      med: { conv: "6 ticks", msg: "0", rating: "Tối ưu", badge: "green" },
      large: { conv: "31 ticks", msg: "0", rating: "Kẹt", badge: "yellow" }
    }
  },
  rpl: {
    ram: "Low-Med (5-10 KB)",
    ramBadge: "blue",
    rom: "High (~70 KB)",
    romBadge: "red",
    cpu: "O(D) - Low",
    cpuBadge: "green",
    reliability: "Excellent (DODAG)",
    reliabilityBadge: "green",
    overhead: "Optimal (Trickle)",
    overheadBadge: "green",
    esp32: "Xuất sắc (Khuyên dùng)",
    esp32Badge: "green",
    recom: "Lựa chọn tốt nhất cho ESP32. Đảm bảo chống lặp bằng Rank và tối ưu tin nhắn điều khiển bằng Trickle Timer.",
    perf: {
      small: { conv: "22 ticks", msg: "90", rating: "Thừa", badge: "yellow" },
      med: { conv: "19 ticks", msg: "186", rating: "Tốt", badge: "green" },
      large: { conv: "100 ticks", msg: "6639", rating: "Tối ưu", badge: "green" }
    }
  }
};

export function updateHardwareSuitability(protocol) {
  const container = document.getElementById('hardware-suitability');
  if (!container) return;

  const data = hardwareData[protocol];
  if (!data) {
    container.innerHTML = '<p class="hint">Select a protocol to inspect</p>';
    return;
  }

  container.innerHTML = `
    <div class="hw-row">
      <span class="hw-label">RAM Footprint</span>
      <span class="badge badge-${data.ramBadge}">${data.ram}</span>
    </div>
    <div class="hw-row">
      <span class="hw-label">ROM/Flash</span>
      <span class="badge badge-${data.romBadge}">${data.rom}</span>
    </div>
    <div class="hw-row">
      <span class="hw-label">CPU Complexity</span>
      <span class="badge badge-${data.cpuBadge}">${data.cpu}</span>
    </div>
    <div class="hw-row">
      <span class="hw-label">Control Overhead</span>
      <span class="badge badge-${data.overheadBadge}">${data.overhead}</span>
    </div>
    <div class="hw-row">
      <span class="hw-label">Loop Resistance</span>
      <span class="badge badge-${data.reliabilityBadge}">${data.reliability}</span>
    </div>
    <div class="hw-row">
      <span class="hw-label">ESP32 Suitability</span>
      <span class="badge badge-${data.esp32Badge}">${data.esp32}</span>
    </div>
    
    <div style="margin-top: 14px; border-top: 1px solid var(--border); padding-top: 10px;">
      <span style="font-size: 10px; font-weight: 700; text-transform: uppercase; color: var(--text-dim); display: block; margin-bottom: 6px; letter-spacing: 0.5px;">Hiệu năng theo quy mô</span>
      <table style="width: 100%; font-size: 10px; border-collapse: collapse; margin-bottom: 8px;">
        <thead>
          <tr style="border-bottom: 1px solid var(--border); color: var(--text-dim);">
            <th style="text-align: left; padding: 4px 0; font-weight: 600;">Quy mô</th>
            <th style="text-align: center; padding: 4px 0; font-weight: 600;">Ticks / Msgs</th>
            <th style="text-align: right; padding: 4px 0; font-weight: 600;">Đánh giá</th>
          </tr>
        </thead>
        <tbody>
          <tr style="border-bottom: 1px solid rgba(16,185,129,0.04);">
            <td style="padding: 5px 0; font-weight: 500; color: var(--text);">Nhỏ (6)</td>
            <td style="text-align: center; padding: 5px 0; color: var(--text-dim);">${data.perf.small.conv.split(' ')[0]} / ${data.perf.small.msg}</td>
            <td style="text-align: right; padding: 5px 0;"><span class="badge badge-${data.perf.small.badge}">${data.perf.small.rating}</span></td>
          </tr>
          <tr style="border-bottom: 1px solid rgba(16,185,129,0.04);">
            <td style="padding: 5px 0; font-weight: 500; color: var(--text);">Vừa (16)</td>
            <td style="text-align: center; padding: 5px 0; color: var(--text-dim);">${data.perf.med.conv.split(' ')[0]} / ${data.perf.med.msg}</td>
            <td style="text-align: right; padding: 5px 0;"><span class="badge badge-${data.perf.med.badge}">${data.perf.med.rating}</span></td>
          </tr>
          <tr style="border-bottom: 1px solid rgba(16,185,129,0.04);">
            <td style="padding: 5px 0; font-weight: 500; color: var(--text);">Lớn (90+)</td>
            <td style="text-align: center; padding: 5px 0; color: var(--text-dim);">${data.perf.large.conv.split(' ')[0]} / ${data.perf.large.msg}</td>
            <td style="text-align: right; padding: 5px 0;"><span class="badge badge-${data.perf.large.badge}">${data.perf.large.rating}</span></td>
          </tr>
        </tbody>
      </table>
    </div>

    <div class="hw-recom">
      <strong>Khuyến nghị phần cứng:</strong><br/>
      ${data.recom}
    </div>
  `;
}

export function updateFloorAnalysis() {
  const container = document.getElementById('floor-analysis');
  if (!container) return;

  if (!state.topology || !state.topology.node_positions) {
    container.innerHTML = '<p class="hint">Topology not loaded</p>';
    return;
  }

  const positions = state.topology.node_positions;
  const floorNodes = Object.keys(positions).filter(id => positions[id].floor === state.currentFloor);
  const totalNodes = floorNodes.length;

  if (totalNodes === 0) {
    container.innerHTML = '<p class="hint">No nodes on this floor</p>';
    return;
  }

  const hasStates = Object.keys(state.currentNodeStates).length > 0;
  
  const fireNodes = floorNodes.filter(id => {
    const s = state.currentNodeStates[id];
    return s && s.on_fire;
  });

  const activeStairs = floorNodes.filter(id => {
    const p = positions[id];
    const s = state.currentNodeStates[id] || {};
    return p.type === 'stairs' && !s.on_fire;
  });

  const hasDirectExit = floorNodes.some(id => positions[id].type === 'exit');

  // Count active links on this floor
  const activeLinks = state.topology.links.filter(([n1, n2]) => {
    const p1 = positions[n1];
    const p2 = positions[n2];
    if (!p1 || !p2) return false;
    if (p1.floor !== state.currentFloor || p2.floor !== state.currentFloor) return false;
    const s1 = state.currentNodeStates[n1] || {};
    const s2 = state.currentNodeStates[n2] || {};
    return !s1.on_fire && !s2.on_fire;
  });

  // Average hops
  let avgHops = "Chưa hội tụ";
  let outageRisk = "Thấp";
  let riskBadge = "green";
  let recommendation = "Hoạt động bình thường.";

  if (hasStates) {
    let reachableCount = 0;
    let totalCost = 0;
    
    floorNodes.forEach(id => {
      const s = state.currentNodeStates[id];
      if (s && !s.on_fire && s.cost != null && s.cost < INF) {
        reachableCount++;
        totalCost += s.cost;
      }
    });

    if (reachableCount > 0) {
      avgHops = (totalCost / reachableCount).toFixed(1) + " Hops";
    } else {
      avgHops = "Vô hạn (Bị cô lập)";
    }

    // Determine Risk Level
    if (reachableCount === 0) {
      outageRisk = "Nguy hiểm (Cô lập)";
      riskBadge = "red";
      recommendation = "CẢNH BÁO: Tầng bị mất hoàn toàn kết nối tới lối thoát hiểm. Cần kích hoạt thoát nạn thủ công!";
    } else if (fireNodes.length > 0) {
      if (activeStairs.length === 0 && !hasDirectExit) {
        outageRisk = "Nguy hiểm (Cô lập)";
        riskBadge = "red";
        recommendation = "CẦN DI TẢN: Toàn bộ lối cầu thang của tầng đã bị cháy!";
      } else if (activeStairs.length === 1 && !hasDirectExit) {
        outageRisk = "Cao";
        riskBadge = "risk-high";
        recommendation = "CHÚ Ý: Chỉ còn 1 cầu thang hoạt động. Điều hướng cảm biến sang cầu thang này.";
      } else {
        outageRisk = "Trung bình";
        riskBadge = "risk-medium";
        recommendation = "Hệ thống đang tự khắc phục hỏa hoạn, chuyển luồng sang cầu thang an toàn.";
      }
    } else {
      if (hasDirectExit) {
        outageRisk = "Rất thấp";
        riskBadge = "green";
        recommendation = "Tầng trệt có lối ra trực tiếp. Mạng lưới cực kỳ an toàn.";
      } else if (activeStairs.length === 1) {
        outageRisk = "Trung bình";
        riskBadge = "risk-medium";
        recommendation = "Tầng chỉ có 1 cầu thang. Nếu cầu thang này bị cháy, tầng sẽ bị cô lập.";
      } else {
        outageRisk = "Thấp";
        riskBadge = "green";
        recommendation = "Mạng mesh hoạt động ổn định với nhiều đường cầu thang dự phòng.";
      }
    }
  } else {
    // Before simulation runs
    if (hasDirectExit) {
      outageRisk = "Thấp";
      riskBadge = "green";
      recommendation = "Tầng có lối thoát hiểm trực tiếp.";
    } else if (activeStairs.length === 1) {
      outageRisk = "Trung bình";
      riskBadge = "risk-medium";
      recommendation = "Tầng chỉ có 1 cầu thang kết nối.";
    } else {
      outageRisk = "Thấp";
      riskBadge = "green";
      recommendation = "Mạng có nhiều cầu thang kết nối dự phòng.";
    }
  }

  container.innerHTML = `
    <div class="hw-row">
      <span class="hw-label">Số cảm biến (Nodes)</span>
      <span class="hw-value">${totalNodes} node</span>
    </div>
    <div class="hw-row">
      <span class="hw-label">Liên kết hoạt động</span>
      <span class="hw-value">${activeLinks.length} links</span>
    </div>
    <div class="hw-row">
      <span class="hw-label">Số cầu thang hoạt động</span>
      <span class="hw-value">${activeStairs.length} cầu thang</span>
    </div>
    <div class="hw-row">
      <span class="hw-label">Trễ trung bình (Hops)</span>
      <span class="hw-value" style="font-weight: 700; color: var(--primary);">${avgHops}</span>
    </div>
    <div class="hw-row">
      <span class="hw-label">Rủi ro cô lập (Outage Risk)</span>
      <span class="badge badge-${riskBadge}">${outageRisk}</span>
    </div>
    <div class="hw-recom" style="border-left-color: ${riskBadge === 'red' ? '#ef4444' : riskBadge === 'risk-high' ? '#f97316' : riskBadge === 'risk-medium' ? '#f59e0b' : '#10b981'};">
      <strong>Khuyến cáo của tầng:</strong><br/>
      ${recommendation}
    </div>
  `;
}
