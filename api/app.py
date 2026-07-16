# FILE: api/app.py
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

BUILDING_LAYOUT_PATH = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "topologies",
    "building_layout.json",
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
    smoke_propagation_enabled: bool = False
    smoke_increment: float = 40.0

class FireRequest(BaseModel):
    node_id: str
    smoke_level: Optional[float] = None

class NodeStateRequest(BaseModel):
    node_id: str
    online: bool

class LinkStateRequest(BaseModel):
    node_a: str
    node_b: str
    enabled: bool

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
            "smoke_level": node.smoke_level,
            "smoke_threshold": node.smoke_threshold,
            "online": node.online,
            "tx_msg_count": getattr(node, 'routing_message_tx_count', 0),
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
@app.get("/api/building-layout")
def get_building_layout():
    """Return the static architectural blueprint of the building (corridors)."""
    if not os.path.exists(BUILDING_LAYOUT_PATH):
        raise HTTPException(404, f"Building layout not found at {BUILDING_LAYOUT_PATH}")
    with open(BUILDING_LAYOUT_PATH, "r") as f:
        return json.load(f)


@app.get("/api/topology")
def get_topology():
    """Return the building topology structure and node positions for rendering."""
    with open(TOPOLOGY_PATH, "r") as f:
        topo = json.load(f)
    node_ids = list(topo.get("nodes", {}).keys())
    generated = generate_positions(node_ids)
    
    positions = {}
    for nid, props in topo.get("nodes", {}).items():
        if "x" in props and "y" in props:
            positions[nid] = {
                "floor": props.get("floor", 0),
                "x": props["x"],
                "y": props["y"],
                "type": props.get("type", "hallway"),
                "label": props.get("label", nid),
            }
        else:
            positions[nid] = generated.get(nid, {"floor": 0, "x": 500, "y": 200, "type": "hallway", "label": nid})
            
    return {
        "nodes": topo.get("nodes", {}),
        "links": topo.get("links", []),
        "node_positions": positions,
    }


@app.post("/api/topology")
def save_topology(payload: Dict[str, Any]):
    """Save the updated topology configuration (nodes, links, and positions) to JSON."""
    backup_path = TOPOLOGY_PATH + ".bak"
    if not os.path.exists(backup_path) and os.path.exists(TOPOLOGY_PATH):
        import shutil
        shutil.copyfile(TOPOLOGY_PATH, backup_path)

    nodes_data = {}
    positions = payload.get("node_positions", {})
    for nid, props in payload.get("nodes", {}).items():
        pos = positions.get(nid, {})
        nodes_data[nid] = {
            "is_exit": props.get("is_exit", False),
            "floor": pos.get("floor", 0),
            "x": pos.get("x", 500),
            "y": pos.get("y", 200),
            "type": pos.get("type", "hallway"),
            "label": pos.get("label", nid),
            "smoke_threshold": float(props.get("smoke_threshold", 400.0))
        }

    updated_topo = {
        "nodes": nodes_data,
        "links": payload.get("links", [])
    }

    with open(TOPOLOGY_PATH, "w") as f:
        json.dump(updated_topo, f, indent=2)

    # Reset active simulation
    state["network"] = None
    state["protocol"] = None
    return {"status": "saved"}


@app.post("/api/topology/revert")
def revert_topology():
    """Restore the default topology from the backup file."""
    backup_path = TOPOLOGY_PATH + ".bak"
    if os.path.exists(backup_path):
        import shutil
        shutil.copyfile(backup_path, TOPOLOGY_PATH)
        
    state["network"] = None
    state["protocol"] = None
    return {"status": "reverted"}


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
        csma_enabled=req.csma_enabled,
        smoke_propagation_enabled=req.smoke_propagation_enabled,
        smoke_increment=req.smoke_increment
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
    """Trigger fire or adjust smoke level on a node and run recovery ticks."""
    if state["network"] is None:
        raise HTTPException(400, "No simulation running. Call /api/simulate first.")

    network: MeshNetwork = state["network"]
    if req.node_id not in network.nodes:
        raise HTTPException(404, f"Node '{req.node_id}' not found in network.")

    node = network.nodes[req.node_id]
    if node.is_exit:
        raise HTTPException(400, "Cannot set fire on an exit node.")

    if req.smoke_level is not None:
        node.smoke_level = req.smoke_level
        if node.smoke_level >= node.smoke_threshold:
            if not node.on_fire:
                node.trigger_fire()
        else:
            # If smoke was lowered, clear fire alarm
            node.on_fire = False
    else:
        # Trigger fire instantly by default
        node.trigger_fire()

    # Run recovery with a wider stability window to account for healing propagation
    timeline, converged, ticks_taken = _run_ticks(network, max_ticks=80, stability_window=12)

    return {
        "fired_node": req.node_id,
        "converged": converged,
        "ticks_taken": ticks_taken,
        "timeline": timeline,
    }


@app.post("/api/node-state")
def set_node_state(req: NodeStateRequest):
    """Temporarily power a node on/off and run stabilized recovery ticks."""
    if state["network"] is None:
        raise HTTPException(400, "No simulation running. Call /api/simulate first.")

    network: MeshNetwork = state["network"]
    if req.node_id not in network.nodes:
        raise HTTPException(404, f"Node '{req.node_id}' not found in network.")
    if network.nodes[req.node_id].on_fire:
        raise HTTPException(400, "A node on fire cannot be powered back online.")

    network.set_node_online(req.node_id, req.online)
    timeline, converged, ticks_taken = _run_ticks(
        network, max_ticks=80, stability_window=12
    )
    return {
        "node_id": req.node_id,
        "online": req.online,
        "converged": converged,
        "ticks_taken": ticks_taken,
        "timeline": timeline,
    }


@app.post("/api/link-state")
def set_link_state(req: LinkStateRequest):
    """Enable/disable an existing physical link without deleting it."""
    if state["network"] is None:
        raise HTTPException(400, "No simulation running. Call /api/simulate first.")

    network: MeshNetwork = state["network"]
    try:
        network.set_link_state(req.node_a, req.node_b, req.enabled)
    except KeyError as exc:
        raise HTTPException(404, str(exc)) from exc
    except ValueError as exc:
        raise HTTPException(400, str(exc)) from exc

    timeline, converged, ticks_taken = _run_ticks(
        network, max_ticks=80, stability_window=12
    )
    return {
        "node_a": req.node_a,
        "node_b": req.node_b,
        "enabled": req.enabled,
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
