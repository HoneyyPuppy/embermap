export const state = {
  topology: null,   // { nodes, links, node_positions }
  currentFloor: 0,
  converged: false,
  simulating: false,
  currentNodeStates: {}, // node_id -> { cost, next_hop, on_fire }
  
  // Pan and zoom states for each floor
  panZoomStates: Array.from({ length: 6 }, () => ({
    x: 0,
    y: 0,
    scale: 1,
    isDragging: false,
    startX: 0,
    startY: 0
  }))
};
