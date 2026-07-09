import { state } from '../state.js';
import { FLOOR_LABELS, TYPE_COLORS, FIRE_COLOR, UNREACHABLE_COLOR, INF } from '../config.js';
import { createSVGEl } from '../utils/helpers.js';
import { showTooltip, moveTooltip, hideTooltip } from './sidebar.js';
import { handleNodeClick } from './controls.js';

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
    if (e.button !== 0 || e.target.classList.contains('node-circle')) return;
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
  const positions = state.topology.node_positions;

  // 1. Draw outer hallway walls (thick outlines)
  state.topology.links.forEach(([a, b]) => {
    const pa = positions[a];
    const pb = positions[b];
    if (!pa || !pb) return;
    if (pa.floor === floor && pb.floor === floor) {
      const wallLine = createSVGEl('line', {
        x1: pa.x, y1: pa.y, x2: pb.x, y2: pb.y,
        stroke: '#cbd5e1', // Soft gray-slate wall borders
        'stroke-width': 42,
        'stroke-linecap': 'round',
      });
      wallsG.appendChild(wallLine);
    }
  });

  // 2. Draw hallway floors (clean white floor paths)
  state.topology.links.forEach(([a, b]) => {
    const pa = positions[a];
    const pb = positions[b];
    if (!pa || !pb) return;
    if (pa.floor === floor && pb.floor === floor) {
      const floorLine = createSVGEl('line', {
        x1: pa.x, y1: pa.y, x2: pb.x, y2: pb.y,
        stroke: '#ffffff', // Crisp white walkway corridor
        'stroke-width': 36,
        'stroke-linecap': 'round',
      });
      wallsG.appendChild(floorLine);
    }
  });

  // 3. Draw carpet tile dotted grid lines down the hallways (green-tinted)
  state.topology.links.forEach(([a, b]) => {
    const pa = positions[a];
    const pb = positions[b];
    if (!pa || !pb) return;
    if (pa.floor === floor && pb.floor === floor) {
      const tileLine = createSVGEl('line', {
        x1: pa.x, y1: pa.y, x2: pb.x, y2: pb.y,
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
        stroke: 'rgba(255, 255, 255, 0.03)',
        'stroke-width': 12,
        'stroke-linecap': 'round',
      });
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

export function drawNodes(svg, floor) {
  const g = svg.querySelector('.nodes-layer');
  const positions = state.topology.node_positions;

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
          stroke: '#10b981', 'stroke-width': 4, 'stroke-opacity': 0.15,
          'stroke-linecap': 'round',
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

      // Expand visual fire zone to fill sector
      if (fireZone) {
        fireZone.setAttribute('r', '80');
        fireZone.setAttribute('opacity', '0.25');
      }
    } else if (s.cost >= INF) {
      // Unreachable offline node
      circle.setAttribute('fill', UNREACHABLE_COLOR);
      circle.style.opacity = '0.3';
    } else {
      // Normal operating green/amber guidelines
      circle.setAttribute('fill', TYPE_COLORS[pos.type] || '#666');
      circle.style.opacity = '1';
      if (pos.type === 'exit') circle.classList.add('node-exit');
    }
  });
}
