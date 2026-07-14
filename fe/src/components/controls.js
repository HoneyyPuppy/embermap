import { state } from '../state.js';
import { runSimulation, triggerFireIncident, resetSimulation } from '../api.js';
import { setStatus, sleep } from '../utils/helpers.js';
import { updateNodeVisuals, updateRouteLines, buildFloorSVGs } from './floorSvg.js';
import { updateRoutingTable } from './sidebar.js';

export function goToFloor(f) {
  state.currentFloor = f;
  const slider = document.getElementById('floor-slider');
  if (slider) slider.style.transform = `translateX(-${f * 100}%)`;

  document.querySelectorAll('.floor-pill').forEach((pill) => {
    pill.classList.toggle('active', parseInt(pill.dataset.floor) === f);
  });

  updateRoutingTable();
}

export async function runConvergence() {
  if (state.simulating) return;
  state.simulating = true;
  state.converged = false;

  const protocol = document.getElementById('protocol-select').value;
  const btn = document.getElementById('btn-converge');
  if (btn) {
    btn.disabled = true;
    btn.textContent = '⏳ Calculating...';
  }

  setStatus('Protocol', protocol);
  setStatus('State', 'Converging…');

  const lossRate = parseFloat(document.getElementById('per-loss-rate').value) / 100;
  const maxDistance = parseFloat(document.getElementById('per-max-dist').value);
  const firePenalty = parseFloat(document.getElementById('per-fire-penalty').value) / 100;
  const maxTicks = parseInt(document.getElementById('per-max-ticks').value);
  const delayFactor = parseFloat(document.getElementById('per-delay-factor').value);
  const jitterTicks = parseInt(document.getElementById('per-jitter-ticks').value);

  try {
    const data = await runSimulation(protocol, lossRate, maxDistance, firePenalty, maxTicks, delayFactor, jitterTicks);
    await playTimeline(data.timeline, 'Converging');

    state.converged = true;
    setStatus('State', `Converged ✓`);
    setStatus('Tick', `${data.ticks_taken} ticks`);
  } catch (e) {
    console.warn('Simulate error:', e);
    setStatus('State', 'Error');
  }

  if (btn) {
    btn.disabled = false;
    btn.textContent = '▶ Converge';
  }
  state.simulating = false;
}

export async function triggerFire(nodeId) {
  if (state.simulating || !state.converged) return;

  const s = state.currentNodeStates[nodeId];
  if (s && (s.on_fire || s.cost === 0)) return;

  state.simulating = true;
  const btn = document.getElementById('btn-converge');
  if (btn) btn.disabled = true;
  setStatus('State', `🔥 Fire on ${nodeId}…`);

  try {
    const data = await triggerFireIncident(nodeId);
    await playTimeline(data.timeline, 'Healing');

    setStatus('State', `Healed ✓`);
    setStatus('Tick', `${data.ticks_taken} ticks`);
  } catch (e) {
    console.warn('Fire error:', e);
    setStatus('State', 'Error');
  }

  if (btn) btn.disabled = false;
  state.simulating = false;
}

export async function playTimeline(timeline, phase) {
  for (let i = 0; i < timeline.length; i++) {
    const frame = timeline[i];
    state.currentNodeStates = frame.nodes;
    setStatus('Tick', `${phase} ${frame.tick}/${timeline.length}`);

    updateNodeVisuals();
    updateRouteLines();
    updateRoutingTable();

    const delay = timeline.length > 20 ? 80 : 180;
    await sleep(delay);
  }
}

export async function doReset() {
  try {
    await resetSimulation();
  } catch (e) { /* ignore */ }

  state.converged = false;
  state.simulating = false;
  state.currentNodeStates = {};

  buildFloorSVGs();
  goToFloor(state.currentFloor);

  setStatus('Protocol', '—');
  setStatus('State', 'Idle');
  setStatus('Tick', '—');

  const rt = document.getElementById('routing-table');
  if (rt) rt.innerHTML = '<p class="hint">Run convergence first</p>';
  const btn = document.getElementById('btn-converge');
  if (btn) {
    btn.disabled = false;
    btn.textContent = '▶ Converge';
  }
}

export function handleNodeClick(nodeId) {
  if (!state.converged || state.simulating) return;
  const pos = state.topology.node_positions[nodeId];
  if (pos && pos.type === 'exit') return;
  triggerFire(nodeId);
}

export function bindControls() {
  document.getElementById('btn-converge').addEventListener('click', runConvergence);
  document.getElementById('btn-reset').addEventListener('click', doReset);

  // Bind PER Settings sliders
  const sliderLoss = document.getElementById('per-loss-rate');
  const spanLoss = document.getElementById('val-loss-rate');
  if (sliderLoss && spanLoss) {
    sliderLoss.addEventListener('input', () => {
      spanLoss.textContent = `${sliderLoss.value}%`;
    });
  }

  const sliderDist = document.getElementById('per-max-dist');
  const spanDist = document.getElementById('val-max-dist');
  if (sliderDist && spanDist) {
    sliderDist.addEventListener('input', () => {
      spanDist.textContent = sliderDist.value;
    });
  }

  const sliderFire = document.getElementById('per-fire-penalty');
  const spanFire = document.getElementById('val-fire-penalty');
  if (sliderFire && spanFire) {
    sliderFire.addEventListener('input', () => {
      spanFire.textContent = `${sliderFire.value}%`;
    });
  }

  const sliderTicks = document.getElementById('per-max-ticks');
  const spanTicks = document.getElementById('val-max-ticks');
  if (sliderTicks && spanTicks) {
    sliderTicks.addEventListener('input', () => {
      spanTicks.textContent = sliderTicks.value;
    });
  }

  const sliderDelay = document.getElementById('per-delay-factor');
  const spanDelay = document.getElementById('val-delay-factor');
  if (sliderDelay && spanDelay) {
    sliderDelay.addEventListener('input', () => {
      spanDelay.textContent = parseFloat(sliderDelay.value).toFixed(1);
    });
  }

  const sliderJitter = document.getElementById('per-jitter-ticks');
  const spanJitter = document.getElementById('val-jitter-ticks');
  if (sliderJitter && spanJitter) {
    sliderJitter.addEventListener('input', () => {
      spanJitter.textContent = sliderJitter.value;
    });
  }

  document.querySelectorAll('.floor-pill').forEach((pill) => {
    pill.addEventListener('click', () => goToFloor(parseInt(pill.dataset.floor)));
  });

  document.addEventListener('keydown', (e) => {
    if (e.key === 'ArrowRight' && state.currentFloor < 5) goToFloor(state.currentFloor + 1);
    if (e.key === 'ArrowLeft' && state.currentFloor > 0) goToFloor(state.currentFloor - 1);
  });
}
