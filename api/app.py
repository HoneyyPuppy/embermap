"""
EscapeMesh Simulation API — FastAPI Backend
Wraps the Python routing simulation into REST endpoints for the web visualization.
"""

import sys
import os
import json
from typing import Optional, Dict, Any, List
from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel

# Add project root to path so we can import escapemesh
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from escapemesh.core.network import MeshNetwork

# ---------------------------------------------------------------------------
# App setup
# ---------------------------------------------------------------------------
app = FastAPI(title="EscapeMesh API", version="1.0.0")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

# ---------------------------------------------------------------------------
# In-memory simulation state
# ---------------------------------------------------------------------------
state: Dict[str, Any] = {"network": None, "protocol": None}

TOPOLOGY_PATH = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "topologies",
    "topology_extreme.json",
)

# ---------------------------------------------------------------------------
# Node positions for frontend SVG rendering (per-floor layout)
# ---------------------------------------------------------------------------
def generate_positions(node_ids) -> Dict[str, Dict[str, Any]]:
    positions = {}
    for nid in node_ids:
        if nid == "EXIT_W":
            positions[nid] = {"floor": 0, "x": 30, "y": 150, "type": "exit", "label": "Exit W"}
            continue
        if nid == "EXIT_E":
            positions[nid] = {"floor": 0, "x": 890, "y": 150, "type": "exit", "label": "Exit E"}
            continue
            
        parts = nid.split("_")
        if len(parts) < 2:
            continue
            
        floor_str = parts[0]
        try:
            floor = int(floor_str[1:])
        except ValueError:
            floor = 1
            
        ntype_name = parts[1]
        x = 500
        y = 150
        ntype = "hallway"
        label = nid
        
        if ntype_name == "Stairs":
            ntype = "stairs"
            suffix = parts[2] if len(parts) > 2 else "C"
            label = f"Stairs {suffix}"
            if suffix == "W":
                x = 80
                if floor == 3:
                    y = 170
                else:
                    y = 150
            elif suffix == "C":
                if floor == 5:
                    x = 440
                    y = 80
                elif floor == 4:
                    x = 575
                    y = 240
                elif floor == 3:
                    x = 320
                    y = 280
                elif floor == 2:
                    x = 440
                    y = 230
                elif floor == 1:
                    x = 400
                    y = 230
            elif suffix == "E":
                if floor == 5:
                    x = 890
                    y = 150
                elif floor == 4:
                    x = 890
                    y = 150
                elif floor == 3:
                    x = 575
                    y = 240
                elif floor == 2:
                    x = 720
                    y = 290
                elif floor == 1:
                    x = 840
                    y = 150
                    
        elif ntype_name.startswith("H"):
            try:
                idx = int(ntype_name[1:])
            except ValueError:
                idx = 1
            label = f"H{idx}"
            
            if floor == 5:
                if 1 <= idx <= 12:
                    x = 125 + (idx - 1) * 45
                    y = 180
                elif 18 <= idx <= 22:
                    x = 665 + (idx - 18) * 45
                    y = 180
                elif idx in (13, 14, 15):
                    x = 215 + (idx - 13) * 45
                    y = 90
                elif idx in (16, 17):
                    x = 575 + (idx - 16) * 45
                    y = 90
            elif floor == 4:
                if 1 <= idx <= 11:
                    x = 125 + (idx - 1) * 45
                    y = 150
                elif idx in (13, 14):
                    x = 620 + (idx - 13) * 45
                    y = 150
                elif 19 <= idx <= 21:
                    x = 710 + (idx - 19) * 45
                    y = 150
                elif idx in (15, 16):
                    if idx == 16:
                        x = 260
                    else:
                        x = 215
                    y = 240
                elif idx in (17, 18):
                    if idx == 18:
                        x = 395
                    else:
                        x = 350
                    y = 240
                elif 22 <= idx <= 24:
                    x = 575 + (idx - 21) * 45
                    y = 240
            elif floor == 3:
                if 1 <= idx <= 10:
                    x = 125 + (idx - 1) * 45
                    y = 120
                elif 11 <= idx <= 20:
                    x = 125 + (20 - idx) * 45
                    y = 220
                elif idx in (21, 22):
                    x = 575 + (idx - 21) * 45
                    y = 170
            elif floor == 2:
                if 1 <= idx <= 11:
                    x = 120 + (idx - 1) * 40
                    y = 150
                elif 13 <= idx <= 20:
                    x = 560 + (idx - 13) * 40
                    y = 150
                elif 21 <= idx <= 23:
                    x = 200 + (idx - 21) * 40
                    y = 230
                elif 24 <= idx <= 27:
                    x = 680 + (idx - 24) * 40
                    y = 230
            elif floor == 1:
                if 1 <= idx <= 14:
                    x = 120 + (idx - 1) * 40
                    y = 150
                elif 16 <= idx <= 18:
                    x = 680 + (idx - 16) * 40
                    y = 150
                elif idx == 20:
                    x = 800
                    y = 150
                elif idx in (21, 22):
                    x = 320 + (idx - 21) * 40
                    y = 230
                elif idx in (23, 24):
                    x = 720 + (idx - 23) * 40
                    y = 230
        elif ntype_name.startswith("Dead"):
            ntype = "deadend"
            label = "Dead End"
            if floor == 5:
                x = 350
                y = 90
            elif floor == 4:
                if "1" in nid:
                    x = 170
                else:
                    x = 440
                y = 240
            elif floor == 3:
                x = 665
                y = 170
            elif floor == 2:
                x = 320
                y = 230
            elif floor == 1:
                x = 800
                y = 230
                
        positions[nid] = {"floor": floor, "x": x, "y": y, "type": ntype, "label": label}
    return positions

# ---------------------------------------------------------------------------
# Pydantic request models
# ---------------------------------------------------------------------------
class SimulateRequest(BaseModel):
    protocol: str = "gradient"

class FireRequest(BaseModel):
    node_id: str

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
def _snapshot(network: MeshNetwork) -> Dict[str, Dict[str, Any]]:
    """Capture the current routing state of every node."""
    return {
        node_id: {
            "cost": node.cost,
            "next_hop": node.points_to.id if node.points_to else None,
            "on_fire": node.on_fire,
        }
        for node_id, node in network.nodes.items()
    }


def _run_ticks(network: MeshNetwork, max_ticks: int = 80, stability_window: int = 8):
    """
    Run simulation ticks until convergence or max_ticks.
    Returns (timeline, converged, ticks_taken).
    """
    timeline: List[Dict[str, Any]] = []
    history: List[Dict] = []

    for tick in range(1, max_ticks + 1):
        snap = _snapshot(network)
        history.append(snap)
        if len(history) > stability_window + 2:
            history.pop(0)

        # Check if routing tables have been identical for `stability_window` consecutive ticks
        if len(history) >= stability_window and all(
            history[i] == history[0] for i in range(1, len(history))
        ):
            timeline.append({"tick": tick, "nodes": snap})
            return timeline, True, tick

        changed = network.tick()
        timeline.append({"tick": tick, "nodes": snap})

        if not changed and len(history) >= 2 and history[-1] == history[-2]:
            # No change and states match — early exit
            pass  # keep going a few more ticks for proactive protocols

    return timeline, False, max_ticks

# ---------------------------------------------------------------------------
# Endpoints
# ---------------------------------------------------------------------------
@app.get("/api/topology")
def get_topology():
    """Return the building topology structure and node positions for rendering."""
    with open(TOPOLOGY_PATH, "r") as f:
        topo = json.load(f)
    node_ids = list(topo.get("nodes", {}).keys())
    return {
        "nodes": topo.get("nodes", {}),
        "links": topo.get("links", []),
        "node_positions": generate_positions(node_ids),
    }


@app.post("/api/simulate")
def simulate(req: SimulateRequest):
    """Initialize network with chosen protocol and run convergence."""
    valid_protocols = ["gradient", "link_state", "dsdv", "aodv", "potential_field", "rpl"]
    if req.protocol not in valid_protocols:
        raise HTTPException(400, f"Invalid protocol. Choose from: {valid_protocols}")

    network = MeshNetwork()
    network.load_from_topology(TOPOLOGY_PATH, routing_mode=req.protocol)

    timeline, converged, ticks_taken = _run_ticks(network, max_ticks=80, stability_window=8)

    # Store network for subsequent fire calls
    state["network"] = network
    state["protocol"] = req.protocol

    return {
        "protocol": req.protocol,
        "converged": converged,
        "ticks_taken": ticks_taken,
        "timeline": timeline,
    }


@app.post("/api/fire")
def fire(req: FireRequest):
    """Trigger fire on a node and run recovery ticks."""
    if state["network"] is None:
        raise HTTPException(400, "No simulation running. Call /api/simulate first.")

    network: MeshNetwork = state["network"]
    if req.node_id not in network.nodes:
        raise HTTPException(404, f"Node '{req.node_id}' not found in network.")

    node = network.nodes[req.node_id]
    if node.is_exit:
        raise HTTPException(400, "Cannot set fire on an exit node.")
    if node.on_fire:
        raise HTTPException(400, f"Node '{req.node_id}' is already on fire.")

    node.trigger_fire()

    # Run recovery with a wider stability window to account for healing propagation
    timeline, converged, ticks_taken = _run_ticks(network, max_ticks=80, stability_window=12)

    return {
        "fired_node": req.node_id,
        "converged": converged,
        "ticks_taken": ticks_taken,
        "timeline": timeline,
    }


@app.post("/api/reset")
def reset():
    """Clear stored simulation state."""
    state["network"] = None
    state["protocol"] = None
    return {"status": "ok"}
