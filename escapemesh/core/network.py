# FILE: escapemesh/core/network.py
import json
from typing import Dict

from escapemesh.core.base_node import JacobiQueue, Node
from escapemesh.nodes.aodv_node import AODVNode
from escapemesh.nodes.dsdv_node import DSDVNode
from escapemesh.nodes.gradient_node import GradientNode
from escapemesh.nodes.link_state_node import LinkStateNode
from escapemesh.nodes.potential_field_node import PotentialFieldNode
from escapemesh.nodes.rpl_node import RPLNode


class MeshNetwork:
    """Manage nodes, physical link state, and synchronous simulation ticks."""

    def __init__(self):
        self.nodes: Dict[str, Node] = {}

    def load_from_topology(self, filepath: str, routing_mode: str = "gradient"):
        self.nodes.clear()
        with open(filepath, "r", encoding="utf-8") as topology_file:
            data = json.load(topology_file)

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
            self.nodes[name] = node_class(name, props.get("is_exit", False))

        for node_a, node_b in data.get("links", []):
            if node_a in self.nodes and node_b in self.nodes:
                self.nodes[node_a].add_neighbor(self.nodes[node_b])
                self.nodes[node_b].add_neighbor(self.nodes[node_a])

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
