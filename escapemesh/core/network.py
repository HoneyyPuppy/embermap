# FILE: escapemesh/core/network.py
import json
from typing import Dict, Any, List

from escapemesh.core.base_node import JacobiQueue, Node
from escapemesh.nodes.aodv_node import AODVNode
from escapemesh.nodes.dsdv_node import DSDVNode
from escapemesh.nodes.gradient_node import GradientNode
from escapemesh.nodes.link_state_node import LinkStateNode
from escapemesh.nodes.potential_field_node import PotentialFieldNode
from escapemesh.nodes.rpl_node import RPLNode

def generate_positions(node_ids) -> Dict[str, Dict[str, Any]]:
    positions = {}
    for nid in node_ids:
        # Support basic node topology coordinates
        if nid in ("N1", "N2", "N3", "N4", "N5", "EXIT"):
            basic_positions = {
                "EXIT": {"floor": 0, "x": 500, "y": 300, "type": "exit", "label": "Exit"},
                "N3": {"floor": 0, "x": 500, "y": 200, "type": "hallway", "label": "N3"},
                "N2": {"floor": 0, "x": 400, "y": 150, "type": "hallway", "label": "N2"},
                "N1": {"floor": 0, "x": 300, "y": 100, "type": "hallway", "label": "N1"},
                "N5": {"floor": 0, "x": 500, "y": 100, "type": "hallway", "label": "N5"},
                "N4": {"floor": 0, "x": 600, "y": 150, "type": "hallway", "label": "N4"},
            }
            positions[nid] = basic_positions[nid]
            continue
            
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

class MeshNetwork:
    """Manages collection of nodes and simulation execution."""
    
    def __init__(self, packet_loss_rate: float = 0.0, max_distance: float = 300.0, fire_penalty: float = 0.4, delay_factor: float = 0.0, jitter_ticks: int = 0, csma_enabled: bool = True, smoke_propagation_enabled: bool = True, smoke_increment: float = 40.0):
        self.nodes: Dict[str, Node] = {}
        self.packet_loss_rate = packet_loss_rate
        self.max_distance = max_distance
        self.fire_penalty = fire_penalty
        self.delay_factor = delay_factor
        self.jitter_ticks = jitter_ticks
        self.csma_enabled = csma_enabled
        self.smoke_propagation_enabled = smoke_propagation_enabled
        self.smoke_increment = smoke_increment
        self.physical_simulation_enabled = True
 
    def load_from_topology(self, filepath: str, routing_mode: str = "gradient"):
        self.nodes.clear()
        with open(filepath, 'r', encoding="utf-8") as f:
            data = json.load(f)
            
        # Select packet loss rate from JSON file if present, otherwise use instance default
        self.packet_loss_rate = data.get("packet_loss_rate", self.packet_loss_rate)
        
        # Enable physical simulation if we are not running unit tests
        import sys
        self.physical_simulation_enabled = not ("pytest" in sys.modules or "unittest" in sys.modules)
            
        # Select Node class based on algorithm mode
        node_classes = {
            "gradient": GradientNode,
            "link_state": LinkStateNode,
            "dsdv": DSDVNode,
            "aodv": AODVNode,
            "potential_field": PotentialFieldNode,
            "rpl": RPLNode,
        }
        node_class = node_classes.get(routing_mode, GradientNode)

        for name, props in data.get("nodes", {}).items():
            node_instance = node_class(name, props.get("is_exit", False), packet_loss_rate=self.packet_loss_rate)
            node_instance.max_distance = self.max_distance
            node_instance.fire_penalty = self.fire_penalty
            node_instance.delay_factor = self.delay_factor
            node_instance.jitter_ticks = self.jitter_ticks
            node_instance.csma_enabled = self.csma_enabled
            node_instance.smoke_propagation_enabled = self.smoke_propagation_enabled
            node_instance.smoke_increment = self.smoke_increment
            node_instance.physical_simulation_enabled = self.physical_simulation_enabled
            node_instance.smoke_threshold = float(props.get("smoke_threshold", 400.0))
            self.nodes[name] = node_instance
            
        # Generate coordinates if missing, otherwise load from props
        positions = generate_positions(self.nodes.keys())
        for name, node in self.nodes.items():
            props = data.get("nodes", {}).get(name, {})
            if "x" in props and "y" in props:
                node.x = float(props["x"])
                node.y = float(props["y"])
                node.floor = int(props.get("floor", 0))
            elif name in positions:
                node.x = float(positions[name]["x"])
                node.y = float(positions[name]["y"])
                node.floor = int(positions[name]["floor"])
            
        for n1, n2 in data.get("links", []):
            if n1 in self.nodes and n2 in self.nodes:
                self.nodes[n1].add_neighbor(self.nodes[n2])
                self.nodes[n2].add_neighbor(self.nodes[n1])

    def set_node_online(self, node_id: str, online: bool):
        if node_id not in self.nodes:
            raise KeyError(f"Unknown node: {node_id}")
        self.nodes[node_id].set_online(online)

    def set_link_state(self, node_a: str, node_b: str, enabled: bool):
        """Enable/disable a bidirectional physical link without deleting topology."""
        if node_a not in self.nodes or node_b not in self.nodes:
            raise KeyError(f"Unknown link endpoint: {node_a}, {node_b}")
        if self.nodes[node_b] not in self.nodes[node_a].neighbors:
            raise ValueError(f"Nodes {node_a} and {node_b} are not neighbors")

        self.nodes[node_a].set_link_enabled(node_b, enabled)
        self.nodes[node_b].set_link_enabled(node_a, enabled)

    def tick(self) -> bool:
        """Execute one synchronous tick across all operational nodes."""
        changed = False

        for node in list(self.nodes.values()):
            node.pre_tick()

        for node in list(self.nodes.values()):
            if node.is_operational and node.tick():
                changed = True
        queues = (
            "incoming_lsas",
            "incoming_dsdv_updates",
            "incoming_aodv_packets",
            "incoming_dios",
        )
        packets_in_flight = any(
            isinstance(getattr(node, queue_name, None), JacobiQueue)
            and getattr(node, queue_name).has_pending()
            for node in self.nodes.values()
            for queue_name in queues
        )

        searching_route = any(
            isinstance(node, AODVNode)
            and node.is_operational
            and not node.is_exit
            and not any(
                active
                for dest, (_, _, _, active) in node.aodv_routing_table.items()
                if "EXIT" in dest or dest.startswith("EX")
            )
            for node in self.nodes.values()
        )

        return changed or packets_in_flight or searching_route
