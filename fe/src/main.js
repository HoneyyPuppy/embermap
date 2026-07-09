import './style.css';
import { state } from './state.js';
import { fetchTopology } from './api.js';
import { buildFloorSVGs } from './components/floorSvg.js';
import { bindControls, goToFloor } from './components/controls.js';

async function init() {
  try {
    state.topology = await fetchTopology();
  } catch (e) {
    console.warn('Failed to fetch topology — is the API running?', e);
    const viewport = document.querySelector('#floor-viewport');
    if (viewport) {
      viewport.innerHTML =
        '<p style="padding:40px;color:#ef4444;font-size:14px;">⚠ Cannot connect to API at localhost:5000. Start the FastAPI backend first.</p>';
    }
    return;
  }

  buildFloorSVGs();
  bindControls();
  goToFloor(0);
}

init();
