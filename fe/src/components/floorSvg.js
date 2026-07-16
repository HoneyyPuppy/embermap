import { state } from '../state.js';
import { FLOOR_LABELS, TYPE_COLORS, FIRE_COLOR, UNREACHABLE_COLOR, INF } from '../config.js';
import { createSVGEl } from '../utils/helpers.js';
import { showTooltip, moveTooltip, hideTooltip } from './sidebar.js';
import { handleNodeClick, selectNode } from './controls.js';

export function buildFloorSVGs() {
  const slider = document.getElementById('floor-slider');
  slider.innerHTML = '';

  for (let f = 0; f < 6; f++) {
    const panel = document.createElement('div');
    panel.className = 'floor-panel';
    panel.dataset.floor = f;

    const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
    svg.setAttribute('viewBox', '0 0 1000 400');
    svg.setAttribute('preserveAspectRatio', 'xMidYMid meet');
    svg.id = `floor-svg-${f}`;
    svg.style.cursor = 'grab';

    svg.innerHTML = `
      <defs>
        <!-- Radial gradient for fire overlay -->
        <radialGradient id="fire-radial" cx="50%" cy="50%" r="50%">
          <stop offset="0%" stop-color="#ef4444" stop-opacity="0.8"/>
          <stop offset="50%" stop-color="#f97316" stop-opacity="0.3"/>
          <stop offset="100%" stop-color="#ef4444" stop-opacity="0"/>
        </radialGradient>
        
        <!-- Arrow markers for route lights -->
        <marker id="arrow-${f}" markerWidth="6" markerHeight="6" refX="6" refY="3" orient="auto">
          <polygon points="0 0, 6 3, 0 6" fill="#10b981"/>
        </marker>
        
        <!-- Floor grid patterns for blueprint texture -->
        <pattern id="grid-${f}" width="20" height="20" patternUnits="userSpaceOnUse">
          <path d="M 20 0 L 0 0 0 20" fill="none" stroke="rgba(16,185,129,0.04)" stroke-width="1"/>
        </pattern>
      </defs>
      
      <!-- Infinite drawing board grid background -->
      <rect x="0" y="0" width="1000" height="400" fill="url(#grid-${f})" style="pointer-events: none;"/>
      
      <text x="45" y="45" class="floor-title" fill="#0f172a" style="pointer-events: none;">${FLOOR_LABELS[f]}</text>
      
      <!-- Viewport group for scroll zoom and drag panning -->
      <g id="viewport-group-${f}" transform="translate(0, 0) scale(1)" style="transition: transform 0.05s ease-out;">
        <g class="walls-layer" id="walls-${f}"></g>
        <g class="labels-layer" id="labels-${f}"></g>
        <g class="links-layer" id="links-${f}"></g>
        <g class="routes-layer" id="routes-${f}"></g>
        <g class="nodes-layer" id="nodes-${f}"></g>
      </g>
    `;

    // Draw realistic walls, rooms and doors
    drawArchitecturalPlan(svg, f);

    // Draw inter-floor connections
    drawLinks(svg, f);

    // Draw sensor nodes
    drawNodes(svg, f);

    // Setup events for panning and zooming
    setupPanZoom(svg, f);

    panel.appendChild(svg);
    slider.appendChild(panel);
  }
}

export function setupPanZoom(svg, f) {
  const group = svg.getElementById(`viewport-group-${f}`);
  const s = state.panZoomStates[f];

  const updateTransform = () => {
    group.setAttribute('transform', `translate(${s.x}, ${s.y}) scale(${s.scale})`);
  };

  svg.addEventListener('mousedown', (e) => {
    if (e.button !== 0 || e.target.classList.contains('node-circle') || e.target.classList.contains('node-delete-btn')) return;
    s.isDragging = true;
    svg.style.cursor = 'grabbing';
    s.startX = e.clientX - s.x;
    s.startY = e.clientY - s.y;
  });

  window.addEventListener('mousemove', (e) => {
    if (!s.isDragging) return;
    s.x = e.clientX - s.startX;
    s.y = e.clientY - s.startY;
    updateTransform();
  });

  window.addEventListener('mouseup', () => {
    if (s.isDragging) {
      s.isDragging = false;
      svg.style.cursor = 'grab';
    }
  });

  svg.addEventListener('wheel', (e) => {
    e.preventDefault();
    const zoomFactor = 1.1;
    const prevScale = s.scale;
    
    if (e.deltaY < 0) {
      s.scale = Math.min(s.scale * zoomFactor, 4.0);
    } else {
      s.scale = Math.max(s.scale / zoomFactor, 0.5);
    }

    const rect = svg.getBoundingClientRect();
    const mouseX = e.clientX - rect.left;
    const mouseY = e.clientY - rect.top;

    s.x = mouseX - (mouseX - s.x) * (s.scale / prevScale);
    s.y = mouseY - (mouseY - s.y) * (s.scale / prevScale);

    updateTransform();
  }, { passive: false });
}

export function drawArchitecturalPlan(svg, floor) {
  const wallsG = svg.querySelector('.walls-layer');
  const labelsG = svg.querySelector('.labels-layer');
  const corridors = (state.buildingLayout && state.buildingLayout.corridors) || [];

  // 1. Draw outer hallway walls (thick outlines)
  corridors.forEach((corr) => {
    if (corr.floor === floor) {
      const wallLine = createSVGEl('line', {
        x1: corr.x1, y1: corr.y1, x2: corr.x2, y2: corr.y2,
        stroke: '#cbd5e1', // Soft gray-slate wall borders
        'stroke-width': 42,
        'stroke-linecap': 'round',
      });
      wallsG.appendChild(wallLine);
    }
  });

  // 2. Draw hallway floors (clean white floor paths)
  corridors.forEach((corr) => {
    if (corr.floor === floor) {
      const floorLine = createSVGEl('line', {
        x1: corr.x1, y1: corr.y1, x2: corr.x2, y2: corr.y2,
        stroke: '#ffffff', // Crisp white walkway corridor
        'stroke-width': 36,
        'stroke-linecap': 'round',
      });
      wallsG.appendChild(floorLine);
    }
  });

  // 3. Draw carpet tile dotted grid lines down the hallways (green-tinted)
  corridors.forEach((corr) => {
    if (corr.floor === floor) {
      const tileLine = createSVGEl('line', {
        x1: corr.x1, y1: corr.y1, x2: corr.x2, y2: corr.y2,
        stroke: 'rgba(16,185,129,0.08)',
        'stroke-width': 34,
        'stroke-dasharray': '2 18',
        'stroke-linecap': 'butt',
      });
      wallsG.appendChild(tileLine);
    }
  });

  // 4. Draw floor descriptive tags
  const yOffset = floor === 0 ? 330 : 60;
  const labelText = createSVGEl('text', {
    x: 500, y: yOffset,
    fill: 'rgba(16,185,129,0.08)',
    'font-size': '16px',
    'font-weight': '700',
    'letter-spacing': '2px',
    'text-anchor': 'middle',
    'pointer-events': 'none',
  });
  labelText.textContent = FLOOR_LABELS[floor].toUpperCase();
  labelsG.appendChild(labelText);
}

export function drawLinks(svg, floor) {
  const g = svg.querySelector('.links-layer');
  const positions = state.topology.node_positions;

  state.topology.links.forEach(([a, b]) => {
    const pa = positions[a];
    const pb = positions[b];
    if (!pa || !pb) return;

    // Direct hallway floor link (drawn on floor)
    if (pa.floor === floor && pb.floor === floor) {
      const line = createSVGEl('line', {
        x1: pa.x, y1: pa.y, x2: pb.x, y2: pb.y,
        stroke: 'rgba(59, 130, 246, 0.15)', // Standby glow
        'stroke-width': 8,
        'stroke-linecap': 'round',
        id: `link-${a}-${b}`
      });
      line.dataset.from = a;
      line.dataset.to = b;

      // Link selection and deletion handles in Edit Mode
      if (state.editMode) {
        line.style.cursor = 'pointer';
        line.setAttribute('stroke', 'rgba(59, 130, 246, 0.4)');
        line.addEventListener('mouseenter', () => line.setAttribute('stroke', 'rgba(239, 68, 68, 0.8)'));
        line.addEventListener('mouseleave', () => line.setAttribute('stroke', 'rgba(59, 130, 246, 0.4)'));
        line.addEventListener('click', (e) => {
          e.stopPropagation();
          if (confirm(`Delete connection link between ${a} and ${b}?`)) {
            state.topology.links = state.topology.links.filter(([n1, n2]) => 
              !(n1 === a && n2 === b) && !(n1 === b && n2 === a)
            );
            buildFloorSVGs();
          }
        });
      }
      g.appendChild(line);
    }

    // Inter-floor vertical stairs connection (drawn on floor)
    if ((pa.floor === floor && pb.floor !== floor) || (pb.floor === floor && pa.floor !== floor)) {
      const local = pa.floor === floor ? pa : pb;
      const remote = pa.floor === floor ? pb : pa;
      const direction = remote.floor < local.floor ? '▼ DOWN' : '▲ UP';
      const text = createSVGEl('text', {
        x: local.x, y: local.y - 12,
        fill: 'rgba(59, 130, 246, 0.5)',
        'font-size': '8px',
        'font-weight': '700',
        'text-anchor': 'middle',
      });
      text.textContent = direction;
      g.appendChild(text);
    }
  });
}

function getSVGCoords(e, svg, floor) {
  const pt = svg.createSVGPoint();
  pt.x = e.clientX;
  pt.y = e.clientY;
  const group = svg.getElementById(`viewport-group-${floor}`);
  if (!group) return { x: e.clientX, y: e.clientY };
  const screenCTM = group.getScreenCTM();
  if (!screenCTM) return { x: e.clientX, y: e.clientY };
  return pt.matrixTransform(screenCTM.inverse());
}

function deleteNode(nodeId) {
  if (!confirm(`Delete MQ2 Sensor node ${nodeId} and all its connection links?`)) return;
  state.topology.links = state.topology.links.filter(([a, b]) => a !== nodeId && b !== nodeId);
  delete state.topology.nodes[nodeId];
  delete state.topology.node_positions[nodeId];
  if (state.selectedNodeId === nodeId) {
    state.selectedNodeId = null;
    const panel = document.getElementById('node-config-panel');
    if (panel) panel.style.display = 'none';
  }
  buildFloorSVGs();
}

function addLink(nodeA, nodeB) {
  const exists = state.topology.links.some(([a, b]) => (a === nodeA && b === nodeB) || (a === nodeB && b === nodeA));
  if (exists) return;
  state.topology.links.push([nodeA, nodeB]);
  buildFloorSVGs();
}

function moveNodeVisuals(nodeId, newX, newY, svg, floor) {
  const nodeG = svg.querySelector(`g[data-node-id="${nodeId}"]`);
  if (nodeG) {
    const circle = nodeG.querySelector('.node-circle');
    if (circle) { circle.setAttribute('cx', newX); circle.setAttribute('cy', newY); }
    const dot = nodeG.querySelector('circle[pointer-events="none"]');
    if (dot) { dot.setAttribute('cx', newX); dot.setAttribute('cy', newY); }
    const glow = nodeG.querySelector('.glow-ring');
    if (glow) { glow.setAttribute('cx', newX); glow.setAttribute('cy', newY); }
    const label = nodeG.querySelector('.node-label');
    if (label) { label.setAttribute('x', newX); label.setAttribute('y', newY + 16); }
    const deleteBtn = nodeG.querySelector('.node-delete-btn');
    if (deleteBtn) { deleteBtn.setAttribute('cx', newX + 8); deleteBtn.setAttribute('cy', newY - 8); }
    const delDots = nodeG.querySelectorAll('circle');
    if (delDots.length > 2) {
      delDots[2].setAttribute('cx', newX + 8);
      delDots[2].setAttribute('cy', newY - 8);
    }
  }
  
  const lines = svg.querySelectorAll(`line`);
  lines.forEach(line => {
    if (line.dataset.from === nodeId) {
      line.setAttribute('x1', newX);
      line.setAttribute('y1', newY);
    } else if (line.dataset.to === nodeId) {
      line.setAttribute('x2', newX);
      line.setAttribute('y2', newY);
    }
  });
}

export function drawNodes(svg, floor) {
  const g = svg.querySelector('.nodes-layer');
  const positions = state.topology.node_positions;

  // Add double-click handler for node addition on background
  svg.addEventListener('dblclick', (e) => {
    if (!state.editMode) return;
    if (e.target.classList.contains('node-circle') || e.target.classList.contains('node-delete-btn')) return;

    const coords = getSVGCoords(e, svg, floor);
    const nodeId = prompt(`Enter ID for new MQ2 Sensor on Floor ${floor} (e.g. F${floor}_H26):`);
    if (!nodeId) return;

    if (state.topology.nodes[nodeId]) {
      alert('Node ID already exists!');
      return;
    }

    const type = prompt('Enter sensor type (hallway, stairs, exit, deadend):', 'hallway');
    if (!['hallway', 'stairs', 'exit', 'deadend'].includes(type)) {
      alert('Invalid node type selected.');
      return;
    }

    // Add to local state
    state.topology.nodes[nodeId] = { is_exit: type === 'exit', smoke_level: 100.0, smoke_threshold: 400.0 };
    state.topology.node_positions[nodeId] = {
      floor: floor,
      x: Math.round(coords.x),
      y: Math.round(coords.y),
      type: type,
      label: nodeId
    };

    buildFloorSVGs();
  });

  // Handle temporary drag lines for linking
  svg.addEventListener('mousemove', (e) => {
    if (state.editMode) {
      if (state.linkingStartNode) {
        const tempLine = svg.getElementById(`temp-link-line-${floor}`);
        if (tempLine) {
          const coords = getSVGCoords(e, svg, floor);
          tempLine.setAttribute('x2', coords.x);
          tempLine.setAttribute('y2', coords.y);
        }
      } else if (state.draggingNodeId) {
        const coords = getSVGCoords(e, svg, floor);
        const nodePos = state.topology.node_positions[state.draggingNodeId];
        if (nodePos && nodePos.floor === floor) {
          nodePos.x = Math.round(coords.x);
          nodePos.y = Math.round(coords.y);
          moveNodeVisuals(state.draggingNodeId, nodePos.x, nodePos.y, svg, floor);
        }
      }
    }
  });

  svg.addEventListener('mouseup', () => {
    if (state.draggingNodeId) {
      state.draggingNodeId = null;
      svg.style.cursor = 'grab';
      buildFloorSVGs(); // Re-render to restore clean layout structures
    }
    state.linkingStartNode = null;
    const tempLine = svg.getElementById(`temp-link-line-${floor}`);
    if (tempLine) tempLine.remove();
  });

  Object.entries(positions)
    .filter(([, p]) => p.floor === floor)
    .forEach(([id, pos]) => {
      const nodeG = createSVGEl('g', { 'data-node-id': id });

      // Room dynamic fire hazard overlay (fills room sector)
      const fireZone = createSVGEl('circle', {
        cx: pos.x, cy: pos.y, r: 0, fill: 'url(#fire-radial)',
        opacity: 0, id: `fire-zone-${id}`,
        style: 'transition: r 0.4s ease-out, opacity 0.4s ease-out; pointer-events:none;',
      });
      nodeG.appendChild(fireZone);

      // Pulse glow for status indicators
      const glowRing = createSVGEl('circle', {
        cx: pos.x, cy: pos.y, r: 6, fill: 'none',
        stroke: TYPE_COLORS[pos.type] || '#666', 'stroke-width': 2, opacity: 0,
        class: 'glow-ring', id: `glow-${id}`,
      });
      nodeG.appendChild(glowRing);

      // Small floor sensor node (radius 6)
      const circle = createSVGEl('circle', {
        cx: pos.x, cy: pos.y, r: 6,
        fill: TYPE_COLORS[pos.type] || '#666',
        class: 'node-circle',
        id: `node-${id}`,
        stroke: '#0f172a',
        'stroke-width': 1.5,
      });
      nodeG.appendChild(circle);

      // Micro dot inside
      const dot = createSVGEl('circle', {
        cx: pos.x, cy: pos.y, r: 1.5,
        fill: '#fff',
        'pointer-events': 'none',
      });
      nodeG.appendChild(dot);

      // Micro floor label text
      const label = createSVGEl('text', {
        x: pos.x, y: pos.y + 16, class: 'node-label',
      });
      label.textContent = id.replace(/^F\d_/, '');
      nodeG.appendChild(label);

      // Add delete handle if in Edit Mode
      if (state.editMode) {
        const deleteHandle = createSVGEl('circle', {
          cx: pos.x + 8, cy: pos.y - 8, r: 4,
          class: 'node-delete-btn',
          title: `Delete ${id}`
        });
        deleteHandle.addEventListener('click', (e) => {
          e.stopPropagation();
          deleteNode(id);
        });
        nodeG.appendChild(deleteHandle);
        
        // Micro dot inside delete handle
        const delDot = createSVGEl('circle', {
          cx: pos.x + 8, cy: pos.y - 8, r: 1,
          fill: '#fff',
          'pointer-events': 'none'
        });
        nodeG.appendChild(delDot);
      }

      // Drag linking and node moving events
      circle.addEventListener('mousedown', (e) => {
        if (!state.editMode) return;
        e.stopPropagation();

        if (e.shiftKey) {
          // Link Mode (Shift + Drag)
          state.linkingStartNode = id;
          const tempLine = createSVGEl('line', {
            x1: pos.x, y1: pos.y,
            x2: pos.x, y2: pos.y,
            class: 'temp-link-line',
            id: `temp-link-line-${floor}`
          });
          svg.getElementById(`viewport-group-${floor}`).appendChild(tempLine);
        } else {
          // Move Mode (Drag Node)
          state.draggingNodeId = id;
          svg.style.cursor = 'move';
        }
      });

      circle.addEventListener('mouseup', (e) => {
        if (state.editMode && state.linkingStartNode && state.linkingStartNode !== id) {
          e.stopPropagation();
          addLink(state.linkingStartNode, id);
          state.linkingStartNode = null;
          const tempLine = svg.getElementById(`temp-link-line-${floor}`);
          if (tempLine) tempLine.remove();
        }
      });

      // Events
      nodeG.addEventListener('mouseenter', (e) => showTooltip(e, id));
      nodeG.addEventListener('mousemove', (e) => moveTooltip(e));
      nodeG.addEventListener('mouseleave', hideTooltip);
      nodeG.addEventListener('click', () => handleNodeClick(id));

      g.appendChild(nodeG);
    });
}

export function updateRouteLines() {
  const positions = state.topology.node_positions;

  for (let f = 0; f < 6; f++) {
    const routesG = document.getElementById(`routes-${f}`);
    if (!routesG) continue;
    routesG.innerHTML = '';

    // Update the links layer to change colors/glow proximity to fire!
    const linksG = document.getElementById(`links-${f}`);
    if (linksG) {
      linksG.querySelectorAll('line').forEach(line => {
        const fromNode = line.dataset.from;
        const toNode = line.dataset.to;
        if (fromNode && toNode) {
          const sFrom = state.currentNodeStates[fromNode] || {};
          const sTo = state.currentNodeStates[toNode] || {};
          if (sFrom.on_fire || sTo.on_fire) {
            // Danger LED link
            line.setAttribute('stroke', '#ef4444');
            line.setAttribute('stroke-width', '4');
            line.classList.add('node-fire');
          } else {
            // Normal Standby Link
            line.setAttribute('stroke', 'rgba(59, 130, 246, 0.15)');
            line.setAttribute('stroke-width', '2');
            line.classList.remove('node-fire');
          }
        }
      });
    }

    Object.entries(state.currentNodeStates).forEach(([id, s]) => {
      const pos = positions[id];
      if (!pos || pos.floor !== f || !s.next_hop) return;

      const nextPos = positions[s.next_hop];
      if (!nextPos) return;

      // Same floor guide path (glowing LED strip running down floor)
      if (nextPos.floor === f) {
        // Neon pathway strip
        const glowLine = createSVGEl('line', {
          x1: pos.x, y1: pos.y, x2: nextPos.x, y2: nextPos.y,
          stroke: '#10b981', 'stroke-width': 6, 'stroke-opacity': 0.35,
          'stroke-linecap': 'round',
          style: 'filter: drop-shadow(0 0 3px #10b981);'
        });
        routesG.appendChild(glowLine);

        // Core light flow guide
        const line = createSVGEl('line', {
          x1: pos.x, y1: pos.y, x2: nextPos.x, y2: nextPos.y,
          class: 'route-line',
          stroke: '#10b981', // Clean green LED flow guide
          'marker-end': `url(#arrow-${f})`,
        });
        routesG.appendChild(line);
      } else {
        // Vertical stairs guidance lights
        const dir = nextPos.floor < f ? '▼ ESCAPE DOWN' : '▲ ESCAPE UP';
        const text = createSVGEl('text', {
          x: pos.x, y: pos.y + 26,
          fill: '#10b981', 'font-size': '7px', 'font-weight': '800',
          'font-family': 'Inter, sans-serif', 'text-anchor': 'middle',
          class: 'route-vertical-indicator',
        });
        text.textContent = dir;
        routesG.appendChild(text);
      }
    });
  }
}

export function updateNodeVisuals() {
  const positions = state.topology.node_positions;

  Object.entries(positions).forEach(([id, pos]) => {
    const circle = document.getElementById(`node-${id}`);
    const fireZone = document.getElementById(`fire-zone-${id}`);
    if (!circle) return;

    const s = state.currentNodeStates[id];
    if (!s) return;

    // Reset animations
    circle.classList.remove('node-fire', 'node-exit');
    if (fireZone) {
      fireZone.setAttribute('r', '0');
      fireZone.setAttribute('opacity', '0');
    }

    if (s.on_fire) {
      // Fire alarm LED state
      circle.setAttribute('fill', FIRE_COLOR);
      circle.style.opacity = '1';
      circle.classList.add('node-fire');

      // Expand visual fire zone to fill sector based on smoke PPM
      if (fireZone) {
        const smoke = s.smoke_level != null ? s.smoke_level : 1000.0;
        const radius = (smoke / 1000.0) * 120;
        const opacity = (smoke / 1000.0) * 0.4;
        fireZone.setAttribute('r', radius);
        fireZone.setAttribute('opacity', opacity);
      }
    } else if (s.cost >= INF) {
      // Unreachable offline node
      circle.setAttribute('fill', UNREACHABLE_COLOR);
      circle.style.opacity = '0.3';
    } else {
      // Normal operating green/amber guidelines
      circle.setAttribute('fill', TYPE_COLORS[pos.type] || '#666');
      circle.style.opacity = '1';
      
      // Also show partial smoke clouds if smoke_level is raised but not triggering fire
      if (fireZone && s.smoke_level > 100.0) {
        const radius = (s.smoke_level / 1000.0) * 100;
        const opacity = (s.smoke_level / 1000.0) * 0.2;
        fireZone.setAttribute('r', radius);
        fireZone.setAttribute('opacity', opacity);
      }

      if (pos.type === 'exit') circle.classList.add('node-exit');
    }
  });
}
