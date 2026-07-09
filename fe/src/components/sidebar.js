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

  tt.innerHTML = `
    <div class="tt-id">${nodeId}</div>
    <div class="tt-row">Cost/Rank: <strong>${cost}</strong></div>
    <div class="tt-row">Next Hop: <strong>${s.next_hop || 'None'}</strong></div>
    ${s.on_fire ? '<div class="tt-row" style="color:#ef4444">🔥 FIRE DETECTED</div>' : ''}
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
