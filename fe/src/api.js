import { API } from './config.js';

export async function fetchTopology() {
  const res = await fetch(`${API}/api/topology`);
  if (!res.ok) throw new Error('Failed to fetch topology');
  return await res.json();
}

export async function runSimulation(protocol, lossRate = 0.0, maxDistance = 300.0, firePenalty = 0.4, maxTicks = 80, delayFactor = 0.0, jitterTicks = 0) {
  const res = await fetch(`${API}/api/simulate`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      protocol,
      packet_loss_rate: lossRate,
      max_distance: maxDistance,
      fire_penalty: firePenalty,
      max_ticks: maxTicks,
      delay_factor: delayFactor,
      jitter_ticks: jitterTicks
    }),
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
