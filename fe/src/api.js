import { API } from './config.js';

export async function fetchTopology() {
  const res = await fetch(`${API}/api/topology`);
  if (!res.ok) throw new Error('Failed to fetch topology');
  return await res.json();
}

export async function runSimulation(protocol) {
  const res = await fetch(`${API}/api/simulate`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ protocol }),
  });
  if (!res.ok) throw new Error('Simulation API error');
  return await res.json();
}

export async function triggerFireIncident(nodeId) {
  const res = await fetch(`${API}/api/fire`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ node_id: nodeId }),
  });
  if (!res.ok) throw new Error('Fire API error');
  return await res.json();
}

export async function resetSimulation() {
  const res = await fetch(`${API}/api/reset`, { method: 'POST' });
  if (!res.ok) throw new Error('Reset API error');
  return await res.json();
}
