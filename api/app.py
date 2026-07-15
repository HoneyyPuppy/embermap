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
from escapemesh.core.network import MeshNetwork, generate_positions

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
# Pydantic request models
# ---------------------------------------------------------------------------
class SimulateRequest(BaseModel):
    protocol: str = "gradient"
    packet_loss_rate: float = 0.0
    max_distance: float = 300.0
    fire_penalty: float = 0.4
    max_ticks: int = 80
    delay_factor: float = 0.0
    jitter_ticks: int = 0
    csma_enabled: bool = True

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
    """Initialize network with chosen protocol and run convergence with customizable PER parameters."""
    valid_protocols = ["gradient", "link_state", "dsdv", "aodv", "potential_field", "rpl"]
    if req.protocol not in valid_protocols:
        raise HTTPException(400, f"Invalid protocol. Choose from: {valid_protocols}")

    network = MeshNetwork(
        packet_loss_rate=req.packet_loss_rate,
        max_distance=req.max_distance,
        fire_penalty=req.fire_penalty,
        delay_factor=req.delay_factor,
        jitter_ticks=req.jitter_ticks,
        csma_enabled=req.csma_enabled
    )
    network.load_from_topology(TOPOLOGY_PATH, routing_mode=req.protocol)

    timeline, converged, ticks_taken = _run_ticks(network, max_ticks=req.max_ticks, stability_window=8)

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
