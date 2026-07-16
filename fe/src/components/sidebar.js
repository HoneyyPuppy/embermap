import { state } from '../state.js';
import { INF } from '../config.js';

export function updateRoutingTable() {
  const container = document.getElementById('routing-table');
  if (!container) return;
  
  const positions = state.topology ? state.topology.node_positions : null;
  if (!positions) {
    container.innerHTML = '<p class="hint">Topology not loaded</p>';
    return;
  }

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
  if (!tt) return;
  const s = state.currentNodeStates[nodeId] || {};
  const cost = s.cost != null ? (s.cost >= INF ? '∞' : s.cost.toFixed(1)) : '—';
  const props = state.topology ? (state.topology.nodes[nodeId] || {}) : {};
  const smoke = s.smoke_level != null ? Math.round(s.smoke_level) : Math.round(props.smoke_level || 100);
  const threshold = s.smoke_threshold != null ? Math.round(s.smoke_threshold) : Math.round(props.smoke_threshold || 400);
  const txCount = s.tx_msg_count != null ? s.tx_msg_count : 0;

  // Calculate distance to selected node if there is one selected
  let distanceHtml = '';
  if (state.selectedNodeId && state.selectedNodeId !== nodeId && state.topology && state.topology.node_positions) {
    const pos1 = state.topology.node_positions[state.selectedNodeId];
    const pos2 = state.topology.node_positions[nodeId];
    if (pos1 && pos2) {
      const dx = pos1.x - pos2.x;
      const dy = pos1.y - pos2.y;
      const dz = (pos1.floor - pos2.floor) * 100;
      const dist = Math.round(Math.sqrt(dx*dx + dy*dy + dz*dz));
      distanceHtml = `<div class="tt-row" style="color:var(--primary); font-weight:700;">Distance to ${state.selectedNodeId}: ${dist} px</div>`;
    }
  }

  tt.innerHTML = `
    <div class="tt-id">${nodeId}</div>
    <div class="tt-row">Cost/Rank: <strong>${cost}</strong></div>
    <div class="tt-row">Next Hop: <strong>${s.next_hop || 'None'}</strong></div>
    <div class="tt-row">MQ2 Smoke: <strong>${smoke}/${threshold} PPM</strong></div>
    <div class="tt-row">TX Messages: <strong>${txCount}</strong></div>
    ${distanceHtml}
    ${s.on_fire ? '<div class="tt-row" style="color:#ef4444">🔥 ALARM (FIRE)</div>' : ''}
  `;
  tt.classList.remove('hidden');
  moveTooltip(e);
}

export function moveTooltip(e) {
  const tt = document.getElementById('tooltip');
  if (tt) {
    tt.style.left = (e.clientX + 14) + 'px';
    tt.style.top = (e.clientY + 14) + 'px';
  }
}

export function hideTooltip() {
  const tt = document.getElementById('tooltip');
  if (tt) tt.classList.add('hidden');
}

export function updateHardwareSuitability(protocol) {
  const container = document.getElementById('hardware-suitability');
  if (!container) return;

  if (!state.topology) {
    container.innerHTML = '<p class="hint">Topology not loaded</p>';
    return;
  }

  // V (total network nodes) and E (total network links)
  const V = Object.keys(state.topology.nodes).length;
  const E = state.topology.links.length;
  
  // Calculate dynamic RAM based on network size
  let ramBytes = 64;
  let romKB = 8.2;
  let complexity = "O(1)";
  
  switch(protocol) {
    case 'gradient':
      ramBytes = 64;
      romKB = 8.2;
      complexity = "O(1)";
      break;
    case 'potential_field':
      ramBytes = 80;
      romKB = 9.5;
      complexity = "O(1)";
      break;
    case 'dsdv':
      ramBytes = V * 32;
      romKB = 26.4;
      complexity = "O(V)";
      break;
    case 'aodv':
      ramBytes = V * 24;
      romKB = 68.1;
      complexity = "O(V)";
      break;
    case 'rpl':
      ramBytes = V * 28;
      romKB = 52.7;
      complexity = "O(V)";
      break;
    case 'link_state':
      ramBytes = V * 32 + E * 16;
      romKB = 48.3;
      complexity = "O(V log V)";
      break;
  }

  const ticks = state.ticksTaken || 0;
  const totalMsgs = state.totalMessages || 0;
  
  // Dynamic average messages per node per tick
  const avgMsg = (ticks > 0 && V > 0) ? (totalMsgs / (V * ticks)).toFixed(2) : "0.00";

  // Calculate dynamic suitability score (0-100%)
  const ramKB = ramBytes / 1024;
  let score = 100;
  score -= ramKB * 1.5; // deduction for RAM memory footprint
  score -= (totalMsgs / V) * 0.8; // deduction for packet overhead
  score -= ticks * 0.2; // deduction for convergence speed
  
  // Bounded between 5% and 100%
  const finalScore = Math.max(5, Math.min(100, Math.round(score)));
  
  let scoreBadge = "green";
  if (finalScore < 40) scoreBadge = "red";
  else if (finalScore < 70) scoreBadge = "risk-high";
  else if (finalScore < 85) scoreBadge = "risk-medium";

  container.innerHTML = `
    <div class="hw-row">
      <span class="hw-label">Active Nodes</span>
      <span class="hw-value">${V}</span>
    </div>
    <div class="hw-row">
      <span class="hw-label">Est. RAM / Node</span>
      <span class="hw-value" style="font-weight: 600;">${ramBytes < 1024 ? ramBytes + ' B' : (ramBytes/1024).toFixed(2) + ' KB'}</span>
    </div>
    <div class="hw-row">
      <span class="hw-label">Est. ROM Size</span>
      <span class="hw-value">${romKB.toFixed(1)} KB</span>
    </div>
    <div class="hw-row">
      <span class="hw-label">CPU Complexity</span>
      <span class="hw-value" style="font-family: monospace;">${complexity}</span>
    </div>
    <div class="hw-row">
      <span class="hw-label">Control Messages</span>
      <span class="hw-value">${totalMsgs}</span>
    </div>
    <div class="hw-row">
      <span class="hw-label">Msg / Node / Tick</span>
      <span class="hw-value">${avgMsg}</span>
    </div>
    <div class="hw-row">
      <span class="hw-label">Convergence Time</span>
      <span class="hw-value">${ticks > 0 ? ticks + ' ticks' : '—'}</span>
    </div>
    <div class="hw-row" style="margin-top: 10px; border-top: 1px dashed var(--border); padding-top: 8px;">
      <span class="hw-label" style="font-weight: 700;">Suitability Score</span>
      <span class="badge badge-${scoreBadge}" style="font-size: 11px; font-weight: 700;">${finalScore}%</span>
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
  let avgHops = "—";
  let riskPercent = 0;
  let riskBadge = "green";

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
      avgHops = (totalCost / reachableCount).toFixed(1) + " hops";
    } else {
      avgHops = "∞ (isolated)";
    }

    // Determine Risk Level (0-100%)
    if (reachableCount === 0) {
      riskPercent = 100;
    } else if (fireNodes.length > 0) {
      if (activeStairs.length === 0 && !hasDirectExit) {
        riskPercent = 100;
      } else if (activeStairs.length === 1 && !hasDirectExit) {
        riskPercent = 80;
      } else {
        riskPercent = 45;
      }
    } else {
      if (hasDirectExit) {
        riskPercent = 5;
      } else if (activeStairs.length === 1) {
        riskPercent = 35;
      } else {
        riskPercent = 15;
      }
    }
  } else {
    // Before simulation runs
    if (hasDirectExit) {
      riskPercent = 10;
    } else if (activeStairs.length === 1) {
      riskPercent = 40;
    } else {
      riskPercent = 20;
    }
  }
  
  if (riskPercent < 30) riskBadge = "green";
  else if (riskPercent < 60) riskBadge = "risk-medium";
  else if (riskPercent < 80) riskBadge = "risk-high";
  else riskBadge = "red";

  container.innerHTML = `
    <div class="hw-row">
      <span class="hw-label">Floor Level</span>
      <span class="hw-value">${state.currentFloor}</span>
    </div>
    <div class="hw-row">
      <span class="hw-label">Floor Nodes</span>
      <span class="hw-value">${totalNodes}</span>
    </div>
    <div class="hw-row">
      <span class="hw-label">Active Links</span>
      <span class="hw-value">${activeLinks.length}</span>
    </div>
    <div class="hw-row">
      <span class="hw-label">Active Stairs</span>
      <span class="hw-value">${activeStairs.length}</span>
    </div>
    <div class="hw-row">
      <span class="hw-label">Avg Path Cost</span>
      <span class="hw-value" style="font-weight: 600; color: var(--primary);">${avgHops}</span>
    </div>
    <div class="hw-row" style="margin-top: 10px; border-top: 1px dashed var(--border); padding-top: 8px;">
      <span class="hw-label" style="font-weight: 700;">Outage Risk Score</span>
      <span class="badge badge-${riskBadge}" style="font-size: 11px; font-weight: 700;">${riskPercent}%</span>
    </div>
  `;
}
