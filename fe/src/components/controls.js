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
  const csmaEnabled = document.getElementById('per-csma-enabled').checked;
  const smokePropagationEnabled = document.getElementById('per-smoke-propagation-enabled').checked;
  const smokeIncrement = parseFloat(document.getElementById('per-smoke-increment').value);

  try {
    const data = await runSimulation(protocol, lossRate, maxDistance, firePenalty, maxTicks, delayFactor, jitterTicks, csmaEnabled, smokePropagationEnabled, smokeIncrement);
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
  if (state.editMode) {
    selectNode(nodeId);
    return;
  }
  selectNode(nodeId);
  if (state.converged && !state.simulating) {
    const pos = state.topology.node_positions[nodeId];
    if (pos && pos.type !== 'exit') {
      triggerFire(nodeId);
    }
  }
}

export function selectNode(nodeId) {
  state.selectedNodeId = nodeId;
  const pos = state.topology.node_positions[nodeId];
  const props = state.topology.nodes[nodeId] || {};
  if (!pos) return;

  const panel = document.getElementById('node-config-panel');
  if (panel) panel.style.display = 'flex';

  const smokeLevelInput = document.getElementById('node-smoke-level');
  const valSmokeSpan = document.getElementById('val-node-smoke');
  const thresholdInput = document.getElementById('node-smoke-threshold');

  const nodeState = state.currentNodeStates[nodeId] || {};
  const currentSmoke = nodeState.smoke_level != null ? nodeState.smoke_level : (props.smoke_level || 100.0);
  const threshold = props.smoke_threshold != null ? props.smoke_threshold : (nodeState.smoke_threshold || 400.0);

  if (smokeLevelInput) smokeLevelInput.value = currentSmoke;
  if (valSmokeSpan) valSmokeSpan.textContent = Math.round(currentSmoke);
  if (thresholdInput) thresholdInput.value = threshold;

  const infoDiv = document.getElementById('node-info');
  if (infoDiv) {
    infoDiv.innerHTML = `
      <div style="font-size: 13px; font-weight: 600; color: #10b981;">${nodeId}</div>
      <div style="font-size: 11px; margin-top: 4px; display: flex; flex-direction: column; gap: 2px;">
        <div>Floor: <strong>${pos.floor}</strong></div>
        <div>Type: <strong>${pos.type.toUpperCase()}</strong></div>
        <div>Status: <strong style="color: ${nodeState.on_fire ? '#ef4444' : '#10b981'};">${nodeState.on_fire ? '🔥 ALARM (FIRE)' : '✓ Normal'}</strong></div>
      </div>
    `;
  }

  const stairsRow = document.getElementById('stairs-connector-row');
  if (pos.type === 'stairs') {
    if (stairsRow) stairsRow.style.display = 'flex';
    const select = document.getElementById('node-stairs-select');
    if (select) {
      select.innerHTML = '';
      Object.entries(state.topology.node_positions)
        .filter(([otherId, otherPos]) => otherPos.type === 'stairs' && otherId !== nodeId && otherPos.floor !== pos.floor)
        .forEach(([otherId]) => {
          const opt = document.createElement('option');
          opt.value = otherId;
          opt.textContent = `${otherId} (Floor ${state.topology.node_positions[otherId].floor})`;
          select.appendChild(opt);
        });
    }
  } else {
    if (stairsRow) stairsRow.style.display = 'none';
  }
}

export async function setNodeSmokeLevel(nodeId, level) {
  if (state.simulating || !state.converged) return;
  state.simulating = true;
  
  const btn = document.getElementById('btn-converge');
  if (btn) btn.disabled = true;
  setStatus('State', `💨 Set Smoke on ${nodeId}...`);

  try {
    const data = await triggerFireIncident(nodeId, level);
    await playTimeline(data.timeline, 'Evacuation');
    setStatus('State', `Simulated ✓`);
    setStatus('Tick', `${data.ticks_taken} ticks`);
  } catch (e) {
    console.warn(e);
    setStatus('State', 'Error');
  }
  if (btn) btn.disabled = false;
  state.simulating = false;
}

export function setEditMode(enable) {
  state.editMode = enable;
  document.getElementById('btn-mode-inspect').classList.toggle('active', !enable);
  document.getElementById('btn-mode-edit').classList.toggle('active', enable);
  document.getElementById('editor-actions').style.display = enable ? 'flex' : 'none';
  
  if (!enable) {
    const panel = document.getElementById('node-config-panel');
    if (panel) panel.style.display = 'none';
    state.selectedNodeId = null;
  }
  buildFloorSVGs();
}

export async function handleSaveTopology() {
  const { saveTopology } = await import('../api.js');
  const btn = document.getElementById('btn-topo-save');
  if (btn) {
    btn.disabled = true;
    btn.textContent = '⏳ Saving...';
  }
  try {
    const payload = {
      nodes: state.topology.nodes,
      links: state.topology.links,
      node_positions: state.topology.node_positions
    };
    await saveTopology(payload);
    setEditMode(false);
  } catch (e) {
    console.error('Save error:', e);
    alert('Failed to save topology layout.');
  }
  if (btn) {
    btn.disabled = false;
    btn.textContent = '💾 Save';
  }
}

export async function handleRevertTopology() {
  if (!confirm('Revert all custom topology changes to the default layout?')) return;
  const { revertTopology, fetchTopology } = await import('../api.js');
  try {
    await revertTopology();
    state.topology = await fetchTopology();
    buildFloorSVGs();
    goToFloor(state.currentFloor);
  } catch (e) {
    console.error('Revert error:', e);
    alert('Failed to revert topology.');
  }
}

export function bindControls() {
  document.getElementById('btn-converge').addEventListener('click', runConvergence);
  document.getElementById('btn-reset').addEventListener('click', doReset);

  // Editor mode switches
  document.getElementById('btn-mode-inspect').addEventListener('click', () => setEditMode(false));
  document.getElementById('btn-mode-edit').addEventListener('click', () => setEditMode(true));
  document.getElementById('btn-topo-save').addEventListener('click', handleSaveTopology);
  document.getElementById('btn-topo-revert').addEventListener('click', handleRevertTopology);

  // Selected Node Configuration sliders/inputs
  const smokeSlider = document.getElementById('node-smoke-level');
  if (smokeSlider) {
    smokeSlider.addEventListener('change', () => {
      if (state.selectedNodeId) {
        const val = parseFloat(smokeSlider.value);
        if (state.topology.nodes[state.selectedNodeId]) {
          state.topology.nodes[state.selectedNodeId].smoke_level = val;
        }
        setNodeSmokeLevel(state.selectedNodeId, val);
      }
    });
    smokeSlider.addEventListener('input', () => {
      const span = document.getElementById('val-node-smoke');
      if (span) span.textContent = smokeSlider.value;
    });
  }

  const thresholdInput = document.getElementById('node-smoke-threshold');
  if (thresholdInput) {
    thresholdInput.addEventListener('change', () => {
      if (state.selectedNodeId) {
        const val = parseFloat(thresholdInput.value);
        if (state.topology.nodes[state.selectedNodeId]) {
          state.topology.nodes[state.selectedNodeId].smoke_threshold = val;
        }
        // Force convergence recalculation if threshold changed
        doReset();
      }
    });
  }

  const btnStairsConnect = document.getElementById('btn-stairs-connect');
  if (btnStairsConnect) {
    btnStairsConnect.addEventListener('click', () => {
      const select = document.getElementById('node-stairs-select');
      if (state.selectedNodeId && select && select.value) {
        const targetStairs = select.value;
        const exists = state.topology.links.some(([a, b]) => 
          (a === state.selectedNodeId && b === targetStairs) || 
          (a === targetStairs && b === state.selectedNodeId)
        );
        if (!exists) {
          state.topology.links.push([state.selectedNodeId, targetStairs]);
          buildFloorSVGs();
          alert(`Successfully linked stairs node ${state.selectedNodeId} to ${targetStairs}!`);
          selectNode(state.selectedNodeId); // Refresh connections dropdown list
        } else {
          alert('This connection already exists!');
        }
      }
    });
  }

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

  const sliderSmokeInc = document.getElementById('per-smoke-increment');
  const spanSmokeInc = document.getElementById('val-smoke-increment');
  if (sliderSmokeInc && spanSmokeInc) {
    sliderSmokeInc.addEventListener('input', () => {
      spanSmokeInc.textContent = `${sliderSmokeInc.value} PPM`;
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
