import json
from typing import Dict
from escapemesh.core.base_node import Node
from escapemesh.nodes.gradient_node import GradientNode
from escapemesh.nodes.link_state_node import LinkStateNode
from escapemesh.nodes.dsdv_node import DSDVNode
from escapemesh.nodes.aodv_node import AODVNode
from escapemesh.nodes.potential_field_node import PotentialFieldNode
from escapemesh.nodes.rpl_node import RPLNode

class MeshNetwork:
    """Manages collection of nodes and simulation execution."""
    
    def __init__(self):
        self.nodes: Dict[str, Node] = {}

    def load_from_topology(self, filepath: str, routing_mode: str = "gradient"):
        """Loads and parses JSON topology file instantiating the matching Node subclass."""
        self.nodes.clear()
        with open(filepath, 'r') as f:
            data = json.load(f)
            
        # Select Node class based on algorithm mode
        node_classes = {
            "gradient": GradientNode,
            "link_state": LinkStateNode,
            "dsdv": DSDVNode,
            "aodv": AODVNode,
            "potential_field": PotentialFieldNode,
            "rpl": RPLNode
        }
        node_class = node_classes.get(routing_mode, GradientNode)
            
        for name, props in data.get("nodes", {}).items():
            self.nodes[name] = node_class(name, props.get("is_exit", False))
            
        for n1, n2 in data.get("links", []):
            if n1 in self.nodes and n2 in self.nodes:
                self.nodes[n1].add_neighbor(self.nodes[n2])
                self.nodes[n2].add_neighbor(self.nodes[n1])

    def tick(self) -> bool:
        """
        Executes a single synchronous tick across all nodes.
        Returns True if routing states changed OR if packets are still in flight.
        """
        changed = False
        # Run keepalives and ticks sequentially
        for node in list(self.nodes.values()):
            node.pre_tick()
            
        for node in list(self.nodes.values()):
            if node.tick():
                changed = True
                
        # Check if there are pending messages to process in the next tick (LSA, DSDV, AODV, or RPL DIO)
        packets_in_flight = any(
            len(getattr(node, 'incoming_lsas', [])) > 0 or 
            len(getattr(node, 'incoming_dsdv_updates', [])) > 0 or
            len(getattr(node, 'incoming_aodv_packets', [])) > 0 or
            len(getattr(node, 'incoming_dios', [])) > 0
            for node in self.nodes.values()
        )
        
        # Keep ticking if any AODV node is actively searching for a route (i.e. has no active route to EXIT)
        searching_route = any(
            isinstance(node, AODVNode) and not node.is_exit and 
            not any(
                active for dest, (_, _, _, active) in node.aodv_routing_table.items()
                if "EXIT" in dest or dest.startswith("EX")
            )
            for node in self.nodes.values()
        )
        
        return changed or packets_in_flight or searching_route
