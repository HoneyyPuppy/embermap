export function createSVGEl(tag, attrs = {}) {
  const el = document.createElementNS('http://www.w3.org/2000/svg', tag);
  Object.entries(attrs).forEach(([k, v]) => el.setAttribute(k, v));
  return el;
}

export function setStatus(label, value) {
  const map = { 'Protocol': 'status-protocol', 'State': 'status-state', 'Tick': 'status-tick' };
  const el = document.getElementById(map[label]);
  if (el) el.textContent = value;
}

export function sleep(ms) {
  return new Promise(r => setTimeout(r, ms));
}
